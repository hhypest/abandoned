# Shared by the installed controller, build worker and standalone PIE launcher.
$RequiredFunctionalTests = @('Abandoned.Verification.AirlockSessionIsolation', 'Abandoned.Verification.RadiationTerminalReplay')
$readOnlyZenNode = '(ReadOnly=true,Type=Zen,Host=http://[::1]:8558,Namespace=ue.ddc)'
$readOnlyZenRoot = '(Type=Hierarchical,Inner=Local,Inner=AcceptedLocalCache)'

function Assert-ReadOnlyLocalCacheLog([string]$Log) {
    # GETINI prints the effective graph after all overrides. Validate its final values, not the echoed command line.
    $lines = [IO.File]::ReadAllLines($Log)
    if (!($lines -match 'LogInit: Command Line:.*\s-NoZenAutoLaunch=127\.0\.0\.1:0(?:\s|$)') -or
        !($lines -match ('Root = ' + [regex]::Escape($readOnlyZenRoot) + '$')) -or
        !($lines -match ('AcceptedLocalCache = ' + [regex]::Escape($readOnlyZenNode) + '$')) -or
        !($lines -match 'LogDerivedDataCache: Display: AcceptedLocalCache: Using ZenServer HTTP service at \[::1\](:8558)? with namespace ue\.ddc status: OK') -or
        ($lines -match 'LogZenServiceInstance:.*AutoLaunch initialization completed')) {
        throw 'Editor did not confirm the private filesystem and the read-only loopback shader cache.'
    }
}

function Invoke-UEAutomationEditor {
    param([string]$EngineRoot, [string]$Project, [string]$Directory, [string[]]$Names, [int]$TimeoutSeconds,
        [switch]$PrivateDDC, [switch]$ReadOnlyLocalShaderCache)

    # An existing report directory never qualifies as output for a new invocation.
    $null = New-Item -ItemType Directory -Path $Directory -ErrorAction Stop
    $editor = (Resolve-Path -LiteralPath "$EngineRoot/Engine/Binaries/Win64/UnrealEditor-Cmd.exe").Path
    $start = [Diagnostics.ProcessStartInfo]::new($editor)
    $start.WorkingDirectory = $Project
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.ArgumentList.Add((Join-Path $Project 'Abandoned.uproject'))
    $start.ArgumentList.Add('/Engine/Maps/Entry')
    $commands = "Automation RunTests $($Names -join '+')"
    if ($PrivateDDC) {
        # The pinned source engine supplies this writable filesystem graph.
        # A fresh worker profile must not start or replace the desktop's Zen server.
        $cache = [IO.Directory]::CreateDirectory((Join-Path $Directory 'DDC')).FullName
        # With a warm reader, route any unintended default-service fallback to port zero.
        $zen = if ($ReadOnlyLocalShaderCache) { '-NoZenAutoLaunch=127.0.0.1:0' } else { '-NoZenAutoLaunch' }
        foreach ($arg in @('-DDC=NoZenLocalFallback', "-LocalDataCachePath=$cache", $zen,
                '-SharedDataCachePath=None', '-ZenSharedDataCacheHost=None', '-CloudDataCacheHost=None')) {
            $start.ArgumentList.Add($arg)
        }
    }
    if ($ReadOnlyLocalShaderCache) {
        if (!$PrivateDDC) { throw 'A private writable filesystem cache is required.' }
        $ready = Invoke-WebRequest -Uri 'http://[::1]:8558/health/ready' -NoProxy -MaximumRedirection 0 -TimeoutSec 5 -ErrorAction Stop
        if ($ready.StatusCode -ne 200) { throw 'The existing loopback Zen cache is unavailable.' }
        $start.ArgumentList.Add("-ini:Engine:[NoZenLocalFallback]:AcceptedLocalCache=$readOnlyZenNode")
        $start.ArgumentList.Add("-ini:Engine:[NoZenLocalFallback]:Root=$readOnlyZenRoot")
        $commands = "GetIni Engine:NoZenLocalFallback Root,GetIni Engine:NoZenLocalFallback AcceptedLocalCache,$commands"
    }
    foreach ($arg in @('-unattended', '-nop4',
            '-nosound', '-vulkan', '-RenderOffscreen', "-UserDir=$Directory/Profile", "-abslog=$Directory/Editor.log",
            "-ReportExportPath=$Directory/Report", "-ExecCmds=$commands",
            '-TestExit=Automation Test Queue Empty')) {
        $start.ArgumentList.Add($arg)
    }
    $started = [DateTimeOffset]::UtcNow
    $process = [Diagnostics.Process]::Start($start)
    try {
        if (!$process.WaitForExit(1000 * $TimeoutSeconds)) {
            $process.Kill($true)
            $process.WaitForExit()
            throw "Editor timed out. See $Directory/Editor.log"
        }
        if ($process.ExitCode -ne 0) { throw "Editor exited with code $($process.ExitCode). See $Directory/Editor.log" }
        if ($ReadOnlyLocalShaderCache) { Assert-ReadOnlyLocalCacheLog "$Directory/Editor.log" }
        return [ordered]@{ ProcessId = $process.Id; StartedUnix = $started.ToUnixTimeSeconds(); EndedUnix = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() }
    } finally { $process.Dispose() }
}

