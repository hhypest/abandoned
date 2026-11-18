[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$EngineRoot,

    [ValidateSet('EntryCourse', 'EntryCourseAlternate', 'EntryCheckpoint', 'EntryWeightNegative', 'Radiation', 'RadiationNegative', 'WristTerminal', 'WristTerminalNegative', 'DoorClamp', 'Portals',
                 'Training', 'TrainingNegative', 'ChamberHints', 'ChamberHintsNegative', 'PlateBalance', 'PlateBalanceNegative', 'ParallelCircuit', 'ParallelCircuitNegative', 'Optics', 'FreeReflector', 'Gravity', 'VanguardRoute', 'VanguardRouteNegative', 'VanguardOxygenRecovery', 'VanguardFalls', 'VanguardFallsAudio', 'Dev20', 'Dev20Recovery', 'Dev20VelocityNegative')]
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
$radiationPassage = "${vanguard}?Chamber=Passage?RadiationRun=1"
$radiationSuccess = @('Radiation exposure increased dose during a flare:', 'Radiation shelter held dose at',
                      'Radiation recovery outside the zone:', 'Radiation dose limit reached. Returning to the passage checkpoint.',
                      'Radiation checkpoint restored: safe position, zero dose and fresh forecast.',
                      'Vanguard station cleared remaining radiation dose:', 'Radiation passage verification passed')
$wristSuccess = @('Wrist terminal verification passed', 'Radiation passage verification passed')
$routeSuccess = @('Vanguard shoulder flashlight switched on, off and on with F.',
                  'Verification observed the raised airlock before crossing.', 'Verification crossed the moving passage airlock.',
                  'Vanguard segment ready: station.', 'Vanguard shoulder flashlight stayed on across world travel.',
                  'Vanguard oxygen supply restored.', 'Vanguard verification passed: passage transit and station resupply.')
$entryFinish = @('Entry test: reset restored the weight lock', 'Entry test: Weight accepted 20 kg',
                 'Entry test: crossed Weight exit.', 'First solution opened the door.', 'Alternate arrangement opened the door.',
                 'Entry course verification passed: balance released the passage.',
                 'Passage ledge stopped walking, a short Space jump and a crouch jump.',
                 'Passage climb rejected an obstructed landing.', 'Passage handhold verified',
                 'Passage climb verified: held Space and landed on top.') + $routeSuccess
$courseSuccess = @('Entry test: crossed Stairs exit.', 'Entry test: crossed Button exit.') + $entryFinish
$dev20Map = "${entry}?game=/Script/Abandoned.PortalLabGameMode?Dev20Run=1"
$dev20Transit = @('34 kg Chaos mass and carry slowdown confirmed.', 'loss of portal power returned the same item to its cradle.',
                  'same 34 kg body remained in the grip after the portal.', "DEV-20 departed the portal bay in the player's grip.")
$balancePassage = "${vanguard}?Chamber=Passage?Checkpoint=Balance"
$fallsSuccess = @('Vanguard fall verified at 250 cm.', 'Passage fall 300.0 cm: minor injury.',
                  'Vanguard minor injury slowed ground movement, then restored normal speed.', 'Vanguard fall verified at 300 cm.',
                  'Passage fall 325.0 cm: fatal.', 'Passage checkpoint recovery: Balance (fall).', 'Balance chamber verification passed')
$scenarios = @{
    VanguardRoute = @{
        Map = "${vanguard}?Chamber=Passage"
        Flag = '-VerifyVanguard'
        CaptureFlag = '-CaptureVanguard'
        Success = $routeSuccess
    }
    VanguardRouteNegative = @{
        Map = "${vanguard}?Chamber=Vanguard"
        Flag = '-VerifyVanguard'
        ExpectedError = 'LogVanguardRouteTest: Error: Vanguard verification failed: prior oxygen 100.00, current 100.00.'
        Success = @('Vanguard verification passed')
    }
    VanguardOxygenRecovery = @{
        Map = $balancePassage
        Flag = '-VerifyVanguardOxygen'
        ExtraFlag = '-VerifyBalanceChamber'
        Chambers = @('Balance')
        Success = @('Passage checkpoint recovery: Balance (oxygen depleted).', 'Balance chamber verification passed')
    }
    VanguardFalls = @{
        Map = $balancePassage; Flag = '-VerifyVanguardFalls'; ExtraFlag = '-VerifyBalanceChamber'
        Chambers = @('Balance'); Success = $fallsSuccess
    }
    VanguardFallsAudio = @{
        Map = $balancePassage; Flag = '-VerifyVanguardFalls'; ExtraFlag = @('-VerifyInjuryAudio', '-VerifyBalanceChamber')
        Chambers = @('Balance'); Success = $fallsSuccess + 'Vanguard injury breath decoded in audio mixer.'
        RealTime = $true
    }
    EntryCourse = @{
        Map = $entry
        Flag = '-VerifyEntryCourse'
        CaptureFlag = '-CaptureVanguard'
        Chambers = @('Stairs', 'Button', 'Weight', 'Balance')
        Success = $courseSuccess
    }
    EntryCourseAlternate = @{
        Map = $entry
        Flag = '-VerifyEntryCourse'
        ExtraFlag = @('-VerifyEntryAlternatePlate', '-VerifyEntryAlternateWeight')
        CaptureFlag = '-CaptureEntryCourse'
        Chambers = @('Stairs', 'Button', 'Weight', 'Balance')
        Success = $courseSuccess + @('Entry alternate solution: cube snapped to plate B.', 'alternate solution assembled, 10 + 5 + 5 kg.')
    }
    EntryCheckpoint = @{
        Map = $entry
        Flag = '-VerifyEntryCheckpoint'
        SeedCheckpoint = 'Weight'
        Chambers = @('Weight', 'Balance')
        Success = $entryFinish + @('Loaded campaign checkpoint v2: Weight', 'Saved campaign checkpoint v2: Balance (1 archive records).')
    }
    EntryWeightNegative = @{
        Map = "${entry}?Chamber=Weight"
        Flag = '-VerifyEntryCourse'
        ExtraFlag = '-ini:Game:[/Script/Abandoned.EntryWeightLayout]:PlateActivationMassKg=10'
        ExpectedError = 'LogEntryChamberTest: Error: Entry test failed: weight lock must reject the partial stack'
        Success = @('Vanguard verification passed')
    }
    Radiation = @{
        Map = $radiationPassage
        Flag = '-VerifyRadiation'
        CaptureFlag = '-CaptureRadiation'
        Success = $radiationSuccess
    }
    RadiationNegative = @{
        Map = "${vanguard}?Chamber=Vanguard"
        Flag = '-VerifyRadiation'
        ExpectedError = 'LogRadiationVerification: Error: Radiation verification failed: the station should clear the remaining dose after a full passage run'
        Success = @('Radiation passage verification passed')
    }
    WristTerminal = @{
        Map = $radiationPassage
        Flag = '-VerifyWristTerminal'
        CaptureFlag = '-CaptureWristTerminal'
        Success = $wristSuccess + @('Wrist status matched the live exposure dose.', 'Wrist archive after checkpoint recovery: one forecast record.')
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
        Success = @('Portal collision shapes verified', 'Rejected carry transit restored', 'Powered entrance captured the linked room.',
                    'Occupied exit stopped transit.', 'Player arrived airborne with the same cube, rotated view and unchanged speed.',
                    'The inverse passage retained the cube and speed.', 'Portal lab verification passed')
    }
    Training = @{
        Map = $entry
        Flag = '-VerifyTrainingChamber'
        CaptureFlag = '-CapturePlacementGhost'
        Success = @('Held cube snapped to the plate at the ghost position.', 'Left mouse threw the cube forward.',
                    'Player climbed from the stack onto the practice ledge.',
                    'Verification passed: player weight, placement ghost, throw, staircase, detach and reset')
    }
    TrainingNegative = @{
        Map = $entry
        Flag = '-VerifyTrainingChamber'
        ExtraFlag = '-ini:Game:[/Script/Abandoned.TrainingChamberLayout]:PlateActivationMassKg=10'
        ExpectedError = 'LogTrainingVerification: Error: Training verification failed: settled 5 kg cube did not activate the plate'
        Success = @('Verification passed: player weight, placement ghost, throw, staircase, detach and reset')
    }
    ChamberHints = @{ Map = $entry; Flag = '-VerifyChamberHints'; CaptureFlag = '-CaptureChamberHints'; Success = @('Chamber hints verification passed') }
    ChamberHintsNegative = @{
        Map = $entry
        Flag = '-VerifyChamberHints'
        ExtraFlag = '-ini:Game:[/Script/Abandoned.EntryWeightLayout]:bRequiresButton=False'
        ExpectedError = 'LogHintVerification: Error: Chamber hints failed at step 0: missing wall switch.'
        Success = @('Chamber hints verification passed')
    }
    PlateBalance = @{
        Map = $entry
        Flag = '-VerifyBalanceChamber'
        CaptureFlag = '-CaptureBalanceChamber'
        Success = @('First solution opened the door.', 'Alternate arrangement opened the door.', 'Balance chamber verification passed')
    }
    PlateBalanceNegative = @{
        Map = $entry
        Flag = '-VerifyBalanceChamber'
        ExtraFlag = '-ini:Game:[/Script/Abandoned.WeightBalanceLayout]:BalanceTotalKg=15'
        ExpectedError = 'LogWeightBalanceTest: Error: Balance test failed: 15 kg distributed evenly must not open the door'
        Success = @('Balance chamber verification passed')
    }
    ParallelCircuit = @{ Map = $entry; Flag = '-VerifyParallelCircuit'; CaptureFlag = '-CaptureParallelCircuit'; Success = @('Parallel circuit verification passed') }
    ParallelCircuitNegative = @{
        Map = $entry
        Flag = '-VerifyParallelCircuit'
        ExtraFlag = '-ini:Game:[/Script/Abandoned.EntryButtonLayout]:PlateMaximumMassKg=150'
        ExpectedError = 'LogParallelCircuit: Error: Parallel circuit verification failed: overloaded plate A still sends a signal'
        Success = @('Parallel circuit verification passed')
    }
    Optics = @{ Map = "${entry}?game=/Script/Abandoned.PowerLabGameMode"; Flag = '-VerifyPowerLab'; CaptureFlag = '-CapturePowerLab'; Success = @('Power lab verification passed', 'Timed passage verification passed') }
    FreeReflector = @{ Map = "${entry}?game=/Script/Abandoned.PowerLabGameMode"; Flag = '-VerifyPowerLab'; ExtraFlag = '-VerifyFreeReflector'; CaptureFlag = '-CapturePowerLab'; Success = @('Free reflector verification passed', 'Power lab verification passed', 'Timed passage verification passed') }
    Gravity = @{ Map = "${entry}?game=/Script/Abandoned.GravityLabGameMode"; Flag = '-VerifyGravityLab'; CaptureFlag = '-CaptureGravityLab'; Success = @('Gravity lab verification passed') }
    Dev20 = @{
        Map = $dev20Map
        Flag = '-VerifyDev20'
        CaptureFlag = '-CaptureDev20'
        Chambers = @('Button', 'Weight')
        Success = $dev20Transit + @('DEV-20: Button measured and rejected 34 kg.', 'DEV-20: Weight measured and rejected 34 kg.',
                    'DEV-20: Button held the player at the exit', 'DEV-20: Weight held the player at the exit',
                    'DEV-20 verification passed: portal, Button, Weight and final exit')
    }
    Dev20Recovery = @{
        Map = $dev20Map
        Flag = '-VerifyDev20'
        ExtraFlag = '-VerifyDev20Recovery'
        Chambers = @('Button')
        Success = $dev20Transit + @('DEV-20: exercising loss outside the Button chamber bounds.', 'DEV-20 recovery verification passed')
    }
    Dev20VelocityNegative = @{
        Map = $dev20Map
        Flag = '-VerifyDev20'
        ExtraFlag = '-ini:Game:[/Script/Abandoned.PortalLabLayout]:Departure=(Center=(X=2430,Y=-330,Z=145),Yaw=85,Console=(X=2630,Y=-260,Z=126))'
        ExpectedError = 'LogPortalDev20Test: Error: DEV-20 portal test failed: carrier velocity did not match the 90 degree portal turn'
        Success = @('same 34 kg body remained in the grip after the portal.')
    }
}

function Invoke-Scenario($name, $definition, $profile) {
    $null = New-Item -ItemType Directory -Path $profile
    if ($definition.SeedCheckpoint) {
        $saveFolder = New-Item -ItemType Directory -Path (Join-Path $profile 'Saved/SaveGames') -Force
        $slot = Join-Path $saveFolder.FullName 'AbandonedCampaign.v2.json'
        @{ version = 2; checkpoint = $definition.SeedCheckpoint; archive = @() } | ConvertTo-Json | Set-Content -LiteralPath $slot -Encoding utf8
    }
    $log = Join-Path $profile 'Game.log'
    $result = [ordered]@{
        Name = $name
        Map = $definition.Map
        FixedStepFps = if (!$definition.RealTime) { $FramesPerSecond } else { $null }
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
        FirstVisiblePortal = $null
        SuccessMarkers = @()
        FailureMarkers = @()
        Outcome = 'Failed'
        Reason = $null
    }

    $start = [System.Diagnostics.ProcessStartInfo]::new($editor)
    $start.UseShellExecute = $false
    $start.WorkingDirectory = $repo
    foreach ($arg in @($project, $definition.Map, $definition.Flag, '-game', '-vulkan', '-unattended',
                       '-windowed', "-ResX=$($dimensions[0])", "-ResY=$($dimensions[1])",
                       "-UserDir=$profile", "-abslog=$log", "-ExecCmds=$renderQueries")) {
        $start.ArgumentList.Add($arg)
    }
    if (!$definition.RealTime) {
        $start.ArgumentList.Add('-benchmark')
        $start.ArgumentList.Add("-fps=$FramesPerSecond")
    }
    if (($Capture -or $definition.CheckPortalFrame) -and $definition.CaptureFlag) {
        $start.ArgumentList.Add($definition.CaptureFlag)
    }
    foreach ($arg in $definition.ExtraFlag) { $start.ArgumentList.Add($arg) }
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
    if ($definition.Chambers) {
        $result.Chambers = @(foreach ($line in $lines) {
            if ($line -match 'LogTrainingChamber: Display: Training chamber ready\. (\w+),') { $Matches[1] }
        })
    }
    if ($definition.CheckPortalFrame) {
        $firstFrame = Join-Path $profile 'Saved/Screenshots/WindowsEditor/PortalLab-FirstVisible.png'
        if (Test-Path -LiteralPath $firstFrame -PathType Leaf) {
            $result.FirstVisiblePortal = & (Join-Path $PSScriptRoot 'Read-PortalFrame.ps1') -Path $firstFrame
        }
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
    } elseif ($definition.CheckPortalFrame -and (!$result.FirstVisiblePortal -or !$result.FirstVisiblePortal.WarmWall -or
        $result.FirstVisiblePortal.Width -ne [int]$dimensions[0] -or $result.FirstVisiblePortal.Height -ne [int]$dimensions[1])) {
        $result.Reason = 'The first visible portal frame was not confirmed.'
    } elseif ($definition.CheckPortalFrame -and (!$result.DisplayedPortal -or !$result.DisplayedPortal.WarmWall -or
        $result.DisplayedPortal.Width -ne [int]$dimensions[0] -or $result.DisplayedPortal.Height -ne [int]$dimensions[1])) {
        $result.Reason = 'The displayed portal aperture frame was not confirmed.'
    } elseif ($result.SuccessMarkers.Count -ne $definition.Success.Count) {
        $missing = $definition.Success | Where-Object { $_ -notin $result.SuccessMarkers }
        $result.Reason = "Missing success markers: $($missing -join ', ')"
    } elseif ($definition.Chambers -and ($result.Chambers -join ',') -cne ($definition.Chambers -join ',')) {
        $result.Reason = "Unexpected chamber sequence: $($result.Chambers -join ', ')"
    } elseif ($definition.SeedCheckpoint) {
        $saved = Get-Content -LiteralPath $slot -Raw | ConvertFrom-Json
        if ($saved.version -eq 2 -and $saved.checkpoint -ceq 'Balance' -and $saved.archive.Count -eq 1 -and $saved.archive[0] -ceq 'oxygen-restored') {
            $result.Outcome = 'Passed'
        } else {
            $result.Reason = 'The resumed route did not persist Balance and the oxygen-restored record.'
        }
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
