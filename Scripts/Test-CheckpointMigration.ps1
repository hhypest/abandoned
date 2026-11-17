[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EngineRoot,
    [Parameter(Mandatory)][string]$LegacyProfiles
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$editor = Join-Path (Resolve-Path -LiteralPath $EngineRoot).Path 'Engine/Binaries/Win64/UnrealEditor.exe'
$project = Join-Path $repo 'Abandoned.uproject'
$fixtures = (Resolve-Path -LiteralPath $LegacyProfiles).Path
$root = Join-Path $repo "Saved/CheckpointMigration/$([guid]::NewGuid().ToString('N'))"
$legacy = 'Saved/SaveGames/AbandonedEntryCourse.sav'
$campaign = 'Saved/SaveGames/AbandonedCampaign.v2.json'
$entry = '/Engine/Maps/Entry'

function New-Profile($Name, $LegacyId) {
    $profile = Join-Path $root $Name
    $folder = Join-Path $profile 'Saved/SaveGames'
    $null = New-Item -ItemType Directory -Path $folder -Force
    Copy-Item -LiteralPath (Join-Path (Join-Path $fixtures $LegacyId) $legacy) -Destination $folder
    return $profile
}

function Invoke-Game($Profile, $Name, $Map, $Markers, $Flag = '') {
    $log = Join-Path $Profile "$Name.log"
    $launch = '"{0}" "{1}" {2} -game -vulkan -unattended -windowed -ResX=1280 -ResY=720 -benchmark -fps=60 -forcelogflush -UserDir="{3}" -abslog="{4}"' -f $project,$Map,$Flag,$Profile,$log
    $game = Start-Process -FilePath $editor -ArgumentList $launch -WindowStyle Hidden -PassThru
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $expected = @('Vulkan RHI ShaderPlatform for SM5: VULKAN_SM5') + $Markers
    try {
        while ($watch.Elapsed.TotalSeconds -lt 90) {
            if (Test-Path -LiteralPath $log) {
                $text = Get-Content -LiteralPath $log -Raw
                if ($text) {
                    if ($text -match ': Error:|Fatal error:') { throw "Unexpected error in $log" }
                    if (!($expected | Where-Object { !$text.Contains($_) })) { return }
                }
            }
            if ($game.HasExited) { break }
            Start-Sleep -Milliseconds 300
        }
        throw "Missing required markers in $log"
    } finally {
        if (!$game.HasExited) { Stop-Process -Id $game.Id; $game.WaitForExit() }
        $game.Dispose()
    }
}

function Assert-Hash($Path, $Expected) {
    if ((Get-FileHash -LiteralPath $Path).Hash -ne $Expected) { throw "Unexpected change to $Path" }
}

Write-Host "Migration logs: $root"
foreach ($id in 'Stairs','Button','Weight','Balance') {
    $profile = New-Profile $id $id
    $source = Join-Path $profile $legacy
    $target = Join-Path $profile $campaign
    $hash = (Get-FileHash -LiteralPath $source).Hash
    Invoke-Game $profile 'Migrate' $entry @("Loaded campaign checkpoint v1: $id", "Saved campaign checkpoint v2: $id", "Training chamber ready. $id")
    $save = Get-Content -LiteralPath $target -Raw | ConvertFrom-Json
    if ($save.version -ne 2 -or $save.checkpoint -cne $id -or $save.archive.Count -ne 0) { throw "Wrong migrated snapshot for $id" }
    Invoke-Game $profile 'Reopen' $entry @("Loaded campaign checkpoint v2: $id", "Training chamber ready. $id")
    Assert-Hash $source $hash
    Write-Host "$id migrated and reopened, v1 SHA-256 unchanged."
}

$blocked = New-Profile 'FirstPublishBlocked' 'Weight'
$before = (Get-FileHash -LiteralPath (Join-Path $blocked $legacy)).Hash
$null = New-Item -ItemType Directory -Path (Join-Path $blocked $campaign)
Invoke-Game $blocked 'Blocked' $entry @('Loaded campaign checkpoint v1: Weight','Campaign checkpoint replacement failed','Training chamber ready. Weight')
Assert-Hash (Join-Path $blocked $legacy) $before
Write-Host 'First v2 publication failed, v1 SHA-256 unchanged.'

$profile = Join-Path $root 'Weight'
$target = Join-Path $profile $campaign
$legacyHash = (Get-FileHash -LiteralPath (Join-Path $profile $legacy)).Hash
$v2Hash = (Get-FileHash -LiteralPath $target).Hash
$lock = [IO.File]::Open($target, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
try {
    Invoke-Game $profile 'ReplaceBlocked' "${entry}?Chamber=Button?EntryCourse=1" @(
        'Campaign checkpoint replacement failed','Could not save the entry checkpoint. Previous data preserved.',
        'Training chamber ready. Button')
} finally {
    $lock.Dispose()
}
Assert-Hash $target $v2Hash
Assert-Hash (Join-Path $profile $legacy) $legacyHash
Invoke-Game $profile 'AfterFailure' $entry @('Loaded campaign checkpoint v2: Weight','Training chamber ready. Weight')
Write-Host 'Replacement failed, both hashes unchanged, previous v2 reopened.'

$future = New-Profile 'UnknownVersion' 'Weight'
$futureSlot = Join-Path $future $campaign
Set-Content -LiteralPath $futureSlot -Value '{"version":99,"checkpoint":"Weight","archive":[]}' -Encoding utf8
$futureHash = (Get-FileHash -LiteralPath $futureSlot).Hash
Invoke-Game $future 'Fallback' $entry @('Unsupported checkpoint version.','Training chamber ready. Stairs')
Invoke-Game $future 'Protect' "${entry}?Chamber=Button?EntryCourse=1" @('Campaign checkpoint preserved. Unsupported checkpoint version.','Training chamber ready. Button')
Assert-Hash $futureSlot $futureHash
Write-Host 'Unknown version was preserved during fallback and subsequent writes.'

$balance = Join-Path $root 'Balance'
Invoke-Game $balance 'Resupply' $entry @('Saved campaign checkpoint v2: Balance (1 archive records).','Vanguard verification passed') '-VerifyEntryCheckpoint'
$save = Get-Content -LiteralPath (Join-Path $balance $campaign) -Raw | ConvertFrom-Json
if ($save.checkpoint -cne 'Balance' -or $save.archive.Count -ne 1 -or $save.archive[0] -cne 'oxygen-restored') {
    throw 'Wrong persisted campaign resupply record.'
}
Write-Host 'Campaign resupply record persisted.'
