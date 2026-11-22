#requires -Version 7.0
$ErrorActionPreference = 'Stop'
$reader = Join-Path $PSScriptRoot 'Read-UePackageReceipt.ps1'
$temp = [IO.Path]::GetTempPath()
$fixture = Join-Path $temp ("abandoned-package-receipt-" + [guid]::NewGuid().ToString('N'))
$files = @(
    'Abandoned/Binaries/Win64/Abandoned.exe',
    'Abandoned/Content/Paks/Game.pak',
    'Abandoned/Content/Paks/Game.utoc',
    'Abandoned/Content/Paks/Game.ucas',
    'Abandoned/Content/Paks/Game_s1.ucas',
    'Abandoned/Plugins/Test/Content/Paks/Extra.pak'
)
try {
    foreach ($relativeFile in $files) {
        $file = Join-Path $fixture $relativeFile
        [IO.Directory]::CreateDirectory((Split-Path -Parent $file)) | Out-Null
        [IO.File]::WriteAllText($file, $relativeFile)
    }
    $chunk = Join-Path $fixture $files[4]
    $initial = & $reader -PackageRoot $fixture -Verify {
        $blocked = $false
        try { [IO.File]::WriteAllText($chunk, 'alternate mesh') } catch [IO.IOException] { $blocked = $true }
        if (!$blocked) { throw 'A running package allowed a mounted chunk to be replaced.' }
        [pscustomobject]@{ Name = 'Fixture'; Outcome = 'Passed' }
    }
    if ($initial.ContentContainers.Count -ne 5 -or $initial.Scenarios[0].Outcome -ne 'Passed') {
        throw 'Missing container or verification result.'
    }
    # Replace only the split IoStore payload, leaving its byte count and the EXE unchanged.
    [IO.File]::WriteAllText($chunk, 'Z' * (Get-Item $chunk).Length)
    $changed = & $reader -PackageRoot $fixture
    $diff = @(Compare-Object $initial.ContentContainers $changed.ContentContainers -Property Path, Bytes, Sha256)
    if ($initial.ExecutableSha256 -cne $changed.ExecutableSha256 -or $diff.Count -ne 2 -or
        @($diff | Where-Object Path -cne $files[4]).Count) {
        throw 'The content-only change was not isolated to its partition.'
    }

    $rejected = $false
    try {
        & $reader -PackageRoot $fixture -Verify { [IO.File]::WriteAllText("$fixture/late.pak", 'late') } | Out-Null
    } catch {
        if ($_.Exception.Message -notlike 'The set of staged Unreal containers changed*') { throw }
        $rejected = $true
    }
    if (!$rejected) { throw 'A container was added during a run without invalidating its receipt.' }
    Write-Host 'Package identity distinguished a content-only change and rejected in-run mutations.'
} finally {
    if (Test-Path -LiteralPath $fixture) {
        $target = (Resolve-Path -LiteralPath $fixture).Path
        if ([IO.Path]::GetDirectoryName($target) -ne $temp.TrimEnd('\','/') -or
            (Split-Path -Leaf $target) -notmatch '^abandoned-package-receipt-[a-f0-9]{32}$') {
            throw "Unexpected fixture directory: $target"
        }
        Remove-Item -LiteralPath $target -Recurse -Force
    }
}
