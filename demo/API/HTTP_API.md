# GreenHouse HTTP API (Frontend Usage)

This document describes how the HTTP service works for frontend clients and how to use it safely with executor control.

Base URL:

- `http://<host>:8080`

Server entry points:

- UI page: `GET /`
- Frontend script: `GET /app.js`

---

## 1) Read APIs (state and schema)

### `GET /status`

Health check.

Response:

```json
{ "status": "ok" }
```

---

### `GET /schema/getters`

Returns getter type schema: `key -> type`.

Response example:

```json
{
  "temp": "double",
  "temp2": "double",
  "date": "string"
}
```

---

### `GET /schema/executors`

Returns executor type schema: `name -> type`.

Response example:

```json
{
  "LOW_DCM_D_0": "bool",
  "LOW_DCM_D_1": "bool"
}
```

---

### `GET /getters`

Returns all getter values with metadata.

Response shape:

- top-level object keyed by getter name
- each entry:
  - `valid: boolean`
  - `stampMs: number`
  - `data: { type: "bool"|"int"|"double"|"string"|"unknown", value: any|null }`

Response example:

```json
{
  "temp": {
    "valid": true,
    "stampMs": 123456789,
    "data": { "type": "double", "value": 24.12 }
  },
  "date": {
    "valid": true,
    "stampMs": 123456790,
    "data": { "type": "string", "value": "2026-02-13 21:00" }
  }
}
```

---

### `GET /getters/<key>`

Returns one getter by key.

Success response:

```json
{
  "key": "temp",
  "valid": true,
  "stampMs": 123456789,
  "data": { "type": "double", "value": 24.12 }
}
```

If key does not exist: HTTP `404`, body:

```json
{ "error": "Getter key not found: tempX" }
```

---

### `GET /executors`

Returns all executors.

Response shape (array of objects):

- `id: number`
- `name: string`
- `valid: boolean`
- `stampMs: number`
- `mode: "MANUAL" | "AUTO"`
- `data: { type: "...", value: ... }`

Response example:

```json
[
  {
    "id": 1,
    "name": "LOW_DCM_D_0",
    "valid": true,
    "stampMs": 123456799,
    "mode": "MANUAL",
    "data": { "type": "bool", "value": false }
  }
]
```

---

## 2) Write API (executor commands)

### `POST /api/executors/<name>/<action>`

Generic command route used by frontend buttons.

Headers:

- `Content-Type: application/json`

Body format:

```json
{ "value": "..." }
```

`value` is optional for actions that do not need it.

Supported actions:

1. `mode`
   - `value` must be one of: `"manual"`, `"MANUAL"`, `"0"`, `"auto"`, `"AUTO"`, `"1"`
   - success:
     ```json
     { "ok": true, "name": "LOW_DCM_D_0", "action": "mode", "value": "MANUAL" }
     ```
2. `on`
   - no value required
   - executor **must already be in `MANUAL` mode**
   - success:
     ```json
     { "ok": true, "name": "LOW_DCM_D_0", "action": "on" }
     ```
3. `off`
   - no value required
   - executor **must already be in `MANUAL` mode**
   - success:
     ```json
     { "ok": true, "name": "LOW_DCM_D_0", "action": "off" }
     ```
4. `set`
   - for numeric executor values (`value` parsed as integer via `stoi`)
   - executor **must already be in `MANUAL` mode**
   - success:
     ```json
     { "ok": true, "name": "SOME_PWM_EXEC", "action": "set", "value": 128 }
     ```

Error behavior:

- Invalid route/action/mode or other runtime issues return HTTP `400` with:

```json
{ "error": "<message>" }
```

- Unknown executor name, wrong mode, invalid integer, etc. are returned through this error format.

---

## 3) Frontend control flow (important)

Use this sequence for reliable manual control:

1. Read available executors from `GET /executors`
2. If target executor mode is not `MANUAL`, send:
   - `POST /api/executors/<name>/mode` with `{ "value": "manual" }`
3. Send operation:
   - `POST /api/executors/<name>/on` or `/off`
   - or `POST /api/executors/<name>/set` with integer value
4. Refresh state using `GET /executors` and/or `GET /getters`

Notes:

- `on/off/set` are rejected in `AUTO` mode.
- UI should disable manual action buttons when mode is not `MANUAL`.

---

## 4) Minimal frontend helpers

```js
async function jget(path) {
  const r = await fetch(path, { cache: "no-store" });
  if (!r.ok) throw new Error(`${path} -> ${r.status}`);
  return r.json();
}

async function jpost(path, body = {}) {
  const r = await fetch(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body)
  });
  const text = await r.text();
  let data = {};
  try { data = JSON.parse(text); } catch {}
  if (!r.ok) throw new Error(data?.error || `${path} -> ${r.status}`);
  return data;
}
```

Example usage:

```js
await jpost("/api/executors/LOW_DCM_D_0/mode", { value: "manual" });
await jpost("/api/executors/LOW_DCM_D_0/on", {});
const executors = await jget("/executors");
```

---

## 5) Current project executor naming context

Configured DCM digital executors are named `LOW_DCM_D_0 ... LOW_DCM_D_7` (see `DG_EXE_CONFIG.txt`), and they are currently typed as `bool`.

This means typical frontend control operations are:

- switch mode `manual/auto`
- toggle `on/off`

For integer `set` commands, ensure the target executor is configured and mapped for integer semantics.
