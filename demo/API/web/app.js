const $ = (id) => document.getElementById(id);

const dot = $("dot");
const apiBase = location.origin;
$("apiBase").textContent = apiBase;

let timer = null;

const historyMap = {};
const selectedGetters = new Set(["temp"]);
const MAX_POINTS = 120;

function setDot(ok) {
  dot.style.background = ok ? "#49f28a" : "#ff6b6b";
  dot.style.boxShadow = ok
    ? "0 0 14px rgba(73,242,138,.45)"
    : "0 0 14px rgba(255,107,107,.45)";
}

function fmtMs(ms) {
  if (!ms) return "—";
  return ms + "ms";
}

function badgeValid(valid) {
  return valid
    ? `<span class="pill good">valid</span>`
    : `<span class="pill bad">invalid</span>`;
}

function badgeDirty(dirty) {
  return dirty
    ? `<span class="pill warn">dirty</span>`
    : `<span class="pill muted">clean</span>`;
}

function badgePending(pending) {
  return pending
    ? `<span class="pill warn">pending</span>`
    : `<span class="pill good">idle</span>`;
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

function formatGetterData(entry) {
  if (!entry?.data) return "—";

  const t = entry.data.type;
  const v = entry.data.value;

  if (t === "time") {
    return `time:${entry.data.formatted || String(v)}`;
  }

  return `${t}:${String(v)}`;
}

function formatAnyData(data) {
  if (!data) return "—";

  if (data.type === "time") {
    return `time:${data.formatted || String(data.value)}`;
  }

  return `${data.type}:${String(data.value)}`;
}

function safeUpper(v) {
  return String(v || "").toUpperCase();
}

function effectiveMode(executor) {
  const dm = executor?.desired?.mode;
  const am = executor?.actual?.mode;
  return dm || am || executor?.mode || "";
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

  const selectedKeys = [...selectedGetters].filter(
    k => historyMap[k] && historyMap[k].length > 0
  );

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

function formatKB(kb) {
  if (!Number.isFinite(kb)) return "—";

  const mb = kb / 1024;
  const gb = mb / 1024;

  if (gb >= 1) return `${gb.toFixed(2)} GB`;
  return `${mb.toFixed(1)} MB`;
}

function formatBytesFromKB(kb) {
  if (!Number.isFinite(kb)) return "—";

  const mb = kb / 1024;
  const gb = mb / 1024;
  const tb = gb / 1024;

  if (tb >= 1) return `${tb.toFixed(2)} TB`;
  if (gb >= 1) return `${gb.toFixed(2)} GB`;
  return `${mb.toFixed(1)} MB`;
}

function clamp(v, a, b) {
  return Math.max(a, Math.min(b, v));
}

function lerp(a, b, t) {
  return a + (b - a) * t;
}

function usageColorByFreeRatio(freeRatio) {
  const t = clamp(freeRatio, 0, 1);

  let r, g, b = 90;

  if (t < 0.5) {
    const k = t / 0.5;
    r = 255;
    g = Math.round(lerp(90, 210, k));
  } else {
    const k = (t - 0.5) / 0.5;
    r = Math.round(lerp(255, 73, k));
    g = Math.round(lerp(210, 242, k));
  }

  return `rgb(${r},${g},${b})`;
}

function createDonutCard(id, cfg) {
  return `
    <div class="donut-item">
      <div class="donut-title">${cfg.title || ""}</div>
      <div class="donut-subtitle">${cfg.subtitle || ""}</div>
      <canvas id="${id}" class="donut-canvas" width="200" height="180"></canvas>
      <div class="donut-legend">
        ${(cfg.parts || []).map(p => `
          <div class="donut-legend-row">
            <span class="donut-color-box"
                  style="background:${p.color || "#999"};opacity:${p.alpha ?? 1}"></span>
            <span>${p.label}: ${p.text ?? p.value}</span>
          </div>
        `).join("")}
      </div>
      <div class="donut-footer">${cfg.footerText || ""}</div>
    </div>
  `;
}

function drawDonut(canvasId, cfg) {
  const canvas = document.getElementById(canvasId);
  if (!canvas) return;

  const ctx = canvas.getContext("2d");
  const w = canvas.width;
  const h = canvas.height;

  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = "#0f152d";
  ctx.fillRect(0, 0, w, h);

  const parts = (cfg.parts || []).filter(p => Number(p.value) > 0);
  const sum = parts.reduce((a, p) => a + Number(p.value || 0), 0);

  if (sum <= 0) {
    ctx.fillStyle = "rgba(255,255,255,.55)";
    ctx.font = "13px sans-serif";
    ctx.fillText("No data", 70, h / 2);
    return;
  }

  const cx = w / 2;
  const cy = h / 2 - 8;
  const radius = 54;
  const lineWidth = 18;

  ctx.beginPath();
  ctx.strokeStyle = "rgba(255,255,255,.08)";
  ctx.lineWidth = lineWidth;
  ctx.arc(cx, cy, radius, 0, Math.PI * 2);
  ctx.stroke();

  let start = -Math.PI / 2;

  parts.forEach(part => {
    const angle = (Number(part.value) / sum) * Math.PI * 2;

    ctx.beginPath();
    ctx.strokeStyle = part.color || "#999";
    ctx.globalAlpha = part.alpha ?? 1.0;
    ctx.lineWidth = lineWidth;
    ctx.lineCap = "round";
    ctx.arc(cx, cy, radius, start, start + angle);
    ctx.stroke();
    ctx.globalAlpha = 1.0;

    start += angle;
  });

  ctx.textAlign = "center";
  ctx.fillStyle = "#e7e9ee";
  ctx.font = "bold 18px sans-serif";
  ctx.fillText(cfg.centerText || "", cx, cy - 4);

  ctx.font = "11px sans-serif";
  ctx.fillStyle = "rgba(255,255,255,.72)";
  ctx.fillText(cfg.centerSubtext || "", cx, cy + 14);
}

function renderDonutModules(donutConfigs) {
  const grid = $("donutGrid");
  if (!grid) return;

  grid.innerHTML = donutConfigs
    .map((cfg, i) => createDonutCard(`donutCanvas_${i}`, cfg))
    .join("");

  donutConfigs.forEach((cfg, i) => {
    drawDonut(`donutCanvas_${i}`, cfg);
  });
}

function makeRamDonut(getters) {
  const total = Number(getters?.sysRamTotal?.data?.value);
  const available = Number(getters?.sysRamAvailable?.data?.value);
  const free = Number(getters?.sysRamFree?.data?.value);
  const processRamRaw = Number(getters?.sysRamProcess?.data?.value);

  if (!Number.isFinite(total) || !Number.isFinite(available) || total <= 0) {
    return {
      title: "RAM",
      subtitle: "System memory",
      centerText: "—",
      centerSubtext: "No data",
      footerText: "RAM unavailable",
      parts: []
    };
  }

  const used = Math.max(0, total - available);
  const process = Number.isFinite(processRamRaw) ? Math.max(0, Math.min(processRamRaw, used)) : 0;
  const other = Math.max(0, used - process);

  const usedPct = (used / total) * 100;
  const processPct = (process / total) * 100;
  const otherPct = (other / total) * 100;
  const availPct = (available / total) * 100;

  const freeRatio = available / total;
  const dynColor = usageColorByFreeRatio(freeRatio);

  return {
    title: "RAM",
    subtitle: "GreenHouse / Other / Available",
    centerText: `${usedPct.toFixed(0)}%`,
    centerSubtext: "RAM used",
    footerText:
      `Total ${formatKB(total)} | Free ${formatKB(free)} | Available ${formatKB(available)}`,
    parts: [
      {
        label: "GreenHouse",
        value: process,
        text: `${formatKB(process)} (${processPct.toFixed(1)}%)`,
        color: "#bd93f9"
      },
      {
        label: "Other processes",
        value: other,
        text: `${formatKB(other)} (${otherPct.toFixed(1)}%)`,
        color: dynColor
      },
      {
        label: "Available",
        value: available,
        text: `${formatKB(available)} (${availPct.toFixed(1)}%)`,
        color: dynColor,
        alpha: 0.35
      }
    ]
  };
}

function makeCpuDonut(getters) {
  const cpu = Number(getters?.sysCpuUsage?.data?.value);

  if (!Number.isFinite(cpu)) {
    return {
      title: "CPU",
      subtitle: "System CPU usage",
      centerText: "—",
      centerSubtext: "No data",
      footerText: "CPU unavailable",
      parts: []
    };
  }

  const used = Math.max(0, Math.min(cpu, 100));
  const idle = 100 - used;
  const color = usageColorByFreeRatio(idle / 100);

  return {
    title: "CPU",
    subtitle: "Used / Idle",
    centerText: `${used.toFixed(0)}%`,
    centerSubtext: "CPU used",
    footerText: `Idle ${idle.toFixed(1)}%`,
    parts: [
      {
        label: "Used",
        value: used,
        text: `${used.toFixed(1)}%`,
        color: color
      },
      {
        label: "Idle",
        value: idle,
        text: `${idle.toFixed(1)}%`,
        color: color,
        alpha: 0.35
      }
    ]
  };
}

function makeDiskDonut(getters) {
  const total = Number(getters?.sysDiskTotal?.data?.value);
  const free = Number(getters?.sysDiskFree?.data?.value);

  if (!Number.isFinite(total) || !Number.isFinite(free) || total <= 0) {
    return {
      title: "Disk",
      subtitle: "Storage usage",
      centerText: "—",
      centerSubtext: "No data",
      footerText: "Disk unavailable",
      parts: []
    };
  }

  const freeSafe = Math.max(0, Math.min(free, total));
  const used = Math.max(0, total - freeSafe);

  const usedPct = (used / total) * 100;
  const freePct = (freeSafe / total) * 100;

  const freeRatio = freeSafe / total;
  const dynColor = usageColorByFreeRatio(freeRatio);

  return {
    title: "Disk",
    subtitle: "Used / Free",
    centerText: `${usedPct.toFixed(0)}%`,
    centerSubtext: "Disk used",
    footerText: `Total ${formatBytesFromKB(total)} | Free ${formatBytesFromKB(freeSafe)}`,
    parts: [
      {
        label: "Used",
        value: used,
        text: `${formatBytesFromKB(used)} (${usedPct.toFixed(1)}%)`,
        color: dynColor
      },
      {
        label: "Free",
        value: freeSafe,
        text: `${formatBytesFromKB(freeSafe)} (${freePct.toFixed(1)}%)`,
        color: dynColor,
        alpha: 0.35
      }
    ]
  };
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

function renderExecutorDataBlock(label, block) {
  if (!block) return `${label}: —`;

  const valid = badgeValid(!!block.valid);
  const mode = badgeMode(block.mode);
  const stamp = `<span class="pill muted">stamp:${fmtMs(block.stampMs)}</span>`;
  const data = formatAnyData(block.data);

  const extra = [];
  if (Object.prototype.hasOwnProperty.call(block, "dirty")) {
    extra.push(badgeDirty(!!block.dirty));
  }
  if (Object.prototype.hasOwnProperty.call(block, "pending")) {
    extra.push(badgePending(!!block.pending));
  }
  if (block.lastWriter) {
    extra.push(`<span class="pill muted">writer:${block.lastWriter}</span>`);
  }
  if (block.lastAppliedMs) {
    extra.push(`<span class="pill muted">applied:${fmtMs(block.lastAppliedMs)}</span>`);
  }

  const err = block.lastError
    ? `<div class="small bad mono">error:${block.lastError}</div>`
    : "";

  return `
    <div class="small muted mono" style="margin-top:4px">
      <div><b>${label}</b></div>
      <div>${valid} ${mode} ${stamp} ${extra.join(" ")}</div>
      <div>${data}</div>
      ${err}
    </div>
  `;
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

    const donutConfigs = [
      makeRamDonut(getters),
      makeCpuDonut(getters),
      makeDiskDonut(getters),
    ];
    renderDonutModules(donutConfigs);

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
        const data = formatGetterData(e);
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

        const compatValid = badgeValid(!!x.valid);
        const compatMode = badgeMode(x.mode);
        const compatStamp = `<span class="pill muted">stamp:${fmtMs(x.stampMs)}</span>`;
        const compatData = formatAnyData(x.data);

        const effMode = safeUpper(effectiveMode(x));
        const isManual = effMode === "MANUAL";

        const controls = `
          <div class="row" style="justify-content:flex-end">
            <button onclick="execCmd('${name}','mode','manual')">MANUAL</button>
            <button onclick="execCmd('${name}','mode','auto')">AUTO</button>
            <button ${isManual ? "" : "disabled"} onclick="execCmd('${name}','on')">ON</button>
            <button ${isManual ? "" : "disabled"} onclick="execCmd('${name}','off')">OFF</button>
          </div>
        `;

        const desiredBlock = renderExecutorDataBlock("desired", x.desired);
        const actualBlock = renderExecutorDataBlock("actual", x.actual);

        return `
          <div class="item">
            <div class="left">
              <div class="k">
                <span class="mono">${x.id}</span>
                <span class="mono muted">${name}</span>
              </div>
              <div class="meta">
                ${schemaT} ${compatValid} ${compatMode} ${compatStamp}
              </div>
              <div class="small muted mono">
                compat:${compatData}
              </div>
              ${desiredBlock}
              ${actualBlock}
            </div>
            <div class="right">${controls}</div>
          </div>
        `;
      })
      .join("");

    $("executors").innerHTML = eHtml || `<div class="small muted">Нет данных</div>`;
  } catch (e) {
    setDot(false);
    $("getters").innerHTML =
      `<div class="small bad">Ошибка загрузки: ${String(e.message || e)}</div>`;
    $("executors").innerHTML =
      `<div class="small bad">Ошибка загрузки: ${String(e.message || e)}</div>`;
  }
}

function setIntervalRefresh(ms) {
  if (timer) clearInterval(timer);
  timer = null;
  if (ms > 0) timer = setInterval(reloadAll, ms);
}

$("btnReload").addEventListener("click", reloadAll);
$("refresh").addEventListener("change", (e) => {
  setIntervalRefresh(parseInt(e.target.value, 10));
});
$("search").addEventListener("input", () => reloadAll());

reloadAll();
setIntervalRefresh(parseInt($("refresh").value, 10));