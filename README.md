# C++ Multithreaded Task Scheduler

A **C++20-based multithreaded task scheduler** built from first principles to explore **concurrency correctness, task scheduling, and system-level trade-offs**.

---

## Problem

Most developers use abstractions like thread pools without understanding:

* How tasks are scheduled across threads
* How synchronization impacts performance
* What breaks under contention and shutdown

This leads to systems that:

* Work in simple cases
* Fail under **high load, race conditions, or improper shutdown**

---

## Approach

This project builds a **task scheduler from scratch**, focusing on:

### Core Goals

* Correctness under **multi-producer, multi-consumer concurrency**
* Clear **state-driven lifecycle management**
* Explicit handling of **synchronization and coordination**

---

## System Architecture

```text
Client
   |
   v
ThreadPool / Scheduler
   |
   v
SafeQueue (MPMC)
   |
   v
Worker Threads
   |
   v
OS Threads
```

---

## Core Components

## 1. SafeQueue< T >

* Thread-safe **MPMC queue**
* Uses `std::mutex` + `std::condition_variable`

### Responsibilities

* Safe task submission from multiple producers
* Blocking workers when queue is empty
* Coordinated wake-up on new tasks

### Current Limitation

* **Unbounded queue** -> no backpressure yet

---

## 2. ThreadPool

* Manages a fixed set of worker threads
* Dispatches tasks to workers

### Responsibility

* Task scheduling
* Thread lifecycle management
* Coordinating shutdown

---

## 3. Task System

* Uses `std::function<void()>` for task abstraction

### Why

* Allows flexible submission of any callable

### Trade-off

* Potential heap allocation and indirect call overhead

---

## Execution Model

1. Producers submit tasks -> pushed into queue
2. Workers wait on condition variable
3. On notification:

   * Worker wakes up
   * Pops task
   * Executes

---

## State Management

The scheduler follows a **state-driven lifecycle**:

* **RUNNING** -> Accept tasks, workers active
* **STOPPING** -> Stop accepting tasks, drain queue
* **STOPPED** -> All workers terminated

---

## Current System Behavior (Important)

### Under High Load

* Tasks accumulate in queue (unbounded)
* Memory usage increases with task volume
* Workers process tasks at fixed rate

### Implication

* System correctness is maintained
* But **no protection against overload yet**

---

## Design Trade-offs

| Decision            | Benefit              | Cost                            |
| ------------------- | -------------------- | ------------------------------- |
| Mutex + CV queue    | Simpler, correct     | Lock contention                 |
| Unbounded queue     | No producer blocking | Memory growth under load        |
| std::function tasks | Flexible API         | Allocation + indirect call cost |
| Fixed thread pool   | Stable, predictable  | No dynamic scaling              |

---

## Key Engineering Focus

This project focuses on:

* **Race condition avoidance**
* Correct use of **condition variables**
* Understanding **lock contention**
* Designing safe **shutdown semantics**

---

## Work in Progress

The following are actively being developed:

* Backpressure (bounded queue / rejection policy)
* Benchmarking (throughput, latency)
* Stress testing under high concurrency
* Work-stealing scheduler
* Priority-based scheduling

---

## Project Structure

```text
src/
  core/
    SafeQueue.hpp
    ThreadPool.hpp
    Worker.hpp
  task/
    Task.hpp
    Result.hpp
  scheduler/
    PriorityScheduler.hpp
  utils/
    Logger.hpp

tests/
  StressTest.cpp
```

---

## Impact (What This Project Demonstrates)

* Ability to design **concurrent systems from scratch**
* Understanding of **thread coordination and synchronization**
* Awareness of **real-world failure modes (contention, overload, shutdown)**

---

## Final Note

This project is not about using threads.

It is about:

> building systems that remain correct under concurrency, stress, and failure - and understanding where they break.
