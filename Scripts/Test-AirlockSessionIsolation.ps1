#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EngineRoot,
    [ValidateRange(1, 3600)][int]$TimeoutSeconds = 180
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$editor = (Resolve-Path -LiteralPath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe')).Path
$name = 'Abandoned.Verification.AirlockSessionIsolation'
$run = New-Item -ItemType Directory -Path (Join-Path $repo "Saved/AirlockSessionIsolation/$([guid]::NewGuid().ToString('N'))")
$report = Join-Path $run.FullName 'Report'
$log = Join-Path $run.FullName 'Editor.log'
$start = [Diagnostics.ProcessStartInfo]::new($editor)
$start.WorkingDirectory = $repo
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
foreach ($arg in @((Join-Path $repo 'Abandoned.uproject'), '/Engine/Maps/Entry', '-unattended', '-nop4',
        '-nosound', '-vulkan', '-RenderOffscreen', "-UserDir=$($run.FullName)/Profile", "-abslog=$log",
        "-ReportExportPath=$report", "-ExecCmds=Automation RunTests $name", '-TestExit=Automation Test Queue Empty')) {
    $start.ArgumentList.Add($arg)
}

$process = [Diagnostics.Process]::Start($start)
try {
    if (!$process.WaitForExit(1000 * $TimeoutSeconds)) {
        $process.Kill($true)
        $process.WaitForExit()
        throw "Editor timed out. See $log"
    }
    if ($process.ExitCode -ne 0) { throw "Editor exited with code $($process.ExitCode). See $log" }
} finally {
    $process.Dispose()
}

$index = Join-Path $report 'index.json'
$result = Get-Content -LiteralPath $index -Raw | ConvertFrom-Json
$test = @($result.tests)
if ($test.Count -ne 1 -or $test[0].fullTestPath -cne $name -or $test[0].state -cne 'Success' -or
        $test[0].errors -ne 0 -or $result.succeeded -ne 1) {
    throw "Expected one successful $name. See $index"
}
Write-Output "$name passed. Report: $index"
