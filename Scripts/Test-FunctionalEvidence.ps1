#requires -Version 7.0
# Uses native report-shaped fixtures, without an engine installation or remote operations.
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/TrustedBuild/FunctionalTests.ps1"
$root = Join-Path ([IO.Path]::GetTempPath()) ([guid]::NewGuid().ToString('N'))
$directory = Join-Path $root 'Functional'
$report = Join-Path $directory 'Report/index.json'
$sha = 'a' * 40
$dll = 'B' * 64
$device = 'C' * 32
$instant = [DateTimeOffset]::UtcNow
$editor = @{ ProcessId = $PID; StartedUnix = $instant.ToUnixTimeSeconds() - 1; EndedUnix = $instant.ToUnixTimeSeconds() + 1 }
$raw = @{
    reportCreatedOn = $instant.ToString('yyyy.MM.dd-HH.mm.ss')
    devices = @(@{ instance = $device; instanceName = "$([Environment]::MachineName)-$PID"; renderMode = 'Vulkan_SM6' })
    succeeded = 2; failed = 0; succeededWithWarnings = 0; notRun = 0; inProcess = 0
    tests = @(foreach ($name in $RequiredFunctionalTests) {
        @{ fullTestPath = $name; state = 'Success'; errors = 0; deviceInstance = @($device) }
    })
}
function Write-Fixture { $raw | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $report }
function Expect-Rejection([scriptblock]$Operation, [string]$Message) {
    try { & $Operation } catch { if ($_.Exception.Message -like $Message) { return }; throw }
    throw "Accepted invalid evidence, expected: $Message"
}
try {
    $null = New-Item -ItemType Directory -Path (Split-Path $report)
    Write-Fixture
    $binding = @{
        Commit = $sha; RunId = Split-Path $root -Leaf; ModuleSha256 = $dll; Editor = $editor
        Tests = $RequiredFunctionalTests; ReportSha256 = (Get-FileHash -LiteralPath $report).Hash
    }
    $check = { Assert-TrustedFunctionalReport $root $sha $dll $binding }
    & $check
    Expect-Rejection { Assert-TrustedFunctionalReport $root ('d' * 40) $dll $binding } 'Functional evidence does not match*'
    Expect-Rejection { Assert-TrustedFunctionalReport $root $sha ('E' * 64) $binding } 'Functional evidence does not match*'
    Expect-Rejection { Assert-TrustedFunctionalReport $root $sha $dll $null } 'Functional evidence does not match*'
    $binding.RunId = 'previous'
    Expect-Rejection $check 'Functional evidence does not match*'
    $binding.RunId = Split-Path $root -Leaf
    $raw.tests[1].fullTestPath = $RequiredFunctionalTests[0]
    Write-Fixture
    Expect-Rejection $check 'Missing or unsuccessful*'
    $raw.tests[1].fullTestPath = $RequiredFunctionalTests[1]
    $raw.tests[1].state = 'Fail'
    Write-Fixture
    Expect-Rejection $check 'Missing or unsuccessful*'
    $raw.tests[1].state = 'Success'
    $raw.reportCreatedOn = $instant.AddDays(-1).ToString('yyyy.MM.dd-HH.mm.ss')
    Write-Fixture
    Expect-Rejection $check 'UE report does not belong*'
    $raw.reportCreatedOn = $instant.ToString('yyyy.MM.dd-HH.mm.ss')
    $raw.devices[0].renderMode = 'D3D12'
    Write-Fixture
    Expect-Rejection $check 'UE report does not belong*'
    $raw.devices[0].renderMode = 'Vulkan_SM6'
    $raw.tests[1].deviceInstance = @('F' * 32)
    Write-Fixture
    Expect-Rejection $check 'Missing or unsuccessful*'
    $raw.tests[1].deviceInstance = @($device)
    Write-Fixture
    Add-Content -LiteralPath $report -Value ' '
    Expect-Rejection $check 'UE Automation report digest differs*'
    Remove-Item -LiteralPath $report
    Expect-Rejection $check "*$([IO.Path]::GetFileName($report))*"
} finally {
    $temp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
    if ((Split-Path $root -Parent).TrimEnd('\') -ne $temp -or (Split-Path $root -Leaf) -notmatch '^[0-9a-f]{32}$') {
        throw 'Unexpected temporary evidence path.'
    }
    Remove-Item -LiteralPath $root -Recurse -Force
}
Write-Output 'Functional evidence controls passed for commit, run, module, raw report, Vulkan device and suite.'
