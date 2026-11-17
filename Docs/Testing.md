# Проверка камер Abandoned

Соберите `AbandonedEditor Win64 Development` в UE 5.7 по [инструкции запуска](../Readme.md#запуск-на-windows). В меню **M** можно вернуться к курсу или открыть стенды. Для ручной проверки отдельной камеры используйте приведённый ниже запуск с изолированной папкой сохранений. Задайте пустую строку в `$test` и уберите `-unattended`, `-benchmark` и `-fps`. К карте `/Engine/Maps/Entry` добавьте `?Chamber=Stairs`, `?Chamber=Button`, `?Chamber=Weight` или `?Chamber=Sandbox`.

## Вручную

| Камера или стенд | Действие | Ожидаемый результат |
| --- | --- | --- |
| 01. Балластная лестница | Возьмите малый куб клавишей **E**, наведите на большой у янтарной кромки, установите по призраку. Поднимитесь через низкую ступень и стопку. Попробуйте установить большой куб на малый. | Малая опора отклоняет большой куб. Стопка в зоне открывает дверь. Если убрать нижний куб, верхний падает. |
| 02. Цепь двери | Положите куб 5 кг на A до нажатия кнопки. Нажмите кнопку. Положите второй куб на B, затем снимите куб с A и встаньте на неё. Снимите последний куб с B. Повторите решение с B и нажмите **R**. | Обе ветви показывают массу и собственный сигнал. При выключенной кнопке линия к двери погашена. После взвода дверь остаётся открытой, пока подходит хотя бы одна нагрузка от 5 до 20 кг. Перегруз A оставляет B активной. После снятия последнего куба дверь закрывается. Сброс освобождает обе плиты, выключает кнопку и возвращает кубы. |
| 03. Весовая блокировка | После кнопки попробуйте 10 кг, затем 15 кг, затем 20 кг. Соберите 20 кг сначала из двух больших кубов, затем из большого и двух малых. | Оба решения 20 кг открывают дверь. Значения 10 и 15 кг видны в HUD и сохраняют блокировку. |
| Камера баланса | **Load Balance Chamber**. Выставьте `5/5/5` кг и `20/10/0` кг, затем `10/10/10` кг. Для второго решения используйте один большой куб и две стопки из малых. Встаньте с малым кубом на плиту с грузом 10 кг. Проверьте сброс **R** и пройдите через выход. | Срабатывают только `10/10/10` кг. Перегруз одной плиты снимает питание с двери, а HUD показывает нагрузку на каждой плите. Сброс освобождает все три плиты и возвращает грузы. |
| Подсказки камер | У входа повернитесь к панели **HINTS** и нажмите **E**. Дождитесь уведомления во время решения камеры, затем прочитайте новую запись на той же панели. Нажмите **R** и проверьте записи снова. | Первая подсказка доступна сразу. Через шесть минут без первого правильного состояния нового узла появляется уведомление. Пока панель открыта, **E** листает доступные записи и закрывает последнюю. Сброс возвращает первую ступень. |
| Движение | **Load Movement Lab**, **Q/E** меняют дорожки, **R** возвращает на начало. Сверьте прыжки, ступени и разрывы с [протоколом замеров](MovementLab.md#результаты). | Шаг, пробег и дальность прыжков соответствуют профилю. После промаха стенд возвращает на текущую дорожку. |
| Стенд устройств | На `?Chamber=Sandbox` проверьте движение мыши вверх и вниз, захват и бросок на **ЛКМ**, обычный прыжок и прыжок со стопки. Нажмите **R**, стоя на плите. | Камера движется вслед за мышью. Бросок освобождает предмет. Сброс возвращает плиту, индикатор, дверь и кубы в исходное состояние. Потерянный куб на учебном маршруте возвращает диспенсер. |
| Переход и станция | Выйдите из камеры 04. Включите и выключите фонарь клавишей **F**, меняя направление взгляда. Оставьте его включённым, пройдите открывающийся шлюз и на станции воспользуйтесь стойкой с **E**. Затем перезапустите игру на `/Engine/Maps/Entry` с тем же изолированным профилем. | Луч следует за взглядом и остаётся включённым на станции. Путь и шлюз читаются с выключенным фонарём. Кислород расходуется в переходе. Стойка пополняет запас. После перезапуска открывается вход камеры 04 с исходной расстановкой и закрытой дверью. |
| Обязательный уступ | За шлюзом подойдите к белой кромке. Попробуйте идти вперёд, коротко нажать Space и прыгнуть с приседанием. Затем посмотрите на кромку и удерживайте **Space** до начала подъёма. Слегка поверните взгляд во время контакта и повторите подъём после перезапуска перехода. | Ходьба и обычные прыжки останавливаются перед уступом. При подъёме обе перчатки опираются на край и сохраняют точки контакта при движении камеры. На площадке руки уже скрыты. В автопрогоне проверяется также площадка с препятствием. |
| Радиационный пролёт | **Load Radiation Passage**. Подождите вспышку под навесом, выйдите и снова укройтесь. Отступите за границу зоны, затем проверьте гибель на открытой галерее и выход с остаточной дозой. | Укрытие останавливает набор. Вне зоны доза спадает, станция обнуляет остаток. При предельной дозе восстанавливается вход пролёта. [Подробный сценарий](Radiation.md) |
| Оптика | **Load Optics Lab**. В первом зале перекройте луч собой, затем кубом. Во втором перенесите зеркало на площадку, наведите излучатель и зеркало. | Приёмник показывает состояние луча. Вторая дверь пропускает только при наличии отражённого сигнала. |
| Свободный отражатель | Во втором зале поставьте зеркало на пол более чем в метре от площадки. Наведите излучатель клавишей **E**, затем отразите луч в приёмник клавишей **Q**. Поверните зеркало в сторону и снова наведите на приёмник. Нажмите **R**. | Свободное зеркало опирается на пол. Отражение открывает дверь, поворот в сторону закрывает, повторное наведение открывает снова. Сброс возвращает положение зеркала, ориентацию поверхности и питание. |
| Импульсная дверь | После двух отсеков оптики нажмите **E** на **PULSE**, повторите нажатие во время отсчёта и дождитесь закрытия. Подайте новый импульс и пройдите в дальний отсек. Нажмите **E** на **RETURN**, задержитесь в проёме до закрытия. Затем подайте импульс и сбросьте стенд через **R**. | Дверь получает питание на 3,5 с. Повторное нажатие во время импульса сохраняет срок. Панель и HUD показывают остаток. При закрытии в проёме игрок возвращается к панели с сохранённым питанием оптики. **R** закрывает дверь и сбрасывает отсчёт и прохождение. |
| Гравитация | **Load Gravity Lab**. Установите малый куб на большой, включите боковое поле. Пройдите по стене и выйдите за границу поля. | Стопка распадается. Куб 5 кг ускоряется сильнее куба 10 кг. За границей возвращаются обычная тяжесть и горизонт. |
| Порталы | **Load Portal Lab**. Проверьте неподвижный обесточенный затвор. Включите пару, пройдите туда и обратно с кубом. Разместите второй куб у точки выхода и повторите проход. | Через запитанную раму видна вторая комната. Куб остаётся в захвате, направление скорости поворачивается. Занятый выход блокирует перенос. |
| DEV‑20 | **Load DEV-20 Route**. Включите консоль, возьмите узел с подставки, пройдите в портал и дойдите до янтарной панели второго пролёта. В камерах 02 и 03 положите узел, решите головоломку кубами, верните узел в руки и выйдите. | При входе в каждую камеру в руках ровно один DEV‑20. Плита показывает 34 кг и отклоняет этот вес. Выход курса срабатывает с узлом в руках. **R** возвращает к подставке в портальном зале. |

## Автопрогоны Vulkan

Соберите `AbandonedEditor Win64 Development` для проверяемого коммита. Запустите из корня проекта в PowerShell 7, подставив путь к UE 5.7.

```powershell
pwsh -NoProfile -File Scripts/Invoke-UeVerification.ps1 -EngineRoot 'C:\Program Files\Epic Games\UE_5.7'
```

Запускатель проверяет `EntryCourse`, `WristTerminal` и `WristTerminalNegative`. У каждого сценария отдельный профиль внутри `Saved/UeVerification/<запуск>`. Там находятся журналы и общий `report.json` с SHA, состоянием отслеживаемых файлов, версией UE, RHI, GPU и результатами. Для одного сценария добавьте `-Scenario WristTerminal`, для шага 120 кадров/с — `-FramesPerSecond 120`. Параметр `-TimeoutSeconds` ограничивает время ожидания каждого процесса.

Успех требует Vulkan SM5, кода `0`, всех ожидаемых маркеров и отсутствия ошибок. Отрицательный контроль запускает проверку терминала сразу на станции с пустым архивом. Он должен зарегистрировать предусмотренный провал с `ExpectedFailure`, даже если Unreal завершится с кодом `0`. Таймаут, посторонняя ошибка, отсутствие маркеров или неожиданное поведение отрицательного контроля дают `Failed` и ненулевой код запускателя. GitHub Actions выполняет автономные C++20-тесты и Git LFS. UE-прогон выполняется локально на доверенной Windows-машине с Vulkan.

### Остальные сценарии и ручной запуск

Меняйте `$map` и `$test` по таблице. Для повтора на 120 Гц задайте `$hz = 120`. Смотрите код завершения и маркеры конкретного сценария в журнале.

```powershell
$editor = 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe'
$project = (Resolve-Path .\Abandoned.uproject).Path
$profile = Join-Path $PWD 'Saved\QAProfile'
$null = New-Item -ItemType Directory -Force -Path (Join-Path $PWD 'Saved\Logs')
$log = Join-Path $PWD 'Saved\Logs\Chambers-Vulkan.log'
$map = '/Engine/Maps/Entry'
$test = '-VerifyEntryCourse'
$hz = 60
$args = '"{0}" "{1}" {2} -game -vulkan -unattended -windowed -ResX=1280 -ResY=720 -benchmark -fps={3} -UserDir="{4}" -abslog="{5}"' -f $project, $map, $test, $hz, $profile, $log
$run = Start-Process $editor -ArgumentList $args -Wait -PassThru
"Unreal exit code $($run.ExitCode). See $log"
```

| Сценарий | Суффикс карты `/Engine/Maps/Entry` | Флаги | Признак успеха в журнале |
| --- | --- | --- | --- |
| Четыре камеры и станция | | `-VerifyEntryCourse -CaptureVanguard -CaptureBalanceChamber` | `reset restored the weight lock`, `Training chamber ready. Balance`, `Entry course verification passed`, `Passage handhold verified`, затем `Vanguard verification passed` |
| Цепь ИЛИ в камере 02 | `?Chamber=Button` | `-VerifyParallelCircuit -CaptureParallelCircuit` | `Parallel circuit verification passed`. Кадр `Entry-Parallel-Circuit-B.png` показывает ветвь B и открытую дверь. |
| Альтернативное решение 02 | | `-VerifyEntryCourse -VerifyEntryAlternatePlate` | `Entry alternate solution: cube snapped to plate B`, затем `Vanguard verification passed` |
| Прежнее сохранение версии 1 | | `-VerifyEntryCheckpoint` | С изолированным профилем, где сохранён `Weight`, журнал начинает маршрут с `Training chamber ready. Weight`, после неё показывает `Training chamber ready. Balance` и завершает станцией. Следующий обычный запуск начинает камеру `Balance`. |
| Альтернативное решение 03 | | `-VerifyEntryCourse -VerifyEntryAlternateWeight` | `alternate solution assembled, 10 + 5 + 5 kg`, затем `Vanguard verification passed` |
| Камера баланса | `?Chamber=Balance` | `-VerifyBalanceChamber -CaptureBalanceChamber` | `Balance chamber verification passed` |
| Зажим створкой | | `-VerifyDoorClamp -CaptureDoorClamp` | `Door retained the object`, `Door released the object`, `Door clamp verification passed` |
| Подсказки | | `-VerifyChamberHints -CaptureChamberHints` | `Chamber hints verification passed` |
| Стенд устройств | | `-VerifyTrainingChamber` | `Verification passed: player weight, placement ghost, throw, staircase, detach and reset` |
| Переход и станция отдельно | `?game=/Script/Abandoned.VanguardRouteGameMode?Chamber=Passage` | `-VerifyVanguard` | `Vanguard verification passed` |
| Падения в переходе | `?game=/Script/Abandoned.VanguardRouteGameMode?Chamber=Passage?Checkpoint=Balance` | `-VerifyVanguardFalls` | `Vanguard fall verified at 250 cm`, `Passage fall 300.0 cm: minor injury`, `Vanguard minor injury slowed ground movement, then restored normal speed`, `Passage fall 325.0 cm: fatal`, `Passage checkpoint recovery: Balance (fall)`, `Training chamber ready. Balance` |
| Исчерпание кислорода | `?game=/Script/Abandoned.VanguardRouteGameMode?Chamber=Passage` | `-VerifyVanguardOxygen` | Заранее сохраните вход в камеру 04 в изолированном профиле. Прогон начинает переход с почти исчерпанным запасом. Ищите `Passage checkpoint recovery: Balance (oxygen depleted)` и `Training chamber ready. Balance`. |
| Радиационный пролёт | `?game=/Script/Abandoned.VanguardRouteGameMode?Chamber=Passage?RadiationRun=1` | `-VerifyRadiation -CaptureRadiation` | `Radiation shelter held dose`, `Radiation recovery outside the zone`, `Radiation checkpoint restored`, `Vanguard station cleared remaining radiation dose` и `Radiation passage verification passed` |
| Наручный терминал | `?game=/Script/Abandoned.VanguardRouteGameMode?Chamber=Passage?RadiationRun=1` | `-VerifyWristTerminal -CaptureWristTerminal` | `Wrist terminal verification passed` и `Radiation passage verification passed`. Кадры `Wrist-Terminal-Status.png` и `Wrist-Terminal-Archive.png` показывают экран у входа и на станции. |
| Оптика и импульс | `?game=/Script/Abandoned.PowerLabGameMode` | `-VerifyPowerLab -CapturePowerLab` | `Power lab verification passed`, `Timed door crossed with`, затем `Timed passage verification passed`. |
| Свободный отражатель | `?game=/Script/Abandoned.PowerLabGameMode` | `-VerifyPowerLab -VerifyFreeReflector -CapturePowerLab` | `Free reflector verification passed`, затем `Timed passage verification passed` после сброса. Кадр `PowerLab-Free-Reflector.png` показывает зеркало вне площадки. |
| Гравитация | `?game=/Script/Abandoned.GravityLabGameMode` | `-VerifyGravityLab` | `Gravity lab verification passed` |
| Порталы | `?game=/Script/Abandoned.PortalLabGameMode` | `-VerifyPortalLab` | `Portal lab verification passed` |
| DEV‑20 | `?game=/Script/Abandoned.PortalLabGameMode?Dev20Run=1` | `-VerifyDev20 -CaptureDev20` | `DEV-20 verification passed: portal, Button, Weight and final exit` |
| Возврат DEV‑20 после потери в камере | `?game=/Script/Abandoned.PortalLabGameMode?Dev20Run=1` | `-VerifyDev20 -VerifyDev20Recovery` | `DEV-20 recovery verification passed` |

В каждом журнале найдите `Vulkan RHI ShaderPlatform for SM5: VULKAN_SM5` и имя GPU. В сценарии DEV‑20 дополнительно проверьте записи о массе Chaos, возврате к подставке при пропадании питания, сохранении захвата в портале и получении узла в обеих камерах. Автотесты задают стартовые позиции для отдельных действий и используют реальные захваты, трассировки и переходы миров. Для оценки читаемости сигналов и управления пройдите ручные сценарии отдельно.

Прогоны падения и исчерпания кислорода оставляют игру открытой у контрольной точки. Проверяйте их по сообщениям журнала и закрывайте игру после загрузки камеры. Сквозной прогон маршрута проверяет нажатия **F** через Enhanced Input и состояние фонаря после загрузки станции. `-VerifyEntryCheckpoint` читает и записывает сохранение, поэтому используйте отдельный профиль.

`-VerifyParallelCircuit` оставляет слот кампании нетронутым. Для проверки поместите копию сохранения камеры баланса в тестовый профиль, сравните SHA-256 слота до и после прогона и запустите игру с тем же профилем обычным способом. Должна открыться камера баланса. В пустом профиле слот после прогона не появляется.

### Переходы и выбор точки возврата

`Test-Architecture.ps1` проверяет кодирование и обратное чтение URL для камер, пролётов, станции, стендов и этапов DEV‑20. На Vulkan запустите обычный маршрут, радиационный пролёт и оба сценария DEV‑20 из таблицы. Чтобы проверить приоритет восстановления, запустите обычный пролёт с `-VerifyVanguardOxygen` и сохранением `Button` в отдельном профиле. При `?Checkpoint=Balance` должна открыться камера `Balance`. При `?Checkpoint=Sandbox` ожидаются предупреждение `Invalid passage checkpoint origin` и возврат в `Button`. В пустом профиле без `Checkpoint` должна открыться `Weight`.

Для проверки ошибочного назначения откройте `/Engine/Maps/Entry?Chamber=Unknown` с изолированным профилем. В журнале появляется `Invalid segment travel options`. Игра оставляет слот профиля нетронутым.

### Миграция сохранений

В предыдущей сборке, например на коммите `482b352`, запустите `/Engine/Maps/Entry?Chamber=<ID>?EntryCourse=1` с отдельным `-UserDir` для каждой из четырёх камер. Дождитесь появления `Saved/SaveGames/AbandonedEntryCourse.sav` в профиле. Поместите профили `Stairs`, `Button`, `Weight` и `Balance` под одним каталогом. В новой сборке выполните:

```powershell
pwsh -NoProfile -File Scripts/Test-CheckpointMigration.ps1 -EngineRoot 'C:\Program Files\Epic Games\UE_5.7' -LegacyProfiles 'C:\QA\LegacyProfiles'
```

Скрипт копирует исходные профили в `Saved/CheckpointMigration`, проверяет создание v2 и повторное открытие каждой точки на Vulkan. Для проверки отказа первой публикации путь v2 занят каталогом, для проверки замены действующего файла открыт дескриптор без разрешения удаления. После отказа сравниваются SHA‑256 обоих сохранений и снова загружается прежняя точка. Отдельные сценарии проверяют сохранность незнакомой версии и запись о пополнении кислорода в кампании.

Для проверки звука повторите прогон падений с `-VerifyVanguardFalls -VerifyInjuryAudio` и активным звуковым устройством. Уберите `-benchmark`, `-fps` и `-nosound`. Запись `Vanguard injury breath decoded in audio mixer` подтверждает, что микшер обработал звуковые отсчёты. Для ручной проверки оставьте звук включённым. При спуске с тестовой платформы 3 м сообщение скафандра и короткий вдох с выдохом должны начаться вместе. Шаг, бег и движение пригнувшись замедляются на 35% сразу после удара и возвращаются к обычной скорости за 2,5 с.

`-VerifyChamberHints` загружает камеру 03 и сокращает интервал для проверки до двух секунд. Доменный тест проверяет штатную границу в 360 секунд, повторное действие на уже настроенном узле, остановку отсчёта после завершения камеры и полный сброс.

[Кадр DEV‑20 на подставке, Vulkan SM5, RTX 4070 Ti SUPER, 1280 × 720](Images/DEV20-Station.png)
