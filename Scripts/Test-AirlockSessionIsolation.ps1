#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EngineRoot,
    [ValidateSet('AirlockSessionIsolation', 'RadiationTerminalReplay')][string]$TestName = 'AirlockSessionIsolation',
    [ValidateRange(1, 3600)][int]$TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. "$PSScriptRoot/TrustedBuild/FunctionalTests.ps1"
$name = "Abandoned.Verification.$TestName"
$directory = Join-Path $repo "Saved/$TestName/$([guid]::NewGuid().ToString('N'))"
$editor = Invoke-UEAutomationEditor $EngineRoot $repo $directory @($name) $TimeoutSeconds
$null = Read-UEAutomationReport $directory @($name) $editor
Write-Output "$name passed. Report: $directory/Report/index.json"
