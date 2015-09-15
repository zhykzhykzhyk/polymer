#include "polymer.h"

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

int main() {
  constexpr int nVertices = 4;
  constexpr double damping = 0.85, epsilon = 0.0000001;
  try {
    typedef Vertex::vertex_data_type DT;
    initGraph(graph, 2, nVertices, edges, edges + sizeof(edges) / sizeof(*edges));
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
      vertexMap(graph, [tg](DT& v) {
        auto next = v.next * damping + (1 - damping) / nVertices;
        tg->view() += fabs(next - v.curr);
        v.curr = next;
        v.next = 0;
        return true;
      });
      tg->wait();
      if (tg->data() < epsilon) break;
    } while (1);
  } catch (OSError e) {
    errno = e.err;
    perror(e.reason);
  }
}
