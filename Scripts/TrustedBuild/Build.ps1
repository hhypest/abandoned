#requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$EngineRoot, [string]$Commit, [int]$ReviewedPullRequest, [switch]$FunctionalTests,
    [switch]$ReadOnlyLocalShaderCache)
$ErrorActionPreference = 'Stop'
if ($ReadOnlyLocalShaderCache -and (!$FunctionalTests -or $ReviewedPullRequest)) {
    throw 'The read-only loopback cache is restricted to functional tests on accepted main revisions.'
}
$run = (Get-Location).Path
$project = Join-Path $run 'Project'
$lock = Import-PowerShellDataFile (Join-Path $PSScriptRoot 'Toolchain.psd1')
$report = [ordered]@{
    Result = 'Failed'
    WorkerSid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    PullRequest = $ReviewedPullRequest
    RequestedCommit = $Commit
    Commit = $null
    Main = $null
    Target = 'AbandonedEditor Win64 Development'
    EngineBase = $lock.EngineBase
    Windows = [Environment]::OSVersion.Version.ToString()
    Steps = [ordered]@{}
}
$step = 'Checkout'
function Invoke-Git {
    & git -c core.autocrlf=true @args
    if ($LASTEXITCODE -ne 0) { throw "Git $($args[0]) failed (exit $LASTEXITCODE)." }
}

