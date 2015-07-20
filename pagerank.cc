#include "polymer.h"

struct Vertex {
  typedef unsigned long vertex_id_type;
  vertex_id_type id;

  struct vertex_data_type {
    double curr, next;
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
  void apply(data_type& data) {
    data.next += acc;
  }

  double acc;
};

int main() {
  constexpr int nVertices = 4;
  try {
    typedef Vertex::vertex_data_type DT;
    initGraph(graph, 2, nVertices, edges, edges + sizeof(edges) / sizeof(*edges));
    vertexMap(graph, [](DT& v) {
      v.curr = 1.0 / nVertices;
      v.next = 0;
      return true;
    });
    edgeMap<EdgeView>(graph, [](auto&& x, auto&& y, auto&& z) {
      return true;
    });
  } catch (OSError e) {
    errno = e.err;
    perror(e.reason);
  }
}
