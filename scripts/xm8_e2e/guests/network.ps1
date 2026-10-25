param([ValidateSet('arm','restore','watchdog')][string]$Action, [string]$Guard)
$ErrorActionPreference = 'Stop'
if ($Guard -notmatch '^xm8-e2e-net-[a-f0-9]+$') { throw 'Invalid guard id' }
$state = Join-Path $env:ProgramData $Guard
$file = Join-Path $state 'adapters.json'
if ($Action -eq 'watchdog') { Start-Sleep -Seconds 120; $Action = 'restore' }
if ($Action -eq 'restore') {
    if (Test-Path $file) {
        $ids = @(Get-Content -Raw $file | ConvertFrom-Json)
        Get-NetAdapter | Where-Object { $_.InterfaceGuid.ToString() -in $ids } | Enable-NetAdapter -Confirm:$false
    }
    Unregister-ScheduledTask -TaskName $Guard -Confirm:$false -ErrorAction SilentlyContinue
    exit 0
}
if (Test-Path $state) { throw 'Guard already exists' }
$adapters = @(Get-NetAdapter | Where-Object { $_.Status -eq 'Up' })
if ($adapters.Count -ne 1) { throw 'Expected one active internet adapter in isolated clone' }
New-Item -ItemType Directory $state | Out-Null
ConvertTo-Json -InputObject @($adapters | ForEach-Object { $_.InterfaceGuid.ToString() }) | Set-Content $file
Copy-Item $PSCommandPath (Join-Path $state 'guard.ps1')
$guardScript = Join-Path $state 'guard.ps1'
$taskAction = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -File `"$guardScript`" restore $Guard"
$trigger = New-ScheduledTaskTrigger -Once -At (Get-Date).AddSeconds(120)
Register-ScheduledTask -TaskName $Guard -Action $taskAction -Trigger $trigger -User 'SYSTEM' -RunLevel Highest | Out-Null
$adapters | Disable-NetAdapter -Confirm:$false
