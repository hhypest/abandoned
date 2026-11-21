/* Offline design prototypes. All state belongs to this page. */
"use strict";
const theme = document.documentElement.dataset.theme;
const variants = {stand: "Бортовой стенд", passport: "Паспорт допуска", visor: "Оптический контур"};
const slots = {empty: "Новый профиль", saved: "Есть сохранение", damaged: "Повреждённый слот"};
const query = new URLSearchParams(location.search);
const demo = {
  slot: Object.hasOwn(slots, query.get("slot")) ? query.get("slot") : "empty",
  sync: query.get("sync") !== "0",
  fps: [30, 60, 120, 180].includes(Number(query.get("fps"))) ? Number(query.get("fps")) : 60,
  complete: 3,
  powered: true,
};
const panel = document.querySelector("#panel");
const sheet = document.querySelector("#sheet");
const instrument = [
  ["Датчик давления", "Герметичен"],
  ["Запас кислорода", "100 %"],
  ["Наручный терминал", "На связи"],
];
const brand = '<span class="brand"><i class="ft" aria-hidden="true"></i>ФосТех</span>';
const systemName = '<span class="system-name">Экспедиционные системы</span>';
const progress = '<span class="live-state"><i aria-hidden="true"></i><span data-status></span></span>';
const topbar = `<header class="topbar"><div>${brand}${systemName}</div>${progress}</header>`;
const plot = (caption) => `<figure class="plot"><div class="plot-axes" aria-hidden="true"></div><div class="scan-ring" aria-hidden="true"></div><img src="helmet.webp" alt="Шлем с закрытым визором, уплотнительным кольцом и боковым разъёмом"><figcaption>${caption}</figcaption></figure>`;
const results = `<section class="results" aria-label="Проверки скафандра"><div class="module-heading"><h2>Контроль систем</h2><span data-count></span></div><dl>${instrument.map(([name], i) => `<div><dt><span class="tick" data-tick="${i}" aria-hidden="true">✓</span>${name}</dt><dd data-reading="${i}"></dd></div>`).join("")}</dl><button class="repeat" data-action="test">↻ Повторить тест</button></section>`;
const actions = `<section class="actions" aria-label="Команды главного меню"><div class="start-actions"><button data-action="new">Новая игра <b aria-hidden="true">↗</b></button><button data-action="continue">Продолжить <b aria-hidden="true">→</b></button></div><p class="slot-info" data-slot-message></p><div class="aux"><button data-action="settings">Настройки</button><button data-action="quit">Выход</button></div></section>`;
const nav = `<footer class="runtime-footer"><span>АВТОНОМНЫЙ РЕЖИМ</span><span><kbd>Tab</kbd> Выбор <kbd>Enter</kbd> Подтвердить</span></footer>`;
const surfaces = {
  stand: `${topbar}<div class="stand-body"><section class="intro"><span class="kicker">Предпусковая самодиагностика</span><h1>Контроль<br>скафандра</h1><p class="lead" data-verdict></p>${actions}</section><div class="assembly">${plot("Узел контроля · шлем")}</div></div>${results}${nav}`,
  passport: `${topbar}<header class="paper-heading"><span class="kicker">Эксплуатационная документация</span><h1>Паспорт допуска</h1><p>Скафандр экспедиционный <span>/</span> Лист самодиагностики</p></header><div class="paper-body"><div class="paper-figure">${plot("Общий вид · шлем скафандра")}</div><section class="paper-data"><p class="stamp" data-verdict></p>${results}</section></div>${actions}${nav}`,
  visor: `${topbar}<div class="visor-body"><div class="visor-screen"><span class="kicker">Оптический контур</span><h1>Проверка визора</h1>${plot("Совмещение по оптической оси")}<p class="visor-verdict" data-verdict></p></div><div class="visor-side">${results}${actions}</div></div>${nav}`
};
const verdicts = {
  stand: ["Скафандр готов к работе", "Идёт проверка систем"],
  passport: ["Допуск получен", "На проверке"],
  visor: ["Контуры согласованы", "Калибровка контуров"],
};
let timer;
function renderPanel() {
  panel.innerHTML = surfaces[theme] + '<span data-live class="sr-only" role="status" aria-live="polite"></span>';
  updatePanel();
}
function updatePanel() {
  if (!demo.powered) return;
  const ready = demo.complete === instrument.length;
  document.body.classList.toggle("scanning", !ready);
  panel.querySelector("[data-status]").textContent = ready ? "Системы в норме" : "Идёт проверка";
  panel.querySelector("[data-verdict]").textContent = verdicts[theme][ready ? 0 : 1];
  panel.querySelector("[data-count]").textContent = demo.complete + " / " + instrument.length;
  instrument.forEach(([, value], i) => {
    panel.querySelector(`[data-tick="${i}"]`).textContent = i < demo.complete ? "✓" : "·";
    panel.querySelector(`[data-reading="${i}"]`).textContent = i < demo.complete ? value : "Ожидание";
  });
  const available = demo.slot === "saved";
  const resume = panel.querySelector('[data-action="continue"]');
  resume.disabled = !ready || !available;
  resume.classList.toggle("primary", available);
  const fresh = panel.querySelector('[data-action="new"]');
  fresh.disabled = !ready;
  fresh.classList.toggle("primary", !available);
  panel.querySelector('[data-action="test"]').disabled = !ready;
  panel.querySelector("[data-slot-message]").textContent = {
    empty: "Сохранение отсутствует",
    saved: "Точка восстановления · Входной контур",
    damaged: "Сохранение повреждено. Доступна новая игра."
  }[demo.slot];
  panel.querySelector("[data-live]").textContent = ready ? "Самодиагностика завершена. " + panel.querySelector("[data-slot-message]").textContent : `Проверено систем: ${demo.complete} из 3.`;
}
function testSuit() {
  clearInterval(timer);
  demo.complete = 0;
  updatePanel();
  timer = setInterval(() => {
    demo.complete++;
    updatePanel();
    if (demo.complete === instrument.length) clearInterval(timer);
  }, 650);
}
function updateLinks() {
  const params = new URLSearchParams({slot: demo.slot, sync: demo.sync ? "1" : "0", fps: String(demo.fps)});
  document.querySelectorAll("[data-variant]").forEach(link => link.href = link.dataset.variant + ".html?" + params);
}
const review = document.querySelector("#review");
review.innerHTML = `<span class="review-name">Макеты</span><nav aria-label="Вариант макета">${Object.entries(variants).map(([key, name]) => `<a href="${key}.html" data-variant="${key}" ${theme === key ? 'aria-current="page"' : ""}>${name}</a>`).join("")}</nav><label>Тестовый слот <select id="slot">${Object.entries(slots).map(([value,name]) => `<option value="${value}">${name}</option>`).join("")}</select></label>`;
document.querySelector("#slot").value = demo.slot;
document.querySelector("#slot").addEventListener("change", event => {
  demo.slot = event.target.value;
  updatePanel();
  updateLinks();
});
function show(title, body) {
  sheet.innerHTML = `<form method="dialog"><div class="dialog-head"><span class="kicker">ФосТех</span><button value="close" aria-label="Закрыть">×</button></div><h2 id="sheet-title">${title}</h2>${body}</form>`;
  if (!sheet.open) sheet.showModal();
}
function route(kind) {
  const description = kind === "new" ? "Пробуждение · Начало экспедиции" : "Входной контур · Контрольная точка";
  show("Маршрут готов", `<p>${description}</p><p class="note">Здесь макет передаст управление игре.</p><button class="primary" value="close">Вернуться к прибору</button>`);
}
function settings() {
  show("Настройки изображения", `<label class="setting"><span>Вертикальная синхронизация</span><input type="checkbox" id="vsync" ${demo.sync ? "checked" : ""}></label><label class="setting"><span>Лимит кадров</span><select id="fps">${[30,60,120,180].map(fps => `<option value="${fps}" ${demo.fps === fps ? "selected" : ""}>${fps} к/с</option>`).join("")}</select></label><p class="note">Для просмотра макетов в этом сеансе.</p><button class="primary" value="close">Готово</button>`);
  sheet.querySelector("#vsync").onchange = e => {demo.sync = e.target.checked; updateLinks();};
  sheet.querySelector("#fps").onchange = e => {demo.fps = Number(e.target.value); updateLinks();};
}
panel.addEventListener("click", e => {
  const action = e.target.closest("button")?.dataset.action;
  switch (action) {
    case "test": testSuit(); break;
    case "new":
      if (demo.slot === "empty") route("new");
      else show("Начать новую игру?", '<p>Существующий слот останется в демонстрационном состоянии.</p><button type="button" class="primary" data-confirm="new">Начать</button> <button value="close">Отмена</button>');
      break;
    case "continue": if (demo.slot === "saved") route("continue"); break;
    case "settings": settings(); break;
    case "quit": show("Завершить сеанс?", '<p>Дисплей самодиагностики выключится.</p><button type="button" class="primary" data-confirm="quit">Выключить</button> <button value="close">Отмена</button>'); break;
    case "power":
      demo.powered = true;
      renderPanel();
      testSuit();
      panel.querySelector('[data-action="settings"]').focus();
      break;
  }
});
sheet.addEventListener("click", e => {
  const op = e.target.closest("button")?.dataset.confirm;
  if (op === "new") {
    route("new");
    sheet.querySelector('button[value="close"].primary').focus();
  }
  if (op === "quit") {
    clearInterval(timer);
    demo.powered = false;
    document.body.classList.remove("scanning");
    sheet.close();
    panel.innerHTML = `<div class="offline">${brand}<h1>Питание отключено</h1><button class="primary" data-action="power">Включить прибор</button></div>`;
    panel.querySelector("button").focus();
  }
});
renderPanel();
updateLinks();
