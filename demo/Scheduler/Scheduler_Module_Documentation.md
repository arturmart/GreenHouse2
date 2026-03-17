
# Scheduler Module Documentation

## Overview
The Scheduler module is a lightweight task scheduling engine designed to manage delayed and periodic execution of functions in a multithreaded C++ application.

It separates:
- task scheduling
- task execution

This allows precise timing while still supporting parallel execution.

The scheduler uses:
- std::priority_queue for time-based scheduling
- boost::asio::thread_pool for parallel execution
- std::thread for a dispatcher loop
- std::mutex and std::condition_variable for synchronization

---

# Architecture

The module is composed of two main layers.

## 1. Dispatcher Layer

Responsible for:

- storing tasks
- determining which task should run next
- waiting until the correct time
- sending tasks to worker threads

Runs inside a single thread.

dispatcher thread → loop()

---

## 2. Execution Layer

Responsible for:

- executing user functions
- running tasks in parallel

Uses:

boost::asio::thread_pool

This allows many tasks to execute simultaneously while the dispatcher remains lightweight.

---

# High Level Flow

addDelayed / addPeriodic
        ↓
   addTask()
        ↓
 priority_queue (sorted by time)
        ↓
 dispatcher thread
        ↓
 if execution time reached
        ↓
 boost::asio::post()
        ↓
 worker thread pool
        ↓
 execute user function

---

# Core Data Structures

## TaskId

using TaskId = std::uint64_t;

Each task receives a unique identifier generated using:

std::atomic<TaskId> nextId_

This allows:
- safe multi-threaded creation
- cancellation by ID

---

## Fn

using Fn = std::function<void()>;

Represents the user function executed by the scheduler.

Example:

scheduler.addDelayed([]{
    // task code
}, std::chrono::seconds(1));

---

## Item

Internal representation of a scheduled task.

struct Item
{
    TimePoint when;
    TaskId id;
    Fn fn;
    Ms period;
    bool periodic;
    std::string name;
};

Fields:

| Field | Description |
|------|-------------|
| when | time when task must run |
| id | unique identifier |
| fn | function to execute |
| period | period for repeating tasks |
| periodic | whether task repeats |
| name | debug name |

---

# Task Queue

Tasks are stored in:

std::priority_queue<Item>

Sorted by execution time.

Earliest task always appears at the top.

Comparison operator:

bool operator<(Item const& other) const
{
    return when > other.when;
}

This converts the queue into a min-heap by time.

---

# Scheduler Lifecycle

## Initialization

Scheduler is implemented as a singleton.

Scheduler::instance()

Only one scheduler exists in the application.

---

## Task Scheduling

### Delayed task

addDelayed(fn, delay)

Executes once after delay.

Example:

scheduler.addDelayed(task, 200ms);

---

### Periodic task

addPeriodic(fn, period)

Runs repeatedly.

Example:

scheduler.addPeriodic(updateSensors, 1000ms);

---

# Execution Model

Periodic tasks use fixed-delay scheduling.

Meaning:

next_run = now + period

If a task takes longer to execute, the next execution is delayed accordingly.

Advantages:

- stable behavior
- avoids burst execution
- safer for IO tasks

Disadvantages:

- timing drift

---

# Cancellation System

Tasks are cancelled using:

cancel(TaskId id)

Internally:

std::unordered_set<TaskId> cancelSet_

The task remains in the queue but is ignored when encountered.

Advantages:

- avoids complex removal from priority queue
- fast operation

Limitations:

- cannot cancel tasks already running

---

# Threading Model

Threads used by scheduler:

| Thread | Responsibility |
|------|------|
| Dispatcher thread | scheduling logic |
| Worker threads | executing tasks |

Workers are managed by:

boost::asio::thread_pool

---

# Running Task Tracking

The scheduler records currently executing tasks.

std::unordered_map<TaskId, RunningMeta> running_

Contains:

- thread ID
- task name

This allows debugging and UI monitoring.

---

# Worker Identification

Each worker thread receives a stable index.

Example:

W0
W1
W2

Stored in:

std::unordered_map<std::thread::id, int> workerIndex_

Useful for:

- logs
- monitoring dashboards

---

# Stop Mechanism

Scheduler shutdown:

scheduler.stop()

Steps:

1. set stop flag
2. wake dispatcher
3. join dispatcher thread
4. join thread pool

This is graceful shutdown.

All already scheduled tasks will finish.

---

# Observability Tools

Scheduler exposes diagnostic methods:

listTasks()
listRunningDetailed()
debugDump()

These allow external monitoring systems or web interfaces to display:

- pending tasks
- running tasks
- worker usage

---

# Strengths of Architecture

✔ clear separation of scheduling and execution

✔ supports parallel task execution

✔ safe cancellation model

✔ lightweight timing system

✔ extensible design

✔ suitable for server or embedded applications

---

# Limitations

1. Cannot cancel already running tasks

2. Periodic tasks drift over time

3. No task priority besides time

4. No built-in statistics for task performance

5. Exceptions inside tasks are currently swallowed

---

# Possible Improvements

## Exception logging

catch(const std::exception& e)
{
    std::cerr << e.what();
}

---

## Task statistics

Possible structure:

struct TaskStats
{
    uint64_t runs;
    uint64_t failures;
    duration lastDuration;
};

---

## Fixed-rate scheduling

Alternative periodic logic:

next.when += next.period;

Used when precise timing is required.

---

# Typical Use Cases

Scheduler fits well for:

- sensor polling
- telemetry updates
- logging
- watchdog monitoring
- network maintenance tasks
- greenhouse automation systems

---

# Example Usage

auto& scheduler = Scheduler::instance();

scheduler.addPeriodic([]{
    // sensor reading
}, std::chrono::seconds(2));

scheduler.addDelayed([]{
    // startup event
}, std::chrono::seconds(5));

---

Generated: 2026-03-16T23:53:11.314988 UTC