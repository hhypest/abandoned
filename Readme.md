# Abandoned

Одиночная трёхмерная головоломка от первого лица с физикой и атмосферным хоррором. Инженер экспедиции приходит в себя внутри древнего сооружения на TOI-700 d. Чтобы выбраться, он изучает оставленные установки, управляет питанием и использует свойства кубов, света, гравитации и порталов. Тревогу создают пространство, звук и следы экспедиции. Камеры головоломок дают время разобраться в задаче.

| | Цель проекта |
| --- | --- |
| Движок | Unreal Engine 5.7 |
| Графика | Vulkan RHI |
| Код | C++20 с правилами безопасного владения и доступа к данным |
| Платформа и распространение | Windows x64, Steam, полностью бесплатно для игроков |
| Лицензия проекта | [MIT](LICENSE) |

## Текущее состояние

Создан стартовый C++-проект с целями Game и Editor, конфигурацией Vulkan и Enhanced Input. При открытии используется пустая карта движка. Управление, устройства головоломок, игровые карты и интеграция со Steam запланированы.

## Запуск на Windows

1. Установите UE 5.7 через Epic Games Launcher. В Visual Studio 2022 17.14 установите **Game development with C++**, MSVC v143 14.44.35214 и Windows SDK 10.0.22621.0 или новее. [Руководство Epic для UE 5.7](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine?application_version=5.7)
2. Клонируйте репозиторий или откройте существующую рабочую копию.

   ```powershell
   git clone https://github.com/hhypest/abandoned.git
   Set-Location abandoned
   ```

3. Из корня проекта выполните в PowerShell. Подставьте свой путь к UE 5.7, если он отличается.

   ```powershell
   $ueRoot = 'C:\Program Files\Epic Games\UE_5.7'
   $project = (Resolve-Path .\Abandoned.uproject).Path
   & "$ueRoot\Engine\Build\BatchFiles\Build.bat" AbandonedEditor Win64 Development "`"-Project=$project`"" -WaitMutex
   if ($LASTEXITCODE -ne 0) { throw "Сборка AbandonedEditor завершилась с кодом $LASTEXITCODE." }
   & "$ueRoot\Engine\Binaries\Win64\UnrealEditor.exe" $project -log
   ```

4. Дождитесь открытия редактора и проверьте в `Saved/Logs/Abandoned.log`, что активен Vulkan RHI. Первый запуск компилирует шейдеры. Для явной диагностики Vulkan см. [проверки в CONTRIBUTING](CONTRIBUTING.md#проверки-перед-pr).

## Первый игровой результат

Собрать начало Входного контура из серой геометрии. Переход ведёт через две учебные камеры к первой станции авангарда. На этом маршруте проверяются движение, задачи с кубом и плитой, повторяемый сброс, восстановление контрольной точки и подгрузка за шлюзом. [Порядок работ и критерии приёмки](Docs/Design.md#вертикальный-срез)

## Документация

- [Architecture.md](Architecture.md) описывает структуру проекта и границы игровых систем.
- [Docs/Design.md](Docs/Design.md) фиксирует требования к ритму, геометрии и первому срезу.
- [Docs/Assets.md](Docs/Assets.md) описывает проверку и импорт моделей. [Реестр исходников](Docs/AssetInventory.csv)
- [CONTRIBUTING.md](CONTRIBUTING.md) содержит правила работы и команды проверки Windows-сборки.
- [AGENTS.md](AGENTS.md) содержит инструкции для агентов, работающих с проектом.

MIT распространяется на код и документацию этого репозитория. Движок распространяется по условиям Epic, а внешние модели и материалы учитываются по их собственным лицензиям. [Лицензия проекта](LICENSE) · [Условия Unreal Engine](https://www.unrealengine.com/eula/unreal) · [Учёт ассетов](Docs/Assets.md)
