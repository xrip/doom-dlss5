param([string]$Exe, [string]$Scene, [string[]]$Modes = @('--dlss5', '--fg'), [string]$Log = 'maximize', [ValidateRange(1, 100)][int]$Cycles = 2)
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class WindowCheck {
    public delegate bool EnumCallback(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumCallback callback, IntPtr parameter);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr window);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr window, uint message, IntPtr first, IntPtr second);
    public static IntPtr Find(uint process) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, parameter) => {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            if (owner == process && IsWindowVisible(window)) { found = window; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@
$executable = (Resolve-Path -LiteralPath $Exe).Path
$scenePath = (Resolve-Path -LiteralPath $Scene).Path
$arguments = @('"' + $scenePath + '"', '--visible', '--no-music') + $Modes
$process = Start-Process -FilePath $executable -ArgumentList $arguments -PassThru -WindowStyle Hidden -RedirectStandardOutput "$Log.out.log" -RedirectStandardError "$Log.err.log"
$null = $process.Handle
$window = [IntPtr]::Zero
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    while ($window -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline) {
        $owners = @($process.Id) + @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$($process.Id)" | Select-Object -ExpandProperty ProcessId)
        foreach ($owner in $owners) {
            $window = [WindowCheck]::Find($owner)
            if ($window -ne [IntPtr]::Zero) { break }
        }
        if ($process.HasExited) { throw "Game exited during startup: $($process.ExitCode)" }
        Start-Sleep -Milliseconds 100
    }
    if ($window -eq [IntPtr]::Zero) { throw 'Own game window not found' }
    Start-Sleep -Seconds 4
    foreach ($cycle in 1..$Cycles) {
        foreach ($action in @(@('maximize', 0xF030), @('restore', 0xF120), @('minimize', 0xF020), @('restore', 0xF120))) {
            if (-not [WindowCheck]::PostMessageW($window, 0x112, [IntPtr]$action[1], [IntPtr]::Zero)) { throw 'PostMessage failed' }
            Start-Sleep -Milliseconds 1200
            if ($process.HasExited) { throw "Game exited after $($action[0]): $($process.ExitCode)" }
            $maximized = [WindowCheck]::IsZoomed($window)
            $minimized = [WindowCheck]::IsIconic($window)
            if ($action[0] -eq 'maximize' -and -not $maximized) { throw 'Maximize not applied' }
            if ($action[0] -eq 'minimize' -and -not $minimized) { throw 'Minimize not applied' }
            if ($action[0] -eq 'restore' -and ($maximized -or $minimized)) { throw 'Restore not applied' }
            Write-Output "Cycle $cycle $($action[0]): alive"
        }
    }
    [void][WindowCheck]::PostMessageW($window, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
    if (-not $process.WaitForExit(20000)) { throw 'Game did not close after WM_CLOSE' }
    if ($process.ExitCode -ne 0) { throw "Game exit: $($process.ExitCode)" }
    Write-Output 'Maximize/minimize/restore and close passed'
} finally {
    if (-not $process.HasExited) {
        if ($window -ne [IntPtr]::Zero) {
            [void][WindowCheck]::PostMessageW($window, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)
            [void]$process.WaitForExit(10000)
        }
        if (-not $process.HasExited) { & taskkill.exe /PID $process.Id /T /F | Out-Null }
    }
}
