const $ = (id) => document.getElementById(id);

const dot = $("dot");
const apiBase = location.origin; // тот же хост/порт => без CORS
$("apiBase").textContent = apiBase;

let timer = null;

function setDot(ok) {
  dot.style.background = ok ? "#49f28a" : "#ff6b6b";
  dot.style.boxShadow = ok ? "0 0 14px rgba(73,242,138,.45)" : "0 0 14px rgba(255,107,107,.45)";
}

function fmtMs(ms) {
  if (!ms) return "—";
  return ms + "ms";
}

function badgeValid(valid) {
  return valid ? `<span class="pill good">valid</span>` : `<span class="pill bad">invalid</span>`;
}

function badgeMode(mode) {
  if (!mode) return "";
  const m = String(mode).toUpperCase();
  const cls = (m === "AUTO") ? "good" : "warn";
  return `<span class="pill ${cls}">${m}</span>`;
}

function renderItem({ title, meta, right, sub }) {
  return `
    <div class="item">
      <div class="left">
        <div class="k">${title}</div>
        <div class="meta">${meta || ""}</div>
        ${sub ? `<div class="small muted mono">${sub}</div>` : ""}
      </div>
      <div class="right">${right || ""}</div>
    </div>
  `;
}

async function jget(path) {
  const r = await fetch(path, { cache: "no-store" });
  if (!r.ok) throw new Error(`${path} -> ${r.status}`);
  return await r.json();
}

function matchesSearch(s, key) {
  const q = (s || "").trim().toLowerCase();
  if (!q) return true;
  return String(key).toLowerCase().includes(q);
}

async function reloadAll() {
  const s = $("search").value;

  try {
    const [status, schemaG, schemaE, getters, executors] = await Promise.all([
      jget("/status"),
      jget("/schema/getters"),
      jget("/schema/executors"),
      jget("/getters"),
      jget("/executors"),
    ]);

    setDot(status?.status === "ok");

    // getters
    const gKeys = Object.keys(getters || {}).sort();
    $("gCount").textContent = String(gKeys.length);

    const gHtml = gKeys
      .filter(k => matchesSearch(s, k))
      .map(k => {
        const e = getters[k];
        const type = schemaG?.[k] ? `<span class="pill muted">type:${schemaG[k]}</span>` : `<span class="pill muted">type:?</span>`;
        const valid = badgeValid(!!e?.valid);
        const stamp = `<span class="pill muted">stamp:${fmtMs(e?.stampMs)}</span>`;
        const data = e?.data ? `${e.data.type}:${String(e.data.value)}` : "—";
        return renderItem({
          title: `<span class="mono">${k}</span>`,
          meta: `${type} ${valid} ${stamp}`,
          right: `<span class="pill mono">${data}</span>`,
        });
      })
      .join("");

    $("getters").innerHTML = gHtml || `<div class="small muted">Нет данных</div>`;

    // executors
    $("eCount").textContent = String((executors || []).length);

    const eHtml = (executors || [])
      .filter(x => matchesSearch(s, x.name || x.id))
      .sort((a,b) => (a.id ?? 0) - (b.id ?? 0))
      .map(x => {
        const name = x.name || "";
        const schemaT = schemaE?.[name] ? `<span class="pill muted">type:${schemaE[name]}</span>` : `<span class="pill muted">type:?</span>`;
        const valid = badgeValid(!!x.valid);
        const mode = badgeMode(x.mode);
        const stamp = `<span class="pill muted">stamp:${fmtMs(x.stampMs)}</span>`;
        const data = x.data ? `${x.data.type}:${String(x.data.value)}` : "—";
        return renderItem({
          title: `<span class="mono">${x.id}</span> <span class="mono muted">${name}</span>`,
          meta: `${schemaT} ${valid} ${mode} ${stamp}`,
          right: `<span class="pill mono">${data}</span>`,
        });
      })
      .join("");

    $("executors").innerHTML = eHtml || `<div class="small muted">Нет данных</div>`;
  } catch (e) {
    setDot(false);
    $("getters").innerHTML = `<div class="small bad">Ошибка загрузки: ${String(e.message || e)}</div>`;
    $("executors").innerHTML = `<div class="small bad">Ошибка загрузки: ${String(e.message || e)}</div>`;
  }
}

function setIntervalRefresh(ms) {
  if (timer) clearInterval(timer);
  timer = null;
  if (ms > 0) timer = setInterval(reloadAll, ms);
}

$("btnReload").addEventListener("click", reloadAll);
$("refresh").addEventListener("change", (e) => setIntervalRefresh(parseInt(e.target.value, 10)));
$("search").addEventListener("input", () => reloadAll());

reloadAll();
setIntervalRefresh(parseInt($("refresh").value, 10));
