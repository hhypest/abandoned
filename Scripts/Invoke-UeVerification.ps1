[CmdletBinding(DefaultParameterSetName = 'Editor')]
param(
    [Parameter(Mandatory, ParameterSetName = 'Editor')]
    [string]$EngineRoot,

    [Parameter(Mandatory, ParameterSetName = 'Package')]
    [string]$PackageRoot,

    [ValidateSet('EntryCourse', 'EntryCourseAlternate', 'EntryCheckpoint', 'EntryWeightNegative', 'Radiation', 'RadiationNegative', 'WristTerminal', 'WristTerminalNegative', 'DoorClamp', 'Portals', 'WorldLifetime', 'VulkanExitControl', 'VulkanTravelControl',
                 'Training', 'TrainingNegative', 'ChamberHints', 'ChamberHintsNegative', 'PlateBalance', 'PlateBalanceNegative', 'ParallelCircuit', 'ParallelCircuitNegative', 'Optics', 'FreeReflector', 'Gravity', 'VanguardRoute', 'VanguardRouteNegative', 'VanguardOxygenRecovery', 'VanguardFalls', 'VanguardFallsAudio', 'Dev20', 'Dev20Recovery', 'Dev20VelocityNegative')]
    [string[]]$Scenario = @('EntryCourse', 'WristTerminal', 'WristTerminalNegative', 'DoorClamp', 'Portals'),

    [ValidateSet(60, 120)]
    [int]$FramesPerSecond = 60,

    [ValidateSet('1280x720', '1920x1080')]
    [string]$Resolution = '1920x1080',

    [switch]$Capture,
    [switch]$SeparateChamberDecor,
    [switch]$CompoundWeightShell,

    [ValidateRange(1, 3600)]
    [int]$TimeoutSeconds = 180,

    [Parameter(ParameterSetName = 'Package')]
    [ValidateRange(0, 128)][int]$MapPageReserve
)

$ErrorActionPreference = 'Stop'
$testReserve = $PSBoundParameters.ContainsKey('MapPageReserve')
$repo = Split-Path -Parent $PSScriptRoot
$target = $PSCmdlet.ParameterSetName
if ($target -eq 'Package') {
    $workingDir = (Resolve-Path -LiteralPath $PackageRoot).Path
    # The bootstrap executable starts a child. Wait for the actual game and its RHI shutdown.
    $executable = Join-Path $workingDir 'Abandoned/Binaries/Win64/Abandoned.exe'
    $screenshotPlatform = 'Windows'
} else {
    $engine = (Resolve-Path -LiteralPath $EngineRoot).Path
    $workingDir = $repo
    $executable = Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor.exe'
    $screenshotPlatform = 'WindowsEditor'
}
if (!(Test-Path -LiteralPath $executable -PathType Leaf)) { throw "Missing executable: $executable" }
$version = (Get-Item -LiteralPath $executable).VersionInfo
if ($version.ProductMajorPart -ne 5 -or $version.ProductMinorPart -ne 7) {
    throw "This project requires UE 5.7. Found $($version.ProductVersion) at $executable"
}

$entry = '/Engine/Maps/Entry'
$controlMap = "${entry}?game=/Script/Engine.GameModeBase"
$fieldProbe = 'rhi.DumpResourceMemory all Name=PhysicsField_ClipmapBuffer'
$lightmapProbe = 'rhi.DumpResourceMemory all Name=VolumetricLightmap'
# Sample the initial world and each settled replacement in the same process.
$controlReloads = 12
$travelCommands = @("100:$lightmapProbe", "120:$fieldProbe")
for ($i = 1; $i -le $controlReloads; $i++) {
    $travelCommands += "$($i * 120 + 30):open $controlMap", "$($i * 120 + 115):$lightmapProbe"
}
$lastProbeFrame = ($controlReloads + 1) * 120
$travelCommands += "${lastProbeFrame}:$fieldProbe", "$($lastProbeFrame + 60):quit"
$dimensions = $Resolution.Split('x')
$expectedRenderer = [ordered]@{
    'r.DynamicGlobalIlluminationMethod' = 1
    'r.ReflectionMethod' = 1
    'r.GenerateMeshDistanceFields' = 1
    'r.Lumen.HardwareRayTracing' = 0
    'r.Nanite.ProjectEnabled' = 1
    'r.Shadow.Virtual.Enable' = 1
    'r.AntiAliasingMethod' = 4
    'r.PhysicsField.EnableField' = 0
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
                  "occupied exit kept the sole unit in the carrier's grip.", 'same 34 kg body remained in the grip after the portal.', "DEV-20 departed the portal bay in the player's grip.")
