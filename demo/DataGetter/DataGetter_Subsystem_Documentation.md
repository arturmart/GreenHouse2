
# DataGetter Subsystem Documentation

## Overview

The **DataGetter subsystem** is responsible for collecting data from different sources and publishing them into the system's global state (`GlobalState`) through `Field<T>` references.

This subsystem provides a unified architecture for:

- Physical sensors (DS18B20)
- System metrics (CPU, RAM, Disk)
- External APIs (Weather services)
- Time sources

The design is based on the **Strategy Pattern**, allowing the system to easily extend support for new data sources.

---

# Architecture

The subsystem consists of three main layers:

1. **Strategy Interface**
2. **Concrete Data Strategies**
3. **DataGetter Container (Manager)**

High‑level architecture:

```
Scheduler
   │
   ▼
DataGetter::tick()
   │
   ▼
for each Strategy
   │
   ▼
strategy->tick()
   │
   ▼
getData()
   │
   ▼
Field<T>::set(value)
   │
   ▼
GlobalState updated
```

---

# Core Components

## 1. ADataGetterStrategyBase

Base abstract class for all strategies.

Responsibilities:

- common interface for strategies
- lifecycle control (`init`, `tick`)
- polymorphic storage in container

Example interface:

```
class ADataGetterStrategyBase {
public:
    using Ctx = std::unordered_map<std::string, std::any>;

    virtual ~ADataGetterStrategyBase() = default;

    virtual void init(const Ctx&) {}
    virtual void tick() = 0;
    virtual std::string name() const;
};
```

Purpose:

Allows storing strategies of different data types inside one container.

---

# Template Strategy Layer

## ADataGetterStrategy<T>

This templated class provides a type‑safe strategy implementation.

Responsibilities:

- hold the last measured value
- publish data into `Field<T>`
- enforce implementation of `getData()`

Key fields:

```
T sensorValue_;
Field<T>* ref_;
```

Important methods:

### initRef()

Connects strategy to global field.

```
void initRef(Field<T>& field);
```

### getData()

Pure virtual function implemented by concrete strategies.

```
virtual T getData() = 0;
```

### tick()

Default update cycle.

```
void tick() {
    getDataRef();
}
```

---

# DataGetter Manager

The **DataGetter container** manages all strategies.

Responsibilities:

- register strategies
- initialize strategies
- update them periodically
- provide access by key

Internal storage:

```
std::unordered_map<std::string, StrategyUP> strategies_;
```

Where:

```
using StrategyUP = std::unique_ptr<ADataGetterStrategyBase>;
```

---

# DataGetter API

## add()

Registers an already created strategy.

```
void add(key, strategy)
```

---

## emplace()

Creates strategy inside the container.

Example:

```
auto& sensor =
    dataGetter.emplace<DG_DS18B20>("temp1", "28-0000000001");
```

---

## init()

Initializes all strategies.

```
void init(const Ctx& ctx)
```

The context can contain:

- config
- shared services
- mock devices
- API objects

---

## tick()

Updates all registered strategies.

```
void tick()
```

Usually called periodically by the Scheduler.

---

## get()

Returns pointer to strategy by key.

```
StrategyBase* get(key)
```

---

# Implemented Strategies

Current subsystem includes several built‑in strategies.

---

# DS18B20 Temperature Strategy

File:

```
DG_DS18B20.hpp
```

Reads temperature via Linux **1‑Wire interface**.

Path example:

```
/sys/bus/w1/devices/<id>/w1_slave
```

Returned type:

```
float
```

Workflow:

1. open sensor file
2. read two lines
3. verify CRC
4. extract temperature
5. convert to Celsius

Example output:

```
23.625 °C
```

---

# Weather API Strategy

File:

```
DG_OWM_Weather.hpp
```

Fetches weather metrics using **OpenWeather API**.

Supported fields:

- temperature
- humidity
- pressure
- wind speed

Features:

- caching
- external API wrapper
- numeric conversion

Return type:

```
double
```

---

# CPU Usage Strategy

File:

```
DG_SYS_CPU.hpp
```

Reads CPU usage based on `/proc/stat` snapshots.

Algorithm:

1. read CPU counters
2. compare with previous snapshot
3. compute usage delta

Return type:

```
double
```

Example:

```
CPU = 32.4%
```

---

# Disk Metrics Strategy

File:

```
DG_SYS_DISK.hpp
```

Reads filesystem metrics.

Fields:

```
TOTAL
FREE
AVAILABLE
```

Return type:

```
double (KB)
```

Example:

```
Disk Free = 32400000 KB
```

---

# Memory Metrics Strategy

File:

```
DG_SYS_MEM.hpp
```

Reads system memory information.

Fields:

```
MEM_TOTAL
MEM_FREE
MEM_AVAILABLE
MEM_PROCESS
```

Source:

```
/proc/meminfo
```

Return type:

```
double (KB)
```

---

# System Time Strategy

File:

```
DG_SYS_TIME.hpp
```

Provides current Unix time in milliseconds.

Return type:

```
tools::UnixMs
```

Used for:

- timestamping
- telemetry
- event ordering

---

# Typical Usage

Example integration:

```
dg::DataGetter dg;

auto& temp =
    dg.emplace<DG_DS18B20>("temp_inside","28-xxxx");

auto& cpu =
    dg.emplace<DG_SYS_CPU>("cpu");

auto& mem =
    dg.emplace<DG_SYS_MEM>("ram",
        DG_SYS_MEM::Field::MEM_AVAILABLE);

dg.init(ctx);

scheduler.addPeriodic([&]{
    dg.tick();
}, 1s);
```

---

# Advantages of the Architecture

✔ Modular sensor system  
✔ Easy to extend with new data sources  
✔ Strong type safety  
✔ Compatible with Scheduler module  
✔ Clean separation between data collection and state storage  

---

# Known Limitations

1. Exceptions inside strategies are not isolated
2. One failing strategy can break the tick loop
3. No built‑in retry logic
4. No health state reporting
5. No statistics about update latency

---

# Suggested Improvements

Future improvements could include:

### Strategy health status

```
enum class SensorState {
    OK,
    ERROR,
    DISCONNECTED
}
```

### Retry system

Automatic retry for unreliable sensors.

### Metrics

Tracking:

- read time
- error count
- last update time

### Error isolation

Wrap each strategy execution in try/catch.

---

# Subsystem Role in GreenHouse Architecture

The DataGetter subsystem acts as the **data acquisition layer**.

It provides inputs for:

- automation logic
- telemetry
- monitoring dashboard
- control decisions

---

Generated: 2026-03-16T23:58:19.214412+00:00