[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Path,
    [Parameter(Mandatory)][string]$Projection
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$number = '(-?\d+(?:\.\d+)?)'
if ($Projection -notmatch "rectangle X=$number Y=$number \.\. X=$number Y=$number") {
    throw "Missing glass bounds for $Path"
}
$format = [System.Globalization.CultureInfo]::InvariantCulture
$left, $top, $right, $bottom = 1..4 | ForEach-Object { [double]::Parse($Matches[$_], $format) }
$bitmap = [System.Drawing.Bitmap]::new((Resolve-Path -LiteralPath $Path).Path)
try {
    if ($left -lt 0 -or $top -lt 0 -or $right -ge $bitmap.Width -or $bottom -ge $bitmap.Height) {
        throw "Glass bounds exceed the captured frame in $Path"
    }
    # Sample inside the readout, excluding the bezel and any overlapping HUD at its edge.
    # Both captures show the green STATUS page before the flare. A blank reflective pane
    # can pass the corner-projection test but cannot supply two distinct rows of green ink.
    $xMin = [int]($left + 0.05 * ($right - $left))
    $xMax = [int]($right - 0.10 * ($right - $left))
    $yMin = [int]($top + 0.05 * ($bottom - $top))
    $yMax = [int]($bottom - 0.05 * ($bottom - $top))
    $lines = 0
    $inkRows = 0
    $previousInk = $false
    for ($y = $yMin; $y -le $yMax; $y += 2) {
        $pixels = 0
        for ($x = $xMin; $x -le $xMax; $x += 2) {
            $c = $bitmap.GetPixel($x, $y)
            if ($c.G -ge 105 -and $c.G -gt ($c.R + 12) -and $c.G -gt ($c.B + 4)) { ++$pixels }
        }
        $ink = $pixels -ge [Math]::Max(4, [Math]::Ceiling(($xMax - $xMin) * 0.016))
        if ($ink) {
            ++$inkRows
            if (!$previousInk) { ++$lines }
        }
        $previousInk = $ink
    }
    [ordered]@{ Capture = (Split-Path -Leaf $Path); LitBands = $lines; SampledInkRows = $inkRows
        Visible = $lines -ge 2 -and $inkRows -ge 4 }
} finally {
    $bitmap.Dispose()
}
