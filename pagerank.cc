#include <iostream>
#include "polymer.h"
#include <sys/mman.h>
#include <numa.h>

struct Vertex {
  typedef unsigned long vertex_id_type;
  vertex_id_type id;

  struct vertex_data_type {
    double curr, next;
    size_t outDegree;
  };
};

Edge edges[] {
  {0, 1},
  {0, 3},
  {1, 3},
  {2, 1},
  {0, 23},
  {1, 0},
  {1, 2},
  {3, 2},
  {4, 3},
  {5, 4},
  {6, 5},
  {7, 6},
  {8, 7},
  {9, 8},
  {10, 9},
  {11, 10},
  {12, 11},
  {13, 12},
  {14, 13},
  {15, 14},
  {16, 15},
  {17, 16},
  {18, 17},
  {19, 18},
  {20, 19},
  {21, 20},
  {22, 21},
  {23, 22},
};

PolymerGraph<Edge, Vertex> graph;

struct EdgeView {
  EdgeView() : acc(0) { }

  typedef Vertex::vertex_data_type data_type;
  void apply(data_type& data) const {
    data.next += acc;
  }

  EdgeView& operator += (double v) { acc += v; return *this; }

  double acc;
};

struct DeltaView {
  typedef double task_data_type;
  DeltaView(TaskGroup<DeltaView> *tg) : acc{} { }

  void apply(TaskGroup<DeltaView> *tg) const {
    tg->reduce([tg, acc = acc] {
      tg->data() += acc;
    });
  }

  DeltaView& operator += (double v) { acc += v; return *this; }
  double acc;
};
/*
void * operator new(std::size_t n) throw(std::bad_alloc) {
  auto ptr = (std::size_t *)numa_alloc_local(n + sizeof(n));
  if (ptr == NULL) throw std::bad_alloc();
  *ptr = n + sizeof(n);
  return (std::size_t *)ptr + 1;
}

void operator delete(void *p) throw() {
  auto ptr = (std::size_t *)p;
  numa_free(ptr - 1, ptr[-1]);
}

void * operator new[](std::size_t n) throw(std::bad_alloc) {
  auto ptr = (std::size_t *)numa_alloc_local(n + sizeof(n));
  if (ptr == NULL) throw std::bad_alloc();
  *ptr = n + sizeof(n);
  return (std::size_t *)ptr + 1;
}

void operator delete[](void *p) throw() {
  auto ptr = (std::size_t *)p;
  numa_free(ptr - 1, ptr[-1]);
}
*/
int main(int argc, char *argv[]) {
  constexpr int nVertices = 0x4000000;
  constexpr double damping = 0.85, epsilon = 0.0000001;
  try {
    typedef Vertex::vertex_data_type DT;
    if (argc > 1)
    {
      File f;
      f.open(argv[1]);
      auto size = f.size();
      auto edges = (Edge *)mmap(NULL, size, PROT_READ, MAP_SHARED, f.get(), 0);
      madvise(edges, size, MADV_SEQUENTIAL);
      std::cout << "Size: " << size << std::endl;
      initGraph(graph, nVertices / 0x100000 + 1, nVertices, edges, edges + size / sizeof(*edges));
      std::cout << "Init complete!" << std::endl;
      munmap(edges, size);
    }
    else {
      // initGraph(graph, 2, 4, edges, edges + sizeof(edges) / sizeof(*edges));
      initGraph(graph, 24, 24, edges, edges + sizeof(edges) / sizeof(*edges));
    }
    graph.activeAll();
    vertexMap(graph, [](DT& v) {
      v.curr = 1.0 / nVertices;
      v.next = 0;
      return true;
    });
    do {
      edgeMap<EdgeView>(graph, [](auto&& x, auto&& y, auto&& z) {
        z += x.curr / x.outDegree;
        return true;
      });
      auto tg = std::make_shared<TaskGroup<DeltaView>>();
      printf("VM %p\n", tg.get());
      vertexMap(graph, [tg](DT& v) {
        auto next = v.next * damping + (1 - damping) / nVertices;
        tg->view() += fabs(next - v.curr);
        v.curr = next;
        v.next = 0;
        return true;
      });
      // tg->wait();
      if (tg->data() < epsilon) break;
    } while (1);
  } catch (OSError e) {
    errno = e.err;
    perror(e.reason);
  }
}
