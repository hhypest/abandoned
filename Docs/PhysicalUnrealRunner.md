# Проверка Unreal до слияния на физическом Windows‑ПК

Проверяем `AbandonedEditor Win64 Development` для открытого PR из `hhypest/abandoned` в `main`. Оператор просматривает весь diff, включая правила UBT, инструменты и тесты, и передаёт номер PR и полный одобренный SHA. Обновление ветки требует проверки нового SHA. Runner запускается по запросу оператора. Публичные workflows не имеют очереди заданий на персональном ПК.

## Локальная проверка

```powershell
./Scripts/Invoke-UnrealPullRequestCheck.ps1 -PullRequest $pr -ApprovedHead $sha -EngineRoot C:\UE\5.7.4 -RunsRoot C:\UEBuilds -LocalOnly
```

Команда использует текущую учётную запись и сохраняет `report.json` и журналы в новом каталоге. Она читает метаданные PR, сверяет SHA с полученным `refs/pull/<номер>/head` до checkout и проверяет, что актуальная `main` входит в историю сборки. После UBT она повторно проверяет PR, базу и SHA‑256 полученной DLL. Режим `-LocalOnly` не публикует статусы.

## Подготовка отдельной учётной записи

1. Установите общесистемные PowerShell 7 x64, Git с Git LFS, VS 2022 и Windows SDK. [Версии и подготовка UE](TrustedUnrealBuild.md)
2. Из доверенной копии запустите `Scripts/Install-UnrealBuildWorker.ps1 -WhatIf` для просмотра операции. Для установки откройте повышенный PowerShell 7, запустите тот же скрипт без `-WhatIf` и задайте пароль локально. Сохраните его в менеджере паролей оператора.
3. Установщик создаёт обычного пользователя `AbandonedUEBuild` и закрытый для записи пользователем каталог инструментов в `%ProgramFiles%\AbandonedUEWorker\Scripts`. Пользователь получает право изменения в двух подкаталогах, `Unreal` и `Runs`. Отдельно подготовьте в `Unreal` рабочую копию закреплённого UE с каталогом `Engine`, метаданными Git и заранее собранными общими модулями, как в инструкции по UE. Используйте собственные файлы этой копии. Сохраните ключи Epic, Git credential helpers с секретами, конфигурацию издателя и личные исходники в профиле оператора. Проверяйте свободное место с учётом размера второй установки движка и отдельных клонов.
4. При подготовке и обновлении инструментов проверяйте, что обычный пользователь не может изменять каталог `Scripts`, PowerShell, Git и глобальный toolchain или читать учётные данные издателя. Проверяйте запуск под отдельным SID. Сборки делят ядро Windows и отдельную установку UE. Подозрение на изменение среды требует заново подготовить её до следующего допуска. После завершения сборки и разбора журнала оператор может архивировать или удалить её индивидуальный каталог в `Runs`.
5. Для репозитория заведите отдельное GitHub App владельца `hhypest`, установите только в `hhypest/abandoned`. Нужны чтение pull requests, чтение метаданных и запись commit statuses. На стороне издателя предоставьте GitHub CLI короткоживущий installation token этого приложения через `GH_TOKEN`. Ключ приложения и токен остаются у издателя. [Токены установки GitHub App](https://docs.github.com/en/apps/creating-github-apps/authenticating-with-a-github-app/generating-an-installation-access-token-for-a-github-app) · [Commit statuses](https://docs.github.com/en/rest/commits/statuses)

## Публикация и включение защиты

Из обычного сеанса оператора с полномочиями приложения запускайте установленную копию. Получите `$sha` при просмотре изменений и сохраните его перед запуском.

```powershell
$install = Join-Path $env:ProgramFiles 'AbandonedUEWorker'
$worker = Get-Credential -UserName "$env:COMPUTERNAME\AbandonedUEBuild" -Message 'Локальный пользователь сборки UE'
& "$install/Scripts/Invoke-UnrealPullRequestCheck.ps1" -PullRequest $pr -ApprovedHead $sha -AppSlug $appSlug -WorkerCredential $worker -EngineRoot "$install/Unreal" -RunsRoot "$install/Runs"
```

Издатель ставит `pending` на одобренный SHA, запускает сборку под отдельным SID с очищенным окружением, сверяет отчёт и публикует `success` либо `failure` на том же SHA. После аварийного завершения издателя сохранится `pending`. Прогон можно повторить. Любое обновление PR требует нового результата.

После **первой успешной изолированной сборки** запустите установку правила от имени администратора репозитория в отдельном сеансе GitHub CLI. Скрипт сверяет PR, текущий SHA, последнее состояние и издателя. Сначала проверьте с `-WhatIf`, затем выполните команду:

```powershell
& "$install/Scripts/Enable-UnrealMergeCheck.ps1" -PullRequest $pr -ApprovedHead $sha -AppSlug $appSlug
```

Создаётся дополнительное строгое правило для `main` с контекстом `Unreal Editor Win64 (isolated)` и ID конкретного приложения. Существующее правило C++20 и Git LFS сохраняется. Команда считывает действующие правила обратно. Для приёмки отправьте новый коммит в проверочный PR, убедитесь, что GitHub ожидает новый результат, и повторите сборку после просмотра нового SHA. [API правил репозитория](https://docs.github.com/en/rest/repos/rules)
