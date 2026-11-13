param([Parameter(Mandatory=$true)][string]$Helper)
# Run in Windows. All adapter/task commands are mocked in child processes.
$ErrorActionPreference='Stop'
$folder=Join-Path $env:TEMP ('xm8-e2e-network-test-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $folder | Out-Null
$fixture=Join-Path $folder 'fixture.ps1'
@'
param([string]$Helper,[string]$Root,[string]$Case)
$ErrorActionPreference='Stop'
$env:ProgramData=$Root
$global:FakeStatus='Up'
$global:Registered=$false
$global:Events=Join-Path $Root 'events.txt'
$guid='{11111111-2222-3333-4444-555555555555}'
function Get-NetAdapter { [pscustomobject]@{InterfaceGuid=$guid;Status=$global:FakeStatus} }
function Enable-NetAdapter {
 param([Parameter(ValueFromPipeline=$true)]$InputObject,[switch]$Confirm)
 process { [IO.File]::AppendAllText($global:Events,"enable`n");$global:FakeStatus='Up' }
}
function Disable-NetAdapter {
 param([Parameter(ValueFromPipeline=$true)]$InputObject,[switch]$Confirm)
 process {
  if(!$global:Registered){throw 'Cut preceded watchdog registration'}
  [IO.File]::AppendAllText($global:Events,"disable`n");$global:FakeStatus='Disabled'
 }
}
function New-ScheduledTaskAction {param($Execute,$Argument) @{Execute=$Execute;Arguments=$Argument} }
function New-ScheduledTaskTrigger {param([switch]$Once,$At) @{At=$At} }
function New-ScheduledTaskSettingsSet {
 param([switch]$StartWhenAvailable,[switch]$AllowStartIfOnBatteries,[switch]$DontStopIfGoingOnBatteries,$RestartCount,$RestartInterval)
 @{StartWhenAvailable=$StartWhenAvailable;AllowStartIfOnBatteries=$AllowStartIfOnBatteries;DontStopIfGoingOnBatteries=$DontStopIfGoingOnBatteries}
}
function Register-ScheduledTask {
 param($TaskName,$Action,$Trigger,$Settings,$User,$RunLevel)
 if($Case -eq 'register-failure'){throw 'Injected registration failure'}
 if($User -ne 'SYSTEM' -or $RunLevel -ne 'Highest' -or $Action.Arguments -notmatch '-ExecutionPolicy Bypass'){throw 'Watchdog policy missing'}
 if(!$Settings.StartWhenAvailable -or !$Settings.AllowStartIfOnBatteries -or !$Settings.DontStopIfGoingOnBatteries){throw 'Watchdog availability missing'}
 $seconds=($Trigger.At-(Get-Date)).TotalSeconds
 if($seconds -lt 115 -or $seconds -gt 120){throw 'Watchdog deadline differs'}
 $global:Registered=$true
}
function Get-ScheduledTask {param($TaskName) if($global:Registered){@{TaskName=$TaskName}} }
function Unregister-ScheduledTask {param($TaskName,[switch]$Confirm) if($Case -ne 'unregister-failure'){$global:Registered=$false} }
$guard='xm8-e2e-net-abcd'
$state=Join-Path $Root $guard
if($Case -in @('restore-array','restored-old-guard','wrong-adapter','unregister-failure')){
 New-Item -ItemType Directory $state | Out-Null
 $ids=if($Case -eq 'wrong-adapter'){'{99999999-2222-3333-4444-555555555555}'}else{$guid}
 ConvertTo-Json -InputObject @($ids) | Set-Content (Join-Path $state 'adapters.json')
 $global:FakeStatus='Disabled'
 $global:Registered=$true
 if($Case -eq 'restored-old-guard'){[IO.File]::WriteAllText((Join-Path $state 'restored'),'restored')}
 & $Helper -Action restore -Guard $guard
}else{
 & $Helper -Action arm -Guard $guard
}
'@ | Set-Content -Encoding UTF8 $fixture
$cases=@(
 @{name='arm';expected=0;event='disable'},
 @{name='restore-array';expected=0;event='enable'},
 @{name='restored-old-guard';expected=1;event=''},
 @{name='wrong-adapter';expected=1;event=''},
 @{name='register-failure';expected=1;event=''},
 @{name='unregister-failure';expected=1;event='enable'}
)
try {
 foreach($case in $cases){
  $root=Join-Path $folder $case.name
  New-Item -ItemType Directory $root | Out-Null
  $stdout=Join-Path $root 'stdout.txt';$stderr=Join-Path $root 'stderr.txt'
  $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$fixture+'"'),'-Helper',('"'+$Helper+'"'),'-Root',('"'+$root+'"'),'-Case',$case.name)
  $process=Start-Process powershell.exe -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr
  if($process.ExitCode -ne $case.expected){throw ('Unexpected result: '+$case.name)}
  $events=if(Test-Path (Join-Path $root 'events.txt')){(Get-Content -Raw (Join-Path $root 'events.txt')).Trim()}else{''}
  if($events -ne $case.event){throw ('Unexpected adapter mutation: '+$case.name)}
  if($case.expected -eq 0){
   $proof=Get-Content -Raw $stdout | ConvertFrom-Json
   if(!$proof.verified -or $proof.adapter_count -ne 1){throw ('Unverified transition: '+$case.name)}
  }
  Write-Output ('PASS '+$case.name)
 }
} finally {
 if(Test-Path $folder){Remove-Item $folder -Recurse -Force}
}
exit 0
