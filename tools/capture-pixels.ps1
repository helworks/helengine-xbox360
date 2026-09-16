<#
.SYNOPSIS
Reads pixels from a window capture (PNG) and optionally compares two captures
pixel by pixel. Uses System.Drawing only, so no extra dependency.

.EXAMPLE
./tools/capture-pixels.ps1 -Image build/logs/cube-a1/capture-0.png -Points "560,360","720,360"
./tools/capture-pixels.ps1 -Image a.png -Compare b.png
./tools/capture-pixels.ps1 -Image a.png -Background "10,10"   # bounding box of non-background pixels
#>
param(
    [Parameter(Mandatory = $true)][string]$Image,
    [string[]]$Points = @(),
    [string]$Compare = "",
    [string]$Background = "",
    [int]$Tolerance = 16
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
$a = New-Object System.Drawing.Bitmap ((Resolve-Path $Image).Path)
Write-Host ("{0}: {1}x{2}" -f $Image, $a.Width, $a.Height)

foreach ($p in $Points) {
    $xy = $p -split ","
    $c = $a.GetPixel([int]$xy[0], [int]$xy[1])
    Write-Host ("  ({0},{1}) rgb=({2},{3},{4})" -f $xy[0], $xy[1], $c.R, $c.G, $c.B)
}

if ($Background) {
    $xy = $Background -split ","
    $bg = $a.GetPixel([int]$xy[0], [int]$xy[1])
    $minX = $a.Width; $minY = $a.Height; $maxX = -1; $maxY = -1
    for ($y = 0; $y -lt $a.Height; $y++) {
        for ($x = 0; $x -lt $a.Width; $x++) {
            $c = $a.GetPixel($x, $y)
            if ([Math]::Abs($c.R - $bg.R) -gt $Tolerance -or [Math]::Abs($c.G - $bg.G) -gt $Tolerance -or
                [Math]::Abs($c.B - $bg.B) -gt $Tolerance) {
                if ($x -lt $minX) { $minX = $x }; if ($x -gt $maxX) { $maxX = $x }
                if ($y -lt $minY) { $minY = $y }; if ($y -gt $maxY) { $maxY = $y }
            }
        }
    }
    Write-Host ("  background rgb=({0},{1},{2})" -f $bg.R, $bg.G, $bg.B)
    if ($maxX -ge 0) {
        Write-Host ("  non-background bbox x={0}..{1} y={2}..{3} size={4}x{5}" -f $minX, $maxX, $minY, $maxY, ($maxX - $minX + 1), ($maxY - $minY + 1))
    } else { Write-Host "  no non-background pixels" }
}

if ($Compare) {
    $b = New-Object System.Drawing.Bitmap ((Resolve-Path $Compare).Path)
    if ($a.Width -ne $b.Width -or $a.Height -ne $b.Height) {
        Write-Host ("  size differs: {0}x{1} vs {2}x{3}" -f $a.Width, $a.Height, $b.Width, $b.Height); exit 1
    }
    $diff = 0; $maxDelta = 0
    for ($y = 0; $y -lt $a.Height; $y++) {
        for ($x = 0; $x -lt $a.Width; $x++) {
            $ca = $a.GetPixel($x, $y); $cb = $b.GetPixel($x, $y)
            $d = [Math]::Max([Math]::Abs($ca.R - $cb.R), [Math]::Max([Math]::Abs($ca.G - $cb.G), [Math]::Abs($ca.B - $cb.B)))
            if ($d -gt 0) { $diff++; if ($d -gt $maxDelta) { $maxDelta = $d } }
        }
    }
    Write-Host ("  compare {0}: differing pixels={1} max channel delta={2}" -f $Compare, $diff, $maxDelta)
    $b.Dispose()
}
$a.Dispose()
