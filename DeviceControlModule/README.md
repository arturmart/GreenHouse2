# PROTOCOL.md — Device Control Module Protocol Specification
### Arduino Mega 2560 — Digital + PWM Controller  
**Supports**: CRC8 • Multi-Layer Parser • Keywords • Multi-Packet Commands • Per-Channel Inversion • Full State Initialization • Structured Feedback Mask

---

# 1. System Overview

DeviceControlModule (DCM) is a fully structured UART-based ASCII protocol for controlling **Digital (ID=68)** and **PWM (ID=80)** channels on Arduino Mega 2560.

The firmware supports:

- 8 Digital outputs  
- 3 PWM outputs  
- CRC8 validation (poly 0x07)  
- Per-channel **inversion** for Digital and PWM  
- Multi-packet command messages  
- Keyword commands  
- Two-layer state: **hardware state** + **logical state**  
- Full feedback mask (13 bits)  

---

# 2. Pin Tables

## 2.1 Digital Table (ID = 68)
| Index | Arduino Pin |
|-------|-------------|
| 0 | 22 |
| 1 | 24 |
| 2 | 26 |
| 3 | 28 |
| 4 | 30 |
| 5 | 32 |
| 6 | 34 |
| 7 | 36 |

## 2.2 PWM Table (ID = 80)
| Index | Arduino Pin |
|-------|-------------|
| 0 | 2 |
| 1 | 3 |
| 2 | 5 |

---

# 3. Internal State Tables

## 3.1 Hardware state
```cpp
bool    digitalState[8];   // 0 or 1
uint8_t pwmState[3];       // 0..255
```

## 3.2 Inversion state
```cpp
bool invertDigital[8] = { true,true,true,true,true,true,true,true };
bool invertPWM[3]     = { false,false,false };
```

- Digital inversion: `logicVal = invertDigital[i] ? !input : input`
- PWM inversion: `logicPWM = invertPWM[i] ? (255 - input) : input`

## 3.3 Logical state
```cpp
logicDigital[i] = invertDigital[i] ? !digitalState[i] : digitalState[i];
logicPWM[i]     = invertPWM[i]     ? (255 - pwmState[i]) : pwmState[i];
```

Logical state = representation AFTER applying inversion.

---

# 4. Initialization Process (`pinsInit()`)

On boot:

1. `pinMode(pin, OUTPUT)` for each channel  
2. Digital output initialized as:  

```cpp
digitalWrite(pin, digitalState[i] ^ invertDigital[i] ? HIGH : LOW);
```

3. PWM outputs start with:

```cpp
analogWrite(pin, 0);
pwmState[i] = 0;
```

4. Logical tables computed by `updateLogicStates()`  
5. Module prints:

```text
deviceControlModule (CRC8, Digital+PWM, per-channel invert) started
```

---

# 5. CRC8 Specification

DCM uses CRC8 for all packets:

- Polynomial: **0x07**
- Init value: **0x00**
- No reflection
- No XOR-in/out

Computation:

```cpp
crc ^= data[i];
for (8 bits)
    crc = (crc & 0x80) ? ((crc << 1) ^ 0x07) : (crc << 1);
```

CRC is formatted as **2-digit hex** (uppercase).

Example:

```text
68,0,1 → 1E → command is: 68,0,1/1E
```

---

# 6. Message Format (Global Parser Layer 1)

Every message is:

```text
<Data>/<CRC>\n
```

Example:

```text
80,0,255/7A
```

Layer-1 validation rules:

| Rule | Error |
|------|--------|
| No slash `/` | ERROR_SYNTAX |
| More than one slash | ERROR_1L_TOO_MANY_DATA |
| Empty data before slash | ERROR_1L_NO_DATA |
| Empty CRC part | ERROR_NULL_CRC |
| CRC not hex | ERROR_INVALID_CRC |
| CRC mismatch | ERROR_INVALID_CRC |

---

# 7. Keyword Commands (Layer 2: Keyword Handler)

A message is a keyword if the first token is **non-numeric**.

### 7.1 `inited`
Response:
```text
ok/<CRC>
```
Feedback bit: `GET_KEYWORD`

---

### 7.2 `showall`
Returns all hardware states, non-inverted:

```text
68,0,1;68,1,0;...;80,0,255;80,1,0;80,2,128
```

### 7.3 `showall,68,<idx>`
Returns one digital entry.

### 7.4 `showall,80,all`
Returns all PWM entries.

### 7.5 `setAll`
Enters “state update mode”, meaning the **next valid frame** is treated as a full hardware state update.

Responds:
```text
setAll_wait/CRC
```

### 7.6 `end`
Exits `setAll` mode.

---

# 8. Data Packets (Layer 3: Multi-Packet Parser)

Non-keyword messages contain one or more packets separated by `;`:

```text
68,0,1;68,1,0;80,0,255
```

Each packet must match:

```text
<TableID>,<Index>,<Value>
```

