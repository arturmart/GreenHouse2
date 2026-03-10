const $ = (id) => document.getElementById(id);

const dot = $("dot");
const apiBase = location.origin;
$("apiBase").textContent = apiBase;

let timer = null;

const historyMap = {};          // { getterKey: [{t, v}, ...] }
const selectedGetters = new Set(["temp"]);
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

function isNumericGetter(entry) {
  const t = entry?.data?.type;
  return t === "int" || t === "double";
}

function historyPush(getterKey, value) {
  if (!historyMap[getterKey]) {
    historyMap[getterKey] = [];
  }
  historyMap[getterKey].push({ t: Date.now(), v: value });
  while (historyMap[getterKey].length > MAX_POINTS) {
    historyMap[getterKey].shift();
  }
}

function lineColor(index) {
  const colors = [
    "#49f28a",
    "#6ecbff",
    "#ffb86c",
    "#ff79c6",
    "#f1fa8c",
    "#bd93f9",
    "#8be9fd",
    "#ff5555"
  ];
  return colors[index % colors.length];
}

function drawMultiGetterChart() {
  const canvas = $("tempChart");
  if (!canvas) return;

  const ctx = canvas.getContext("2d");
  const w = canvas.width;
  const h = canvas.height;

  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = "#0f152d";
  ctx.fillRect(0, 0, w, h);

  const selectedKeys = [...selectedGetters].filter(k => historyMap[k] && historyMap[k].length > 0);

  ctx.strokeStyle = "rgba(255,255,255,.08)";
  for (let i = 0; i < 5; i++) {
    const y = (h - 20) * i / 4 + 10;
    ctx.beginPath();
    ctx.moveTo(40, y);
    ctx.lineTo(w - 10, y);
    ctx.stroke();
  }

  if (selectedKeys.length === 0) {
    ctx.fillStyle = "rgba(255,255,255,.55)";
    ctx.font = "14px sans-serif";
    ctx.fillText("Select numeric getters to draw chart", 45, 40);
    return;
  }

  const allValues = [];
  for (const key of selectedKeys) {
    for (const p of historyMap[key]) {
      if (Number.isFinite(p.v)) allValues.push(p.v);
    }
  }

  if (!allValues.length) {
    ctx.fillStyle = "rgba(255,255,255,.55)";
    ctx.font = "14px sans-serif";
    ctx.fillText("Not enough data", 45, 40);
    return;
  }

  let min = Math.min(...allValues);
  let max = Math.max(...allValues);

  if (Math.abs(max - min) < 0.5) {
    min -= 0.5;
    max += 0.5;
  }

  const left = 45;
  const top = 10;
  const cw = w - 55;
  const ch = h - 20;

  ctx.fillStyle = "rgba(255,255,255,.65)";
  ctx.font = "12px sans-serif";
  ctx.fillText(max.toFixed(2), 4, top + 10);
  ctx.fillText(min.toFixed(2), 4, top + ch);

  selectedKeys.forEach((key, idx) => {
    const arr = historyMap[key];
    if (!arr || arr.length < 2) return;

    ctx.strokeStyle = lineColor(idx);
    ctx.lineWidth = 2;
    ctx.beginPath();

    arr.forEach((p, i) => {
      const x = left + (cw * i) / Math.max(1, arr.length - 1);
      const y = top + ch - ((p.v - min) / (max - min)) * ch;
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    });

    ctx.stroke();
  });

  // legend
  let legendX = 60;
  const legendY = 20;

  selectedKeys.forEach((key, idx) => {
    const color = lineColor(idx);

    ctx.fillStyle = color;
    ctx.fillRect(legendX, legendY, 12, 12);

    ctx.fillStyle = "#e7e9ee";
    ctx.font = "12px sans-serif";
    ctx.fillText(key, legendX + 18, legendY + 10);

    legendX += 90;
  });
}

function renderGetterSelectors(getters, schemaG) {
  const box = $("chartGetters");
  if (!box) return;

  const keys = Object.keys(getters || {})
    .filter(k => isNumericGetter(getters[k]))
    .sort();

  box.innerHTML = keys.map(k => {
    const checked = selectedGetters.has(k) ? "checked" : "";
    const type = schemaG?.[k] || getters?.[k]?.data?.type || "?";
    return `
      <label class="chart-opt">
        <input type="checkbox" data-getter="${k}" ${checked} />
        <span class="mono">${k}</span>
        <span class="small muted">(${type})</span>
      </label>
    `;
  }).join("");

  box.querySelectorAll("input[type='checkbox']").forEach(cb => {
    cb.addEventListener("change", (e) => {
      const key = e.target.dataset.getter;
      if (e.target.checked) selectedGetters.add(key);
      else selectedGetters.delete(key);
      drawMultiGetterChart();
    });
  });
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

    // collect numeric getter history
    Object.entries(getters || {}).forEach(([key, entry]) => {
      if (!entry?.valid) return;
      if (!isNumericGetter(entry)) return;

      const v = Number(entry?.data?.value);
      if (Number.isFinite(v)) {
        historyPush(key, v);
      }
    });

    renderGetterSelectors(getters, schemaG);
    drawMultiGetterChart();

    const gKeys = Object.keys(getters || {}).sort();
    $("gCount").textContent = String(gKeys.length);

    const gHtml = gKeys
      .filter(k => matchesSearch(s, k))
      .map(k => {
        const e = getters[k];
        const type = schemaG?.[k]
          ? `<span class="pill muted">type:${schemaG[k]}</span>`
          : `<span class="pill muted">type:?</span>`;
        const valid = badgeValid(!!e?.valid);
        const stamp = `<span class="pill muted">stamp:${fmtMs(e?.stampMs)}</span>`;
        const data = e?.data ? `${e.data.type}:${String(e.data.value)}` : "—";
        const chartBtn = isNumericGetter(e)
          ? `<label class="pill" style="cursor:pointer">
               <input type="checkbox" data-inline-getter="${k}" ${selectedGetters.has(k) ? "checked" : ""} />
               chart
             </label>`
          : "";

        return `
          <div class="item">
            <div class="left">
              <div class="k"><span class="mono">${k}</span></div>
              <div class="meta">${type} ${valid} ${stamp}</div>
              <div class="small muted mono">${data}</div>
            </div>
            <div class="right">${chartBtn}</div>
          </div>
        `;
      })
      .join("");

    $("getters").innerHTML = gHtml || `<div class="small muted">Нет данных</div>`;

    $("getters").querySelectorAll("input[data-inline-getter]").forEach(cb => {
      cb.addEventListener("change", (e) => {
        const key = e.target.dataset.inlineGetter;
        if (e.target.checked) selectedGetters.add(key);
        else selectedGetters.delete(key);
        renderGetterSelectors(getters, schemaG);
        drawMultiGetterChart();
      });
    });

    $("eCount").textContent = String((executors || []).length);

    const eHtml = (executors || [])
      .filter(x => matchesSearch(s, x.name || x.id))
      .sort((a, b) => (a.id ?? 0) - (b.id ?? 0))
      .map(x => {
        const name = x.name || "";
        const schemaT = schemaE?.[name]
          ? `<span class="pill muted">type:${schemaE[name]}</span>`
          : `<span class="pill muted">type:?</span>`;
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