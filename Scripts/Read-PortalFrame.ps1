[CmdletBinding()]
param([Parameter(Mandatory)][string]$Path)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$bitmap = [System.Drawing.Bitmap]::new((Resolve-Path -LiteralPath $Path).Path)
try {
    if ($bitmap.Width -lt 40 -or $bitmap.Height -lt 50) { throw 'Portal frame is too small.' }
    # The fixture places a warm wall behind the linked aperture. Avoid the center reticle.
    $pixel = $bitmap.GetPixel([int][Math]::Floor($bitmap.Width * 21 / 40),
                             [int][Math]::Floor($bitmap.Height * 27 / 50))
    [ordered]@{
        Image = $Path
        Width = $bitmap.Width
        Height = $bitmap.Height
        Red = [int]$pixel.R
        Green = [int]$pixel.G
        Blue = [int]$pixel.B
        WarmWall = $pixel.R -gt 1.15 * $pixel.B
    }
} finally {
    $bitmap.Dispose()
}
