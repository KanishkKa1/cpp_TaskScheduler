#pragma once

class ThreadPool; // forward declaration

// Thread-local worker identifier (declared only)
extern thread_local int worker_id;

// =======================
// Worker
// =======================
class Worker {
  private:
    ThreadPool &thread_pool_;
    int id_;

  public:
    explicit Worker(ThreadPool &thread_pool, int id);

    void operator()();
};