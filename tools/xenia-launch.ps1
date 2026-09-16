<#
.SYNOPSIS
Launches a guest executable in the pinned Xenia build with an isolated log
and storage root, answers the first-run disclaimer dialog, closes the window
on timeout and terminates only the launched process if that is ignored.
Writes command.txt into the run folder and returns the run folder path.
#>
param(
    [string]$Config = "tools/config.local.json",
    [string]$Target = "build/xenia/helengine.xex",
    [int]$TimeoutSeconds = 30,
    [string]$RunName = "",
    [string[]]$ExtraArgs = @(),
    # Window captures of the launched emulator (PrintWindow of its own main
    # window, cropped to the client area), written as capture-N.png with the
    # average client colour recorded in captures.json.
    [int]$CaptureCount = 0,
    [int]$CaptureAfterMs = 8000,
    [int]$CaptureIntervalMs = 700
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
"target sha256 $((Get-FileHash $targetPath -Algorithm SHA256).Hash)" | Add-Content (Join-Path $logDir "command.txt")

$dialogHelper = @"
using System;
using System.Text;
using System.Runtime.InteropServices;
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
if (-not ("XeniaDialog" -as [type])) { Add-Type -TypeDefinition $dialogHelper }

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
$captureHelper = @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public class XeniaCapture {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
  [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
  [DllImport("user32.dll")] static extern bool ClientToScreen(IntPtr hWnd, ref POINT point);
  [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
  // Returns "width,height,avgR,avgG,avgB" of the client area and saves a PNG.
  public static string Capture(IntPtr hWnd, string path) {
    RECT win; GetWindowRect(hWnd, out win);
    RECT client; GetClientRect(hWnd, out client);
    POINT origin = new POINT(); ClientToScreen(hWnd, ref origin);
    int w = win.Right - win.Left, h = win.Bottom - win.Top;
    if (w <= 0 || h <= 0) return null;
    using (var bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb)) {
      using (var g = Graphics.FromImage(bmp)) {
        IntPtr hdc = g.GetHdc();
        bool ok = PrintWindow(hWnd, hdc, 2); // PW_RENDERFULLCONTENT
        g.ReleaseHdc(hdc);
        if (!ok) return null;
      }
      int cx = origin.X - win.Left, cy = origin.Y - win.Top;
      int cw = client.Right - client.Left, ch = client.Bottom - client.Top;
      if (cw <= 0 || ch <= 0) return null;
      using (var crop = bmp.Clone(new Rectangle(cx, cy, cw, ch), PixelFormat.Format32bppArgb)) {
        long r = 0, gsum = 0, b = 0; long n = 0;
        for (int y = ch / 4; y < ch * 3 / 4; y += 8)
          for (int x = cw / 4; x < cw * 3 / 4; x += 8) {
            Color c = crop.GetPixel(x, y); r += c.R; gsum += c.G; b += c.B; n++;
          }
        crop.Save(path, ImageFormat.Png);
        return string.Format("{0},{1},{2},{3},{4}", cw, ch, r / n, gsum / n, b / n);
      }
    }
  }
}
"@
if ($CaptureCount -gt 0 -and -not ("XeniaCapture" -as [type])) {
    Add-Type -TypeDefinition $captureHelper -ReferencedAssemblies System.Drawing
}
$captures = @()
if ($CaptureCount -gt 0) {
    Start-Sleep -Milliseconds $CaptureAfterMs
    for ($i = 0; $i -lt $CaptureCount -and -not $proc.HasExited; $i++) {
        $proc.Refresh()
        $hwnd = $proc.MainWindowHandle
        $path = Join-Path $logDir ("capture-{0}.png" -f $i)
        $info = $null
        if ($hwnd -ne [IntPtr]::Zero) { $info = [XeniaCapture]::Capture($hwnd, $path) }
        $stamp = (Get-Date).ToString("HH:mm:ss.fff")
        if ($info) {
            $parts = $info.Split(",")
            $captures += [ordered]@{ index = $i; time = $stamp; file = (Split-Path $path -Leaf)
                width = [int]$parts[0]; height = [int]$parts[1]
                avg_rgb = @([int]$parts[2], [int]$parts[3], [int]$parts[4]) }
        } else {
            $captures += [ordered]@{ index = $i; time = $stamp; file = $null }
        }
        Start-Sleep -Milliseconds $CaptureIntervalMs
    }
    $captures | ConvertTo-Json -Depth 3 | Set-Content (Join-Path $logDir "captures.json")
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
        "closed window after ${TimeoutSeconds}s timeout" |
            Add-Content (Join-Path $logDir "command.txt")
    }
} else {
    "exited with code $($proc.ExitCode)" | Add-Content (Join-Path $logDir "command.txt")
}
Start-Sleep -Milliseconds 500
if (-not (Test-Path $logFile)) { throw "no log file was written: $logFile" }
Write-Output $logDir
