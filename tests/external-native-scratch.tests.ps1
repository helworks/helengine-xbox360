param([string]$TestRoot = '')
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$fixedRoot = if ([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT) { 'C:\temp\helengine\x360\path-tests' } else { '/tmp/helengine/x360/path-tests' }
if (-not $TestRoot) { $TestRoot = $fixedRoot }
$root = [IO.Path]::GetFullPath($TestRoot)
if ($root -ne $fixedRoot) { throw 'Path tests require their fixed temporary root.' }
if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
$generated = Join-Path $root 'generated'
$content = Join-Path $root 'content'
$output = Join-Path $root 'output'
$scratch = Join-Path $root 'scratch'
$cache = Join-Path $root 'cache'
New-Item -ItemType Directory -Force -Path $generated, $content | Out-Null
$script = Join-Path $repo 'tools/build-image.ps1'
$checks = 0
. (Join-Path $repo 'tools/build-paths.ps1')
$expectedRoot = if ($PSVersionTable.OS -match '^(Darwin|macOS)(\s|$)') { '/private/tmp/helengine/x360/path-tests' } else { $root }
if ((Resolve-X360HostPath $root $repo) -ne $expectedRoot) { throw 'Rooted output was combined with the source checkout or its platform temporary alias was not canonicalized.' }
$checks++
if ((Resolve-X360HostPath 'generated/core' $repo) -ne [IO.Path]::GetFullPath((Join-Path $repo 'generated/core'))) { throw 'Relative input was not resolved against the actual source checkout.' }
$checks++
function Validate-Scratch([string]$ScratchRoot = $scratch, [string]$Output = $output) {
    & $script -GeneratedCoreRoot $generated -ContentRoot $content -OutputRoot $Output -NativeObjectCacheRoot $cache -NativeScratchRoot $ScratchRoot -ValidateScratchOnly
}
function Must-Fail([scriptblock]$Action, [string]$Reason) {
    $failed = $false
    try { & $Action | Out-Null } catch { $failed = $true }
    if (-not $failed) { throw "Expected rejection: $Reason" }
}
Validate-Scratch | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $scratch 'native-scratch-owner.json'))) { throw 'External ownership receipt missing.' }
$checks++
$marker = Join-Path $scratch 'preserved.txt'
Set-Content -LiteralPath $marker -Value 'preserve'
Validate-Scratch | Out-Null
if ((Get-Content -LiteralPath $marker -Raw).Trim() -ne 'preserve') { throw 'Fixed scratch reuse lost an existing artifact.' }
$checks++
$receipt = Join-Path $scratch 'native-scratch-owner.json'
$before = (Get-FileHash -LiteralPath $receipt).Hash
Must-Fail { Validate-Scratch -Output (Join-Path $root 'another-output') } 'ownership mismatch'
if ((Get-FileHash -LiteralPath $receipt).Hash -ne $before) { throw 'Foreign ownership rejection changed the receipt.' }
$checks++
Must-Fail { Validate-Scratch -ScratchRoot (Join-Path $repo 'src') } 'source checkout output'
$checks++
Must-Fail { Validate-Scratch -ScratchRoot ([IO.Path]::GetPathRoot($root)) } 'filesystem root output'
$checks++
Must-Fail { Validate-Scratch -ScratchRoot $generated } 'input/output overlap'
$checks++
$unowned = Join-Path $root 'unowned'
New-Item -ItemType Directory $unowned | Out-Null
Must-Fail { Validate-Scratch -ScratchRoot $unowned } 'unowned existing scratch'
$checks++
$lock = [IO.File]::Open((Join-Path $scratch 'native-scratch.lock'), [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
try { Must-Fail { Validate-Scratch } 'concurrent scratch owner' } finally { $lock.Dispose() }
$checks++
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    $linkedOutput = Join-Path $root 'linked-output'
    New-Item -ItemType SymbolicLink -Path $linkedOutput -Target $generated | Out-Null
    Must-Fail { Assert-X360ExternalOutput (Join-Path $linkedOutput 'native') $repo } 'nested output symlink'
    if (-not (Test-Path -LiteralPath $generated)) { throw 'Rejecting a nested link changed its target.' }
    $checks++
    if ($PSVersionTable.OS -match '^(Darwin|macOS)(\s|$)') {
        Must-Fail { Assert-X360ExternalOutput '/tmp/helengine/x360/path-tests/native' '/private/tmp/helengine/x360/path-tests' } 'canonical temporary alias source containment'
        $checks++
    }
}
Write-Output "$checks/$checks external native scratch checks passed."
