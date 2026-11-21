#requires -Version 7.0
[CmdletBinding(SupportsShouldProcess)]
param()
$ErrorActionPreference = 'Stop'
$root = Join-Path $env:ProgramFiles 'AbandonedUEWorker'
$name = 'AbandonedUEBuild'
if (!$PSCmdlet.ShouldProcess($root, "Prepare trusted tools, a private engine directory and build workspace for local account $name")) { return }
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Run setup from elevated PowerShell.' }
if ((Test-Path -LiteralPath $root) -or (Get-LocalUser $name -ErrorAction SilentlyContinue)) {
    throw 'The account or install directory already exists. Inspect the existing installation before changing it.'
}
$password = Read-Host 'New local worker password (store it in the operator password manager)' -AsSecureString
$worker = New-LocalUser -Name $name -Password $password -Description 'Abandoned reviewed Unreal builds'
$users = 'S-1-5-32-545'
if ($worker.SID.Value -notin (Get-LocalGroupMember -SID $users).SID.Value) { Add-LocalGroupMember -SID $users -Member $worker }
$null = New-Item -ItemType Directory -Path $root
$control = Join-Path $root 'Scripts'
$null = New-Item -ItemType Directory -Path $control
Copy-Item -LiteralPath "$PSScriptRoot/TrustedBuild" -Destination $control -Recurse
foreach ($file in 'Invoke-TrustedUnrealBuild.ps1','Invoke-UnrealPullRequestCheck.ps1','Enable-UnrealMergeCheck.ps1') {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $control
}
# Program Files protects code and structural directories from the standard worker.
# Each published build uses this worker's own UE installation and scratch data.
foreach ($dir in 'Runs','Unreal') {
    $path = (New-Item -ItemType Directory -Path (Join-Path $root $dir)).FullName
    & icacls.exe $path /grant:r "*$($worker.SID.Value):(OI)(CI)M"
    if ($LASTEXITCODE) { throw "Cannot grant worker access to $path." }
}
Write-Output "Worker SID: $($worker.SID.Value). Prepare its separate UE checkout in $root\Unreal before the first build."
