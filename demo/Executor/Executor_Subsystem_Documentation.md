
# Executor Subsystem Documentation

## Overview

The **Executor subsystem** is responsible for controlling actuators and devices in the system.

While the **DataGetter subsystem** reads data from sensors, the **Executor subsystem performs actions** based on system logic.

Typical examples:

- turning heaters ON/OFF
- controlling pumps
- switching relays
- controlling fans
- sending commands to external modules
- interacting with device control modules

The Executor subsystem follows the **Strategy Pattern**, allowing different device control strategies to be implemented independently.

---

# Architectural Role

Within the overall system architecture:

```
Sensors → DataGetter → GlobalState → Logic → Executor → Devices
```

Responsibilities of the Executor subsystem:

- execute actions on hardware
- translate logical commands into device commands
- maintain device states
- synchronize device states with GlobalState

---

# Main Components

The subsystem consists of four main modules:

1. **Executor Manager**
2. **Executor Strategies**
3. **ExecutorStateBridge**
4. **Device Control Modules**

Files:

```
Executor.hpp
AExecutor_Strategy.hpp
ExecutorStateBridge.hpp
EX_DeviceControlModule.hpp
```

---

# Executor Manager

File:

```
Executor.hpp
```

The **Executor manager** is the central controller responsible for:

- storing executor strategies
- dispatching commands to strategies
- initializing strategies
- managing the lifecycle of device control logic

Internal container:

```
std::unordered_map<std::string, StrategyUP> strategies_;
```

Where:

```
StrategyUP = std::unique_ptr<AExecutorStrategyBase>
```

This allows heterogeneous executor implementations.

---

# Executor Strategy Interface

File:

```
AExecutor_Strategy.hpp
```

This file defines the **base interface for all executor strategies**.

Example structure:

```
class AExecutorStrategyBase
{
public:
    virtual ~AExecutorStrategyBase() = default;

    virtual void init(const Ctx&) {}
    virtual void execute() = 0;
    virtual std::string name() const;
};
```

Responsibilities:

- define execution interface
- allow polymorphic storage
- allow initialization with context
- provide device abstraction

---

# Strategy Pattern

Executor uses the **Strategy Pattern** to separate:

- control logic
- hardware implementation

Example strategies:

```
RelayExecutor
PWMExecutor
StepperExecutor
SerialDeviceExecutor
```

This allows the same logic layer to control many types of devices.

---

# ExecutorStateBridge

File:

```
ExecutorStateBridge.hpp
```

Purpose:

Connects **Executor strategies** with the **GlobalState system**.

Responsibilities:

- read control flags from GlobalState
- convert them into device commands
- update device status back into GlobalState

Example flow:

```
GlobalState command
      │
      ▼
ExecutorStateBridge
      │
      ▼
Executor strategy
      │
      ▼
Hardware module
```

This component ensures **synchronization between system logic and physical devices**.

---

# Device Control Module

File:

```
EX_DeviceControlModule.hpp
```

Represents low-level hardware interaction.

Typical responsibilities:

- send commands over UART
- control GPIO pins
- manage communication with external device boards

Example devices:

```
Relay board
Motor driver
Pump controller
Lighting controller
Cooling system
```

This module hides hardware communication details from the executor strategies.

---

# Execution Flow

Typical command flow:

```
Scheduler
   │
   ▼
Logic Module
   │
   ▼
GlobalState command change
   │
   ▼
ExecutorStateBridge detects change
   │
   ▼
Executor Strategy executes command
   │
   ▼
DeviceControlModule sends hardware command
```

---

# Executor Strategy Lifecycle

## 1. Registration

Strategies are registered inside Executor manager.

Example:

```
executor.emplace<RelayExecutor>("pump");
executor.emplace<FanExecutor>("cooler");
```

---

## 2. Initialization

Executor manager initializes strategies:

```
executor.init(ctx);
```

Context may contain:

- configuration
- device drivers
- communication modules

---

## 3. Execution

Execution may be triggered by:

- Scheduler
- Logic system
- GlobalState changes

Example:

```
executor.tick();
```

---

# Example Usage

Example system setup:

```
Executor executor;

executor.emplace<RelayExecutor>("pump");
executor.emplace<RelayExecutor>("heater");
executor.emplace<PWMExecutor>("fan");

executor.init(ctx);
```

Logic layer updates state:

```
GlobalState::set("pump", true);
```

ExecutorBridge converts that to device command.

---

# Advantages of the Architecture

✔ Clear separation of device logic and control logic

✔ Modular actuator system

✔ Easy to add new hardware devices

✔ Compatible with Scheduler and GlobalState

✔ Supports different hardware interfaces

---

# Limitations

Current limitations may include:

- no built-in retry logic
- limited error reporting
- no actuator health state tracking
- no execution statistics

---

# Possible Improvements

## Device health monitoring

```
enum class DeviceState
{
    OK,
    ERROR,
    DISCONNECTED
};
```

---

## Command queue

Allow buffered execution of commands for slow devices.

---

## Execution statistics

Track:

- command latency
- failure count
- last execution time

---

# Role in GreenHouse System

Executor subsystem represents the **actuation layer** of the system.

It is responsible for controlling:

- heaters
- irrigation pumps
- ventilation fans
- lighting systems
- cooling devices

Together with **DataGetter**, it forms the core feedback loop:

```
Sensors → DataGetter → Logic → Executor → Devices
```

---

Generated: 2026-03-17T00:00:08.737042+00:00