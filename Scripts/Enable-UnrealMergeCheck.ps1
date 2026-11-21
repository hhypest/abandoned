#requires -Version 7.0
[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter(Mandatory)][ValidatePattern('^[a-z0-9-]+$')][string]$AppSlug,
    [Parameter(Mandatory)][ValidateRange(1,2147483647)][int]$PullRequest,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{40}$')][string]$ApprovedHead
)
$ErrorActionPreference = 'Stop'
$repo = 'repos/hhypest/abandoned'
$context = 'Unreal Editor Win64 (isolated)'
$name = 'Require isolated Unreal Editor build'
function Read-GitHub([string]$Path) {
    $json = & gh api --hostname github.com $Path
    if ($LASTEXITCODE) { throw "Could not read $Path." }
    $json | ConvertFrom-Json
}
$app = Read-GitHub "apps/$AppSlug"
if ($app.owner.login -ne 'hhypest' -or !$app.id) { throw 'Use an owner-controlled GitHub App.' }
$pr = Read-GitHub "$repo/pulls/$PullRequest"
if ($pr.state -ne 'open' -or $pr.base.ref -ne 'main' -or $pr.head.repo.full_name -ne 'hhypest/abandoned' -or $pr.head.sha -ne $ApprovedHead) {
    throw 'Use the current head of the reviewed open PR to main.'
}
$combined = Read-GitHub "$repo/commits/$ApprovedHead/status"
$status = $combined.statuses | Where-Object context -CEQ $context | Select-Object -First 1
if ($status.state -ne 'success' -or $status.creator.login -ne "$AppSlug`[bot]") {
    throw 'Activate only after the first successful isolated build from the designated App.'
}
$rule = @{ type='required_status_checks'; parameters=@{ strict_required_status_checks_policy=$true
    required_status_checks=@(@{ context=$context; integration_id=$app.id }) } }
$payload = @{ name=$name; target='branch'; enforcement='active'; bypass_actors=@()
    conditions=@{ ref_name=@{ include=@('refs/heads/main'); exclude=@() } }; rules=@($rule) }
if (!(Read-GitHub "$repo/rulesets?per_page=100" | Where-Object name -CEQ $name)) {
    if (!$PSCmdlet.ShouldProcess('hhypest/abandoned main', "Require $context from App $($app.id)")) { return }
    $payload | ConvertTo-Json -Depth 9 | & gh api --hostname github.com -X POST "$repo/rulesets" --input - | Out-Null
    if ($LASTEXITCODE) { throw 'Could not create the UE ruleset.' }
}
$effective = Read-GitHub "$repo/rules/branches/main"
$checks = @($effective | Where-Object { $_.type -eq 'required_status_checks' -and $_.parameters.strict_required_status_checks_policy } |
    ForEach-Object { $_.parameters.required_status_checks })
if (!($checks | Where-Object { $_.context -ceq $context -and $_.integration_id -eq $app.id })) {
    throw 'The active rules for main do not require the expected publisher and context.'
}
Write-Output "Verified mandatory $context from App $($app.id) on main."
