# Первая станция «Авангарда»

Выход третьей камеры ведёт в переход длиной около 120 м. В начале расположен отделанный пролёт со шлюзом. Шлюз поднимает створку при подходе и закрывается после прохода. Дальше холодный свет сменяется тёплым у входа на станцию. Станция загружается как отдельный игровой мир. На ней есть стойка кислорода, место отдыха, следы работы авангарда и смотровая панель.

В переходе запас кислорода уменьшается. На станции расход прекращается, а **E** возле стойки восстанавливает запас. При исчерпании запаса игрок возвращается к входу третьей камеры. Сохранение по-прежнему хранит вход последней достигнутой камеры. Переход и станция не перезаписывают его.

## Локальные ассеты пролёта

Публичная версия воспроизводит маршрут, коллизии, свет и базовые материалы. Рама и створка импортируются из `Sci_Fi_Industrial_Doo.zip` и `Sci_Fi_Sliding_Door_L.zip` в `Content/AbandonedLocal/Vanguard`. C++ подгоняет визуальные габариты к размерам, указанным в `VanguardPassageLayout`. В материале рамы маска открывает центральный проём 200 × 260 см. Створка связана с отдельным простым коллайдером, который движется вместе с ней. Оригинальные архивы сохраняются без правок. Права на публичное распространение исходников уточняются по записи провайдера.

Для чистового покрытия пролёта предусмотрены две позиции Fab Standard от Quixel. Пол использует [Industrial Concrete Floor](https://www.fab.com/listings/2dece1ec-0941-406c-a7de-ae9fe08085a1). Стены используют [Cast In Situ Concrete](https://www.fab.com/listings/fbfef28c-7e86-4f01-bc49-e3cb21bee1d0). Fab требует подтвердить условия перед загрузкой. Архивы и созданные из них Unreal-пакеты хранятся в локальном каталоге. [Fab EULA](https://www.fab.com/eula)

Распакуйте выбранные наборы текстур в папки `IndustrialFloor` и `CastConcrete` внутри `AssetSource/FabVanguard`. В каждой папке оставьте по одному комплекту карт base color, normal и roughness. Карта AO поддерживается дополнительно. Скрипт импортирует карты и создаёт материалы под путями из [DefaultGame.ini](../Config/DefaultGame.ini).

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

- Начало обычного маршрута — **Play**. Для сквозного автопрогона используйте `-game -vulkan -benchmark -fps=60 -VerifyEntryCourse -CaptureVanguard`. Прогон проходит три камеры, движущуюся дверь перехода, загружает станцию и пользуется её стойкой снабжения.
- Для прогона от перехода используйте карту `/Engine/Maps/Entry?game=/Script/Abandoned.VanguardRouteGameMode?Chamber=Passage` и флаги `-game -vulkan -benchmark -fps=60 -VerifyVanguard`.
- В журнале ищите активный Vulkan SM5, `Verification crossed the moving passage airlock` и `Vanguard verification passed`. Кадры с флагом захвата сохраняются в `Saved/Screenshots`.
