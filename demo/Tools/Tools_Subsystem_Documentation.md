
# Tools Subsystem Documentation

## Overview

The **Tools subsystem** provides low-level utilities and services used across the entire system.

These tools are reusable, hardware-independent (mostly), and support:

- hardware communication (Serial)
- system monitoring (CPU, Memory, Disk)
- external API integration (Weather)
- time utilities
- data integrity (CRC)

This subsystem acts as the **foundation layer** of the architecture.

---

# Architecture Role

```
Tools
  ↓
DataGetter / Executor
  ↓
GlobalState / Logic
```

Tools do not depend on high-level modules.

Instead, higher-level modules depend on Tools.

---

# Components

This subsystem includes:

```
SerialComm.hpp
DeviceControlModule.hpp
SysCpu.hpp
SysDisk.hpp
SysMem.hpp
WeatherAPI.hpp
CRC8.hpp
DateTime.hpp
```

---

# Serial Communication

## File: SerialComm.hpp

### Purpose

Provides UART communication with external devices.

### Features

- open/close serial port
- configurable baud rate
- read/write operations
- blocking communication
- retry and timeout logic (if implemented)

### Usage Example

```
SerialComm serial("/dev/ttyS0", 115200);
serial.write("CMD");
auto response = serial.read();
```

### Use Cases

- communication with Arduino
- relay control modules
- telemetry devices

---

# Device Control Module

## File: DeviceControlModule.hpp

### Purpose

High-level abstraction over hardware control.

### Responsibilities

- send structured commands
- communicate with device via SerialComm
- encode/decode packets
- apply CRC validation

### Architecture

```
Executor Strategy
      ↓
DeviceControlModule
      ↓
SerialComm
      ↓
Hardware
```

---

# System CPU Monitoring

## File: SysCpu.hpp

### Purpose

Reads CPU usage statistics.

### Data Source

```
/proc/stat
```

### Method

- reads CPU times
- calculates usage between two snapshots

### Output

```
double (percentage)
```

---

# System Disk Monitoring

## File: SysDisk.hpp

### Purpose

Reads filesystem information.

### Data Source

```
statvfs()
```

### Metrics

- total space
- free space
- available space

### Output

```
double (KB)
```

---

# System Memory Monitoring

## File: SysMem.hpp

### Purpose

Reads RAM usage.

### Data Source

```
/proc/meminfo
```

### Metrics

- total memory
- free memory
- available memory
- process memory

---

# Weather API

## File: WeatherAPI.hpp

### Purpose

Provides external weather data.

### Features

- HTTP requests
- JSON parsing
- caching (TTL)
- error handling

### Typical Data

- temperature
- humidity
- pressure
- wind speed

---

# CRC8 Utility

## File: CRC8.hpp

### Purpose

Provides CRC8 checksum calculation.

### Usage

- verify data integrity
- detect transmission errors

### Use Cases

- UART communication
- device protocol validation

---

# Date and Time Utilities

## File: DateTime.hpp

### Purpose

Provides time-related utilities.

### Features

- current Unix time
- time formatting
- time conversions

### Example

```
auto now = tools::nowUnixMs();
```

---

# Design Principles

✔ Single responsibility per module  
✔ Reusable across subsystems  
✔ Low coupling  
✔ High portability  
✔ Hardware abstraction  

---

# Integration Example

```
SerialComm serial("/dev/ttyS0", 115200);
DeviceControlModule device(serial);

device.sendCommand("PUMP_ON");
```

---

# Advantages

✔ clean abstraction over hardware  
✔ reusable utilities  
✔ platform-friendly (Linux SBC)  
✔ supports embedded + server environments  

---

# Limitations

- no unified error handling layer
- limited logging inside tools
- no async support (mostly blocking)
- no retry policies in some modules

---

# Possible Improvements

## Async IO

Use Boost.Asio for async serial communication.

## Unified logging

Add logging layer across all tools.

## Error handling

Standardize error codes / exceptions.

## Protocol abstraction

Separate protocol encoding/decoding from transport.

---

# Role in GreenHouse System

Tools subsystem is the **foundation layer**.

It enables:

- communication with hardware
- data acquisition
- system monitoring
- external data integration

Without this layer, higher-level modules cannot function.

---

Generated: 2026-03-17T00:03:35.958680+00:00