param(
    [ValidateSet('snapshot-ready','observe-ra','window','activate','focus-cycle','stop')][string]$Action,
    [string]$Executable
)
# No credentials or database contents are emitted. Run inside the dedicated VM only.
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class E2EWindows {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out Rect r);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("winsqlite3.dll", CallingConvention=CallingConvention.Cdecl)] public static extern int sqlite3_open_v2(byte[] p, out IntPtr db, int flags, IntPtr vfs);
    [DllImport("winsqlite3.dll", CallingConvention=CallingConvention.Cdecl)] public static extern int sqlite3_prepare_v2(IntPtr db, byte[] sql, int n, out IntPtr stmt, IntPtr tail);
    [DllImport("winsqlite3.dll", CallingConvention=CallingConvention.Cdecl)] public static extern int sqlite3_step(IntPtr stmt);
    [DllImport("winsqlite3.dll", CallingConvention=CallingConvention.Cdecl)] public static extern long sqlite3_column_int64(IntPtr stmt, int col);
    [DllImport("winsqlite3.dll", CallingConvention=CallingConvention.Cdecl)] public static extern int sqlite3_finalize(IntPtr stmt);
    [DllImport("winsqlite3.dll", CallingConvention=CallingConvention.Cdecl)] public static extern int sqlite3_close(IntPtr db);
}
'@
function Get-TestApp {
    $apps = @(Get-Process XM8 -ErrorAction SilentlyContinue)
    if ($apps.Count -ne 1 -or !$Executable -or $apps[0].Path -ne $Executable) { throw 'Expected one pinned test application' }
    $apps[0].Refresh()
    return $apps[0]
}
if ($Action -eq 'observe-ra') {
    $database = Join-Path $env:APPDATA 'retro_pc_pi\xm8\ra\library.sqlite3'
    if (!(Test-Path $database)) { throw 'RA aggregate observation unavailable' }
    $db = [IntPtr]::Zero; $stmt = [IntPtr]::Zero
    try {
        $path = [Text.Encoding]::UTF8.GetBytes($database + [char]0)
        if ([E2EWindows]::sqlite3_open_v2($path,[ref]$db,1,[IntPtr]::Zero) -ne 0) { throw 'RA aggregate observation unavailable' }
        $sql = [Text.Encoding]::UTF8.GetBytes('SELECT sum(CASE WHEN status=0 THEN 1 ELSE 0 END),sum(CASE WHEN status=1 THEN 1 ELSE 0 END),count(*) FROM pending_unlocks' + [char]0)
        if ([E2EWindows]::sqlite3_prepare_v2($db,$sql,-1,[ref]$stmt,[IntPtr]::Zero) -ne 0 -or [E2EWindows]::sqlite3_step($stmt) -ne 100) { throw 'RA aggregate observation unavailable' }
        $pending = [E2EWindows]::sqlite3_column_int64($stmt,0)
        $held = [E2EWindows]::sqlite3_column_int64($stmt,1)
        if ($pending + $held -ne [E2EWindows]::sqlite3_column_int64($stmt,2)) { throw 'Unknown outbox status' }
        @{pending=$pending;held=$held} | ConvertTo-Json -Compress
    } finally {
        if ($stmt -ne [IntPtr]::Zero) { [void][E2EWindows]::sqlite3_finalize($stmt) }
        if ($db -ne [IntPtr]::Zero) { [void][E2EWindows]::sqlite3_close($db) }
    }
    exit 0
}
if ($Action -eq 'snapshot-ready') {
    if (Get-Process XM8 -ErrorAction SilentlyContinue) { exit 2 }
    $root = Join-Path $env:APPDATA 'retro_pc_pi\xm8\ra'
    if (Test-Path (Join-Path $root 'pending-unlock-recovery-required')) { exit 2 }
    $database = Join-Path $root 'library.sqlite3'
    if (!(Test-Path $database)) { exit 0 }
    $db = [IntPtr]::Zero; $stmt = [IntPtr]::Zero
    try {
        $path = [Text.Encoding]::UTF8.GetBytes($database + [char]0)
        if ([E2EWindows]::sqlite3_open_v2($path, [ref]$db, 1, [IntPtr]::Zero) -ne 0) { Write-Output 'blocked: database unreadable'; exit 2 }
        $sql = [Text.Encoding]::UTF8.GetBytes('SELECT count(*) FROM pending_unlocks' + [char]0)
        if ([E2EWindows]::sqlite3_prepare_v2($db, $sql, -1, [ref]$stmt, [IntPtr]::Zero) -ne 0) {
            # Versions 1/2 predate the durable outbox. Verify that exact schema;
            # a missing table in a current/unknown database must remain blocked.
            $sql = [Text.Encoding]::UTF8.GetBytes("SELECT count(*) FROM sqlite_master WHERE name='pending_unlocks'" + [char]0)
            if ([E2EWindows]::sqlite3_prepare_v2($db, $sql, -1, [ref]$stmt, [IntPtr]::Zero) -ne 0) { exit 2 }
            if ([E2EWindows]::sqlite3_step($stmt) -ne 100 -or [E2EWindows]::sqlite3_column_int64($stmt, 0) -ne 0) { exit 2 }
            [void][E2EWindows]::sqlite3_finalize($stmt); $stmt = [IntPtr]::Zero
            $sql = [Text.Encoding]::UTF8.GetBytes('SELECT schema_version FROM schema_meta WHERE singleton=1' + [char]0)
            if ([E2EWindows]::sqlite3_prepare_v2($db, $sql, -1, [ref]$stmt, [IntPtr]::Zero) -ne 0) { exit 2 }
            if ([E2EWindows]::sqlite3_step($stmt) -ne 100) { exit 2 }
            $version = [E2EWindows]::sqlite3_column_int64($stmt, 0)
            if ($version -notin @(1,2)) { exit 2 }
            Write-Output ('legacy_schema_without_outbox=' + $version)
            exit 0
        }
        if ([E2EWindows]::sqlite3_step($stmt) -ne 100) { Write-Output 'blocked: aggregate unavailable'; exit 2 }
        $count = [E2EWindows]::sqlite3_column_int64($stmt, 0)
        Write-Output ('pending_or_held=' + $count)
        if ($count -ne 0) { exit 2 }
    } finally {
        if ($stmt -ne [IntPtr]::Zero) { [void][E2EWindows]::sqlite3_finalize($stmt) }
        if ($db -ne [IntPtr]::Zero) { [void][E2EWindows]::sqlite3_close($db) }
    }
    exit 0
}
if ($Action -eq 'stop' -and !(Get-Process XM8 -ErrorAction SilentlyContinue)) { exit 0 }
$app = Get-TestApp
if ($Action -eq 'stop') {
    if (!$app.CloseMainWindow()) { throw 'Graceful close unavailable' }
    if (!$app.WaitForExit(20000)) { throw 'Application did not close normally' }
    exit 0
}
if ($Action -eq 'activate') {
    [void][E2EWindows]::SetForegroundWindow($app.MainWindowHandle)
    Start-Sleep -Milliseconds 300
}
if ($Action -in @('window','activate')) {
    $rect = New-Object E2EWindows+Rect
    if (![E2EWindows]::GetWindowRect($app.MainWindowHandle, [ref]$rect)) { throw 'Window unavailable' }
    @{pid=$app.Id; start_ticks=$app.StartTime.ToUniversalTime().Ticks; focused=([E2EWindows]::GetForegroundWindow() -eq $app.MainWindowHandle); rect=$rect} | ConvertTo-Json -Compress
    exit 0
}
if ($Action -eq 'focus-cycle') {
    # A dedicated, unsaved blank window. No user window is closed or inspected.
    Add-Type -AssemblyName System.Windows.Forms
    $other = New-Object Windows.Forms.Form
    $other.Text = 'XM8M E2E focus helper'
    $other.Show()
    [Windows.Forms.Application]::DoEvents()
    try {
        [void][E2EWindows]::SetForegroundWindow($app.MainWindowHandle)
        Start-Sleep -Milliseconds 300
        if ([E2EWindows]::GetForegroundWindow() -ne $app.MainWindowHandle) { throw 'Application focus unavailable' }
        [E2EWindows]::keybd_event(65, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 200
        [void][E2EWindows]::SetForegroundWindow($other.Handle)
        [Windows.Forms.Application]::DoEvents()
        Start-Sleep -Milliseconds 300
        $left = [E2EWindows]::GetForegroundWindow() -eq $other.Handle
        [E2EWindows]::keybd_event(65, 0, 2, [UIntPtr]::Zero)
        [void][E2EWindows]::SetForegroundWindow($app.MainWindowHandle)
        Start-Sleep -Milliseconds 300
        $returned = [E2EWindows]::GetForegroundWindow() -eq $app.MainWindowHandle
        if (!$left -or !$returned) { throw 'Focus transition was not observed' }
        @{left=$left; returned=$returned; app_pid=$app.Id; helper_pid=$PID} | ConvertTo-Json -Compress
    } finally {
        [E2EWindows]::keybd_event(65, 0, 2, [UIntPtr]::Zero)
        $other.Close()
        $other.Dispose()
    }
}
