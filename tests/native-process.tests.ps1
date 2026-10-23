param([string]$Python = '')
$ErrorActionPreference = 'Stop'
$isWindowsHost = [Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT
$root = if ($isWindowsHost) { 'C:\temp\helengine\x360\launcher-tests' } else { '/tmp/helengine/x360/launcher-tests' }
if (-not $Python) { $Python = if ($isWindowsHost) { 'python' } else { 'python3' } }
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $repo 'tools/native-process.ps1')
. (Join-Path $repo 'tools/build-paths.ps1')
New-Item -ItemType Directory -Force -Path $root | Out-Null
$probe = Join-Path $root 'argument probe.py'
@'
import json, os, sys
print(json.dumps({'arguments': sys.argv[2:], 'cwd': os.getcwd()}))
print('intentional stderr', file=sys.stderr)
sys.exit(int(sys.argv[1]))
'@ | Set-Content -LiteralPath $probe -Encoding UTF8
$arguments = @('value with spaces', 'quoted "value"', 'trailing slash\', '')
foreach ($expectedExitCode in @(0, 17)) {
    $result = Invoke-X360NativeTool -FilePath $Python -Arguments (@($probe, [string]$expectedExitCode) + $arguments) -WorkingDirectory $root
    if ($result.ExitCode -ne $expectedExitCode) { throw 'The native exit code was not preserved.' }
    if ($result.Output -notmatch 'intentional stderr') { throw 'The stderr stream was not captured.' }
    $payload = ($result.Output -split "\r?\n" | Where-Object { $_.StartsWith('{') } | Select-Object -First 1) | ConvertFrom-Json
    if ((Get-X360CanonicalPath $payload.cwd) -ne (Get-X360CanonicalPath $root)) { throw 'The native working directory was not preserved.' }
    if ($payload.arguments.Count -ne $arguments.Count) { throw 'The native argument count changed.' }
    for ($i = 0; $i -lt $arguments.Count; $i++) {
        if ($payload.arguments[$i] -cne $arguments[$i]) { throw "Native argument $i changed." }
    }
}
Remove-Item -LiteralPath $probe
Write-Output '2/2 native launcher tests passed (spaces, quotes, trailing slash, empty argument, stderr, exit code).'
