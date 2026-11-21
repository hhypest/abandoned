#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EngineRoot,
    [Parameter(Mandatory)][string]$RunsRoot,
    [ValidatePattern('^[0-9a-fA-F]{40}$')][string]$Commit
)
$ErrorActionPreference = 'Stop'
$launcher = Join-Path $PSScriptRoot 'Invoke-TrustedUnrealBuild.ps1'
$params = @{ EngineRoot = $EngineRoot; RunsRoot = $RunsRoot }
if ($Commit) { $params.Commit = $Commit }

$first = & $launcher @params
$baseline = Get-Content -Raw -LiteralPath "$first/report.json" | ConvertFrom-Json
$params.Commit = $baseline.Commit
$second = & $launcher @params
$repeat = Get-Content -Raw -LiteralPath "$second/report.json" | ConvertFrom-Json
if ($first -eq $second -or $baseline.Result -ne 'Passed' -or $repeat.Result -ne 'Passed' -or $repeat.Commit -ne $baseline.Commit) {
    throw 'Expected two independent successful builds of the same revision.'
}

$artifact = 'Binaries/Win64/UnrealEditor-Abandoned.dll'
$files = foreach ($run in $first, $second) { Get-Item -LiteralPath "$run/Project/$artifact" }
$hashes = foreach ($file in $files) { (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash }
# FC returns 1 on different data. Preserve that result in the report even in a strict calling shell.
$PSNativeCommandUseErrorActionPreference = $false
& "$env:SystemRoot/System32/fc.exe" /B $files[0].FullName $files[1].FullName *> $null
$comparison = $LASTEXITCODE
$result = [ordered]@{
    Commit = $baseline.Commit
    Target = $baseline.Target
    Artifact = $artifact
    Builds = @(
        [ordered]@{ Run = (Split-Path $first -Leaf); Bytes = $files[0].Length; SHA256 = $hashes[0] }
        [ordered]@{ Run = (Split-Path $second -Leaf); Bytes = $files[1].Length; SHA256 = $hashes[1] }
    )
    BinaryCompareExitCode = $comparison
}
$evidence = Join-Path $second 'reproducibility.json'
$result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $evidence -Encoding utf8
if ($comparison -ne 0 -or $hashes[0] -ne $hashes[1] -or
    $hashes[0] -ne $baseline.Products.$artifact -or $hashes[1] -ne $repeat.Products.$artifact) {
    throw "Editor DLL comparison failed. See $evidence."
}
Write-Output $evidence
