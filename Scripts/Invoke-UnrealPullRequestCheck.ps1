#requires -Version 7.0
[CmdletBinding(DefaultParameterSetName='Isolated')]
param(
    [Parameter(Mandatory)][ValidateRange(1,2147483647)][int]$PullRequest,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{40}$')][string]$ApprovedHead,
    [Parameter(Mandatory)][string]$EngineRoot,
    [Parameter(Mandatory)][string]$RunsRoot,
    [Parameter(Mandatory,ParameterSetName='Local')][switch]$LocalOnly,
    [Parameter(Mandatory,ParameterSetName='Isolated')][pscredential]$WorkerCredential,
    [Parameter(Mandatory,ParameterSetName='Isolated')][ValidatePattern('^[a-z0-9-]+$')][string]$AppSlug
)
$ErrorActionPreference = 'Stop'
$repo = 'repos/hhypest/abandoned'
$context = 'Unreal Editor Win64 (isolated)'
function Read-PR {
    $json = & gh api --hostname github.com "$repo/pulls/$PullRequest"
    if ($LASTEXITCODE) { throw 'Cannot read pull request.' }
    $pr = $json | ConvertFrom-Json
    if ($pr.state -ne 'open' -or $pr.base.ref -ne 'main' -or $pr.base.repo.full_name -ne 'hhypest/abandoned' -or
        $pr.head.repo.full_name -ne 'hhypest/abandoned' -or $pr.head.sha -ne $ApprovedHead) {
        throw 'Expected an open same-repository PR to main at the exact reviewed SHA.'
    }
    return $pr
}
function Publish-Status([string]$State, [string]$Description) {
    $json = & gh api --hostname github.com --method POST "$repo/statuses/$ApprovedHead" -f "context=$context" -f "state=$State" -f "description=$Description"
    if ($LASTEXITCODE) { throw "Cannot publish $State." }
    $status = $json | ConvertFrom-Json
    if ($status.creator.login -ne "$AppSlug`[bot]" -or $status.state -ne $State -or $status.context -cne $context) {
        throw 'Status publisher differs from the expected GitHub App installation.'
    }
}
$pr = Read-PR
$base = $pr.base.sha
$id = [guid]::NewGuid().ToString('N')
$launcher = Join-Path $PSScriptRoot 'Invoke-TrustedUnrealBuild.ps1'
$run = Join-Path ([IO.Path]::GetFullPath($RunsRoot)) $id
$argsBuild = @('-EngineRoot', [IO.Path]::GetFullPath($EngineRoot), '-RunsRoot', (Split-Path $run),
    '-Commit', $ApprovedHead, '-ReviewedPullRequest', "$PullRequest", '-RunId', $id)
if ($LocalOnly) {
    & $launcher -EngineRoot $EngineRoot -RunsRoot $RunsRoot -Commit $ApprovedHead -ReviewedPullRequest $PullRequest -RunId $id | Out-Host
} else {
    $install = Join-Path $env:ProgramFiles 'AbandonedUEWorker'
    if ($PSScriptRoot -ne (Join-Path $install 'Scripts') -or
        (Resolve-Path -LiteralPath $EngineRoot).Path -ne (Join-Path $install 'Unreal') -or
        (Resolve-Path -LiteralPath $RunsRoot).Path -ne (Join-Path $install 'Runs')) {
        throw 'Publish using the installed controller and the dedicated engine and run directories.'
    }
    $worker = Get-LocalUser -Name 'AbandonedUEBuild'
    $caller = [Security.Principal.WindowsIdentity]::GetCurrent()
    if (!$worker.Enabled -or $worker.SID -eq $caller.User -or
        $WorkerCredential.UserName -ne "$env:COMPUTERNAME\AbandonedUEBuild") {
        throw 'Provide the dedicated local worker credential from a separate publisher account.'
    }
    # The worker receives no GH token, publisher profile, or publisher-supplied PowerShell modules.
    $info = [Diagnostics.ProcessStartInfo]::new((Join-Path $PSHOME 'pwsh.exe'))
    $info.UseShellExecute = $false
    $info.WorkingDirectory = Split-Path $launcher
    $info.Domain = $env:COMPUTERNAME
    $info.UserName = $worker.Name
    $info.Password = $WorkerCredential.Password
    $info.LoadUserProfile = $true
    $info.Environment.Clear()
    foreach ($name in 'SystemRoot','windir','SystemDrive','ComSpec','ProgramData','ProgramFiles','ProgramFiles(x86)','ProgramW6432',
        'CommonProgramFiles','CommonProgramFiles(x86)','CommonProgramW6432','OS','PROCESSOR_ARCHITECTURE','NUMBER_OF_PROCESSORS') {
        if (Test-Path "Env:$name") { $info.Environment[$name] = [Environment]::GetEnvironmentVariable($name) }
    }
    $info.Environment['PATH'] = @($PSHOME, (Split-Path (Get-Command git.exe).Source),
        (Split-Path (Get-Command git-lfs.exe).Source), [Environment]::SystemDirectory, $env:SystemRoot) -join ';'
    $info.Environment['PATHEXT'] = '.COM;.EXE;.BAT;.CMD'
    foreach ($arg in @('-NoProfile','-NonInteractive','-File',$launcher) + $argsBuild + @('-ExpectedWorkerSid',$worker.SID.Value)) {
        $info.ArgumentList.Add($arg)
    }
    Publish-Status pending 'Building the explicitly reviewed PR head on the physical Windows worker'
}
try {
    if (!$LocalOnly) {
        $process = [Diagnostics.Process]::Start($info)
        try {
            $process.WaitForExit()
            if ($process.ExitCode -ne 0) { throw "Worker exited with code $($process.ExitCode)." }
        } finally { $process.Dispose() }
    }
    $report = Get-Content -LiteralPath "$run/report.json" -Raw | ConvertFrom-Json
    $dll = 'Binaries/Win64/UnrealEditor-Abandoned.dll'
    if ($report.Result -ne 'Passed' -or $report.Commit -ne $ApprovedHead -or $report.Main -ne $base -or
        $report.PullRequest -ne $PullRequest -or (!$LocalOnly -and $report.WorkerSid -ne $worker.SID.Value) -or
        $report.Products.$dll -ne (Get-FileHash -LiteralPath "$run/Project/$dll").Hash) {
        throw 'Build evidence does not match the approved revision, worker, base and DLL.'
    }
    if ((Read-PR).base.sha -ne $base) { throw 'The base moved during the build. Update and review the PR again.' }
} catch {
    if (!$LocalOnly) { Publish-Status failure 'Build failed or the reviewed PR changed. See the worker report.' }
    throw
}
if (!$LocalOnly) { Publish-Status success 'Built AbandonedEditor Win64 Development at this SHA under the dedicated Windows account' }
Write-Output "$run/report.json"
