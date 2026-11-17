$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

Push-Location $repo
try {
    git lfs fsck --dry-run --objects --pointers HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Git LFS object or pointer validation failed.' }

    $listing = git lfs ls-files --json HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Could not list Git LFS assets.' }
    $assets = ($listing -join "`n" | ConvertFrom-Json).files
    foreach ($asset in $assets) {
        $path = Join-Path $repo $asset.name
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Git LFS asset is missing from the working tree: $($asset.name)"
        }
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $asset.oid) {
            throw "Working-tree content does not match the Git LFS object: $($asset.name)"
        }
    }
    Write-Output "Verified $($assets.Count) materialized Git LFS assets."
} finally {
    Pop-Location
}