$balancePassage = "${vanguard}?Chamber=Passage?Checkpoint=Balance"
$fallsSuccess = @('Vanguard fall verified at 250 cm.', 'Passage fall 300.0 cm: minor injury.',
                  'Vanguard minor injury slowed ground movement, then restored normal speed.', 'Vanguard fall verified at 300 cm.',
                  'Passage fall 325.0 cm: fatal.', 'Passage checkpoint recovery: Balance (fall).', 'Balance chamber verification passed')
$scenarios = @{
    VulkanExitControl = @{
        Map = $controlMap
        ControlVisits = 1
        Flag = '-csvExecCmds=120:quit'
        ExecCommand = 'CsvProfile FRAMES=150'
        Success = @("LogLoad: Game class is 'GameModeBase'", 'LogExit: Exiting.')
    }
    VulkanTravelControl = @{
        Map = $controlMap
        ControlVisits = $controlReloads + 1
        ExpectedEmptyFieldProbes = 2
        LightmapsPerWorld = 9 # The cooked Entry map's eight brick textures and its indirection texture.
        Flag = "-csvExecCmds=$($travelCommands -join ',')"
        ExecCommand = "fx.Niagara.AsyncGpuTrace.GlobalSdfEnabled,r.AllowStaticLighting,CsvProfile FRAMES=$($lastProbeFrame + 90)"
        Success = @('CsvExecCommand : quit', 'LogExit: Exiting.')
    }
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
        QuietChambers = @('Button', 'Weight', 'Balance')
        Success = $courseSuccess
    }
    EntryCourseAlternate = @{
        Map = $entry
        Flag = '-VerifyEntryCourse'
        ExtraFlag = @('-VerifyEntryAlternatePlate', '-VerifyEntryAlternateWeight')
        CaptureFlag = '-CaptureEntryCourse'
        Chambers = @('Stairs', 'Button', 'Weight', 'Balance')
        QuietChambers = @('Button', 'Weight', 'Balance')
        Success = $courseSuccess + @('Entry alternate solution: cube snapped to plate B.', 'alternate solution assembled, 10 + 5 + 5 kg.')
    }
    EntryCheckpoint = @{
        Map = $entry
        Flag = '-VerifyEntryCheckpoint'
        SeedCheckpoint = 'Weight'
        QuietChambers = @('Balance')
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
        Success = @('Weight shell verified: 11 faces, clear capsule passage and floor edge.',
                    'Door retained the object, rejected pickup and showed both warning signs.',
                    'Door released the object with its original mass and pickup restored.',
                    'Chamber reset cleared the clamp, restored the cube and hid its signs.',
                    'Door clamp verification passed: capture, release, reset and player checkpoint.')
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
    Optics = @{ Map = "${entry}?game=/Script/Abandoned.PowerLabGameMode"; Flag = '-VerifyPowerLab'; CaptureFlag = '-CapturePowerLab'; Success = @('Power lab verification passed', 'Timed door crossed again after full reset.', 'Timed passage verification passed') }
    FreeReflector = @{ Map = "${entry}?game=/Script/Abandoned.PowerLabGameMode"; Flag = '-VerifyPowerLab'; ExtraFlag = '-VerifyFreeReflector'; CaptureFlag = '-CapturePowerLab'; Success = @('Free reflector verification passed', 'Power lab verification passed', 'Timed door crossed again after full reset.', 'Timed passage verification passed') }
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

. (Join-Path $PSScriptRoot 'WorldLifetime.ps1')
$scenarios.WorldLifetime = New-WorldLifetimeScenario $entry

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

    $execCommands = (@($renderQueries, $definition.ExecCommand) | Where-Object { $_ }) -join ','
    $start = [System.Diagnostics.ProcessStartInfo]::new($executable)
    $start.UseShellExecute = $false
    $start.WorkingDirectory = $workingDir
    if ($target -eq 'Editor') { $start.ArgumentList.Add((Join-Path $repo 'Abandoned.uproject')) }
    # Exercise the configured startup map for the packaged campaign, including checkpoint restore.
    if ($target -eq 'Editor' -or $definition.Map -ne $entry) { $start.ArgumentList.Add($definition.Map) }
    if ($target -eq 'Editor') { $start.ArgumentList.Add('-game') }
    if ($testReserve) { $start.ArgumentList.Add("-MapPageReserve=$MapPageReserve") }
    if ($SeparateChamberDecor) { $start.ArgumentList.Add('-NoInstancedChamberDecor') }
    if ($CompoundWeightShell) { $start.ArgumentList.Add('-CompoundWeightShell') }
    foreach ($arg in @($definition.Flag, '-vulkan', '-unattended',
                       '-windowed', "-ResX=$($dimensions[0])", "-ResY=$($dimensions[1])",
                       "-UserDir=$profile", "-abslog=$log", "-ExecCmds=$execCommands")) {
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
    if ($testReserve -and !($lines -match "MapPageReserve configured=$MapPageReserve\s*$")) {
        $result.Reason = 'Packaged executable did not confirm requested page reserve.'
        return $result
    }
    $versionLine = $lines -match '^LogInit: Engine Version:' | Select-Object -First 1
    $result.Unreal = if ($versionLine -match 'Engine Version: (\d+\.\d+\.\d+)') { $Matches[1] }
    $result.DevelopmentBuild = [bool]($lines -match 'LogInit: Build Configuration: Development\s*$')
    $result.ShutdownComplete = [bool]($lines -match 'LogExit: Exiting\.')
    $result.AllocatorWarnings = [ordered]@{
        DeviceMemoryWarnings = @($lines -match 'LogVulkanRHI: Warning:\s+Found [1-9]\d* unfreed allocations!').Count
        PooledBuffer = @($lines -match 'LogVulkanRHI: Warning: .*has unfreed PooledBuffer resources').Count
        Image = @($lines -match 'LogVulkanRHI: Warning: .*has unfreed Image resources').Count
        BufferNotReleased = @($lines -match 'LogVulkanRHI: Warning: Suballocation\(s\) for Buffer').Count
        LeakEntries = @($lines -match 'LogVulkanRHI: Warning:\s+\*\* LEAK ').Count
    }
    $checkAllocatorOnExit = $target -eq 'Package' -and $name -in @(
        'EntryCourse', 'EntryCourseAlternate', 'EntryCheckpoint', 'WorldLifetime', 'VulkanExitControl', 'VulkanTravelControl'
    )
    $result.Vulkan = [bool]($lines -match 'LogVulkanRHI:.*Vulkan RHI ShaderPlatform for SM6: VULKAN_SM6')
    $gpuLine = $lines -match 'LogVulkanRHI:.*DeviceName:' | Select-Object -First 1
    if ($gpuLine -match 'DeviceName:\s*(.+)$') { $result.Gpu = $Matches[1].Trim() }
    $rendererPattern = ($expectedRenderer.Keys | ForEach-Object { [regex]::Escape($_) }) -join '|'
    $result.RendererSettings = @($lines -match "(?:$rendererPattern)\s*=")
    $result.RendererMismatches = @($expectedRenderer.Keys | Where-Object {
        $pattern = [regex]::Escape($_) + '\s*=\s*"?' + $expectedRenderer[$_] + '"?(?:\s|$)'
        !($result.RendererSettings -match $pattern)
    })
    $result.RhiWarnings = @($lines -match 'Log(?:VulkanRHI|RHI): Warning:')
    if ($definition.ControlVisits) {
        $result.ControlModes = @(foreach ($line in $lines) {
            if ($line -match "LogLoad: Game class is '([^']+)'") { $Matches[1] }
        })
        $result.FieldProbes = @($lines -match 'RHIResources: Shown .*PhysicsField_ClipmapBuffer')
        if ($definition.LightmapsPerWorld) {
            $result.LightmapCounts = @(foreach ($line in $lines) {
                if ($line -match 'RHIResources: Shown (\d+) entries with name VolumetricLightmap\.') {
                    [int]$Matches[1]
                }
            })
        }
    }
    if ($definition.Chambers) {
        $result.Chambers = @(foreach ($line in $lines) {
            if ($line -match 'LogTrainingChamber: Display: Training chamber ready\. (\w+),') { $Matches[1] }
        })
    }
    if ($definition.QuietChambers) {
        $result.QuietChambers = @(foreach ($line in $lines) {
            if ($line -match 'LogLoad: LoadMap: (\S+)') {
                $url = $Matches[1]
                $options = $url.Split('?')
                if ($options -contains 'quiet') {
                    $chamber = $options -like 'Chamber=*'
                    if ($chamber) { $chamber.Substring(8) } else { $url }
                }
            }
        })
    }
    if ($definition.ExpectedWorldEvents) {
        $result.WorldEvents = @(Read-WorldLifetimeEvents $lines)
    }
    if ($definition.CheckPortalFrame) {
        $firstFrame = Join-Path $profile "Saved/Screenshots/$screenshotPlatform/PortalLab-FirstVisible.png"
        if (Test-Path -LiteralPath $firstFrame -PathType Leaf) {
            $result.FirstVisiblePortal = & (Join-Path $PSScriptRoot 'Read-PortalFrame.ps1') -Path $firstFrame
        }
        $frame = Join-Path $profile "Saved/Screenshots/$screenshotPlatform/PortalLab-Active.png"
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
    if ($result.Unreal -notlike '5.7.*' -or !$result.DevelopmentBuild) {
        $result.Reason = 'UE 5.7 Development was not confirmed in the game log.'
    } elseif (!$result.ShutdownComplete) {
        $result.Reason = 'Unreal did not log completion of shutdown.'
    } elseif (!$result.Vulkan) {
        $result.Reason = 'Vulkan SM6 was not confirmed in the game log.'
    } elseif ($result.RendererMismatches.Count -gt 0) {
        $result.Reason = "Renderer profile was not confirmed: $($result.RendererMismatches -join ', ')"
    } elseif ($result.FailureMarkers.Count -gt 0) {
        $result.Reason = $result.FailureMarkers[0]
    } elseif ($result.ExitCode -ne 0) {
        $result.Reason = "Unreal exited with code $($result.ExitCode)."
    } elseif ($checkAllocatorOnExit -and ($result.AllocatorWarnings.Values -gt 0)) {
        $result.Reason = "Vulkan reported unreleased memory at shutdown: $($result.AllocatorWarnings | ConvertTo-Json -Compress)"
    } elseif ($definition.ControlVisits -and ($result.ControlModes.Count -ne $definition.ControlVisits -or
        @($result.ControlModes -ne 'GameModeBase').Count -gt 0)) {
        $result.Reason = "Unexpected control worlds: $($result.ControlModes -join ', ')"
    } elseif ($definition.ExpectedEmptyFieldProbes -and ($result.FieldProbes.Count -ne $definition.ExpectedEmptyFieldProbes -or
        @($result.FieldProbes -notmatch 'RHIResources: Shown 0 entries with name PhysicsField_ClipmapBuffer\.').Count -gt 0)) {
        $result.Reason = 'Expected two empty GPU physics-field inventories across the control map reloads.'
    } elseif ($definition.LightmapsPerWorld -and ($result.LightmapCounts.Count -ne $definition.ControlVisits -or
        @($result.LightmapCounts -ne $definition.LightmapsPerWorld).Count -gt 0)) {
        $result.Reason = "Expected $($definition.LightmapsPerWorld) live volumetric lightmap textures in each of $($definition.ControlVisits) worlds. Observed: $($result.LightmapCounts -join ', ')"
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
    } elseif ($definition.QuietChambers -and ($result.QuietChambers -join ',') -cne ($definition.QuietChambers -join ',')) {
        $result.Reason = "Unexpected quiet chamber transitions: $($result.QuietChambers -join ', ')"
    } elseif ($definition.ExpectedWorldEvents -and
        ($result.WorldEvents -join '|') -cne ($definition.ExpectedWorldEvents -join '|')) {
        $result.Reason = 'Unexpected world lifetime sequence. Check the loaded modes, live owner counts, teardown and cleanup in Game.log.'
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
        if ($result.Outcome -eq 'Failed' -and $result.ShutdownComplete -and $result.DevelopmentBuild -and $result.Unreal -like '5.7.*' -and $result.Vulkan -and $result.RendererMismatches.Count -eq 0 -and $result.ExitCode -in @(0, 1) -and
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
    Unreal = ($results.Unreal | Where-Object { $_ } | Select-Object -Unique) -join ', '
    Target = $target
    Executable = $executable
    ExecutableSha256 = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
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
