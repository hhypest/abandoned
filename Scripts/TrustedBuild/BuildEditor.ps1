#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RunDirectory,
    [Parameter(Mandatory)][string]$DotNet,
    [Parameter(Mandatory)][string]$UnrealBuildTool,
    [Parameter(Mandatory)][hashtable]$Toolchain
)
$ErrorActionPreference = 'Stop'
# Give MSVC, UHT and the PDB writer a stable project name while each build
# still owns a separate, initially empty set of inputs and outputs.
$view = Join-Path (Split-Path -Parent $RunDirectory) 'CompilerWorkspace'
$identity = [Text.Encoding]::UTF8.GetBytes($view.ToUpperInvariant())
$key = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($identity))
$mutex = [Threading.Mutex]::new($false, "Local\AbandonedEditor-$key")
try {
    try { $null = $mutex.WaitOne() } catch [Threading.AbandonedMutexException] {
        # The mutex is now owned here. An interrupted build's junction must
        # remain intact until the operator has verified its process exited.
    }
    try {
        if (Get-Item -LiteralPath $view -Force -ErrorAction SilentlyContinue) {
            throw "Compiler workspace already exists: $view. Check the previous build before removing its junction."
        }
        $null = New-Item -ItemType Junction -Path $view -Target $RunDirectory
        try {
            $previousTemp, $previousTmp = $env:TEMP, $env:TMP
            $previousCl, $previousLink = $env:_CL_, $env:LINK
            Push-Location -LiteralPath "$view/Project"
            try {
                $env:TEMP = Join-Path $view 'Temp'
                $env:TMP = $env:TEMP
                $env:_CL_ = '/experimental:deterministic /wd5049'
                $env:LINK = '/experimental:deterministic'
                $argsUBT = @('AbandonedEditor','Win64','Development', "-Project=$view/Project/Abandoned.uproject",
                    "-CompilerVersion=$($Toolchain.Compiler)", "-WindowsSdkVersion=$($Toolchain.WindowsSDK)",
                    '-WaitMutex','-NoHotReload','-NoEngineChanges','-NoUBA','-NoXGE','-NoFASTBuild','-NoSNDBS', "-Log=$RunDirectory/UBT.log")
                & $DotNet $UnrealBuildTool @argsUBT
                $result = $LASTEXITCODE
            } finally {
                $env:TEMP, $env:TMP = $previousTemp, $previousTmp
                $env:_CL_, $env:LINK = $previousCl, $previousLink
                Pop-Location
            }
        } finally {
            $link = Get-Item -LiteralPath $view -Force
            if ($link.LinkType -ne 'Junction' -or $link.LinkTarget -ne $RunDirectory) {
                throw "Compiler workspace changed during the build: $view"
            }
            [IO.Directory]::Delete($view)
        }
    } finally { $mutex.ReleaseMutex() }
} finally { $mutex.Dispose() }
exit $result
