# Участие в разработке

Для первой сборки выполните [инструкцию запуска](Readme.md#запуск-на-windows). При работе с бинарными ассетами установите Git LFS и один раз выполните `git lfs install --local` в рабочей копии. После получения изменений с контентом выполните `git lfs pull`. [О Git LFS на GitHub](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-git-large-file-storage)

## Изменения и ревью

1. Создайте ветку от актуальной `main` и ограничьте её одной задачей. Для новой механики запишите ожидаемое поведение, граничные случаи и способ проверки.
2. Размещайте контент проекта под `/Game/Abandoned/`. Используйте последовательные имена (`L_` для карт, `BP_` для акторов Blueprint, `SM_` для Static Mesh, `SK_` для Skeletal Mesh, `MI_` для Material Instance). Переименовывайте ассеты в редакторе и исправляйте redirectors вместе со ссылками.
3. При добавлении контента проверьте `git lfs status` и обновите реестр исходников, лицензию и атрибуцию. Рабочие FBX, текстуры и архивы можно держать в локальной папке `AssetSource/`.
4. В PR укажите сценарий проверки, результаты и ограничения. Для карт укажите путь и условия старта. Для графических изменений добавьте кадры из запуска с указанными RHI, GPU и настройками.

## Исправление Renderer в UE 5.7.4

Для упакованной игры используйте исходную сборку UE 5.7.4. В проверенной ревизии Epic `260bb2e1c5610b31c63a36206eedd289409c5f11` функция `FSceneTextureExtracts::Release()` пропускает ссылку на частичную глубину. Из корня Abandoned один раз примените поправку к отдельной рабочей копии исходников движка, подготовленной для сборки. Бинарная установка из Launcher пригодна для разработки, но её готовый модуль Renderer содержит исходное поведение.

```powershell
$ueRoot = 'C:\src\UnrealEngine-5.7.4'
$patch = (Resolve-Path ./Scripts/EnginePatches/UE-5.7.4-PartialDepth.patch).Path
git -C $ueRoot apply --check $patch
if ($LASTEXITCODE -ne 0) { throw 'Проверьте версию Renderer и наличие исправления.' }
git -C $ueRoot apply $patch
if ($LASTEXITCODE -ne 0) { throw 'Не удалось применить исправление Renderer.' }
```

Затем соберите редактор и игру из этой копии движка. При обновлении UE проверьте освобождение `PartialDepth` в исходниках новой версии и повторите `VulkanExitControl` и `EntryCourse` в пакете. [Протокол Vulkan](Docs/Testing.md#упакованная-игра-win64-development)

## Исправление AudioMixer и трассировка OpenLevel

Для игровой сборки также примените поправку AudioMixer к той же ревизии UE 5.7.4. В исходном движке неподвижный слушатель каждый кадр добавляет в очередь аудиорендера свою прежнюю трансформацию. Поправка сохраняет передачу изменений положения, ориентации и состава слушателей.

```powershell
$patch = (Resolve-Path ./Scripts/EnginePatches/UE-5.7.4-ListenerUpdates.patch).Path
git -C $ueRoot apply --check $patch
if ($LASTEXITCODE -ne 0) { throw 'Проверьте версию AudioMixer и наличие исправления.' }
git -C $ueRoot apply $patch
if ($LASTEXITCODE -ne 0) { throw 'Не удалось применить исправление AudioMixer.' }
```

Для сравнения фаз загрузки примените диагностический патч `Scripts/EnginePatches/UE-5.7.4-LoadMapScopes.patch` к обеим сравниваемым сборкам до компиляции. Он добавляет именованные интервалы CPU в Unreal Insights. [Методика, контрольная сборка и результаты](Docs/A07OpenLevel.md)

## Проверки перед PR

- GitHub Actions запускает **C++20 and Git LFS** для PR в `main` и изменений в `main`. Проверка сравнивает diff с базой, валидирует указатели и содержимое Git LFS и запускает автономные C++20-тесты под MSVC на Windows.
- Запустите `git diff --check`, соберите `AbandonedEditor Win64 Development` и проверьте проект в редакторе.
- Для доменных правил и сценариев запустите `pwsh -File Scripts/Test-Architecture.ps1` из окружения MSVC x64. При ревью проверьте границы контекстов и направление зависимостей по [правилам архитектуры](Architecture.md#ddd-и-clean-architecture).
- Автономные тесты выполняются с `NDEBUG` и без него. Проверка пути ошибки должна вернуть ненулевой код в обеих конфигурациях. Результат каждой группы имеет имя.
- Для изменений в движении, логике и контенте пройдите затронутый маршрут в PIE. Повторите взаимодействие после сброса и после загрузки, если игровой сценарий это предусматривает. Тесты новых систем запускайте через **Session Frontend → Automation**.
- Для изменений в рендере проверьте запуск по умолчанию и выполните [локальные автопрогоны Vulkan](Docs/Testing.md#автопрогоны-vulkan) для проверенного SHA. Приложите результат с RHI и GPU к PR. Для логических сценариев пригодность `-nullrhi` проверяется отдельно.
- Когда появится собственная стартовая карта, задайте её в **Project Settings → Maps & Modes**, проверьте состав включённых карт и выполните пробную сборку. Убедитесь, что она открывает игровой маршрут на машине без редактора.

  ```powershell
  $ueRoot = 'C:\src\UnrealEngine-5.7.4'
  $project = (Resolve-Path .\Abandoned.uproject).Path
  $archive = Join-Path (Get-Location).Path 'Artifacts\Win64'
  & "$ueRoot\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun "-project=$project" -noP4 -platform=Win64 -clientconfig=Development -build -cook -stage -pak -package -archive "-archivedirectory=$archive"
  if ($LASTEXITCODE -ne 0) { throw "Сборка Win64 завершилась с кодом $LASTEXITCODE." }
  ```

## Подготовка к Steam

1. Владелец заводит приложение в Steamworks, проходит Steam Direct и выбирает **This is a free product**. Целевой выпуск бесплатен для игрока. [Steam Direct](https://partner.steamgames.com/steamdirect) · [Настройка бесплатной игры](https://partner.steamgames.com/doc/store/freetoplay)
2. Подготовьте Windows-депо и чистую Shipping-сборку. Проверьте Vulkan, титры и атрибуцию, восстановление сохранения, запуск через Steam и без сетевого доступа.
3. Заполните Content Survey, включая пункт об использовании предварительно сгенерированного ИИ-контента, если такой контент входит в игру. Подготовьте страницу магазина и передайте страницу и сборку на проверку Valve. [Анкета контента](https://partner.steamgames.com/doc/gettingstarted/contentsurvey) · [Процесс проверки](https://partner.steamgames.com/doc/store/review_process)
