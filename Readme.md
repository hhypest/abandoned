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

**Play** запускает [Входной контур из трёх камер](Docs/EntryCourse.md). Маршрут учит собирать лестницу из кубов, управлять кнопкой и плитой, затем решать задачу на точный вес. Выход ведёт к [первой станции «Авангарда»](Docs/Vanguard.md). **M** открывает тестовое меню. Из него доступны стенды [движения](Docs/MovementLab.md), [оптики](Docs/Design.md#стенд-оптики), [гравитации](Docs/Design.md#стенд-гравитации) и [порталов](Docs/Design.md#стенд-порталов). В отдельном игровом окне **F11** переключает полноэкранный режим.

## Запуск на Windows

1. Установите UE 5.7 через Epic Games Launcher. Для сборки нужны MSVC v143 (Visual Studio 2022 17.14, инструментальная цепочка 14.44) и Windows SDK 10.0.22621.0 или новее. Подходит Visual Studio с **Game development with C++** либо Visual Studio Build Tools 2022 с компонентами C++ x64/x86 и Windows SDK. [Руководство Epic для UE 5.7](https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine?application_version=5.7)
2. Установите Git LFS, затем клонируйте репозиторий или откройте существующую рабочую копию. Git LFS нужен для материалов Unreal.

   ```powershell
   git clone https://github.com/hhypest/abandoned.git
   Set-Location abandoned
   git lfs install --local
   git lfs pull
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

Вертикальный срез начинается с перехода, проходит через три камеры Входного контура и заканчивается на первой станции «Авангарда». На маршруте проверяются движение, физические задачи, сброс камеры, восстановление от входа и подгрузка за шлюзом. [Порядок работ и критерии приёмки](Docs/Design.md#вертикальный-срез)

## Документация

- [Architecture.md](Architecture.md) описывает структуру проекта и границы игровых систем.
- [Docs/Design.md](Docs/Design.md) фиксирует требования к ритму, геометрии и первому срезу.
- [Docs/MechanicsDecisions.md](Docs/MechanicsDecisions.md) фиксирует согласованные правила механик.
- [Docs/TrainingChamber.md](Docs/TrainingChamber.md) описывает стенд устройств и проверку кубов.
- [Docs/Assets.md](Docs/Assets.md) описывает проверку и импорт моделей. [Реестр исходников](Docs/AssetInventory.csv)
- [CONTRIBUTING.md](CONTRIBUTING.md) содержит правила работы и команды проверки Windows-сборки.
- [AGENTS.md](AGENTS.md) содержит инструкции для агентов, работающих с проектом.

MIT распространяется на код и документацию этого репозитория. Движок распространяется по условиям Epic, а внешние модели и материалы учитываются по их собственным лицензиям. [Лицензия проекта](LICENSE) · [Условия Unreal Engine](https://www.unrealengine.com/eula/unreal) · [Учёт ассетов](Docs/Assets.md)
