[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$EngineRoot,

    [ValidateSet('EntryCourse', 'WristTerminal', 'WristTerminalNegative', 'DoorClamp', 'Portals',
                 'Training', 'ChamberHints', 'PlateBalance', 'ParallelCircuit', 'Optics', 'FreeReflector', 'Gravity', 'Dev20', 'Dev20Recovery')]
    [string[]]$Scenario = @('EntryCourse', 'WristTerminal', 'WristTerminalNegative', 'DoorClamp', 'Portals'),

    [ValidateSet(60, 120)]
    [int]$FramesPerSecond = 60,

    [ValidateSet('1280x720', '1920x1080')]
    [string]$Resolution = '1920x1080',

    [switch]$Capture,

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
$dimensions = $Resolution.Split('x')
$expectedRenderer = [ordered]@{
    'r.DynamicGlobalIlluminationMethod' = 1
    'r.ReflectionMethod' = 1
    'r.GenerateMeshDistanceFields' = 1
    'r.Lumen.HardwareRayTracing' = 0
    'r.Nanite.ProjectEnabled' = 1
    'r.Shadow.Virtual.Enable' = 1
    'r.AntiAliasingMethod' = 4
    'sg.GlobalIlluminationQuality' = 2
    'sg.ReflectionQuality' = 2
}
$renderQueries = $expectedRenderer.Keys -join ','
$vanguard = "${entry}?game=/Script/Abandoned.VanguardRouteGameMode"
$wristSuccess = @('Wrist terminal verification passed', 'Radiation passage verification passed')
$scenarios = @{
    EntryCourse = @{
        Map = $entry
        Flag = '-VerifyEntryCourse'
        CaptureFlag = '-CaptureVanguard'
        Success = @('Training chamber ready. Balance', 'Entry course verification passed',
                    'Passage handhold verified', 'Vanguard verification passed')
    }
    WristTerminal = @{
        Map = "${vanguard}?Chamber=Passage?RadiationRun=1"
        Flag = '-VerifyWristTerminal'
        CaptureFlag = '-CaptureWristTerminal'
        Success = $wristSuccess
    }
    WristTerminalNegative = @{
        Map = "${vanguard}?Chamber=Vanguard"
        Flag = '-VerifyWristTerminal'
        Success = $wristSuccess
        ExpectedError = 'LogWristTerminal: Error: Wrist terminal verification failed: acquired record should survive travel and occur once'
    }
    DoorClamp = @{
        Map = $entry
        Flag = '-VerifyDoorClamp'
        CaptureFlag = '-CaptureDoorClamp'
        Success = @('Door clamp verification passed')
    }
    Portals = @{
        Map = "${entry}?game=/Script/Abandoned.PortalLabGameMode"
        Flag = '-VerifyPortalLab'
        CaptureFlag = '-CapturePortalLab'
        CheckPortalFrame = $true
        Success = @('Portal collision shapes verified', 'Rejected carry transit restored', 'Portal lab verification passed')
    }
    Training = @{ Map = $entry; Flag = '-VerifyTrainingChamber'; Success = @('Verification passed: player weight, placement ghost, throw, staircase, detach and reset') }
    ChamberHints = @{ Map = $entry; Flag = '-VerifyChamberHints'; Success = @('Chamber hints verification passed') }
    PlateBalance = @{ Map = $entry; Flag = '-VerifyBalanceChamber'; Success = @('Balance chamber verification passed') }
    ParallelCircuit = @{ Map = $entry; Flag = '-VerifyParallelCircuit'; Success = @('Parallel circuit verification passed') }
    Optics = @{ Map = "${entry}?game=/Script/Abandoned.PowerLabGameMode"; Flag = '-VerifyPowerLab'; Success = @('Power lab verification passed', 'Timed passage verification passed') }
    FreeReflector = @{ Map = "${entry}?game=/Script/Abandoned.PowerLabGameMode"; Flag = '-VerifyPowerLab'; ExtraFlag = '-VerifyFreeReflector'; Success = @('Free reflector verification passed', 'Timed passage verification passed') }
    Gravity = @{ Map = "${entry}?game=/Script/Abandoned.GravityLabGameMode"; Flag = '-VerifyGravityLab'; Success = @('Gravity lab verification passed') }
    Dev20 = @{ Map = "${entry}?game=/Script/Abandoned.PortalLabGameMode?Dev20Run=1"; Flag = '-VerifyDev20'; Success = @('DEV-20 verification passed: portal, Button, Weight and final exit') }
    Dev20Recovery = @{ Map = "${entry}?game=/Script/Abandoned.PortalLabGameMode?Dev20Run=1"; Flag = '-VerifyDev20'; ExtraFlag = '-VerifyDev20Recovery'; Success = @('DEV-20 recovery verification passed') }
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
        RendererSettings = @()
        RendererMismatches = @()
        RhiWarnings = @()
        DisplayedPortal = $null
        SuccessMarkers = @()
        FailureMarkers = @()
        Outcome = 'Failed'
        Reason = $null
    }

    $start = [System.Diagnostics.ProcessStartInfo]::new($editor)
    $start.UseShellExecute = $false
    $start.WorkingDirectory = $repo
    foreach ($arg in @($project, $definition.Map, $definition.Flag, '-game', '-vulkan', '-unattended',
                       '-windowed', "-ResX=$($dimensions[0])", "-ResY=$($dimensions[1])", '-benchmark', "-fps=$FramesPerSecond",
                       "-UserDir=$profile", "-abslog=$log", "-ExecCmds=$renderQueries")) {
        $start.ArgumentList.Add($arg)
    }
    if (($Capture -or $definition.CheckPortalFrame) -and $definition.CaptureFlag) {
        $start.ArgumentList.Add($definition.CaptureFlag)
    }
    if ($definition.ExtraFlag) { $start.ArgumentList.Add($definition.ExtraFlag) }
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
    $result.Vulkan = [bool]($lines -match 'LogVulkanRHI:.*Vulkan RHI ShaderPlatform for SM6: VULKAN_SM6')
    $gpuLine = $lines -match 'LogVulkanRHI:.*DeviceName:' | Select-Object -First 1
    if ($gpuLine -match 'DeviceName:\s*(.+)$') { $result.Gpu = $Matches[1].Trim() }
    $result.RendererSettings = @($lines -match '(?:r\.(?:DynamicGlobalIlluminationMethod|ReflectionMethod|GenerateMeshDistanceFields|Lumen.HardwareRayTracing|Nanite.ProjectEnabled|Shadow.Virtual.Enable|AntiAliasingMethod)|sg\.(?:GlobalIlluminationQuality|ReflectionQuality))\s*=')
    $result.RendererMismatches = @($expectedRenderer.Keys | Where-Object {
        $pattern = [regex]::Escape($_) + '\s*=\s*"?' + $expectedRenderer[$_] + '"?(?:\s|$)'
        !($result.RendererSettings -match $pattern)
    })
    $result.RhiWarnings = @($lines -match 'Log(?:VulkanRHI|RHI): Warning:')
    if ($definition.CheckPortalFrame) {
        $frame = Join-Path $profile 'Saved/Screenshots/WindowsEditor/PortalLab-Active.png'
        if (Test-Path -LiteralPath $frame -PathType Leaf) {
            $result.DisplayedPortal = & (Join-Path $PSScriptRoot 'Read-PortalFrame.ps1') -Path $frame
        }
    }
    $exitRequestPattern = 'FPlatformMisc::RequestExitWithStatus\(\s*\d+\s*,\s*[1-9]\d*\s*,'
    $result.FailureMarkers = @($lines -match ": Error:|Fatal error:|$exitRequestPattern")
    $result.SuccessMarkers = @($definition.Success | Where-Object {
        $lines -match [regex]::Escape($_)
    })

    # Use the same verdict for the positive cases and the negative control.
    if (!$result.Vulkan) {
        $result.Reason = 'Vulkan SM6 was not confirmed in the game log.'
    } elseif ($result.RendererMismatches.Count -gt 0) {
        $result.Reason = "Renderer profile was not confirmed: $($result.RendererMismatches -join ', ')"
    } elseif ($result.FailureMarkers.Count -gt 0) {
        $result.Reason = $result.FailureMarkers[0]
    } elseif ($result.ExitCode -ne 0) {
        $result.Reason = "Unreal exited with code $($result.ExitCode)."
    } elseif ($definition.CheckPortalFrame -and (!$result.DisplayedPortal -or !$result.DisplayedPortal.WarmWall -or
        $result.DisplayedPortal.Width -ne [int]$dimensions[0] -or $result.DisplayedPortal.Height -ne [int]$dimensions[1])) {
        $result.Reason = 'The displayed portal aperture frame was not confirmed.'
    } elseif ($result.SuccessMarkers.Count -ne $definition.Success.Count) {
        $missing = $definition.Success | Where-Object { $_ -notin $result.SuccessMarkers }
        $result.Reason = "Missing success markers: $($missing -join ', ')"
    } else {
        $result.Outcome = 'Passed'
    }

    if ($definition.ExpectedError) {
        $expected = [regex]::Escape($definition.ExpectedError)
        $unexpected = $result.FailureMarkers -notmatch "$expected|$exitRequestPattern"
        if ($result.Outcome -eq 'Failed' -and $result.Vulkan -and $result.RendererMismatches.Count -eq 0 -and $result.ExitCode -in @(0, 1) -and
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
    Write-Host "Running $name on Vulkan SM6..."
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
    Rhi = 'VULKAN_SM6'
    Resolution = $Resolution
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
