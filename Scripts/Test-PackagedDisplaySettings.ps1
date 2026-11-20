#requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$PackageRoot)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $PackageRoot).Path
$exe = Join-Path $root 'Abandoned/Binaries/Win64/Abandoned.exe'
$profile = (New-Item -ItemType Directory -Path (Join-Path $PSScriptRoot "../Saved/DisplaySettings/$([guid]::NewGuid())")).FullName

function Test-DisplayRun([string]$Name, [string[]]$Commands, [int[]]$Sync, [int[]]$Limits) {
    $log = Join-Path $profile "$Name.log"
    $start = [Diagnostics.ProcessStartInfo]::new($exe)
    $start.WorkingDirectory = $root
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    foreach ($arg in @('-vulkan', '-unattended', '-windowed', '-ResX=1280', '-ResY=720',
        "-UserDir=$profile", "-abslog=$log", '-ExecCmds=CsvProfile FRAMES=240',
        '-ExitAfterCsvProfiling', "-csvExecCmds=$($Commands -join ',')")) {
        $start.ArgumentList.Add($arg)
    }
    $process = [Diagnostics.Process]::Start($start)
    try {
        if (!$process.WaitForExit(120000)) {
            $process.Kill($true)
            throw "Packaged display check timed out. See $log"
        }
        if ($process.ExitCode -ne 0) { throw "Game exited with $($process.ExitCode). See $log" }
    } finally {
        $process.Dispose()
    }
    $lines = Get-Content -LiteralPath $log
    if (!($lines -match 'Vulkan RHI ShaderPlatform for SM6: VULKAN_SM6') -or !($lines -match 'LogExit: Exiting\.')) {
        throw "Incomplete Vulkan run. See $log"
    }
    foreach ($probe in @(@('r\.VSync', $Sync), @('t\.MaxFPS', $Limits))) {
        $values = @(foreach ($line in $lines) {
            if ($line -match "\b$($probe[0]) = `"(\d+)(?:\.0+)?`"\s+LastSetBy:") { [int]$Matches[1] }
        })
        if (($values -join ',') -ne ($probe[1] -join ',')) {
            throw "$Name $($probe[0]) expected $($probe[1] -join ',') but got $($values -join ','). See $log"
        }
    }
}

# Use the same controller actions as the pause-menu buttons and read back the live RHI/engine settings.
Test-DisplayRun 'NewProfile' @(
    '30:r.VSync', '31:t.MaxFPS', '40:AbandonedToggleVSync', '45:r.VSync', '46:t.MaxFPS',
    '50:AbandonedCycleFrameLimit', '55:t.MaxFPS', '60:AbandonedCycleFrameLimit', '65:t.MaxFPS',
    '70:AbandonedCycleFrameLimit', '75:t.MaxFPS', '80:AbandonedCycleFrameLimit', '85:t.MaxFPS',
    '90:AbandonedCycleFrameLimit', '95:t.MaxFPS', '100:r.VSync'
) @(1, 0, 0) @(60, 60, 120, 180, 30, 60, 120)
Test-DisplayRun 'Restart' @(
    '30:r.VSync', '31:t.MaxFPS', '40:AbandonedToggleVSync', '45:r.VSync', '46:t.MaxFPS'
) @(0, 1) @(120, 120)
# Reproduce a saved profile from the previous unlimited default.
$settings = Join-Path $profile 'Saved/Config/Windows/GameUserSettings.ini'
@('[/Script/Engine.GameUserSettings]', 'Version=5', 'bUseVSync=False', 'FrameRateLimit=0.000000') |
    Set-Content -LiteralPath $settings
Test-DisplayRun 'Legacy' @('30:r.VSync', '31:t.MaxFPS') @(0) @(60)
Write-Output "Packaged Vulkan display settings passed. Logs in $profile"
