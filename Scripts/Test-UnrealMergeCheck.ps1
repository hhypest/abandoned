#requires -Version 7.0
# In-memory GitHub responses. No repository writes, Windows accounts or UE installation required.
$ErrorActionPreference = 'Stop'
$sha = 'a' * 40
$app = 'test-ue-worker'
$check = 'Unreal Editor Win64 (isolated)'
$state = @{ status='success'; author="$app`[bot]"; head=$sha; repository='hhypest/abandoned'; writes=0; installed=$false }
function gh {
    $request = $args -join ' '
    if ($request -like '*-X POST*') {
        $rule = ($input | Out-String | ConvertFrom-Json)
        $params = $rule.rules[0].parameters
        if ($rule.enforcement -ne 'active' -or $rule.conditions.ref_name.include[0] -ne 'refs/heads/main' -or
            $rule.bypass_actors.Count -ne 0 -or !$params.strict_required_status_checks_policy -or
            $params.required_status_checks[0].integration_id -ne 123 -or $params.required_status_checks[0].context -cne $check) {
            throw 'Unexpected protection policy'
        }
        $state.writes++
        $state.installed=$true
        $response=@{}
    } elseif ($request -like '*/pulls/9') {
        $response=@{state='open';head=@{sha=$state.head;repo=@{full_name=$state.repository}};base=@{ref='main';repo=@{full_name='hhypest/abandoned'}}}
    } elseif ($request -like '*/status') {
        $response=@{statuses=@(@{context=$check;state=$state.status;creator=@{login=$state.author}})}
    } elseif ($request -like '* apps/*') {
        $response=@{id=123;owner=@{login='hhypest'}}
    } elseif ($request -like '*/rulesets?per_page=100') {
        $response=if ($state.installed) { ,@(@{name='Require isolated Unreal Editor build'}) } else { ,@() }
    } elseif ($request -like '*/rules/branches/main') {
        $response=@(@{type='required_status_checks';parameters=@{strict_required_status_checks_policy=$true
            required_status_checks=@(@{context=$check;integration_id=123})}})
    } else { throw "Unexpected request: $request" }
    $global:LASTEXITCODE=0
    ConvertTo-Json -InputObject $response -Depth 8 -Compress
}
function Expect-Rejection([scriptblock]$Action,[string]$Message) {
    try { & $Action } catch { if ($_.Exception.Message -like $Message) { return }; throw }
    throw 'Operation succeeded unexpectedly'
}
$enable = { & "$PSScriptRoot/Enable-UnrealMergeCheck.ps1" -AppSlug $app -PullRequest 9 -ApprovedHead $sha }
$local = { & "$PSScriptRoot/Invoke-UnrealPullRequestCheck.ps1" -LocalOnly -PullRequest 9 -ApprovedHead $sha -EngineRoot . -RunsRoot . }
$state.status='pending'
Expect-Rejection $enable 'Activate only after*'
$state.status='success'; $state.author='somebody-else[bot]'
Expect-Rejection $enable 'Activate only after*'
$state.author="$app`[bot]"; $state.head='b' * 40
Expect-Rejection $enable 'Use the current head*'
Expect-Rejection $local 'Expected an open*'
$state.head=$sha; $state.repository='fork/abandoned'
Expect-Rejection $local 'Expected an open*'
$state.repository='hhypest/abandoned'
& "$PSScriptRoot/Enable-UnrealMergeCheck.ps1" -AppSlug $app -PullRequest 9 -ApprovedHead $sha -WhatIf
if ($state.writes) { throw 'A rejected or previewed request changed protection.' }
& $enable
& $enable
if ($state.writes -ne 1) { throw 'Expected one additive ruleset for two requests.' }
Write-Output 'PR admission, stale SHA, publisher, protection preview and idempotence passed.'
