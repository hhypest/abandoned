[CmdletBinding(DefaultParameterSetName='Editor')]
param(
    [Parameter(Mandatory, ParameterSetName='Editor')][string]$EngineRoot,
    [Parameter(Mandatory, ParameterSetName='Package')][string]$PackageRoot,
    [Parameter(Mandatory)][string]$LegacyProfiles
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if ($PSCmdlet.ParameterSetName -eq 'Package') {
    $executable = (Get-Item -LiteralPath (Join-Path $PackageRoot 'Abandoned/Binaries/Win64/Abandoned.exe')).FullName
    $launchPrefix = ''
} else {
    $executable = (Get-Item -LiteralPath (Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor.exe')).FullName
    $project = Join-Path $repo 'Abandoned.uproject'
    $launchPrefix = '"{0}" ' -f $project
}
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

function Invoke-Game($Profile, $Name, $Map, $Markers, $Flag = '', [switch]$RequireNormalExit) {
    $log = Join-Path $Profile "$Name.log"
    $launch = '{0}"{1}" {2} -game -vulkan -unattended -windowed -ResX=1280 -ResY=720 -benchmark -fps=60 -forcelogflush -UserDir="{3}" -abslog="{4}"' -f $launchPrefix,$Map,$Flag,$Profile,$log
    $game = Start-Process -FilePath $executable -ArgumentList $launch -WindowStyle Hidden -PassThru
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $expected = @('Vulkan RHI ShaderPlatform for SM6: VULKAN_SM6') + $Markers
    try {
        while ($watch.Elapsed.TotalSeconds -lt 90) {
            $exited = $game.HasExited
            if (Test-Path -LiteralPath $log) {
                $text = Get-Content -LiteralPath $log -Raw
                if ($text) {
                    if ($text -match ': Error:|Fatal error:') { throw "Unexpected error in $log" }
                    if (!($expected | Where-Object { !$text.Contains($_) })) {
                        if (!$RequireNormalExit) { return }
                        if ($exited) {
                            if ($game.ExitCode -ne 0) { throw "Unexpected exit code $($game.ExitCode) in $log" }
                            return
                        }
                    }
                }
            }
            if ($exited) { break }
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

if ($PSCmdlet.ParameterSetName -eq 'Package') {
    # Use the archived Balance slot. Check order and persistence across rapid travel and an immediate quit.
    $commands = "60:open ${entry}?Chamber=Button,64:open ${entry}?Chamber=Stairs,68:open ${entry}?Chamber=Weight,70:quit"
    $flags = '-ExecCmds="CsvProfile START" "-csvExecCmds={0}"' -f $commands
    Invoke-Game $balance 'TravelQuit' $entry @('Training chamber ready. Weight', 'LogExit: Exiting.') $flags -RequireNormalExit
    $log = Get-Content -LiteralPath (Join-Path $balance 'TravelQuit.log') -Raw
    $rooms = [regex]::Matches($log, 'Training chamber ready\. (\w+),') | ForEach-Object { $_.Groups[1].Value }
    if (($rooms -join ',') -cne 'Balance,Button,Stairs,Weight') { throw "Unexpected travel order: $rooms" }
    $save = Get-Content -LiteralPath (Join-Path $balance $campaign) -Raw | ConvertFrom-Json
    if ($save.version -ne 2 -or $save.checkpoint -cne 'Weight' -or ($save.archive -join ',') -cne 'oxygen-restored') {
        throw 'A rapid travel or shutdown lost the last arrival or a previously persisted archive record.'
    }
    Invoke-Game $balance 'AfterQuit' $entry @('Loaded campaign checkpoint v2: Weight', 'Training chamber ready. Weight')
    Write-Host 'Rapid travel and normal shutdown preserved the last checkpoint and the archive.'
}
