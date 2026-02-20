# Device Control Module (DCM)
### Arduino Mega 2560 — Digital + PWM Controller  
**Features:** CRC8 Integrity • Per-Channel Inversion • Init State Apply • Multi-Packet Commands • Structured Feedback Bitmask

## Overview
The **Device Control Module (DCM)** provides structured control over multiple Digital and PWM output channels on an **Arduino Mega 2560** using a custom ASCII protocol with **CRC8 validation**.

The firmware supports:

- **8 Digital output channels** (ID `68`)
- **3 PWM output channels** (ID `80`)
- **CRC8 (poly 0x07) integrity check**
- **Per-channel inversion configuration**
- **Hardware state initialization from internal state tables**
- **Multi-packet command sequences**
- **Keyword commands (`inited`, `showall`, `setAll`)**
- **Detailed 13-bit feedback status**

## Pin Mapping

### Digital Outputs (ID = 68)
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

### PWM Outputs (ID = 80)
| Index | Arduino Pin | Timer |
|-------|-------------|--------|
| 0 | 2 | Timer3 |
| 1 | 3 | Timer3 |
| 2 | 5 | Timer3 |

## Internal State Tables

### Hardware State
```
bool    digitalState[8];   // 0 or 1
uint8_t pwmState[3];       // 0..255
```

### Per-Channel Inversion
```
bool invertDigital[8];     // false = normal, true = inverted
bool invertPWM[3];         // false = normal, true = reversed
```

### Logical State (computed)
```
logicDigital[i] = invertDigital[i] ? !digitalState[i] : digitalState[i];
logicPWM[i]     = invertPWM[i]     ? (255 - pwmState[i]) : pwmState[i];
```

## Initialization Behavior
Upon startup (`setup()`):

1. All output pins are configured as OUTPUT.
2. Hardware pins are initialized using **digitalState[]** and **pwmState[]**.
3. If a channel has inversion enabled, the inverted hardware value is applied.
4. Logical state tables (`logicDigital[]`, `logicPWM[]`) are updated.

## Protocol Format
All messages use the format:
```
<Data>/<CRC8>

```

Example:
```
68,0,1/1E
```

## CRC8 Specification
- Polynomial: **0x07**
- Initial value: **0x00**
- No XOR-in
- No XOR-out
- No reflection

## Packet Structure

### Level 1 — Command + CRC
```
<Data>/<CRC>
```

### Level 2 — Multiple packets (max 8)
```
Packet1;Packet2;Packet3...
```

### Level 3 — Single packet (3 fields)
```
<TableID>,<Index>,<Value>
```

## Error Feedback Bitmask
Feedback is a **13-bit mask** printed as a binary string:
```
KXXXX87654321
```

## Keyword Commands
- `inited`
- `showall`
- `setAll`

## Device Commands (Digital/PWM)

### Digital Write
```
68,<index>,<0|1>
```

### PWM Write
```
80,<index>,<0..255>
```

## Inversion Logic
### Digital inversion:
```
invertDigital[i] = true → hardwareValue = !logicValue
```

### PWM inversion:
```
invertPWM[i] = true → hardwareValue = 255 - logicValue
```

## QA Test Checklist

### Boot Test
- Hardware matches `digitalState[]`
- PWM matches `pwmState[]`

### CRC Test
- Wrong CRC → `ERROR_INVALID_CRC`

### Digital/PWM Tests
- 68,0,1/1E → ON
- 68,0,0/19 → OFF
- PWM commands work with inversion

### Keyword Tests
- `inited` returns OK
- `showall` returns state list

