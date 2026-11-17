$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$private = Join-Path $repo 'Source/Abandoned/Private'
foreach ($layer in @('Domain', 'Application')) {
    $layerPath = Join-Path $private $layer
    foreach ($file in (Get-ChildItem $layerPath -Recurse -File -Include '*.h', '*.cpp')) {
        foreach ($directive in (Select-String -LiteralPath $file.FullName -Pattern '^\s*#\s*include\s+(.*)$')) {
            $target = $directive.Matches[0].Groups[1].Value
            $allowed = $target -match '^<[a-z_]+>\s*$' -or $target -match '^"Domain/[^"]+"\s*$' -or
                ($layer -eq 'Application' -and $target -match '^"Application/[^"]+"\s*$')
            if (!$allowed) {
                throw "Invalid dependency in $($file.Name):$($directive.LineNumber) $target"
            }
        }
    }
}
$output = Join-Path $repo 'Saved/Architecture'
$null = New-Item -ItemType Directory -Path $output -Force
$test = Join-Path $repo 'Tests/DomainRules.cpp'
foreach ($configuration in @('Checks', 'NDebug')) {
    $executable = Join-Path $output "DomainRules-$configuration.exe"
    $object = Join-Path $output "DomainRules-$configuration.obj"
    $defines = @()
    if ($configuration -eq 'NDebug') { $defines = @('/DNDEBUG') }
    & cl.exe /nologo /std:c++20 /EHsc /W4 /WX @defines "/I$private" "/Fo$object" "/Fe$executable" $test
    if ($LASTEXITCODE -ne 0) { throw "Independent C++20 build failed ($configuration)." }
    & $executable
    if ($LASTEXITCODE -ne 0) { throw "Domain or application test failed ($configuration)." }

    $start = [Diagnostics.ProcessStartInfo]::new($executable)
    $start.UseShellExecute = $false
    $start.RedirectStandardError = $true
    $start.ArgumentList.Add('--verify-failure-path')
    $process = [Diagnostics.Process]::Start($start)
    try {
        $diagnostic = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        if ($process.ExitCode -eq 0 -or !$diagnostic.Contains('FailurePath failed at')) {
            throw "The failed-check control was not detected ($configuration)."
        }
    } finally {
        $process.Dispose()
    }
    Write-Output "Named tests and failed-check control passed ($configuration)."
}
Write-Output 'Architecture boundaries and rule tests passed.'
