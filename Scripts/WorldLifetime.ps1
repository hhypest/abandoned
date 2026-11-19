# The Development engine runs these console commands on capture frames in one game process.
function New-WorldLifetimeScenario([string]$entryMap) {
    $owners = [ordered]@{
        TrainingChamberGameMode = @('TrainingPlateCircuitComponent', 'TrainingDoorDriveComponent')
        PortalLabGameMode = @('PortalTransitComponent', 'PortalRoomViewComponent')
        PowerLabGameMode = @('OpticalCircuitComponent', 'OpticalReflectorComponent', 'PowerLabDoorsComponent')
        GravityLabGameMode = @('GravityLabCharacter')
        VanguardRouteGameMode = @('VanguardRouteStateComponent')
    }
    $modes = @($owners.Keys)
    $visits = $modes + $modes + $modes[0]
    $commands = [Collections.Generic.List[string]]::new()
    $expected = [Collections.Generic.List[string]]::new()
    for ($i = 0; $i -lt $visits.Count; $i++) {
        $mode = $visits[$i]
        $frame = 90 + 60 * $i
        if ($i -gt 0) {
            $commands.Add("$($frame - 30):open ${entryMap}?game=/Script/Abandoned.$mode")
        }
        $expected.Add("Load $mode")
        # A class count includes live instances in every world, excluding default objects.
        $probes = @($mode) + $owners[$mode]
        if ($i -gt 0) {
            $previous = $visits[$i - 1]
            $probes += @($previous) + $owners[$previous]
        }
        foreach ($class in $probes) {
            $commands.Add("${frame}:obj list class=/Script/Abandoned.$class -skipmemorysize -nodetailedinfo")
            $count = [int]($class -eq $mode -or $class -in $owners[$mode])
            $expected.Add("$class $count")
        }
        $expected.Add('Teardown')
        $expected.Add('Cleanup')
    }
    $commands.Add("$($frame + 20):quit")
    return @{
        Map = "${entryMap}?game=/Script/Abandoned.$($visits[0])"
        ExecCommand = "CsvProfile FRAMES=$($frame + 30)"
        Flag = "-csvExecCmds=$($commands -join ',')"
        Success = @()
        ExpectedWorldEvents = $expected
    }
}

function Read-WorldLifetimeEvents([string[]]$lines) {
    $probe = $null
    foreach ($line in $lines) {
        if ($line -match "LogLoad: Game class is '([^']+)'") {
            "Load $($Matches[1])"
        } elseif ($line -match 'Obj List: class=/Script/Abandoned\.(\w+) -skipmemorysize -nodetailedinfo\s*$') {
            $probe = $Matches[1]
        } elseif ($probe -and $line -match '\]\s*(\d+) Objects\s*$') {
            "$probe $($Matches[1])"
            $probe = $null
        } elseif ($line -match 'LogWorld: BeginTearingDown for /Engine/Maps/Entry\s*$') {
            'Teardown'
        } elseif ($line -match 'LogWorld: UWorld::CleanupWorld for Entry, bSessionEnded=true, bCleanupResources=true') {
            'Cleanup'
        }
    }
}
