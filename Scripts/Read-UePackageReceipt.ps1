[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackageRoot,
    [scriptblock]$Verify
)

$ErrorActionPreference = 'Stop'
$packageDirectory = (Resolve-Path -LiteralPath $PackageRoot).Path
$gameBinary = 'Abandoned/Binaries/Win64/Abandoned.exe'

function Get-ContainerNames([string]$Directory) {
    [string[]]$names = @(Get-ChildItem -LiteralPath $Directory -File -Recurse |
        Where-Object Extension -in '.pak', '.utoc', '.ucas' |
        ForEach-Object { [IO.Path]::GetRelativePath($Directory, $_.FullName).Replace('\', '/') })
    [Array]::Sort($names, [StringComparer]::Ordinal)
    $names
}

$pinned = [ordered]@{}
try {
    $containerNames = @(Get-ContainerNames $packageDirectory)
    foreach ($relative in (@($gameBinary) + $containerNames)) {
        # FileShare.Read permits Unreal to mount the same files. Delay reads until the game
        # exits so computing hashes does not warm the OS cache before a cold-start test.
        $pinned[$relative] = [IO.File]::OpenRead((Join-Path $packageDirectory $relative))
    }

    $verifiedScenarios = @()
    if ($Verify) {
        $verifiedScenarios = @(& $Verify)
        $after = @(Get-ContainerNames $packageDirectory)
        if ($after.Count -ne $containerNames.Count -or ($after -join "`n") -cne ($containerNames -join "`n")) {
            throw 'The set of staged Unreal containers changed during verification.'
        }
    }
    [pscustomobject]@{
        ExecutableSha256 = (Get-FileHash -InputStream $pinned[$gameBinary] -Algorithm SHA256).Hash
        ContentContainers = @(foreach ($relative in $containerNames) {
            $stream = $pinned[$relative]
            [pscustomobject]@{
                Path = $relative
                Bytes = $stream.Length
                Sha256 = (Get-FileHash -InputStream $stream -Algorithm SHA256).Hash
            }
        })
        Scenarios = $verifiedScenarios
    }
} finally {
    foreach ($stream in $pinned.Values) { $stream.Dispose() }
}
