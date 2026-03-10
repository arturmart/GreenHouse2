const $ = (id) => document.getElementById(id);

const dot = $("dot");
const apiBase = location.origin;
$("apiBase").textContent = apiBase;

let timer = null;
const tempHistory = [];
const MAX_POINTS = 120;

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

async function jpost(path, body = {}) {
  const r = await fetch(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
  const text = await r.text();
  let data = {};
  try { data = JSON.parse(text); } catch {}
  if (!r.ok) throw new Error(data?.error || `${path} -> ${r.status}`);
  return data;
}

function matchesSearch(s, key) {
  const q = (s || "").trim().toLowerCase();
  if (!q) return true;
  return String(key).toLowerCase().includes(q);
}

async function execCmd(name, action, value = "") {
  try {
    await jpost(`/api/executors/${encodeURIComponent(name)}/${action}`, { value });
    await reloadAll();
  } catch (e) {
    alert(`Command failed: ${String(e.message || e)}`);
  }
}

function drawTempChart() {
  const canvas = $("tempChart");
  if (!canvas) return;
  const ctx = canvas.getContext("2d");
  const w = canvas.width;
  const h = canvas.height;

  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = "#0f152d";
  ctx.fillRect(0, 0, w, h);

  ctx.strokeStyle = "rgba(255,255,255,.08)";
  for (let i = 0; i < 5; i++) {
    const y = (h - 20) * i / 4 + 10;
    ctx.beginPath();
    ctx.moveTo(30, y);
    ctx.lineTo(w - 10, y);
    ctx.stroke();
  }

  if (tempHistory.length < 2) {
    ctx.fillStyle = "rgba(255,255,255,.55)";
    ctx.fillText("Not enough data", 40, 40);
    return;
  }

  const vals = tempHistory.map(x => x.v).filter(v => Number.isFinite(v));
  if (!vals.length) return;

  let min = Math.min(...vals);
  let max = Math.max(...vals);
  if (Math.abs(max - min) < 0.5) {
    min -= 0.5;
    max += 0.5;
  }

  const left = 35;
  const top = 10;
  const cw = w - 45;
  const ch = h - 20;

  ctx.fillStyle = "rgba(255,255,255,.65)";
  ctx.fillText(max.toFixed(1) + "°C", 2, top + 8);
  ctx.fillText(min.toFixed(1) + "°C", 2, top + ch);

  ctx.strokeStyle = "#49f28a";
  ctx.lineWidth = 2;
  ctx.beginPath();

  tempHistory.forEach((p, i) => {
    const x = left + (cw * i) / Math.max(1, tempHistory.length - 1);
    const y = top + ch - ((p.v - min) / (max - min)) * ch;
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  });

  ctx.stroke();
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

    // temp history
    if (getters?.temp?.valid && getters?.temp?.data?.value != null) {
      const v = Number(getters.temp.data.value);
      if (Number.isFinite(v)) {
        tempHistory.push({ t: Date.now(), v });
        while (tempHistory.length > MAX_POINTS) tempHistory.shift();
      }
    }
    drawTempChart();

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

        const isManual = String(x.mode || "").toUpperCase() === "MANUAL";

        const controls = `
          <div class="row" style="justify-content:flex-end">
            <button onclick="execCmd('${name}','mode','manual')">MANUAL</button>
            <button onclick="execCmd('${name}','mode','auto')">AUTO</button>
            <button ${isManual ? "" : "disabled"} onclick="execCmd('${name}','on')">ON</button>
            <button ${isManual ? "" : "disabled"} onclick="execCmd('${name}','off')">OFF</button>
          </div>
        `;

        return `
          <div class="item">
            <div class="left">
              <div class="k"><span class="mono">${x.id}</span> <span class="mono muted">${name}</span></div>
              <div class="meta">${schemaT} ${valid} ${mode} ${stamp}</div>
              <div class="small muted mono">${data}</div>
            </div>
            <div class="right">${controls}</div>
          </div>
        `;
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