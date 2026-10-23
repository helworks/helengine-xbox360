# Shared host-path resolution for build scripts that mount real sources read-only.
function Get-X360CanonicalPath {
    param([Parameter(Mandatory)][string]$Path)
    $full = [IO.Path]::GetFullPath($Path)
    if ($PSVersionTable.OS -match 'Darwin' -and ($full -eq '/tmp' -or $full.StartsWith('/tmp/'))) {
        $temporaryAlias = Get-Item -LiteralPath '/tmp' -Force
        $temporaryTarget = [string]$temporaryAlias.Target
        if (($temporaryAlias.Attributes -band [IO.FileAttributes]::ReparsePoint) -and
            @('private/tmp', '/private/tmp') -contains $temporaryTarget) {
            $aliasTarget = [IO.Path]::GetFullPath((Join-Path '/' $temporaryTarget))
            $targetItem = Get-Item -LiteralPath $aliasTarget -Force
            if ($aliasTarget -eq '/private/tmp' -and -not ($targetItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                # Canonicalize only the exact macOS system alias. Nested links
                # are still checked and rejected by the output guard below.
                return $aliasTarget + $full.Substring(4)
            }
        }
    }
    return $full
}

function Resolve-X360HostPath {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$RepositoryRoot)
    if ([IO.Path]::IsPathRooted($Path)) { return Get-X360CanonicalPath $Path }
    return Get-X360CanonicalPath (Join-Path $RepositoryRoot $Path)
}

function Assert-X360ExternalOutput {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$RepositoryRoot)
    $full = (Get-X360CanonicalPath $Path).TrimEnd('\', '/')
    $source = (Get-X360CanonicalPath $RepositoryRoot).TrimEnd('\', '/')
    if ($full -eq [IO.Path]::GetPathRoot($full).TrimEnd('\', '/') -or
        $full.Equals($source, [StringComparison]::OrdinalIgnoreCase) -or
        $full.StartsWith($source + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Native outputs must be outside the source checkout and cannot name a filesystem root.'
    }
    $ancestor = $full
    while ($ancestor) {
        if (Test-Path -LiteralPath $ancestor) {
            $item = Get-Item -LiteralPath $ancestor -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw 'Native output paths cannot cross filesystem links.'
            }
        }
        $ancestor = [IO.Path]::GetDirectoryName($ancestor)
    }
    return $full
}
