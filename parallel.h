#ifndef PARALLEL_H
#define PARALLEL_H

#include <algorithm>
#include <utility>
#include <future>
#include <map>
#include <assert.h>
#include <queue>
#include <list>

#include "utils.h"


class AbstractTask {
 public:
  virtual void operator()() = 0;
  virtual ~AbstractTask() { }
};

class AbstractTaskGroup : public AbstractTask {
 public:
  virtual void start(int shards, std::function<void(int)> task) = 0;
};

template <typename F>
class GenericTask : AbstractTask {
 public:
  template <typename... Args>
  GenericTask(Args&&... args) : f(std::forward<Args>(args)...) { }

  void operator()() { f(); }

 private:
  F f;
};

class Task : std::unique_ptr<AbstractTask> {
  using unique_ptr::unique_ptr;

  void operator()() {
    // tasks can only run once
    std::unique_ptr<AbstractTask> task = std::move(*this);
    assert(task);
    (*task)();
  }
};

template <typename ViewT>
class TaskGroup : public AbstractTaskGroup {
 public:
  typedef ViewT task_view_type;
  typedef typename ViewT::task_data_type task_data_type;

  template <typename... Args>
  TaskGroup(Args&&... args) : task_data(std::forward<Args>(args)...), exiting(false) {
  }
  
  void start(int shards, std::function<void(int)> task) override {
    assert(!this->task);
    this->shards = shards;
    this->task = std::move(task);
  }

  void operator()() override {
    // re-entry
    auto shard = next_shard++;

    if (shard > shards) return;  // nothing to do

    task_view_type view(this);  // create view
    auto task = this->task;     // copy task to local stack
    views[this] = &view;

    while (shard < shards) {
      auto nextShard = next_shard++;  // prefetch
      // TODO: do prefetch for nextTask
      task(shard);
      shard = nextShard;
    }

    views.erase(this);
  }

  task_data_type& data() {
    return task_data;
  }

  task_view_type& view() {
    return *views[this];
  }

  template <typename ReduceF>
  void reduce(ReduceF&& f) {
    if (++nreducer != 1) {
      // reducer running, do enqueue
      std::lock_guard<std::mutex> lock(mut);
      if (!exiting) {
        reducers.emplace_back(std::forward<ReduceF>(f));
        return;
      }
      // run instead of exiting thread
      exiting = false;
    }

    f();
    ++shards_done;
    while (--nreducer) {
      // reducer in queue
      std::function<void()> f;
      {
        std::lock_guard<std::mutex> lock(mut);
        if (reducers.empty()) {
          exiting = true;
          return;
        }
        f = std::move(reducers.front());
        reducers.pop_front();
      }
      f();
      ++shards_done;
    }

    if (shards_done == shards)
      all_done.set_value();
  }
  
  void lock() { mut.lock(); }
  void unlock() { mut.unlock(); }
  void wait() { all_done.get_future().get(); }

  bool done() { return next_shard < shards; }

 private:
  // static thread_local std::map<TaskGroup *, task_view_type *> views;
  static thread_local std::map<TaskGroup<ViewT> *, typename TaskGroup<ViewT>::task_view_type *> /*TaskGroup<ViewT>::*/views;

  std::function<void(int)> task;
  task_data_type task_data;

  int shards;
  int shards_done;

  std::atomic<int> next_shard;
  std::atomic<int> nreducer;
  std::promise<void> all_done;

  std::mutex mut;
  std::list<std::function<void(int)>> reducers;

  bool exiting;
};

template <typename ViewT>
thread_local std::map<TaskGroup<ViewT> *, typename TaskGroup<ViewT>::task_view_type *> TaskGroup<ViewT>::views;

class ThreadPool {
  template<typename Promise, typename Function, typename... Args>
  void asyncProc(Promise&& p, Function&& f, Args&&... args) {
    try {
      p.set_value(std::forward<Function>(f)(std::forward<Args>(args)...));
    } catch (...) {
      p.set_exception();
    }
  }

  struct PriorityComparer {
    template <typename U, typename V>
    bool operator()(U&& u, V&& v) {
      return u.first < v.first;
    }
  };

 public:
  /*
  template<typename Function, typename... Args>
  std::future<typename std::result_of<Function(Args...)>::type>
    async(Function&& f, Args&&... args) {
      std::promise<typename std::result_of<Function(Args...)>::type> p;
      auto fu = p.get_future();
      queue(0, Task{std::bind(asyncProc, std::forward<Function>(f), std::forward<Args>(args)...)});
      return fu;
  }
  */

  ThreadPool();
  ~ThreadPool();
  void queue(int priority, AbstractTaskGroup *);

 private:
  // queues
  static thread_local ThreadPool *pool;
  std::mutex mut;
  std::condition_variable cond;
  std::priority_queue<std::pair<int, AbstractTaskGroup *>, std::vector<std::pair<int, AbstractTaskGroup *>>, PriorityComparer> tasks;
  std::vector<std::thread> threads;
};

extern ThreadPool defaultPool;

#if defined(CILK)
#  include <cilk/cilk.h>
#  define parallel_for cilk_for
#elif defined(OPENMP)
#  include <omp.h>
#  define parallel_for _Pragma("omp parallel for") for
#endif

template <typename T, typename F>
F parallel_for_each_iter(T first, T last, F f) {
#ifdef CILK
  cilk_for(T curr = first; curr != last; ++curr)
    f(curr);
#else
#  if defined(OPENMP)
#    pragma omp parallel for
#  endif
  for(T curr = first; curr != last; ++curr)
    f(curr);
#endif

  return std::move(f);
}

template <typename T, typename F>
F parallel_for_each(T first, T last, F f) {
  return std::move(parallel_for_each_iter(std::move(first),
                                          std::move(last),
                                          [&f](T& c) { return f(c); }));
}

template <typename F>
F parallel_shards(int shards, F f) {
  return parallel_for_each_iter(0, shards, std::move(f));
}

#endif
