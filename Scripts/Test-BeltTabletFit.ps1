#Requires -Version 7
[CmdletBinding()]
param([Parameter(Mandatory)][string]$PackageRoot)

$ErrorActionPreference = 'Stop'
$package = (Resolve-Path -LiteralPath $PackageRoot).Path
$exe = Join-Path $package 'Abandoned/Binaries/Win64/Abandoned.exe'
if (!(Test-Path -LiteralPath $exe)) { throw "Missing game executable at $exe" }
$run = Join-Path (Split-Path $PSScriptRoot) "Saved/BeltTabletVerification/$([Guid]::NewGuid().ToString('N'))"
$commands = @(
    '40:set AbandonedTestController ControlRotation (Pitch=-85)'
    '110:shot filename=BeltStand -nosuffix', '112:InspectBeltTablet 1'
    '122:shot filename=MountStand -nosuffix', '124:InspectBeltTablet 0'
    '135:Input.+key LeftControl 1', '170:shot filename=BeltCrouch -nosuffix'
    '171:InspectBeltTablet 1', '177:shot filename=MountCrouch -nosuffix', '178:InspectBeltTablet 0'
    '180:set AbandonedTestController ControlRotation (Pitch=-8)'
    '190:Input.+key Tab 1', '192:Input.-key Tab', '222:shot filename=Draw -nosuffix'
    '265:shot filename=ArchiveCrouch -nosuffix', '269:InspectBeltTablet 0'
    '280:Input.+key PageDown 1', '282:Input.-key PageDown'
    '310:Input.-key LeftControl', '358:shot filename=ArchiveStand -nosuffix', '360:InspectBeltTablet 0'
    '368:Input.+key PageUp 1', '370:Input.-key PageUp', '392:InspectBeltTablet 0'
    '400:Input.+key Tab 1', '402:Input.-key Tab', '470:set AbandonedTestController ControlRotation (Pitch=-85)'
    '485:shot filename=Restowed -nosuffix', '486:InspectBeltTablet 0'
    '496:Input.+key Tab 1', '498:Input.-key Tab', '504:Input.+key Tab 1', '506:Input.-key Tab', '545:InspectBeltTablet 0'
    '552:Input.+key Tab 1', '554:Input.-key Tab', '605:Input.+key R 1', '607:Input.-key R', '640:InspectBeltTablet 0'
    '650:BugItGo 320 115 100 0 0 0', '652:Walk', '670:set AbandonedTestController ControlRotation (Pitch=-42)'
    '700:Input.+key E 1', '703:Input.-key E', '725:set AbandonedTestController ControlRotation (Pitch=-26)'
    '735:Input.+key Tab 1', '737:Input.-key Tab', '785:GetAll PhysicsHandleComponent GrabbedComponent', '790:InspectBeltTablet 0'
)
foreach ($width in 1280, 1920) {
    $height = if ($width -eq 1280) { 720 } else { 1080 }
    $userDir = Join-Path $run "${width}x$height"
    $save = New-Item -ItemType Directory -Path "$userDir/Saved/SaveGames" -Force
    $slot = @{ version = 2; checkpoint = 'Stairs'; archive = @() }
    if ($width -eq 1280) { $slot.archive = @('forecast-synchronized', 'decontaminated', 'oxygen-restored') }
    $slot | ConvertTo-Json | Set-Content -LiteralPath "$save/AbandonedCampaign.v2.json" -Encoding utf8NoBOM
    $log = Join-Path $userDir 'Game.log'
    $start = [Diagnostics.ProcessStartInfo]::new($exe)
    $start.WorkingDirectory = $package
    $start.UseShellExecute = $false
    foreach ($arg in @('/Engine/Maps/Entry?Chamber=Stairs', '-CustomMannyFit', '-vulkan', '-RenderOffscreen',
        '-unattended', '-windowed', '-ForceRes', "-ResX=$width", "-ResY=$height", '-benchmark', '-fps=60',
        "-UserDir=$userDir", "-abslog=$log", '-ExecCmds=EnableCheats,CsvProfile FRAMES=805',
        "-csvExecCmds=$($commands -join ',')", '-ExitAfterCsvProfiling')) { $start.ArgumentList.Add($arg) }
    $game = [Diagnostics.Process]::Start($start)
    if (!$game.WaitForExit(150000)) { $game.Kill($true); throw "Timed out. See $log" }
    if ($game.ExitCode) { throw "Game exited with $($game.ExitCode). See $log" }
    $report = Get-Content -LiteralPath $log
    if (!($report -match 'CubeGrip\.GrabbedComponent = Body')) { throw "The player did not keep the cube in hand. See $log" }
    $states = @($report | Select-String 'LogTemp: Display: Tablet fit: (.*)$' | ForEach-Object { $_.Matches[0].Groups[1].Value })
    $closed = 'raised=0 field=0 travel=0.00 belt_cm=0.000 belt_deg=0.000'
    $open = 'raised=1 field=1 travel=1.00'
    $count = $slot.archive.Count
    $next = [int]($count -gt 1)
    $expected = @("crouched=0 $closed", "crouched=0 $closed", "crouched=1 $closed", "crouched=1 $closed",
        "crouched=1 $open .*records=$count index=0", "crouched=0 $open .*records=$count index=$next",
        "crouched=0 $open .*records=$count index=0", "crouched=0 $closed", "crouched=0 $closed", "crouched=0 $closed", "crouched=0 $closed")
    if ($states.Count -ne $expected.Count) { throw "Missing tablet samples. See $log" }
    for ($i = 0; $i -lt $expected.Count; $i++) {
        if ($states[$i] -notmatch $expected[$i]) { throw "Unexpected tablet state at sample $i. $($states[$i])" }
    }
    $pads = @($report | Select-String 'Tablet grip: side=(\d) pad_cm=\(([-.\d]+),([-.\d]+),([-.\d]+)\)')
    if ($pads.Count -ne 6) { throw "Missing palm samples. See $log" }
    for ($i = 0; $i -lt $pads.Count; $i++) {
        $values = $pads[$i].Matches[0].Groups
        $side = $i % 2
        $edge = if ($side -eq 0) { 12 } else { -12 }
        $distance = [Math]::Sqrt([double]$values[2].Value * [double]$values[2].Value +
            [Math]::Pow([double]$values[3].Value - $edge, 2) + [Math]::Pow([double]$values[4].Value, 2))
        if ([int]$values[1].Value -ne $side -or $distance -gt 0.1) { throw "Palm outside the 1 mm contact tolerance. See $log" }
    }
    foreach ($name in 'BeltStand', 'MountStand', 'BeltCrouch', 'MountCrouch', 'Draw', 'ArchiveCrouch', 'ArchiveStand', 'Restowed') {
        if (!(Test-Path -LiteralPath "$userDir/Saved/Screenshots/Windows/$name.png")) { throw "Missing frame $name" }
    }
    Write-Output "Passed ${width}x$height. $userDir"
}