function Read-UEAutomationReport {
    param([string]$Directory, [string[]]$Names, $Editor)

    $index = Join-Path $Directory 'Report/index.json'
    $bytes = [IO.File]::ReadAllBytes($index)
    $result = [Text.Encoding]::UTF8.GetString($bytes).TrimStart([char]0xfeff) | ConvertFrom-Json
    $tests = @($result.tests)
    $devices = @($result.devices)
    $created = [DateTimeOffset]::ParseExact($result.reportCreatedOn, 'yyyy.MM.dd-HH.mm.ss',
        [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::AssumeUniversal).ToUnixTimeSeconds()
    if ($Editor.ProcessId -le 0 -or $created -lt $Editor.StartedUnix -or $created -gt $Editor.EndedUnix -or
        $devices.Count -ne 1 -or $devices[0].instanceName -cne "$([Environment]::MachineName)-$($Editor.ProcessId)" -or
        $devices[0].renderMode -cne 'Vulkan_SM6' -or $devices[0].instance -notmatch '^[a-f0-9]{32}$') {
        throw "UE report does not belong to this Vulkan editor invocation: $index"
    }
    if ($tests.Count -ne $Names.Count -or $result.succeeded -ne $Names.Count -or $result.failed -ne 0 -or
        $result.notRun -ne 0 -or $result.inProcess -ne 0 -or $result.succeededWithWarnings -ne 0) {
        throw "Incomplete or unsuccessful UE Automation suite: $index"
    }
    foreach ($name in $Names) {
        $match = @($tests | Where-Object fullTestPath -CEQ $name)
        if ($match.Count -ne 1 -or $match[0].state -cne 'Success' -or $match[0].errors -ne 0 -or
            @($match[0].deviceInstance).Count -ne 1 -or $match[0].deviceInstance[0] -cne $devices[0].instance) {
            throw "Missing or unsuccessful UE Automation test $name in $index"
        }
    }
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes))
}

function Assert-TrustedFunctionalReport {
    param([string]$RunDirectory, [string]$Commit, [string]$ModuleSha256, $Functional)

    if ($Functional.DerivedDataCache -cne 'PrivateFileSystem' -or
        $Functional.Commit -cne $Commit -or $Functional.RunId -cne (Split-Path $RunDirectory -Leaf) -or
        $Functional.ModuleSha256 -cne $ModuleSha256 -or !$Functional.Editor -or
        @($Functional.Tests).Count -ne $RequiredFunctionalTests.Count -or
        (Compare-Object $RequiredFunctionalTests $Functional.Tests -CaseSensitive)) {
        throw 'Functional evidence does not match this run, revision, editor DLL and required suite.'
    }
    $digest = Read-UEAutomationReport "$RunDirectory/Functional" $RequiredFunctionalTests $Functional.Editor
    if ($Functional.ReportSha256 -cne $digest) { throw 'UE Automation report digest differs from the build evidence.' }
}
