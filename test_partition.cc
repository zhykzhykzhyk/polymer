#include "polymer.h"
#include "utils.h" 

class Vertex {
  typedef unsigned long vertex_id_type;
  vertex_id_type id;
};

Edge edges[] {
  {0, 1},
  {0, 3},
  {1, 3},
  {2, 1},
};

PolymerGraph<Edge, Vertex> graph;

int main() {
  try {
    initGraph(graph, 2, edges, edges + sizeof(edges) / sizeof(*edges));
  } catch (OSError e) {
    errno = e.err;
    perror(e.reason);
  }
}
