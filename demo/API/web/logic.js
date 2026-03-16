const $ = (id) => document.getElementById(id);

const dot = $("dot");
const apiBase = location.origin;
$("apiBase").textContent = apiBase;

let timer = null;

function setDot(ok) {
  dot.style.background = ok ? "#49f28a" : "#ff6b6b";
  dot.style.boxShadow = ok
    ? "0 0 14px rgba(73,242,138,.45)"
    : "0 0 14px rgba(255,107,107,.45)";
}

async function jget(path) {
  const r = await fetch(path, { cache: "no-store" });
  if (!r.ok) throw new Error(`${path} -> ${r.status}`);
  return await r.json();
}

function pill(text, cls = "muted") {
  return `<span class="pill ${cls}">${text}</span>`;
}

function fmtMs(ms) {
  if (!ms) return "—";
  return `${ms}ms`;
}

function renderAction(a) {
  return `
    <div class="small mono muted">
      action → target:${a.target}, type:${a.valueType}, value:${a.value}, trigger:${a.trigger}, enabled:${a.enabled}
    </div>
  `;
}

function renderNode(node) {
  if (!node) return "";

  const rt = node.runtime || {};
  const active = !!rt.effectiveResult;
  const cls = active ? "active" : "inactive";

  const meta = [
    pill(`condition:${node.condition || "—"}`),
    pill(`local:${rt.localResult ? "true" : "false"}`, rt.localResult ? "good" : "bad"),
    pill(`effective:${rt.effectiveResult ? "true" : "false"}`, rt.effectiveResult ? "good" : "bad"),
    pill(`prev:${rt.prevEffectiveResult ? "true" : "false"}`),
    pill(`eval:${fmtMs(rt.lastEvalMs)}`),
    pill(`fire:${fmtMs(rt.lastFireMs)}`)
  ].join(" ");

  const args = (node.args || []).length
    ? `<div class="logic-section small mono">args: ${JSON.stringify(node.args)}</div>`
    : `<div class="logic-section small mono muted">args: []</div>`;

  const resolvedArgs = (rt.resolvedArgs || []).length
    ? `<div class="logic-section small mono">resolved: ${JSON.stringify(rt.resolvedArgs)}</div>`
    : `<div class="logic-section small mono muted">resolved: []</div>`;

  const actions = (node.actions || []).length
    ? `<div class="logic-section">${node.actions.map(renderAction).join("")}</div>`
    : `<div class="logic-section small mono muted">actions: []</div>`;

  const err = rt.lastError
    ? `<div class="logic-section small mono bad">error: ${rt.lastError}</div>`
    : "";

  const children = (node.children || []).length
    ? `<div class="logic-children">${node.children.map(renderNode).join("")}</div>`
    : "";

  return `
    <div class="logic-node ${cls}">
      <div class="logic-title">${node.title || "unnamed"}</div>
      <div class="logic-meta">${meta}</div>
      ${args}
      ${resolvedArgs}
      ${actions}
      ${err}
      ${children}
    </div>
  `;
}

function countNodes(node) {
  if (!node) return 0;
  let total = 1;
  for (const ch of (node.children || [])) {
    total += countNodes(ch);
  }
  return total;
}

async function reloadAll() {
  try {
    const [status, logicFull] = await Promise.all([
      jget("/status"),
      jget("/api/json/logic/full")
    ]);

    setDot(status?.status === "ok");

    const root = logicFull?.root || null;
    $("logicCount").textContent = String(countNodes(root));

    $("logicTree").innerHTML = root
      ? renderNode(root)
      : `<div class="small muted">Нет logic tree</div>`;

    $("rawJson").textContent = JSON.stringify(logicFull, null, 2);
  } catch (e) {
    setDot(false);
    $("logicTree").innerHTML = `<div class="small bad">Ошибка загрузки: ${String(e.message || e)}</div>`;
    $("rawJson").textContent = String(e.message || e);
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

reloadAll();
setIntervalRefresh(parseInt($("refresh").value, 10));