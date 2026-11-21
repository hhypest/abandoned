#requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$PackageRoot)

$ErrorActionPreference = 'Stop'
$package = (Resolve-Path -LiteralPath $PackageRoot).Path
$executable = Join-Path $package 'Abandoned/Binaries/Win64/Abandoned.exe'
$root = New-Item -ItemType Directory -Path (Join-Path $PSScriptRoot "../Saved/WeightRetry/$([guid]::NewGuid())")
$neutral = 'open /Engine/Maps/Entry?game=/Script/Engine.GameModeBase'
$back = 'open /Engine/Maps/Entry?Chamber=Stairs'
$weight = 'open /Engine/Maps/Entry?Chamber=Weight?quiet'
$rows = foreach ($mode in 'Stock', 'Retry', 'QuitPending') {
    $dir = (New-Item -ItemType Directory -Path (Join-Path $root $mode)).FullName
    $log = Join-Path $dir 'Game.log'
    $start = [Diagnostics.ProcessStartInfo]::new($executable)
    $start.WorkingDirectory = $package
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $quitPending = $mode -eq 'QuitPending'
    $frames = if ($quitPending) { 90 } else { 850 }
    foreach ($arg in @('/Engine/Maps/Entry?Chamber=Stairs', '-vulkan', '-windowed', '-ResX=1920', '-ResY=1080',
            '-unattended', '-benchmark', '-fps=60', "-UserDir=$dir", "-abslog=$log", '-ExitAfterCsvProfiling',
            "-ExecCmds=s.StreamableDelegateDelayFrames 280,CsvProfile FRAMES=$frames")) {
        $start.ArgumentList.Add($arg)
    }
    if ($mode -ne 'Stock') {
        $start.ArgumentList.Add('-CompoundWeightShell')
        $start.ArgumentList.Add('-PreloadWeightShell')
    }
    if (!$quitPending) { $start.ArgumentList.Add("-csvExecCmds=90:$neutral,200:obj gc,235:$back,640:$weight") }
    $process = [Diagnostics.Process]::Start($start)
    try {
        $drain = $process.StandardOutput.BaseStream.CopyToAsync([IO.Stream]::Null)
        if (!$process.WaitForExit(120000)) { throw "Timeout in $mode" }
        $null = $drain.GetAwaiter().GetResult()
        if ($process.ExitCode) { throw "Game failed in $mode" }
    } finally {
        if (!$process.HasExited) { $process.Kill($true) }
        $process.Dispose()
    }
    $lines = Get-Content -LiteralPath $log
    if (!($lines -match 'VULKAN_SM6') -or !($lines -match 'LogExit: Exiting\.') -or
        !($lines -match 's\.StreamableDelegateDelayFrames = "280"') -or
        ($lines -match 'Fatal error:|Ensure condition failed|Found [1-9]\d* unfreed allocations|has unfreed (PooledBuffer|Image) resources')) {
        throw "Vulkan, delay, or shutdown check failed in $log"
    }
    $rooms = @(foreach ($line in $lines) {
        if ($line -match 'Training chamber ready\. (\w+),') { $Matches[1] }
    })
    $expected = if ($quitPending) { 'Stairs' } else { 'Stairs,Stairs,Weight' }
    if (($rooms -join ',') -ne $expected -or (!$quitPending -and !($lines -match "Game class is 'GameModeBase'"))) {
        throw "Incomplete route in $log"
    }
    $events = @(foreach ($line in $lines) {
        if ($line -match 'CSVEvent "WeightShell(Request|Ready)" \[Frame (\d+)\]') {
            [pscustomobject]@{ Kind = $Matches[1]; Frame = [int]$Matches[2] }
        }
    })
    $sequence = ($events.Kind -join ',')
    $wanted = @{ Stock = ''; Retry = 'Request,Request,Ready'; QuitPending = 'Request' }
    if ($sequence -ne $wanted[$mode]) { throw "Unexpected completion sequence '$sequence' in $log" }
    if ($mode -ne 'Stock' -and $events[0].Frame -ge 90) { throw "First request missed exit window in $log" }
    if ($mode -eq 'Retry' -and ($events[1].Frame -le 235 -or $events[2].Frame -lt ($events[1].Frame + 279) -or
            $events[2].Frame -ge 640)) { throw "Late completion was attributed to the wrong request in $log" }
    [pscustomobject]@{ Mode = $mode; Route = $rooms -join ','; Events = ($events | ForEach-Object { "$($_.Kind)@$($_.Frame)" }) -join ','
        ExitCode = 0; Vulkan = 'VULKAN_SM6' }
}
$rows | Export-Csv -LiteralPath (Join-Path $root 'Results.csv') -NoTypeInformation
Write-Output $root.FullName
