#include "parallel.h"
#include <numa.h>

struct Exit : AbstractTaskGroup {
  void operator()() override { throw this; }
  void start(int, std::function<void(int)>) override { }
  bool done() const override { return false; }
};

void ThreadPool::queue(int priority, std::shared_ptr<AbstractTaskGroup> tg) {
  if (pool) --priority;

  {
    std::lock_guard<std::mutex> lock(mut);
    tasks.emplace(priority, tg);
    puts("NEW TASK");
  }

  cond.notify_one();
  if (pool) puts("pool task");
  if (pool) (*tg)();
}

ThreadPool::ThreadPool() {
  int ncpus = numa_num_configured_cpus();
  for (int i = 0; i < ncpus; i++) {
    // one thread per cpu
    int node = numa_node_of_cpu(i);
    threads.emplace_back([=](){
      pool = this;
      auto nodemask = numa_allocate_nodemask();
      numa_bitmask_setbit(nodemask, node);
      numa_bind(nodemask);
      numa_free_nodemask(nodemask);
      auto cpuset = CPU_ALLOC(ncpus);
      CPU_SET(i, cpuset);
      sched_setaffinity(0, ncpus, cpuset);
      CPU_FREE(cpuset);
      std::unique_lock<std::mutex> lock(mut);
      std::shared_ptr<AbstractTaskGroup> tg;
      while (1) {
        printf("%3d: ", i);
        puts("WAIT TASK");
        while (tasks.empty()) {
          cond.wait(lock);
          printf("%3d: ", i);
          puts("AGAIN WAIT TASK");
        }
        printf("%3d: ", i);
        puts("GOT TASK");
        int priority;
        std::tie(priority, tg) = tasks.top();
        tasks.pop();
        if (!tg->done()) {
          printf("%3d: ", i);
          puts("RUN TASK");
          tasks.emplace(priority - 1, tg);
          lock.unlock();
          cond.notify_one();
          try {
            (*tg)();
          } catch (Exit *e) {
            return;
          }
          lock.lock();
          printf("%3d: ", i);
          puts("DONE");
        } else {
          printf("%3d: ", i);
          puts("REMOVE");
        }
      }
    });
  }
}

ThreadPool::~ThreadPool() {
  std::shared_ptr<Exit> e(new Exit);
  queue(-threads.size(), e);
  for (auto& t : threads)
    try {
      t.join();
    } catch (...) {
    }
}

ThreadPool defaultPool;
thread_local ThreadPool *ThreadPool::pool;
