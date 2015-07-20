#include "polymer.h"
#include "utils.h" 
#include <errno.h>

struct Vertex {
  typedef unsigned long vertex_id_type;
  vertex_id_type id;

  typedef void vertex_data_type;
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
    initGraph(graph, 2, 4, edges, edges + sizeof(edges) / sizeof(*edges));
  } catch (OSError e) {
    errno = e.err;
    perror(e.reason);
  }
}
