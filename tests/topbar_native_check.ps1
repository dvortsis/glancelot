# TOPBAR-THEME-01 native Windows check (manual; needs an interactive desktop, moves the real mouse pointer).
# Default: screen captures (including the OS caption) of dark/light, 540/1180, framed/frameless, active/inactive.
# -Interact: drag, edge resize, double-click maximize/restore and the app-drawn minimize/maximize/close controls.
# Media is generated into $OutDir; each launch uses an isolated GLANCELOT_CONFIG_DIR and --no-resume.
# Example (Qt bin on PATH): ./tests/topbar_native_check.ps1 -Exe build-topbar-theme/glancelot.exe -OutDir build-topbar-theme/.cache/topbar-native
param([Parameter(Mandatory)][string]$Exe, [Parameter(Mandatory)][string]$OutDir, [switch]$Interact)
$ErrorActionPreference = 'Stop'
$Exe = (Resolve-Path -LiteralPath $Exe).Path
$OutDir = [System.IO.Path]::GetFullPath($OutDir)
$previousConfig = $env:GLANCELOT_CONFIG_DIR
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class W {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, IntPtr e);
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int a, out RECT r, int s);
  public static RECT Frame(IntPtr h) { RECT r; if (DwmGetWindowAttribute(h, 9, out r, 16) != 0) GetWindowRect(h, out r); return r; }
  public static void Down() { mouse_event(2, 0, 0, 0, IntPtr.Zero); } public static void Up() { mouse_event(4, 0, 0, 0, IntPtr.Zero); }
}
'@
[W]::SetProcessDPIAware() | Out-Null
New-Item -ItemType Directory -Force $OutDir | Out-Null
$media = Join-Path $OutDir 'media'; New-Item -ItemType Directory -Force $media | Out-Null
$bmp = New-Object System.Drawing.Bitmap 640, 400
for ($y = 0; $y -lt 400; $y += 4) { for ($x = 0; $x -lt 640; $x += 4) { $c = [System.Drawing.Color]::FromArgb(40 + [int]($x / 5), 90 + [int]($y / 6), 150); for ($i = 0; $i -lt 4; $i++) { for ($j = 0; $j -lt 4; $j++) { $bmp.SetPixel($x + $i, $y + $j, $c) } } } }
$image = Join-Path $media 'Harbour at dusk.png'; $bmp.Save($image, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()

function Capture([IntPtr]$h, [string]$path) {
  $r = [W]::Frame($h); $w = $r.R - $r.L; $hh = $r.B - $r.T
  $shot = New-Object System.Drawing.Bitmap $w, $hh
  $g = [System.Drawing.Graphics]::FromImage($shot); $g.CopyFromScreen($r.L, $r.T, 0, 0, $shot.Size); $g.Dispose()
  $shot.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $shot.Dispose()
}
function Wake([IntPtr]$h) { $r = [W]::Frame($h); $x = [int](($r.L + $r.R) / 2); $y = [int](($r.T + $r.B) / 2); for ($k = 0; $k -lt 4; $k++) { [W]::SetCursorPos($x + 3 * $k, $y); Start-Sleep -Milliseconds 30 }; Start-Sleep -Milliseconds 350 }
function Launch([string]$mode, [string]$size, [bool]$frameless, [int]$quit) {
  # Preserve previous runs and never recursively delete a caller-supplied path.
  $config = Join-Path $OutDir ("config-$mode-$size-$frameless-" + [Guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Path $config | Out-Null
  $env:GLANCELOT_CONFIG_DIR = $config
  $args = @('--no-resume', '--theme-mode', $mode, '--window-size', $size, '--quit-after', "$quit", '--report', (Join-Path $OutDir "report-$mode-$size-$frameless.json"))
  if ($frameless) { $args += '--frameless' }
  $args += $image
  $p = Start-Process -FilePath $Exe -ArgumentList ($args | % { if ($_ -match ' ') { '"' + $_ + '"' } else { $_ } }) -PassThru
  for ($i = 0; $i -lt 50 -and $p.MainWindowHandle -eq 0; $i++) { Start-Sleep -Milliseconds 100; $p.Refresh() }
  Start-Sleep -Milliseconds 1200; $p.Refresh()
  return $p
}

$form = New-Object System.Windows.Forms.Form; $form.Text = 'Other window'; $form.StartPosition = 'Manual'; $form.Location = New-Object System.Drawing.Point(-3000, -3000); $form.Size = New-Object System.Drawing.Size(240, 120); $form.TopMost = $false
$results = @()
try {
if (-not $Interact) { foreach ($mode in 'dark', 'light') { foreach ($size in '540x360', '1180x780') { foreach ($frameless in $false, $true) {
  $p = Launch $mode $size $frameless 9000; $h = $p.MainWindowHandle
  $name = "$mode-$($size.Split('x')[0])-$(if ($frameless) { 'frameless' } else { 'framed' })"
  [W]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 600
  Wake $h; Capture $h (Join-Path $OutDir "native-$name-active.png")
  $form.Show(); $form.Activate(); [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 600
  $inactive = [W]::GetForegroundWindow() -ne $h
  Wake $h; Capture $h (Join-Path $OutDir "native-$name-inactive.png")
  $form.Hide(); [System.Windows.Forms.Application]::DoEvents()
  $results += "$name captured (inactive state confirmed: $inactive)"
  $p.WaitForExit(15000) | Out-Null; $results += "$name exit=$($p.ExitCode)"
} } } }

if ($Interact) {
  function Click([int]$x, [int]$y) { [W]::SetCursorPos($x, $y); Start-Sleep -Milliseconds 150; [W]::Down(); [W]::Up(); Start-Sleep -Milliseconds 700 }
  function DClick([int]$x, [int]$y) { [W]::SetCursorPos($x, $y); Start-Sleep -Milliseconds 150; [W]::Down(); [W]::Up(); Start-Sleep -Milliseconds 60; [W]::Down(); [W]::Up(); Start-Sleep -Milliseconds 800 }
  function Drag([int]$x, [int]$y, [int]$dx, [int]$dy) { [W]::SetCursorPos($x, $y); Start-Sleep -Milliseconds 150; [W]::Down(); Start-Sleep -Milliseconds 150; for ($k = 1; $k -le 10; $k++) { [W]::SetCursorPos($x + [int]($dx * $k / 10), $y + [int]($dy * $k / 10)); Start-Sleep -Milliseconds 40 }; [W]::Up(); Start-Sleep -Milliseconds 500 }
  function Fmt($r) { "$($r.L),$($r.T),$($r.R),$($r.B)" }
  foreach ($frameless in $true, $false) {
    $tag = if ($frameless) { 'frameless' } else { 'framed' }
    $p = Launch 'dark' '1180x780' $frameless 30000; $h = $p.MainWindowHandle; $scale = [W]::GetDpiForWindow($h) / 96.0
    [W]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 500
    $wa = [System.Windows.Forms.Screen]::FromHandle($h).WorkingArea; $work = "$($wa.Left),$($wa.Top),$($wa.Right),$($wa.Bottom)"
    $barY = if ($frameless) { [int](24 * $scale) } else { [int](15 * $scale) }
    $r = [W]::Frame($h); Drag ($r.L + [int](640 * $scale)) ($r.T + $barY) 120 60; $m = [W]::Frame($h)
    $results += "$tag drag on empty title-bar space: frame $(Fmt $r) -> $(Fmt $m)"
    if ($frameless) {
      Drag ($m.R - [int](2 * $scale)) ([int](($m.T + $m.B) / 2)) -80 0; $s = [W]::Frame($h)
      $results += "$tag right-edge resize: width $($m.R - $m.L) -> $($s.R - $s.L)"
      Drag ([int](($s.L + $s.R) / 2)) ($s.B - [int](2 * $scale)) 0 -60; $s2 = [W]::Frame($h)
      $results += "$tag bottom-edge resize: height $($s.B - $s.T) -> $($s2.B - $s2.T)"
    }
    $n = [W]::Frame($h); DClick ($n.L + [int](640 * $scale)) ($n.T + $barY); $x = [W]::Frame($h)
    $results += "$tag double-click: frame $(Fmt $n) -> $(Fmt $x) (work area $work, IsZoomed=$([W]::IsZoomed($h)))"
    Capture $h (Join-Path $OutDir "native-dark-maximized-$tag.png")
    DClick ([int](($x.L + $x.R) / 2)) ($x.T + $barY); $y = [W]::Frame($h)
    $results += "$tag second double-click: frame -> $(Fmt $y) (restored: $((Fmt $y) -eq (Fmt $n)))"
    if ($frameless) {
      $closeX = { param($f) $f.R - [int](28 * $scale) }; $maxX = { param($f) $f.R - [int](64 * $scale) }; $minX = { param($f) $f.R - [int](100 * $scale) }
      $f = [W]::Frame($h); [W]::SetCursorPos((& $maxX $f), $f.T + $barY); Start-Sleep -Milliseconds 400; Capture $h (Join-Path $OutDir 'native-dark-frameless-hover-maximize.png')
      Click (& $maxX $f) ($f.T + $barY); $g = [W]::Frame($h); $results += "$tag maximize button: frame -> $(Fmt $g) (work area $work)"
      Click (& $maxX $g) ($g.T + $barY); $g2 = [W]::Frame($h); $results += "$tag restore button: frame -> $(Fmt $g2) (restored: $((Fmt $g2) -eq (Fmt $f)))"
      Click (& $minX $g2) ($g2.T + $barY); $results += "$tag minimize button: IsIconic=$([W]::IsIconic($h))"
      [W]::ShowWindow($h, 9) | Out-Null; [W]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 700; $results += "$tag restored from taskbar state: IsIconic=$([W]::IsIconic($h))"
      $f = [W]::Frame($h); [W]::SetCursorPos((& $closeX $f), $f.T + $barY); Start-Sleep -Milliseconds 400; Capture $h (Join-Path $OutDir 'native-dark-frameless-hover-close.png')
      [W]::Down(); [W]::Up(); $exited = $p.WaitForExit(5000); $results += "$tag close button: process exited=$exited code=$(if ($exited) { $p.ExitCode })"
    }
    if (-not $p.HasExited) { $p.WaitForExit(35000) | Out-Null }
  }
}
} finally {
  $form.Dispose()
  $env:GLANCELOT_CONFIG_DIR = $previousConfig
}
$results