Example:

```text
68,3,1   → Digital index 3 output HIGH
80,0,255 → PWM index 0 full duty
```

### Allowed ranges:

- Digital value: `0` or `1`
- PWM value: `0..255`
- Max packets per frame: **8**

### Errors:

| Condition | Flag |
|----------|-------|
| No packets | ERROR_2L_NO_DATA_PACKETS |
| More than 8 | ERROR_2L_TOO_MANY_PACKETS |
| Packet missing fields | ERROR_3L_WRONG_DATA_PACKETS |

---

# 9. Hardware Execution (Layer 4: State Application)

## 9.1 Digital channels
```cpp
bool logicVal = (val != 0);

if (invertDigital[idx])
    logicVal = !logicVal;

digitalState[idx] = logicVal;
digitalWrite(digitalPins[idx], logicVal ? HIGH : LOW);
```

## 9.2 PWM channels
```cpp
uint8_t logicPwm = constrain(val, 0, 255);

if (invertPWM[idx])
    logicPwm = 255 - logicPwm;

pwmState[idx] = logicPwm;
analogWrite(pwmPins[idx], logicPwm);
```

After all packets processed:
```cpp
updateLogicStates();
```

---

# 10. Feedback Mask Specification

Feedback is always printed after command processing:

```text
KXXXX87654321
```

Total bits: **13**

| Bit | Name | Meaning |
|-----|-----------|----------|
| 0 | ERROR_SYNTAX | Wrong structure |
| 1 | ERROR_1L_NO_DATA | No data in Level-1 |
| 2 | ERROR_1L_TOO_MANY_DATA | More than 1 '/' |
| 3 | ERROR_INVALID_CRC | Bad CRC |
| 4 | ERROR_NULL_CRC | Empty CRC part |
| 5 | ERROR_2L_NO_DATA_PACKETS | No packets |
| 6 | ERROR_2L_TOO_MANY_PACKETS | >8 packets |
| 7 | ERROR_3L_WRONG_DATA_PACKETS | Wrong packet format |
| 8–11 | PACKETS_COUNT | Number of processed packets |
| 12 | GET_KEYWORD | Keyword command executed |

---

# 11. Example Commands

## Digital ON
```text
68,0,1/1E
```

## Digital OFF
```text
68,0,0/19
```

## PWM set to 128
```text
80,0,128/E2
```

## showall
```text
showall/3B
```

## setAll mode
```text
setAll/6E
```

## end setAll mode
```text
end/D5
```

---

# 12. Full Data Flow (Layer Diagram)

```text
┌─────────────────────────────────────────────┐
│                 INPUT STRING                │
│      "68,0,1;80,2,255/4A\n"                │
└─────────────────────────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│      L1: Slash Parser (Data / CRC)          │
│  - Syntax errors                             │
│  - Missing CRC                               │
│  - CRC mismatch                              │
└─────────────────────────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│   L2: Keyword Detector                       │
│  - if first token non-digit → keyword        │
│  - else → data packets                       │
└─────────────────────────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│   L3: Packet Splitter (max 8 packets)        │
│  - Split by ';'                              │
│  - Validate packet count                     │
└─────────────────────────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│   L4: Packet Parser                          │
│  "Table,Index,Value"                         │
│   → Apply inversion                          │
│   → Update HW state                          │
└─────────────────────────────────────────────┘
                   │
                   ▼
┌─────────────────────────────────────────────┐
│   L5: Feedback Encoder                       │
│  - Build 13-bit mask                         │
│  - Print to serial                           │
└─────────────────────────────────────────────┘
```

---

# 13. Full QA Checklist

## Boot
- [ ] Digital pins correctly initialized with inversion
- [ ] PWM pins initialized to 0

## CRC
- [ ] Wrong CRC activates ERROR_INVALID_CRC
- [ ] Empty CRC activates ERROR_NULL_CRC
- [ ] Extra slash activates ERROR_1L_TOO_MANY_DATA

## Digital control
- [ ] `68,0,1` turns ON (with inversion accounted)
- [ ] `68,0,0` turns OFF

## PWM control
- [ ] Limits enforced (0..255)
- [ ] Inversion works (`255 - value`)

## Packet system
- [ ] Up to 8 packets processed
- [ ] Correct PACKETS_COUNT bits set
- [ ] Wrong packet → ERROR_3L_WRONG_DATA_PACKETS

## Keywords
- [ ] `inited` returns `ok`
- [ ] `showall` prints all states
- [ ] `setAll` enters mode
- [ ] `end` exits mode

## setAll mode
- [ ] Next valid frame updates **entire system**
- [ ] After update, inversion applied  
- [ ] Logic tables updated  

---

# 14. License
Free to use for any commercial or personal projects.

---

# 15. End of Specification
This file documents the entire command protocol, parser architecture, error system, CRC validation, multi-packet engine, inversion logic, and hardware mapping for DeviceControlModule.
