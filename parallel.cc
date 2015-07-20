#include "parallel.h"
#include <numa.h>

void ThreadPool::queue(int priority, AbstractTaskGroup *tg) {
  std::lock_guard<std::mutex> lock(mut);
  tasks.emplace(priority, tg);
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
    });
  }
}

ThreadPool::~ThreadPool() {
  for (auto& t : threads)
    t.join();
}

ThreadPool defaultPool;
thread_local ThreadPool *ThreadPool::pool;
