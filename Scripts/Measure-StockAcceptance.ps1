#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackageRoot,
    [Parameter(Mandatory)][datetimeoffset]$PreviousBootUtc,
    [Parameter(Mandatory)][string]$Captures
)
$ErrorActionPreference = 'Stop'
$boot = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime()
if ($boot -le $PreviousBootUtc.UtcDateTime) { throw 'A new Windows boot is required for the cold sample.' }
if (Get-Process -Name Abandoned -ErrorAction SilentlyContinue) { throw 'The game is already running.' }
# A used capture directory must never be reused to relabel a later attempt as cold.
$root = New-Item -ItemType Directory -Path $Captures -ErrorAction Stop
$PreviousBootUtc.UtcDateTime.ToString('o') | Set-Content -LiteralPath (Join-Path $root 'PreviousBootUtc.txt')
foreach ($tag in 'C','W1','W2','W3') {
    $path = & (Join-Path $PSScriptRoot 'Measure-VulkanPackage.ps1') -PackageRoot $PackageRoot -StockDisplay -WeightVisits 16
    $path | Set-Content -LiteralPath (Join-Path $root "$tag.path")
}
