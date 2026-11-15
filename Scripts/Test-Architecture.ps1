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
$executable = Join-Path $output 'DomainRules.exe'
$object = Join-Path $output 'DomainRules.obj'
& cl.exe /nologo /std:c++20 /EHsc /W4 /WX "/I$private" "/Fo$object" "/Fe$executable" $test
if ($LASTEXITCODE -ne 0) { throw 'Independent C++20 build failed.' }
& $executable
if ($LASTEXITCODE -ne 0) { throw 'Domain or application test failed.' }
Write-Output 'Architecture boundaries and rule tests passed.'
