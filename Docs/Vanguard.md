# Первая станция «Авангарда»

Выход четвёртой камеры ведёт в переход длиной около 120 м. В начале расположен отделанный пролёт со шлюзом. Шлюз поднимает створку при подходе и закрывается после прохода. Дальше холодный свет сменяется тёплым у входа на станцию. Станция загружается как отдельный игровой мир. На ней есть стойка кислорода, место отдыха, следы работы авангарда и смотровая панель.

В переходе запас кислорода уменьшается. На станции расход прекращается, а **E** возле стойки восстанавливает запас. При исчерпании запаса игрок возвращается к последней достигнутой камере. При обычном прохождении это вход в камеру 04. Переход и станция используют ту же контрольную точку.

**F** включает и выключает плечевой фонарь. Он направлен по взгляду, не расходует заряд и сохраняет состояние при входе на станцию. В новом прохождении перехода фонарь выключен. Положение, дальность, яркость и углы луча задаёт секция `VanguardFlashlightSettings` в [DefaultGame.ini](../Config/DefaultGame.ini).

За шлюзом находится обязательный уступ **1,80 м**. Подойдите к световой кромке, повернитесь к ней и **удерживайте Space**, пока не начнётся подъём. Короткое нажатие выполняет обычный прыжок. Во время подъёма персонаж сначала поднимается над краем, затем переносит корпус на площадку. Площадка и путь капсулы должны быть свободны. Камера следует за персонажем, а игрок может осматриваться.

В конфигурации перехода `bEnableMantle` включает подъём, а `MantleLedges` задаёт пригодные уступы. Прототип допускает перепад 1,10–1,85 м от последней опоры. Стены, створки, кубы и геометрия учебных камер сохраняют обычные правила движения.

В переходе последствия падения зависят от перепада между опорой перед отрывом и поверхностью приземления. На 3 м скафандр кратко сообщает о лёгкой травме. Падение выше 3 м возвращает игрока к исходному состоянию последней достигнутой камеры. Измерение контактов округляется до миллиметра. Пороговые платформы для проверки появляются только с флагом `-VerifyVanguardFalls`.

## Локальные ассеты пролёта

Публичная версия воспроизводит маршрут, коллизии, свет и базовые материалы. Рама и створка импортируются из `Sci_Fi_Industrial_Doo.zip` и `Sci_Fi_Sliding_Door_L.zip` в `Content/AbandonedLocal/Vanguard`. C++ подгоняет визуальные габариты к размерам, указанным в `VanguardPassageLayout`. В материале рамы маска открывает центральный проём 200 × 260 см. Створка связана с отдельным простым коллайдером, который движется вместе с ней. Оригинальные архивы сохраняются без правок. Права на публичное распространение исходников уточняются по записи провайдера.

Чистовой пролёт использует два набора текстур 8K от Quixel по стандартной лицензии Fab. Архивы храните в `AssetSource/FabVanguard/Archives`, а созданные Unreal-пакеты — в `Content/AbandonedLocal/Vanguard`. Оба каталога исключены из Git. [Fab EULA](https://www.fab.com/eula)

| Поверхность | Архив в разделе «Набор текстур» | Папка для карт в `AssetSource/FabVanguard` |
| --- | --- | --- |
| Пол | `industrial_concrete_floor_sl2qedtp_8k.zip` ([Industrial Concrete Floor](https://www.fab.com/listings/2dece1ec-0941-406c-a7de-ae9fe08085a1)) | `IndustrialFloor` |
| Стены | `cast_in_situ_concrete_uhwnbdoew_8k.zip` ([Cast In Situ Concrete](https://www.fab.com/listings/fbfef28c-7e86-4f01-bc49-e3cb21bee1d0)) | `CastConcrete` |

Из каждого архива извлеките карты `*_BaseColor.jpg`, `*_Normal.jpg`, `*_Roughness.jpg` и `*_AO.jpg` в указанную папку. Скрипт создаёт или обновляет материалы под путями из [DefaultGame.ini](../Config/DefaultGame.ini). Проекция по мировым координатам сохраняет размер скана 2 × 2 м на полу, стенах и длинных секциях перехода.

```powershell
$ueRoot = 'C:\Program Files\Epic Games\UE_5.7'
$project = (Resolve-Path .\Abandoned.uproject).Path
$script = (Resolve-Path .\Scripts\ImportVanguardAssets.py).Path
$env:ABANDONED_DOOR_SOURCES = (Resolve-Path .\AssetSource\DoorSources).Path
$env:ABANDONED_FAB_SOURCES = (Resolve-Path .\AssetSource\FabVanguard).Path
& "$ueRoot\Engine\Binaries\Win64\UnrealEditor.exe" $project "-ExecutePythonScript=$script" -nop4 -log
```

`ABANDONED_DOOR_SOURCES` указывает на папку с двумя ZIP. Если переменная `ABANDONED_FAB_SOURCES` отсутствует, импортируются только рама и створка. Успешный импорт отмечается строками `VANGUARD_DOOR_READY` и `VANGUARD_FAB_READY` в журнале Unreal. Строка `Passage artwork` при старте игры показывает, какие ресурсы действительно загружены.

## Проверка

- Начало обычного маршрута — **Play**. Для сквозного автопрогона используйте `-game -vulkan -benchmark -fps=60 -VerifyEntryCourse -CaptureVanguard`. Прогон проходит четыре камеры, движущуюся дверь перехода, загружает станцию и пользуется её стойкой снабжения.
- Для прогона от перехода используйте карту `/Engine/Maps/Entry?game=/Script/Abandoned.VanguardRouteGameMode?Chamber=Passage` и флаги `-game -vulkan -benchmark -fps=60 -VerifyVanguard`.
- В журнале ищите активный Vulkan SM5, `Passage artwork: frame=loaded, leaf=loaded, Fab floor=loaded, Fab walls=loaded.`, `Verification crossed the moving passage airlock` и `Vanguard verification passed`. Кадры с флагом захвата сохраняются в `Saved/Screenshots`.
- Метки `Passage ledge stopped walking`, `Passage climb rejected an obstructed landing` и `Passage climb verified` подтверждают проверки уступа. С флагом `-CaptureVanguard` сохраняются также кадры подсказки и верхней площадки.
