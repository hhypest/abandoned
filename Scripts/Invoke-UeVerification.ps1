[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$EngineRoot,

    [ValidateSet('EntryCourse', 'WristTerminal', 'WristTerminalNegative')]
    [string[]]$Scenario = @('EntryCourse', 'WristTerminal', 'WristTerminalNegative'),

    [ValidateSet(60, 120)]
    [int]$FramesPerSecond = 60,

    [ValidateRange(1, 3600)]
    [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$engine = (Resolve-Path -LiteralPath $EngineRoot).Path
$editor = Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor.exe'
$project = Join-Path $repo 'Abandoned.uproject'
if (!(Test-Path -LiteralPath $editor -PathType Leaf)) {
    throw "UnrealEditor.exe was not found at $editor"
}
$buildVersion = Get-Content -LiteralPath (Join-Path $engine 'Engine/Build/Build.version') -Raw | ConvertFrom-Json
if ($buildVersion.MajorVersion -ne 5 -or $buildVersion.MinorVersion -ne 7) {
    throw 'This project requires Unreal Engine 5.7.'
}

$entry = '/Engine/Maps/Entry'
$vanguard = "${entry}?game=/Script/Abandoned.VanguardRouteGameMode"
$wristSuccess = @('Wrist terminal verification passed', 'Radiation passage verification passed')
$scenarios = @{
    EntryCourse = @{
        Map = $entry
        Flag = '-VerifyEntryCourse'
        Success = @('Training chamber ready. Balance', 'Entry course verification passed',
                    'Passage handhold verified', 'Vanguard verification passed')
    }
    WristTerminal = @{
        Map = "${vanguard}?Chamber=Passage?RadiationRun=1"
        Flag = '-VerifyWristTerminal'
        Success = $wristSuccess
    }
    WristTerminalNegative = @{
        Map = "${vanguard}?Chamber=Vanguard"
        Flag = '-VerifyWristTerminal'
        Success = $wristSuccess
        ExpectedError = 'LogWristTerminal: Error: Wrist terminal verification failed: acquired record should survive travel and occur once'
    }
}

function Invoke-Scenario($name, $definition, $profile) {
    $null = New-Item -ItemType Directory -Path $profile
    $log = Join-Path $profile 'Game.log'
    $result = [ordered]@{
        Name = $name
        Map = $definition.Map
        Flag = $definition.Flag
        Log = $log
        ExitCode = $null
        TimedOut = $false
        Vulkan = $false
        Gpu = $null
        SuccessMarkers = @()
        FailureMarkers = @()
        Outcome = 'Failed'
        Reason = $null
    }

    $start = [System.Diagnostics.ProcessStartInfo]::new($editor)
    $start.UseShellExecute = $false
    $start.WorkingDirectory = $repo
    foreach ($arg in @($project, $definition.Map, $definition.Flag, '-game', '-vulkan', '-unattended',
                       '-windowed', '-ResX=1280', '-ResY=720', '-benchmark', "-fps=$FramesPerSecond",
                       "-UserDir=$profile", "-abslog=$log")) {
        $start.ArgumentList.Add($arg)
    }
    $process = [System.Diagnostics.Process]::Start($start)
    try {
        if ($process.WaitForExit($TimeoutSeconds * 1000)) {
            $result.ExitCode = $process.ExitCode
        } else {
            $result.TimedOut = $true
            if (!$process.HasExited) {
                $process.Kill($true)
                $null = $process.WaitForExit(5000)
            }
        }
    } finally {
        $process.Dispose()
    }

    if ($result.TimedOut) {
        $result.Reason = "Timed out after $TimeoutSeconds seconds."
        return $result
    }
    if (!(Test-Path -LiteralPath $log -PathType Leaf)) {
        $result.Reason = 'Unreal did not create the game log.'
        return $result
    }

    $lines = Get-Content -LiteralPath $log
    $result.Vulkan = [bool]($lines -match 'LogVulkanRHI:.*Vulkan RHI ShaderPlatform for SM5: VULKAN_SM5')
    $gpuLine = $lines -match 'LogVulkanRHI:.*DeviceName:' | Select-Object -First 1
    if ($gpuLine -match 'DeviceName:\s*(.+)$') { $result.Gpu = $Matches[1].Trim() }
    $exitRequestPattern = 'FPlatformMisc::RequestExitWithStatus\(\s*\d+\s*,\s*[1-9]\d*\s*,'
    $result.FailureMarkers = @($lines -match ": Error:|Fatal error:|$exitRequestPattern")
    $result.SuccessMarkers = @($definition.Success | Where-Object {
        $lines -match [regex]::Escape($_)
    })

    # Use the same verdict for the positive cases and the negative control.
    if (!$result.Vulkan) {
        $result.Reason = 'Vulkan SM5 was not confirmed in the game log.'
    } elseif ($result.FailureMarkers.Count -gt 0) {
        $result.Reason = $result.FailureMarkers[0]
    } elseif ($result.ExitCode -ne 0) {
        $result.Reason = "Unreal exited with code $($result.ExitCode)."
    } elseif ($result.SuccessMarkers.Count -ne $definition.Success.Count) {
        $missing = $definition.Success | Where-Object { $_ -notin $result.SuccessMarkers }
        $result.Reason = "Missing success markers: $($missing -join ', ')"
    } else {
        $result.Outcome = 'Passed'
    }

    if ($definition.ExpectedError) {
        $expected = [regex]::Escape($definition.ExpectedError)
        $unexpected = $result.FailureMarkers -notmatch "$expected|$exitRequestPattern"
        if ($result.Outcome -eq 'Failed' -and $result.Vulkan -and $result.ExitCode -in @(0, 1) -and
            $result.FailureMarkers -match $expected -and $result.FailureMarkers -match $exitRequestPattern -and
            !$unexpected -and $result.SuccessMarkers.Count -eq 0) {
            $result.Outcome = 'ExpectedFailure'
        } else {
            $result.Reason = "Negative control did not produce the expected verification failure. $($result.Reason)"
            $result.Outcome = 'Failed'
        }
    }
    return $result
}

$commit = git -C $repo rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Could not identify the tested revision.' }
$trackedChanges = git -C $repo status --porcelain --untracked-files=no
if ($LASTEXITCODE -ne 0) { throw 'Could not inspect the tracked working tree.' }
$runId = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N')
$root = Join-Path $repo "Saved/UeVerification/$runId"
$null = New-Item -ItemType Directory -Path $root -Force

$results = @(foreach ($name in ($Scenario | Select-Object -Unique)) {
    Write-Host "Running $name on Vulkan SM5..."
    $profile = Join-Path $root $name
    try {
        Invoke-Scenario $name $scenarios[$name] $profile
    } catch {
        [ordered]@{ Name = $name; Outcome = 'Failed'; Reason = $_.Exception.Message; Log = (Join-Path $profile 'Game.log') }
    }
})
$report = [ordered]@{
    Commit = $commit
    TrackedChanges = [bool]$trackedChanges
    Unreal = "$($buildVersion.MajorVersion).$($buildVersion.MinorVersion).$($buildVersion.PatchVersion)"
    OperatingSystem = $PSVersionTable.OS
    Rhi = 'VULKAN_SM5'
    Resolution = '1280x720'
    FramesPerSecond = $FramesPerSecond
    Scenarios = $results
}
$reportPath = Join-Path $root 'report.json'
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding utf8
foreach ($result in $results) {
    Write-Host "$($result.Name): $($result.Outcome) (Unreal exit code: $($result.ExitCode))"
    if ($result.Reason) { Write-Host $result.Reason }
}
Write-Host "Report: $reportPath"
if ($results.Outcome -contains 'Failed') {
    throw 'One or more Unreal verification scenarios failed.'
}
