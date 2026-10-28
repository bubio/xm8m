param([ValidateSet('arm','restore','watchdog')][string]$Action, [string]$Guard)
$ErrorActionPreference = 'Stop'
if ($Guard -notmatch '^xm8-e2e-net-[a-f0-9]+$') { throw 'Invalid guard id' }
$state = Join-Path $env:ProgramData $Guard
$file = Join-Path $state 'adapters.json'
$restored = Join-Path $state 'restored'
if ($Action -eq 'watchdog') { Start-Sleep -Seconds 120; $Action = 'restore' }
if ($Action -eq 'restore') {
    if (!(Test-Path $file)) { throw 'Network guard state unavailable' }
    $parsed = ConvertFrom-Json -InputObject ([IO.File]::ReadAllText($file))
    $ids = @($parsed | ForEach-Object { [string]$_ })
    $adapters = @(Get-NetAdapter | Where-Object { $_.InterfaceGuid.ToString() -in $ids })
    if ($ids.Count -ne 1 -or $adapters.Count -ne 1) { throw 'Guard adapter identity differs' }
    # A late invocation of an old watchdog must not reconnect a later case.
    if (!(Test-Path $restored)) { $adapters | Enable-NetAdapter -Confirm:$false }
    $deadline = (Get-Date).AddSeconds(20)
    do {
        $current = @(Get-NetAdapter | Where-Object { $_.InterfaceGuid.ToString() -in $ids })
        if ($current.Count -eq 1 -and $current[0].Status -eq 'Up') { break }
        if (Test-Path $restored) { throw 'Restored guard will not change another disconnect' }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)
    if ($current.Count -ne 1 -or $current[0].Status -ne 'Up') { throw 'Guest adapter did not return Up' }
    [IO.File]::WriteAllText($restored, (Get-Date).ToUniversalTime().ToString('o'))
    Unregister-ScheduledTask -TaskName $Guard -Confirm:$false -ErrorAction SilentlyContinue
    if (Get-ScheduledTask -TaskName $Guard -ErrorAction SilentlyContinue) { throw 'Guest watchdog was not removed' }
    @{guard=$Guard;state='up';adapter_count=1;verified=$true;watchdog_removed=$true} | ConvertTo-Json -Compress
    exit 0
}
if (Test-Path $state) { throw 'Guard already exists' }
$adapters = @(Get-NetAdapter | Where-Object { $_.Status -eq 'Up' })
if ($adapters.Count -ne 1) { throw 'Expected one active internet adapter in dedicated VM' }
New-Item -ItemType Directory $state | Out-Null
ConvertTo-Json -InputObject @($adapters | ForEach-Object { $_.InterfaceGuid.ToString() }) | Set-Content $file
Copy-Item $PSCommandPath (Join-Path $state 'guard.ps1')
$guardScript = Join-Path $state 'guard.ps1'
$taskAction = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -ExecutionPolicy Bypass -File `"$guardScript`" restore $Guard"
$trigger = New-ScheduledTaskTrigger -Once -At (Get-Date).AddSeconds(120)
$settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -RestartCount 2 -RestartInterval (New-TimeSpan -Minutes 1)
Register-ScheduledTask -TaskName $Guard -Action $taskAction -Trigger $trigger -Settings $settings -User 'SYSTEM' -RunLevel Highest | Out-Null
if (!(Get-ScheduledTask -TaskName $Guard -ErrorAction Stop)) { throw 'Watchdog was not registered' }
$adapters | Disable-NetAdapter -Confirm:$false
$ids = @($adapters | ForEach-Object { $_.InterfaceGuid.ToString() })
$current = @(Get-NetAdapter | Where-Object { $_.InterfaceGuid.ToString() -in $ids })
if ($current.Count -ne 1 -or $current[0].Status -ne 'Disabled') { throw 'Guest adapter did not become Disabled' }
@{guard=$Guard;state='down';adapter_count=1;verified=$true;watchdog_seconds=120} | ConvertTo-Json -Compress
