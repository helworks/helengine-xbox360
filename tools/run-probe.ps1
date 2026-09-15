<#
.SYNOPSIS
Launches a guest executable in the pinned Xenia build with an isolated log and
storage root, terminates only the launched process on timeout, and reports the
observation markers found in the log.

.DESCRIPTION
The probe loops forever by design, so the timeout is expected. Success is
decided by the log contents: the DbgPrint line written by the guest and the
"Unhandled syscall" line produced by the guest's `sc` marker.
#>
param(
    [string]$Config = "tools/config.local.json",
    [string]$Target = "build/probe/entry.xex",
    [int]$TimeoutSeconds = 30,
    [string]$RunName = "",
    [string[]]$ExtraArgs = @()
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$cfg = Get-Content (Join-Path $repo $Config) -Raw | ConvertFrom-Json

$exe = $cfg.xenia_exe
if (-not (Test-Path $exe)) { throw "Xenia executable not found: $exe" }
$hash = (Get-FileHash $exe -Algorithm SHA256).Hash
if ($cfg.xenia_expected_sha256 -and $hash -ne $cfg.xenia_expected_sha256) {
    throw "Xenia executable hash $hash does not match pinned $($cfg.xenia_expected_sha256)"
}

if (-not $RunName) { $RunName = (Get-Date -Format "yyyyMMdd-HHmmss") }
$logDir = Join-Path $repo (Join-Path $cfg.log_dir $RunName)
New-Item -ItemType Directory -Force $logDir | Out-Null
$storage = Join-Path $repo $cfg.storage_root
New-Item -ItemType Directory -Force $storage | Out-Null
$logFile = Join-Path $logDir "xenia.log"
$targetPath = Resolve-Path (Join-Path $repo $Target)

$args = @(
    "--log_file=$logFile",
    "--log_level=2",
    "--discord=false",
    "--storage_root=$storage"
) + $ExtraArgs + @("$targetPath")
"$exe $($args -join ' ')" | Set-Content (Join-Path $logDir "command.txt")
"xenia sha256 $hash" | Add-Content (Join-Path $logDir "command.txt")

$dialogHelper = @"
using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Collections.Generic;
public class XeniaDialog {
  public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr lParam);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassNameW(IntPtr hWnd, StringBuilder sb, int max);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr SendMessageW(IntPtr hWnd, uint msg, IntPtr wParam, StringBuilder lParam);
  [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
  // Clicks the button with the given label on a standard dialog (#32770)
  // owned by the process. Returns the dialog text or null if none was found.
  public static string ClickButton(uint pid, string label) {
    string found = null;
    EnumWindows((h, l) => {
      uint p; GetWindowThreadProcessId(h, out p);
      if (p != pid) return true;
      var cls = new StringBuilder(256); GetClassNameW(h, cls, 256);
      if (cls.ToString() != "#32770") return true;
      IntPtr button = IntPtr.Zero; var text = new StringBuilder();
      EnumChildWindows(h, (c, l2) => {
        var ccls = new StringBuilder(256); GetClassNameW(c, ccls, 256);
        var ctext = new StringBuilder(4096); SendMessageW(c, 0x000D, (IntPtr)4096, ctext);
        if (ccls.ToString() == "Static") text.Append(ctext.ToString());
        if (ccls.ToString() == "Button" && ctext.ToString().Replace("&", "") == label) button = c;
        return true;
      }, IntPtr.Zero);
      if (button != IntPtr.Zero) { SendMessage(button, 0x00F5, IntPtr.Zero, IntPtr.Zero); found = text.ToString(); return false; }
      return true;
    }, IntPtr.Zero);
    return found;
  }
}
"@
Add-Type -TypeDefinition $dialogHelper

$proc = Start-Process -FilePath $exe -ArgumentList $args -PassThru
# Stock Xenia Canary shows a first-run Yes/No disclaimer dialog asking to open
# the quickstart guide before the emulator initialises. Answer "No" so the run
# proceeds unattended; the dialog text is recorded when it appears.
$deadline = (Get-Date).AddSeconds([Math]::Min($TimeoutSeconds, 15))
while ((Get-Date) -lt $deadline -and -not $proc.HasExited) {
    $dialogText = [XeniaDialog]::ClickButton([uint32]$proc.Id, "No")
    if ($dialogText) {
        "dismissed dialog with No: $dialogText" | Add-Content (Join-Path $logDir "command.txt")
        break
    }
    Start-Sleep -Milliseconds 250
}
$exited = $proc.WaitForExit($TimeoutSeconds * 1000)
if (-not $exited) {
    # Ask the window to close first so Xenia's OnDestroy flushes the log
    # (EmulatorApp::OnDestroy calls xe::FlushLog before quick_exit); only
    # terminate the process we launched if it ignores the request.
    $closed = $false
    if ($proc.CloseMainWindow()) { $closed = $proc.WaitForExit(10000) }
    if (-not $closed) {
        Stop-Process -Id $proc.Id -Force
        "killed after ${TimeoutSeconds}s timeout (window close ignored)" |
            Add-Content (Join-Path $logDir "command.txt")
    } else {
        "closed window after ${TimeoutSeconds}s timeout (expected: the probe loops)" |
            Add-Content (Join-Path $logDir "command.txt")
    }
} else {
    "exited with code $($proc.ExitCode)" | Add-Content (Join-Path $logDir "command.txt")
}
Start-Sleep -Milliseconds 500

if (-not (Test-Path $logFile)) { throw "no log file was written: $logFile" }
$log = Get-Content $logFile
$dbg = $log | Select-String -SimpleMatch "(DbgPrint) HEL "
$sc = $log | Select-String -SimpleMatch "Unhandled syscall"
$launch = $log | Select-String -SimpleMatch "Launching module"
$thread = $log | Select-String -SimpleMatch "XThread::Execute"
$errors = $log | Select-String -Pattern "^!|Failed|failed" | Select-Object -First 10

Write-Host "Log: $logFile"
Write-Host "Launch lines:"; $launch | ForEach-Object { Write-Host "  $_" }
Write-Host "Thread lines:"; $thread | Select-Object -First 3 | ForEach-Object { Write-Host "  $_" }
Write-Host "DbgPrint markers:"; $dbg | ForEach-Object { Write-Host "  $_" }
Write-Host "Syscall markers:"; $sc | ForEach-Object { Write-Host "  $_" }
Write-Host "First error-like lines:"; $errors | ForEach-Object { Write-Host "  $_" }

$summary = [ordered]@{
    run = $RunName; target = "$targetPath"; xenia_sha256 = $hash
    dbgprint_marker = @($dbg | ForEach-Object { $_.Line })
    syscall_marker = @($sc | ForEach-Object { $_.Line })
    launched = ($launch.Count -gt 0); log_lines = $log.Count
}
$summary | ConvertTo-Json -Depth 3 | Set-Content (Join-Path $logDir "summary.json")
if ($dbg.Count -eq 0 -or $sc.Count -eq 0) {
    Write-Host "RESULT: markers missing"
    exit 1
}
Write-Host "RESULT: guest execution observed"
exit 0
