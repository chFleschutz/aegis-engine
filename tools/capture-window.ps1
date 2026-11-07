<#
.SYNOPSIS
    Saves the window of a windowed run as a PNG: what a headless run can't show, such as the editor's
    windowed-only UI (the recovery bar, prompts) or a scene in a window.

.DESCRIPTION
    Windows only (Windows PowerShell 5.1 or later). Starts -Exe with -Arguments, or attaches to the running
    process -Name, waits until it has a window and -Seconds more, then captures the window's client area with
    PrintWindow and PW_RENDERFULLCONTENT (a plain screen copy of a Vulkan window comes out black).

    A started process writes its stderr to <Png>.stderr.txt and its stdout to <Png>.stdout.txt. It is closed
    afterwards (CloseMainWindow, then killed if it is still open after 10 s, behind an unsaved-changes prompt for
    one) unless -KeepOpen. The script then prints the [error] lines of its stderr (validation errors among them)
    and exits 1 if there are any. An attached process is left running.

    Look at the PNG with an image viewer or your file-reading tool. Paths are relative to the current location.

.EXAMPLE
    tools/capture-window.ps1 -Exe build/windows-clang-debug/examples/examples/Examples-Editor.exe -Arguments '--scene','scenes/sponza.scene' -Png sponza.png

.EXAMPLE
    tools/capture-window.ps1 -Name Examples-Editor -Png now.png -Seconds 0
#>
[CmdletBinding(DefaultParameterSetName = 'Start')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Start')] [string] $Exe,
    [Parameter(ParameterSetName = 'Start')] [string[]] $Arguments = @(),
    [Parameter(ParameterSetName = 'Start')] [switch] $KeepOpen,
    [Parameter(Mandatory = $true, ParameterSetName = 'Attach')] [string] $Name,
    [Parameter(Mandatory = $true)] [string] $Png,
    [int] $Seconds = 10
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing
if (-not ('AegisCapture' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class AegisCapture
{
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out Rect rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
}
'@
}
# Physical pixels: a window that scales with the monitor reports its real size only to a DPI-aware caller
[void][AegisCapture]::SetProcessDPIAware()

$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Png)
$stderr = "$output.stderr.txt"
$started = $PSCmdlet.ParameterSetName -eq 'Start'
if ($started) {
    $options = @{
        FilePath               = (Resolve-Path $Exe).Path
        PassThru               = $true
        NoNewWindow            = $true
        RedirectStandardError  = $stderr
        RedirectStandardOutput = "$output.stdout.txt"
    }
    if ($Arguments.Count -gt 0) { $options.ArgumentList = $Arguments }
    $process = Start-Process @options
}
else {
    $process = Get-Process -Name $Name | Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } | Select-Object -First 1
    if ($null -eq $process) { throw "No process '$Name' with a window" }
}

$waited = [Diagnostics.Stopwatch]::StartNew()
while ($process.MainWindowHandle -eq [IntPtr]::Zero) {
    if ($process.HasExited) { throw "The process exited (code $($process.ExitCode)) before it opened a window; see $stderr" }
    if ($waited.Elapsed.TotalSeconds -gt 60) { throw 'No window after 60 s' }
    Start-Sleep -Milliseconds 250
    $process.Refresh()
}
Start-Sleep -Seconds $Seconds

$hwnd = $process.MainWindowHandle
$rect = New-Object AegisCapture+Rect
[void][AegisCapture]::GetClientRect($hwnd, [ref]$rect)
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
if ($width -le 0 -or $height -le 0) { throw 'The window has no client area (minimized?)' }
$bitmap = New-Object System.Drawing.Bitmap $width, $height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$hdc = $graphics.GetHdc()
$printed = [AegisCapture]::PrintWindow($hwnd, $hdc, 3) # PW_CLIENTONLY | PW_RENDERFULLCONTENT
$graphics.ReleaseHdc($hdc)
$graphics.Dispose()
if (-not $printed) { throw 'PrintWindow failed' }
$bitmap.Save($output, [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()
Write-Output "Saved $output ($width x $height)"

if (-not $started) { exit 0 }
if (-not $KeepOpen) {
    [void]$process.CloseMainWindow()
    if (-not $process.WaitForExit(10000)) {
        Write-Warning 'Still open after 10 s (an unsaved-changes prompt?): killing it'
        $process.Kill()
        $process.WaitForExit()
    }
}
$errors = @(Select-String -Path $stderr -Pattern '^\[error\]' | ForEach-Object { $_.Line })
if ($errors.Count -gt 0) {
    Write-Output "$($errors.Count) [error] lines in $($stderr):"
    $errors | Select-Object -First 20 | ForEach-Object { Write-Output "  $_" }
    exit 1
}
exit 0
