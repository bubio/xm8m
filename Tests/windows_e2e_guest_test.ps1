param([Parameter(Mandatory=$true)][string]$Helper)
# Execute inside Windows with XM8 stopped. Synthetic fixtures contain no user data.
$ErrorActionPreference='Stop'
$source=Get-Content -Raw $Helper
$first=$source.IndexOf("Add-Type @'")
$last=$source.IndexOf('function Get-TestApp')
Invoke-Expression $source.Substring($first,$last-$first)
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class FixtureSql {
    [DllImport("winsqlite3.dll",CallingConvention=CallingConvention.Cdecl)]
    public static extern int sqlite3_exec(IntPtr db, byte[] sql, IntPtr cb, IntPtr user, IntPtr error);
}
'@
$original=$env:APPDATA
$folder=Join-Path $env:TEMP ('xm8-e2e-guard-test-'+[Guid]::NewGuid().ToString('N'))
$cases=@(
    @{name='missing';sql='';expected=0},
    @{name='empty-current';sql='CREATE TABLE pending_unlocks(status INTEGER)';expected=0},
    @{name='pending';sql='CREATE TABLE pending_unlocks(status INTEGER);INSERT INTO pending_unlocks VALUES(0)';expected=2},
    @{name='held';sql='CREATE TABLE pending_unlocks(status INTEGER);INSERT INTO pending_unlocks VALUES(1)';expected=2},
    @{name='mixed';sql='CREATE TABLE pending_unlocks(status INTEGER);INSERT INTO pending_unlocks VALUES(0),(1)';expected=2},
    @{name='unknown-status';sql='CREATE TABLE pending_unlocks(status INTEGER);INSERT INTO pending_unlocks VALUES(9)';expected=2},
    @{name='legacy-v2';sql='CREATE TABLE schema_meta(singleton INTEGER,schema_version INTEGER);INSERT INTO schema_meta VALUES(1,2)';expected=0},
    @{name='current-missing-outbox';sql='CREATE TABLE schema_meta(singleton INTEGER,schema_version INTEGER);INSERT INTO schema_meta VALUES(1,4)';expected=2},
    @{name='unknown-schema';sql='CREATE TABLE unrelated(x INTEGER)';expected=2},
    @{name='corrupt';sql='corrupt';expected=2},
    @{name='recovery-marker';sql='marker';expected=2}
)
try {
    foreach($case in $cases){
        $env:APPDATA=Join-Path $folder $case.name
        $root=Join-Path $env:APPDATA 'retro_pc_pi\xm8\ra'
        New-Item -ItemType Directory -Path $root -Force | Out-Null
        $file=Join-Path $root 'library.sqlite3'
        if($case.sql -eq 'marker'){
            [IO.File]::WriteAllText((Join-Path $root 'pending-unlock-recovery-required'),'test')
        }elseif($case.sql -eq 'corrupt'){
            [IO.File]::WriteAllText($file,'not a database')
        }elseif($case.sql){
            $db=[IntPtr]::Zero
            $name=[Text.Encoding]::UTF8.GetBytes($file+[char]0)
            if([E2EWindows]::sqlite3_open_v2($name,[ref]$db,6,[IntPtr]::Zero) -ne 0){throw 'Fixture open failed'}
            try{
                $sql=[Text.Encoding]::UTF8.GetBytes($case.sql+[char]0)
                if([FixtureSql]::sqlite3_exec($db,$sql,[IntPtr]::Zero,[IntPtr]::Zero,[IntPtr]::Zero) -ne 0){throw 'Fixture SQL failed'}
            }finally{[void][E2EWindows]::sqlite3_close($db)}
        }
        $before=if(Test-Path $file){(Get-FileHash $file).Hash}else{'missing'}
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Helper -Action snapshot-ready | Out-Null
        if($LASTEXITCODE -ne $case.expected){throw ('Unexpected guard result: '+$case.name)}
        $after=if(Test-Path $file){(Get-FileHash $file).Hash}else{'missing'}
        if($before -ne $after){throw ('Guard modified database: '+$case.name)}
        Write-Output ('PASS '+$case.name)
        $stdout=Join-Path $env:APPDATA 'aggregate.txt';$stderr=Join-Path $env:APPDATA 'error.txt'
        $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$Helper+'"'),'-Action','observe-ra')
        $process=Start-Process powershell.exe -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        if($case.name -in @('empty-current','pending','held','mixed')){
            if($process.ExitCode -ne 0){throw ('Aggregate unavailable: '+$case.name)}
            $counts=Get-Content -Raw $stdout | ConvertFrom-Json
            $pending=if($case.name -in @('pending','mixed')){1}else{0}
            $held=if($case.name -in @('held','mixed')){1}else{0}
            if($counts.pending -ne $pending -or $counts.held -ne $held){throw ('Wrong aggregate: '+$case.name)}
        }elseif($process.ExitCode -eq 0){throw ('Unobservable aggregate passed: '+$case.name)}
        $after=if(Test-Path $file){(Get-FileHash $file).Hash}else{'missing'}
        if($before -ne $after){throw ('Aggregate modified database: '+$case.name)}
        Write-Output ('PASS aggregate-'+$case.name)
    }
}finally{
    $env:APPDATA=$original
    if(Test-Path $folder){Remove-Item $folder -Recurse -Force}
}
# Expected child failures must not become the CI shell's final exit status.
exit 0
