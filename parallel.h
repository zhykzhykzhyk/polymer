#ifndef PARALLEL_H
#define PARALLEL_H

#include <algorithm>
#include <utility>

#if defined(USE_CILK)
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
  return parallel_for_each_iter(std::move(first),
                                std::move(last),
                                [&f](T& c) { return f(c); });
}

#endif
