#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackageRoot,
    [ValidateSet('Travel', 'Campaign', 'PortalActive', 'PortalFrozen', 'FosTechGallery')][string]$Scenario = 'Travel',
    [switch]$TraceTravel,
    [switch]$StockDisplay,
    [ValidateSet('Separate', 'Instanced')][string]$ChamberDecor,
    [ValidateSet('Separate', 'Compound')][string]$WeightShell,
    [switch]$PreloadWeightShell,
    [switch]$InstanceWeightShellVisuals,
    [switch]$DisableChamberMeshReuse,
    [switch]$RedrawOutgoingChamber,
    [switch]$TraceDeferredGCTail,
    [switch]$DisableRenderThreadLag,
    [switch]$DisableSmallGCPolicy,
    [switch]$TraceTravelTasks,
    [switch]$TraceTravelStats,
    [switch]$TraceMapPages,
    [switch]$DeferMapPageDrain,
    [ValidateSet('Default', 'Binned2', 'Binned3')][string]$Allocator = 'Default',
    [ValidateRange(2, 20)][int]$WeightVisits = 2,
    [ValidateRange(0, 128)][int]$MapPageReserve,
    [switch]$TestMapReserveTrim
)
$ErrorActionPreference = 'Stop'
if ($ChamberDecor -and !$TraceTravel) { throw 'ChamberDecor requires TraceTravel' }
if ($WeightShell -and !$TraceTravel) { throw 'WeightShell requires TraceTravel' }
if ($InstanceWeightShellVisuals -and ($WeightShell -ne 'Compound' -or $ChamberDecor -ne 'Instanced')) {
    throw 'InstanceWeightShellVisuals requires compound collision and instanced decor'
}
if ($PreloadWeightShell -and ($WeightShell -ne 'Compound' -or $DisableChamberMeshReuse)) {
    throw 'PreloadWeightShell requires compound collision and chamber mesh reuse'
}
if ($RedrawOutgoingChamber -and !$TraceTravel) { throw 'RedrawOutgoingChamber requires TraceTravel' }
if ($DisableChamberMeshReuse -and !$TraceTravel) { throw 'DisableChamberMeshReuse requires TraceTravel' }
if ($TraceDeferredGCTail -and !$TraceTravel) { throw 'TraceDeferredGCTail requires TraceTravel' }
if ($DisableRenderThreadLag -and !$TraceTravel) { throw 'DisableRenderThreadLag requires TraceTravel' }
if ($DisableSmallGCPolicy -and !$TraceTravel) { throw 'DisableSmallGCPolicy requires TraceTravel' }
$measureReserve = $PSBoundParameters.ContainsKey('MapPageReserve')
if ($measureReserve -and (!$TraceTravel -or $DeferMapPageDrain)) { throw 'MapPageReserve requires normal traced travel' }
if ($TestMapReserveTrim -and !($measureReserve -and $MapPageReserve -gt 0)) { throw 'TestMapReserveTrim requires a positive reserve' }
if (($TraceMapPages -or $DeferMapPageDrain) -and !$TraceTravel) { throw 'The MemStack experiment requires TraceTravel' }
if ($TraceTravelStats -and !$TraceTravel) { throw 'TraceTravelStats requires TraceTravel' }
if ($TraceTravelTasks -and !$TraceTravel) { throw 'TraceTravelTasks requires TraceTravel' }
if (($TraceTravel -or $PSBoundParameters.ContainsKey('WeightVisits')) -and $Scenario -ne 'Travel') {
    throw 'TraceTravel and WeightVisits require the Travel scenario'
}
$root = (Resolve-Path -LiteralPath $PackageRoot).Path
$exe = Join-Path $root 'Abandoned/Binaries/Win64/Abandoned.exe'
$runDir = Join-Path $PSScriptRoot "../Saved/Performance/$([DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))-$Scenario-$([Guid]::NewGuid().ToString('N'))"
$dir = (New-Item -ItemType Directory -Path $runDir).FullName
$entry = '/Engine/Maps/Entry'
if ($Scenario -eq 'Travel') {
    $map = "${entry}?Chamber=Stairs"
    $expectedRooms = @('Stairs', 'Weight') * $WeightVisits
    $travelSuffix = if ($RedrawOutgoingChamber) { '' } else { '?quiet' }
    $commands = for ($i = 1; $i -lt $expectedRooms.Length; $i++) {
        $captureFrame = 500 + 550 * ($i - 1)
        "${captureFrame}:open ${entry}?Chamber=$($expectedRooms[$i])$travelSuffix"
    }
    $frameCount = $captureFrame + 600
    $flags = @("-csvExecCmds=$($commands -join ',')")
} elseif ($Scenario -eq 'FosTechGallery') {
    $map = "${entry}?game=/Script/Abandoned.FosTechGalleryGameMode"
    $frameCount = $null
    $flags = @('-VerifyFosTechGallery', '-RenderOffscreen', '-fixedseed')
} elseif ($Scenario -eq 'Campaign') {
    $map = $null
    $frameCount = $null
    $flags = @('-VerifyEntryCourse')
} else {
    $map = "${entry}?game=/Script/Abandoned.PortalLabGameMode"
    $frameCount = 1800
    $flags = @('-VerifyPortalLab', '-ProfilePortal')
    if ($Scenario -eq 'PortalFrozen') { $flags += '-FreezePortalCapture' }
}
$start = [Diagnostics.ProcessStartInfo]::new($exe)
$start.WorkingDirectory = $root
$start.UseShellExecute = $false
$start.RedirectStandardOutput = $true
$profileCommand = if ($frameCount) { "CsvProfile FRAMES=$frameCount" } else { 'CsvProfile START' }
if ($map) { $start.ArgumentList.Add($map) }
$syncControl = if ($DisableRenderThreadLag) { 'r.OneFrameThreadLag 0,r.OneFrameThreadLag,' } else { '' }
$gcControl = if ($DisableSmallGCPolicy) { 'gc.SmallFullGCSerialSlots 0,gc.SmallFullGCSerialSlots,' } else { '' }
$displayCommand = if ($StockDisplay) { 'r.VSync,t.MaxFPS' } else { 'r.VSync 0,t.MaxFPS 0' }
foreach ($arg in @('-vulkan', '-unattended', '-windowed', '-ResX=1920', '-ResY=1080',
    "-UserDir=$dir", "-abslog=$dir/Game.log", '-ExitAfterCsvProfiling',
    "-ExecCmds=${syncControl}${gcControl}${displayCommand},r.ScreenPercentage,sg.ShadowQuality,$profileCommand") + $flags) {
    $start.ArgumentList.Add($arg)
}
if ($DisableChamberMeshReuse) { $start.ArgumentList.Add('-NoChamberMeshReuse') }
if ($ChamberDecor -eq 'Separate') { $start.ArgumentList.Add('-NoInstancedChamberDecor') }
if ($WeightShell -eq 'Compound') { $start.ArgumentList.Add('-CompoundWeightShell') }
if ($PreloadWeightShell) { $start.ArgumentList.Add('-PreloadWeightShell') }
if ($InstanceWeightShellVisuals) { $start.ArgumentList.Add('-InstanceWeightShellVisuals') }
if ($measureReserve) { $start.ArgumentList.Add("-MapPageReserve=$MapPageReserve") }
if ($TestMapReserveTrim) { $start.ArgumentList.Add('-TestMapReserveTrim') }
if ($TraceMapPages) { $start.ArgumentList.Add('-AuditMapPages') }
if ($DeferMapPageDrain) { $start.ArgumentList.Add('-DeferMapPageDrain') }
if ($TraceTravelStats) { $start.ArgumentList.Add('-verbosenamedevents') }
if ($Allocator -ne 'Default') {
    $allocatorFlags = @{ Binned2 = '-binnedmalloc2'; Binned3 = '-binnedmalloc3' }
    $start.ArgumentList.Add($allocatorFlags[$Allocator])
}
if ($TraceTravel) {
    $channels = 'cpu,frame,bookmark,loadtime'
    if ($TraceTravelTasks) { $channels += ',task' }
    $start.ArgumentList.Add("-trace=$channels")
    $start.ArgumentList.Add("-tracefile=$dir/Travel.utrace")
    $start.ArgumentList.Add('-LogCmds=LogGarbage Verbose,LogAudioMixer Verbose')
}
if ($TraceDeferredGCTail) { $start.ArgumentList.Add('-TraceDeferredGCTail') }
# WDDM Dedicated Usage is attributed to the game PID, including its driver allocations.
$category = [Diagnostics.PerformanceCounterCategory]::new('GPU Process Memory')
$counters = @()
$process = [Diagnostics.Process]::Start($start)
$processStartUtc = $process.StartTime.ToUniversalTime()
$drain = $process.StandardOutput.BaseStream.CopyToAsync([IO.Stream]::Null)
$timer = [Diagnostics.Stopwatch]::StartNew()
$timeoutSeconds = if ($StockDisplay -and $frameCount) { [Math]::Max(300, 90 + $frameCount / 40) } else { 300 }
$samples = [Collections.Generic.List[object]]::new()
try {
    while (!$process.WaitForExit(100)) {
        if ($timer.Elapsed.TotalSeconds -gt $timeoutSeconds) { throw "$Scenario timed out" }
        $process.Refresh()
        if ($process.HasExited) { break }
        if (!$counters) {
            $instances = $category.GetInstanceNames() -like "pid_$($process.Id)_*"
            $counters = @(foreach ($instance in $instances) {
                [Diagnostics.PerformanceCounter]::new('GPU Process Memory', 'Dedicated Usage', $instance, $true)
            })
        }
        $vram = if ($counters) { ($counters | ForEach-Object RawValue | Measure-Object -Sum).Sum }
        $samples.Add([pscustomobject]@{
            ElapsedMs = $timer.ElapsedMilliseconds
            SampleUtc = [DateTime]::UtcNow.ToString("o")
            PrivateBytes = $process.PrivateMemorySize64
            PeakWorkingSetBytes = $process.PeakWorkingSet64
            DedicatedBytes = $vram
        })
    }
    $null = $drain.GetAwaiter().GetResult()
    if ($process.ExitCode -ne 0) { throw "Game exit $($process.ExitCode)" }
} finally {
    if (!$process.HasExited) { $process.Kill($true) }
    $process.Dispose()
    foreach ($counter in $counters) { $counter.Dispose() }
    $samples | Export-Csv -NoTypeInformation -LiteralPath "$dir/Memory.csv"
}
$log = Get-Content -LiteralPath "$dir/Game.log"
$allocatorLine = @($log | Select-String -Pattern '^.*LogInit: Allocator: (.+?)\s*$')
if ($allocatorLine.Count -ne 1) { throw "Expected one allocator in $dir/Game.log" }
$actualAllocator = $allocatorLine[0].Matches[0].Groups[1].Value
if ($Allocator -ne 'Default' -and $Allocator -ne $actualAllocator) {
    throw "Requested $Allocator but the package used $actualAllocator"
}
if ($measureReserve -and !($log -match "MapPageReserve configured=$MapPageReserve\s*$")) {
    throw 'The package did not confirm the requested page reserve'
}
$displaySettings = if ($StockDisplay) { @('r\.VSync = "1"', 't\.MaxFPS = "60(?:\.0+)?"') } else { @('r\.VSync = "0"', 't\.MaxFPS = "0"') }
foreach ($setting in @($displaySettings) + @( 'r\.ScreenPercentage = "100"', 'sg\.ShadowQuality = "2"')) {
    if (!($log -match $setting)) { throw "Unexpected profile in $dir/Game.log ($setting)" }
}
if (!($log -match 'LogInit: Build Configuration: Development\s*$') -or
    !($log -match 'Vulkan RHI ShaderPlatform for SM6: VULKAN_SM6') -or
    !($log -match 'LogExit: Exiting\.') -or ($log -match 'Found \d+ unfreed allocations!|\*\* LEAK |Portal lab test failed:')) {
    throw "Inspect $dir/Game.log"
}
if ($Scenario -eq 'Travel') {
    $rooms = @(foreach ($line in $log) {
        if ($line -match 'Training chamber ready\. (\w+),') { $Matches[1] }
    })
    if (($rooms -join ',') -ne ($expectedRooms -join ',')) { throw "Unexpected route: $rooms" }
    if ($ChamberDecor) {
        $pattern = if ($ChamberDecor -eq 'Separate') { 'mode=separate groups=0$' } else { 'mode=instanced groups=[1-9]\d*$' }
        $modes = @($log -match 'Room decor mode=')
        if ($modes.Count -ne $rooms.Count -or @($modes -notmatch $pattern).Count) {
            throw "Room decor differs from $ChamberDecor on the measured route"
        }
    }
    if ($WeightShell) {
        $shells = @($log -match 'Weight shell mode=')
        if ($shells.Count -ne $WeightVisits -or @($shells -notmatch "mode=$WeightShell$").Count) {
            throw "Weight shell differs from $WeightShell on the measured route"
        }
    }
    if ($WeightShell -eq 'Compound') {
        $expected = if ($InstanceWeightShellVisuals) { 11 } else { 0 }
        $groups = @($log -match 'Weight shell grouped visuals=')
        if (($InstanceWeightShellVisuals -or $groups.Count) -and
            ($groups.Count -ne $WeightVisits -or @($groups -notmatch "visuals=$expected$").Count)) {
            throw 'Unexpected grouped Weight shell visual count'
        }
    }
    if ($DisableRenderThreadLag -and !($log -match 'r\.OneFrameThreadLag = "0"')) {
        throw 'The render-thread lag control was not applied'
    }
    if ($DisableSmallGCPolicy -and !($log -match 'gc\.SmallFullGCSerialSlots = "0"')) {
        throw 'The small full GC control was not applied'
    }
} elseif ($Scenario -eq 'FosTechGallery') {
    $begin = $log -match 'FosTech\.SampleBegin meshes=13 nanite=(0|13)$'
    if (@($begin).Count -ne 1 -or !($log -match 'FosTech\.SampleEnd$') -or
        !(Test-Path "$dir/Saved/Screenshots/Windows/FosTech-Gallery.png")) {
        throw "Incomplete gallery or mixed raster paths in $dir"
    }
} elseif ($Scenario -eq 'Campaign') {
    if (!($log -match 'Vanguard verification passed: passage transit and station resupply')) { throw 'Campaign did not finish' }
} else {
    $mode = if ($Scenario -eq 'PortalFrozen') { 'frozen' } else { 'active' }
    if (!($log -match "Portal profile ready: $mode, target 960x540")) { throw "Portal pose was not staged" }
}
$csv = @(Get-ChildItem -LiteralPath "$dir/Saved/Profiling/CSV" -Filter '*.csv')
if ($csv.Count -ne 1 -or $csv[0].Length -lt 1000 -or !(($samples.DedicatedBytes | Measure-Object -Maximum).Maximum -gt 0)) {
    throw "Missing CSV or per-process WDDM data in $dir"
}
if ($TraceTravel -and (!(Test-Path -LiteralPath "$dir/Travel.utrace" -PathType Leaf) -or
    (Get-Item -LiteralPath "$dir/Travel.utrace").Length -eq 0)) {
    throw "Missing CPU trace in $dir"
}
[ordered]@{
    ProcessStartUtc = $processStartUtc.ToString("o")
    BootUtc = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString("o")
    StockDisplay = [bool]$StockDisplay
    VisiblePhysicalMemoryKiB = (Get-CimInstance Win32_OperatingSystem).TotalVisibleMemorySize
    Scenario = $Scenario
    ExecutableSha256 = (Get-FileHash -LiteralPath $exe).Hash
    TraceDeferredGCTail = [bool]$TraceDeferredGCTail
    DisableRenderThreadLag = [bool]$DisableRenderThreadLag
    DisableSmallGCPolicy = [bool]$DisableSmallGCPolicy
    RedrawOutgoingChamber = [bool]$RedrawOutgoingChamber
    DisableChamberMeshReuse = [bool]$DisableChamberMeshReuse
    ChamberDecor = $ChamberDecor
    InstanceWeightShellVisuals = [bool]$InstanceWeightShellVisuals
    WeightShell = $WeightShell
    PreloadWeightShell = [bool]$PreloadWeightShell
    TraceTravelTasks = [bool]$TraceTravelTasks
    TraceTravelStats = [bool]$TraceTravelStats
    MapPageReserve = if ($measureReserve) { $MapPageReserve } else { $null }
    TestMapReserveTrim = [bool]$TestMapReserveTrim
    TraceMapPages = [bool]$TraceMapPages
    DeferMapPageDrain = [bool]$DeferMapPageDrain
    Allocator = $actualAllocator
    CaptureFrames = $frameCount
    Profile = $csv[0].Name
} | ConvertTo-Json | Set-Content -LiteralPath "$dir/Run.json"
Write-Output $dir