Start-Transcript -LiteralPath (Join-Path $run 'build.log') | Out-Null
try {
    $env:GIT_LFS_SKIP_SMUDGE = '1'
    Invoke-Git clone --no-checkout --single-branch --branch main --no-tags https://github.com/hhypest/abandoned.git $project
    Set-Location -LiteralPath $project
    $report.Main = Invoke-Git rev-parse refs/remotes/origin/main
    if (!$Commit) { $Commit = $report.Main }
    if ($ReviewedPullRequest) {
        Invoke-Git fetch --no-tags origin "refs/pull/$ReviewedPullRequest/head"
        $fetched = Invoke-Git rev-parse FETCH_HEAD
        if ($Commit -ne $fetched) {
            $report.RejectedRevision = $true
            throw 'The fetched PR head differs from the approved SHA.'
        }
        Invoke-Git merge-base --is-ancestor $report.Main $Commit
    } elseif ($Commit -notmatch '^[a-fA-F0-9]{40}$' -or $Commit -notin (Invoke-Git rev-list --first-parent refs/remotes/origin/main)) {
        $report.RejectedRevision = $true
        throw 'Revision is outside the first-parent history of origin/main.'
    }
    Invoke-Git lfs install --local
    Invoke-Git checkout --detach $Commit
    $report.Commit = Invoke-Git rev-parse HEAD
    Remove-Item Env:GIT_LFS_SKIP_SMUDGE
    Invoke-Git lfs pull origin
    & ./Scripts/Test-GitLfs.ps1
    if (Invoke-Git status --porcelain --untracked-files=all) { throw 'Expected a clean project checkout.' }
    $report.Steps[$step] = 0

    $step = 'Toolchain'
    $version = Get-Content -Raw -LiteralPath "$EngineRoot/Engine/Build/Build.version" | ConvertFrom-Json
    $report.Unreal = "$($version.MajorVersion).$($version.MinorVersion).$($version.PatchVersion)"
    if ($report.Unreal -ne $lock.EngineVersion) { throw "Unexpected UE version $($report.Unreal)." }
    # The dedicated UE checkout can be provisioned by a different Windows account.
    $env:GIT_CONFIG_COUNT = '1'
    $env:GIT_CONFIG_KEY_0 = 'safe.directory'
    $env:GIT_CONFIG_VALUE_0 = $EngineRoot.Replace('\','/')
    Invoke-Git -C $EngineRoot merge-base --is-ancestor $lock.EngineBase HEAD
    $changed = @(Invoke-Git -C $EngineRoot diff --name-only $lock.EngineBase --)
    if (@(Compare-Object @($lock.EngineFiles.Keys) $changed).Count) { throw 'Unexpected Unreal source changes.' }
    $localSources = @(Invoke-Git -C $EngineRoot ls-files --others --exclude-standard -- Engine/Source Engine/Plugins |
        Where-Object { $_ -match '\.(c|cpp|h|hpp|inl|cs|uplugin|ush|usf)$' })
    if ($localSources.Count) { throw "Untracked engine build input: $($localSources[0])" }
    foreach ($file in $lock.EngineFiles.Keys) {
        $object = Invoke-Git -C $EngineRoot hash-object "--path=$file" "$EngineRoot/$file"
        if ($object -ne $lock.EngineFiles[$file]) { throw "Unexpected engine source: $file" }
    }
    # UBT also consults these paths outside the per-run profile.
    foreach ($config in @(
        "$EngineRoot/Engine/Saved/UnrealBuildTool/BuildConfiguration.xml"
        "$EngineRoot/Engine/Restricted/NotForLicensees/Programs/UnrealBuildTool/BuildConfiguration.xml"
        "$env:ProgramData/Unreal Engine/UnrealBuildTool/BuildConfiguration.xml"
        "$([Environment]::GetFolderPath('MyDocuments'))/Unreal Engine/UnrealBuildTool/BuildConfiguration.xml"
    )) {
        if ((Test-Path -LiteralPath $config) -and ([xml](Get-Content -Raw -LiteralPath $config)).SelectNodes('/*/*').Count) {
            throw "External UBT settings affect this build: $config"
        }
    }
    $dotnet = "$EngineRoot/Engine/Binaries/ThirdParty/DotNet/$($lock.DotNet)/win-x64/dotnet.exe"
    $ubt = "$EngineRoot/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll"
    $report.DotNet = & $dotnet --version
    if ($LASTEXITCODE -ne 0 -or $report.DotNet -ne $lock.DotNet) { throw 'Unexpected bundled .NET SDK.' }
    $report.UBTSha256 = (Get-FileHash -LiteralPath $ubt).Hash
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs = & $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or !$vs) { throw 'Visual Studio 2022 Build Tools not found.' }
    Import-Module "$vs/Common7/Tools/Microsoft.VisualStudio.DevShell.dll"
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=amd64 -host_arch=amd64 -vcvars_ver=$($lock.VCTools) -winsdk=$($lock.WindowsSDK)"
    $report.VCTools = $env:VCToolsVersion
    $report.WindowsSDK = $env:WindowsSDKVersion.TrimEnd('\')
    $report.Compiler = (Get-Item (Get-Command cl.exe).Source).VersionInfo.ProductVersion
    if ($report.VCTools -ne $lock.VCTools -or $report.WindowsSDK -ne $lock.WindowsSDK -or $report.Compiler -ne "$($lock.Compiler).0") {
        throw 'Installed compiler or Windows SDK differs from Toolchain.psd1.'
    }
    $report.Steps[$step] = 0

    $step = 'Architecture'
    & ./Scripts/Test-Architecture.ps1
    $report.Steps[$step] = 0

    $step = 'EditorBuild'
    & "$PSScriptRoot/BuildEditor.ps1" -RunDirectory $run -DotNet $dotnet -UnrealBuildTool $ubt -Toolchain $lock
    $report.Steps[$step] = $LASTEXITCODE
    if ($LASTEXITCODE -ne 0) { throw "UBT failed (exit $LASTEXITCODE)." }
    $step = 'Products'
    $report.Products = [ordered]@{}
    foreach ($file in 'Binaries/Win64/AbandonedEditor.target', 'Binaries/Win64/UnrealEditor.modules', 'Binaries/Win64/UnrealEditor-Abandoned.dll') {
        $report.Products[$file] = (Get-FileHash -LiteralPath (Join-Path $project $file)).Hash
    }
    if (Invoke-Git status --porcelain --untracked-files=all) { throw 'Build changed the checked-out sources.' }
    $report.Steps[$step] = 0
    if ($FunctionalTests) {
        $step = 'FunctionalTests'
        . "$PSScriptRoot/FunctionalTests.ps1"
        $dll = 'Binaries/Win64/UnrealEditor-Abandoned.dll'
        $directory = Join-Path $run 'Functional'
        $editor = Invoke-UEAutomationEditor $EngineRoot $project $directory $RequiredFunctionalTests 1800 -PrivateDDC -ReadOnlyLocalShaderCache:$ReadOnlyLocalShaderCache
        $digest = Read-UEAutomationReport $directory $RequiredFunctionalTests $editor
        if ((Invoke-Git rev-parse HEAD) -cne $report.Commit -or (Invoke-Git status --porcelain --untracked-files=all) -or
            (Get-FileHash -LiteralPath "$project/$dll").Hash -cne $report.Products[$dll]) {
            throw 'The tested checkout or module changed during UE Automation.'
        }
        $report.Functional = [ordered]@{
            Commit = $report.Commit
            RunId = Split-Path $run -Leaf
            ModuleSha256 = $report.Products[$dll]
            ReportSha256 = $digest
            DerivedDataCache = if ($ReadOnlyLocalShaderCache) { 'PrivateFileSystemWithReadOnlyLoopback' } else { 'PrivateFileSystem' }
            Editor = $editor
            Tests = $RequiredFunctionalTests
        }
        $report.Steps[$step] = 0
    }
    $report.Result = 'Passed'
} catch {
    if (!$report.Steps.Contains($step)) { $report.Steps[$step] = 1 }
    $report.Error = $_.Exception.Message
    throw
} finally {
    Set-Location -LiteralPath $run
    $report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $run 'report.json') -Encoding utf8
    Stop-Transcript | Out-Null
}
