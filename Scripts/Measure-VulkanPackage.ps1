#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackageRoot,
    [ValidateSet('Travel', 'Campaign', 'PortalActive', 'PortalFrozen')][string]$Scenario = 'Travel',
    [switch]$TraceTravel,
    [switch]$TraceDeferredGCTail,
    [switch]$DisableRenderThreadLag,
    [switch]$DisableSmallGCPolicy,
    [switch]$TraceTravelTasks,
    [switch]$TraceTravelStats,
    [switch]$TraceMapPages,
    [switch]$DeferMapPageDrain,
    [ValidateSet('Default', 'Binned2', 'Binned3')][string]$Allocator = 'Default',
    [ValidateRange(2, 20)][int]$WeightVisits = 2
)
$ErrorActionPreference = 'Stop'
if ($TraceDeferredGCTail -and !$TraceTravel) { throw 'TraceDeferredGCTail requires TraceTravel' }
if ($DisableRenderThreadLag -and !$TraceTravel) { throw 'DisableRenderThreadLag requires TraceTravel' }
if ($DisableSmallGCPolicy -and !$TraceTravel) { throw 'DisableSmallGCPolicy requires TraceTravel' }
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
    $commands = for ($i = 1; $i -lt $expectedRooms.Length; $i++) {
        $captureFrame = 500 + 550 * ($i - 1)
        "${captureFrame}:open ${entry}?Chamber=$($expectedRooms[$i])"
    }
    $frameCount = $captureFrame + 600
    $flags = @("-csvExecCmds=$($commands -join ',')")
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
foreach ($arg in @('-vulkan', '-unattended', '-windowed', '-ResX=1920', '-ResY=1080',
    "-UserDir=$dir", "-abslog=$dir/Game.log", '-ExitAfterCsvProfiling',
    "-ExecCmds=${syncControl}${gcControl}r.VSync 0,t.MaxFPS 0,r.ScreenPercentage,sg.ShadowQuality,$profileCommand") + $flags) {
    $start.ArgumentList.Add($arg)
}
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
$drain = $process.StandardOutput.BaseStream.CopyToAsync([IO.Stream]::Null)
$timer = [Diagnostics.Stopwatch]::StartNew()
$samples = [Collections.Generic.List[object]]::new()
try {
    while (!$process.WaitForExit(100)) {
        if ($timer.Elapsed.TotalMinutes -gt 5) { throw "$Scenario timed out" }
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
foreach ($setting in @('r\.VSync = "0"', 't\.MaxFPS = "0"', 'r\.ScreenPercentage = "100"', 'sg\.ShadowQuality = "2"')) {
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
    if ($DisableRenderThreadLag -and !($log -match 'r\.OneFrameThreadLag = "0"')) {
        throw 'The render-thread lag control was not applied'
    }
    if ($DisableSmallGCPolicy -and !($log -match 'gc\.SmallFullGCSerialSlots = "0"')) {
        throw 'The small full GC control was not applied'
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
    Scenario = $Scenario
    ExecutableSha256 = (Get-FileHash -LiteralPath $exe).Hash
    TraceDeferredGCTail = [bool]$TraceDeferredGCTail
    DisableRenderThreadLag = [bool]$DisableRenderThreadLag
    DisableSmallGCPolicy = [bool]$DisableSmallGCPolicy
    TraceTravelTasks = [bool]$TraceTravelTasks
    TraceTravelStats = [bool]$TraceTravelStats
    TraceMapPages = [bool]$TraceMapPages
    DeferMapPageDrain = [bool]$DeferMapPageDrain
    Allocator = $actualAllocator
    CaptureFrames = $frameCount
    Profile = $csv[0].Name
} | ConvertTo-Json | Set-Content -LiteralPath "$dir/Run.json"
Write-Output $dir
