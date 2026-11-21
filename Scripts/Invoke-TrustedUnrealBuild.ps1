#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EngineRoot,
    [Parameter(Mandatory)][string]$RunsRoot,
    [ValidatePattern('^[0-9a-fA-F]{40}$')][string]$Commit,
    [ValidateRange(1,2147483647)][int]$ReviewedPullRequest,
    [ValidatePattern('^[a-f0-9]{32}$')][string]$RunId,
    [string]$ExpectedWorkerSid
)
$ErrorActionPreference = 'Stop'
if (!$IsWindows) { throw 'A Windows x64 build host is required.' }
$engine = (Resolve-Path -LiteralPath $EngineRoot).Path
$root = [IO.Directory]::CreateDirectory($RunsRoot).FullName
if ($ReviewedPullRequest -and !$Commit) { throw 'A reviewed pull request requires its full approved commit.' }
if ($ExpectedWorkerSid) {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    if ($identity.User.Value -ne $ExpectedWorkerSid -or 'S-1-5-32-544' -in $identity.Groups.Value) {
        throw 'Expected the dedicated, non-administrative Windows worker.'
    }
}
$directoryName = if ($RunId) { $RunId } else { [guid]::NewGuid().ToString('N').Substring(0,12) }
$run = (New-Item -ItemType Directory -Path (Join-Path $root $directoryName)).FullName
$profileDir = Join-Path $run 'Profile'
$info = [Diagnostics.ProcessStartInfo]::new((Join-Path $PSHOME 'pwsh.exe'))
$info.UseShellExecute = $false
$info.WorkingDirectory = $run
$info.Environment.Clear()
foreach ($key in 'SystemRoot','windir','ComSpec','SystemDrive','ProgramFiles','ProgramFiles(x86)','ProgramW6432','ProgramData',
    'CommonProgramFiles','CommonProgramFiles(x86)','CommonProgramW6432','OS','PROCESSOR_ARCHITECTURE','NUMBER_OF_PROCESSORS') {
    $value = [Environment]::GetEnvironmentVariable($key)
    if ($value) { $info.Environment[$key] = $value }
}
$system = [Environment]::SystemDirectory
$gitDir = Split-Path (Get-Command git.exe -CommandType Application | Select-Object -First 1).Source
$lfsDir = Split-Path (Get-Command git-lfs.exe -CommandType Application | Select-Object -First 1).Source
$info.Environment['PATH'] = (@($gitDir, $lfsDir, $PSHOME, $system, $env:SystemRoot,
    "$system/Wbem", "$system/WindowsPowerShell/v1.0") | Select-Object -Unique) -join ';'
foreach ($pair in @{
    USERPROFILE = $profileDir
    APPDATA = "$profileDir/AppData/Roaming"
    LOCALAPPDATA = "$profileDir/AppData/Local"
    TEMP = "$run/Temp"
    TMP = "$run/Temp"
    DOTNET_CLI_HOME = "$run/DotNet"
    NUGET_PACKAGES = "$run/NuGet"
}.GetEnumerator()) {
    $info.Environment[$pair.Key] = [IO.Directory]::CreateDirectory($pair.Value).FullName
}
$info.Environment['PATHEXT'] = '.COM;.EXE;.BAT;.CMD'
$info.Environment['GIT_CONFIG_NOSYSTEM'] = '1'
$info.Environment['GIT_CONFIG_GLOBAL'] = 'NUL'
$info.Environment['GIT_TERMINAL_PROMPT'] = '0'
$info.Environment['DOTNET_CLI_TELEMETRY_OPTOUT'] = '1'
foreach ($arg in @('-NoLogo','-NoProfile','-NonInteractive','-File', (Join-Path $PSScriptRoot 'TrustedBuild/Build.ps1'), '-EngineRoot', $engine)) {
    $info.ArgumentList.Add($arg)
}
if ($Commit) { $info.ArgumentList.Add('-Commit'); $info.ArgumentList.Add($Commit) }
if ($ReviewedPullRequest) {
    $info.ArgumentList.Add('-ReviewedPullRequest')
    $info.ArgumentList.Add($ReviewedPullRequest.ToString())
}
Write-Host "Build workspace: $run"
$child = [Diagnostics.Process]::Start($info)
try {
    $child.WaitForExit()
    if ($child.ExitCode -ne 0) { throw "Trusted Unreal build failed. See $run/report.json and build.log." }
} finally { $child.Dispose() }
Write-Output $run
