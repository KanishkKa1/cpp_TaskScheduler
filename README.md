# 🚀 C++ Multithreaded Task Scheduler

A high-performance, scalable **Multithreaded Task Scheduler** built from scratch using **modern C++20**, focusing on **concurrency correctness, performance, and system design principles**.

---

## 🎯 Motivation

Most developers *use* concurrency abstractions.

This project exists to:

* **build them from first principles**
* deeply understand **threading, synchronization, and scheduling**
* bridge the gap between **DSA knowledge → production systems engineering**

---

## 🧠 What This Project Demonstrates

* Designing **low-level concurrent primitives**
* Managing **thread lifecycle and coordination**
* Handling **race conditions, contention, and memory visibility**
* Building **scalable systems (10k+ tasks/sec mindset)**
* Thinking in **state machines and invariants**, not just code

---

## 🏗️ System Architecture

```text
Client (main / API)
        ↓
Scheduler Layer
  ├── ThreadPool
  ├── Work Stealing (planned)
  ├── Priority Scheduling (planned)
        ↓
Core Primitives
  ├── SafeQueue<T>
  ├── Worker Threads
        ↓
OS Threads / CPU
```

---

## ⚙️ Core Components

### 🔹 1. SafeQueue<T>

A thread-safe queue supporting:

* Multi-producer / multi-consumer
* Blocking + non-blocking operations
* Condition variable synchronization
* Graceful shutdown semantics

**Key challenges solved:**

* Spurious wakeups
* Lost wakeups
* Shutdown race conditions
* Minimizing lock contention

---

### 🔹 2. ThreadPool

Manages worker threads responsible for executing tasks.

**Responsibilities:**

* Efficient task dispatching
* Thread lifecycle management (`std::jthread`)
* Work distribution across cores

---

### 🔹 3. Task & Result System

Supports asynchronous execution using:

* `std::future`
* `std::promise`
* Type-erased task wrappers

---

### 🔹 4. Advanced Scheduling (Planned / Extensible)

* Priority-based scheduling (heap-backed)
* Work stealing for load balancing
* Timed / delayed task execution

---

## 🔥 Key Engineering Challenges

### 1. Race Conditions

Ensuring correctness under:

```text
Multiple producers + multiple consumers + shutdown
```

---

### 2. Lock Contention

* Avoiding global bottlenecks
* Minimizing critical section size

---

### 3. Condition Variables

Handling:

* Spurious wakeups
* Thundering herd problem
* Efficient wake-up strategies (`notify_one` vs `notify_all`)

---

### 4. Shutdown Semantics

Designing a correct state machine:

```text
RUNNING → STOPPING → STOPPED
```

---

### 5. Memory Ordering & Visibility

Understanding:

* Happens-before relationships
* Synchronization via mutex vs atomics

---

## 📊 Performance Mindset

Designed with:

* **High throughput** (10k+ tasks/sec target)
* **Low latency task dispatch**
* **Scalability across multi-core systems (8–64 cores)**

---

## 🧪 Build & Run

### Prerequisites

* GCC (C++20 support)
* CMake ≥ 3.20

---

### Build

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

---

### Run

```bash
./TaskScheduler.exe
```

---

## 📁 Project Structure

```text
cpp_TaskScheduler/
│
├── CMakeLists.txt
├── README.md
├── .gitignore
├── .gitattributes
│
├── src/
│   ├── main.cpp
│   │
│   ├── core/
│   │   ├── SafeQueue.hpp
│   │   ├── ThreadPool.hpp
│   │   ├── Worker.hpp
│   │
│   ├── task/
│   │   ├── Task.hpp
│   │   ├── Result.hpp
│   │
│   ├── scheduler/
│   │   ├── PriorityScheduler.hpp
│   │
│   ├── utils/
│       ├── Logger.hpp
│
└── tests/
    ├── StressTest.cpp
```

---

## 🧠 Key Learnings

### 🔹 1. Concurrency is about correctness, not speed

Naive implementations often:

* work in single-thread
* fail under contention

---

### 🔹 2. Locking is easy — designing around locks is hard

* Global mutex = bottleneck
* Fine-grained control = complexity trade-off

---

### 🔹 3. Condition variables are subtle

Incorrect usage leads to:

* deadlocks
* missed signals
* infinite waits

---

### 🔹 4. Shutdown is the hardest part

Handling:

* in-flight tasks
* waiting threads
* new task submissions

---

### 🔹 5. Atomics are not a silver bullet

* Work for simple state
* Break for complex structures (queues, graphs)

---

## ⚖️ Design Trade-offs

| Decision                 | Trade-off                             |
| ------------------------ | ------------------------------------- |
| Mutex-based queue        | Simpler, safe but contention-heavy    |
| Lock-free queue (future) | High performance, complex correctness |
| notify_one               | Efficient but requires careful logic  |
| notify_all               | Safe but causes wake-up storms        |

---

## 🚧 Future Improvements

* Lock-free queue implementation
* Work-stealing scheduler (per-thread queues)
* Backpressure handling
* Metrics (latency, throughput)
* Benchmarking against existing thread pools

---

## 🎯 Goal

To evolve into a **production-grade concurrent system**, while building:

* Strong systems intuition
* Deep understanding of concurrency primitives
* Interview-level reasoning for backend/system roles

---

## 📌 Final Note

This project is not about “making threads work”.

It is about:

> building systems that remain correct under stress, scale, and failure.
