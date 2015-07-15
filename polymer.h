/*
 * 
 */

#ifndef POLYMER_H
#define POLYMER_H
#include <cstdint>
#include <cstddef>

#include "parallel.h"
#include "io.h"
#include "utils.h"

struct Edge {
  typedef unsigned long vertex_id_type;
  vertex_id_type from;
  vertex_id_type to;
};

template <typename WeightT = unsigned int>
struct WeightedEdge {
  typedef unsigned long vertex_id_type;
  vertex_id_type from;
  vertex_id_type to;
  union {
    WeightT weight;
    char edge_data;
  };
};

template <typename T, typename Q = int>
struct EdgeTraits {
 public:
  constexpr static size_t edge_data_offset = sizeof(T);
  constexpr static size_t edge_data_size = 0;
};

template <typename T>
struct EdgeTraits<T, decltype(T::edge_data, 0)> {
 public:
  constexpr static size_t edge_data_offset = offsetof(T, edge_data);
  constexpr static size_t edge_data_size = sizeof(T) - edge_data_offset;
};

template <typename T>
class ReducePlus {
  void update(T& x, const T& y) const {
    x += y;
  }
  
  void updateAtomic(volatile T* x, const T& y) const {
    __atomic_add_fetch(x, y, __ATOMIC_RELAXED);
  }

  T identity() const { return T{}; }
};

template <typename T, typename Operation>
class Reducer {
 public:
  Reducer(volatile T* value, Operation op = Operation{})
    : op(op), value(value) {
  }

  void reduce(const T& v) {
    op.updateAtomic(*value, v);
  }

 private:
  volatile T* value;
  Operation op;
};

template <typename T, typename Operation>
class SubReducer {
 public:
  SubReducer(Reducer<T, Operation>& red, Operation op = Operation{})
    : reducer(red), op(op), value(op.identity()) {
  }

  void reduce(const T& v) {
    op.update(value, v);
  }

  ~SubReducer() {
    reducer.reducer(value);
  }
 
 private:
  Reducer<T, Operation>& reducer;
  Operation op;
  T value;
};

template <typename VertexT>
class Frontier {
 public:
  Frontier();

 private:
  unsigned long *bitset;
  typename VertexT::vertex_id_type *vlist;
  size_t size;
  bool isDense;
};

template <typename EdgeT, typename VertexT>
class PolymerGraph {
 public:
  void resize(int shards) {
    n_shards_ = shards;
    shardEdges.resize(shards);
    shardVertices.resize(shards);
  }

  long n_shards() const { return n_shards_; }

  void put_edge(uint16_t shard, uint32_t offset, EdgeT data) {
    printf("put edge %d of %d: %d -> %d\n", shard, offset, data.from, data.to);
    auto& vertices = shardVertices[shard];
    auto& edges = shardEdges[shard];

    size_t vid = vertices.tell() / sizeof(size_t);
    size_t cur = edges.tell();
    while (vid <= data.from) {
      vertices.write(&cur, sizeof(size_t));
      ++vid;
    }
    vertices.write(&offset, sizeof(offset));
    edges.write(&offset, sizeof(offset));
    edges.write(
        reinterpret_cast<char *>(&data) + EdgeTraits<EdgeT>::edge_data_offset,
        EdgeTraits<EdgeT>::edge_data_offset);
  }

  Frontier<VertexT> *get_frontiers() { }
 private:
  long n_shards_;
  std::vector<FileBuffer> shardVertices;
  std::vector<FileBuffer> shardEdges;
  std::vector<FileBuffer> shardData;
  std::vector<FileBuffer> shardFrontiers;
  std::vector<FileBuffer> shardNextFrontiers;

  template <typename GraphT, typename F>
  friend void vertexMap(GraphT g, F f);
};

class DefaultHashF {
 public:
  DefaultHashF(size_t shards) : shards(shards)  { }
  std::pair<uint16_t, uint32_t> hash(size_t v) {
    return { v % shards, v / shards };
  }

  size_t unhash(uint16_t x, uint32_t y) {
    return x + y * shards;
  }

 private:
  const size_t shards;
};

template <typename HashF, typename EdgeIterT, typename GraphT>
void initGraph(GraphT& g, size_t shards, EdgeIterT first, EdgeIterT last, HashF f = HashF{}) {
  g.resize(shards);

  // need external sorting if parallel
  for (; first != last; ++first) {
    int shard, offset;
    std::tie(shard, offset) = f.hash(first->to);
    g.put_edge(shard, offset, *first);
  }
} 

template <typename HashF = DefaultHashF, typename EdgeIterT, typename GraphT>
void initGraph(GraphT& g, size_t shards, EdgeIterT first, EdgeIterT last) {
  initGraph(g, shards, first, last, HashF{shards});
}

template <typename GraphT, typename F>
void vertexMap(GraphT g, const F& f) {
  parallel_for_each_iter(0, g.n_shards(), [&](int shard) {
    auto data = static_cast<typename GraphT::vertex_data_type*>(g.shardData[shard].lockSeq());
    auto frontier = static_cast<BitSet*>(g.shardFrontier[shard].lockSeq());
    frontier->for_each([=](int i) {
      // TODO: lock data on page boundaries
      if (!localF(data[i]))
        frontier->unset(i);
    });
    g.shardData[shard].unlockSeq();
    g.shardFrontier[shard].unlockSeq();
  });
}

template <typename GraphT, typename F>
void edgeMap(GraphT g, F f) {
  parallel_for_each_iter(0, g.n_shards(), [&](int shard) {
    auto localdata = static_cast<typename GraphT::vertex_data_type*>(g.shardData[shard].lockSeq());
    auto frontier = static_cast<BitSet*>(g.shardFrontier[shard].lockSeq());

    for (int i = 0; i < g.n_shards(); ++i) {

    }
    frontier->for_each([=](int i) {
      if (!localF(localdata[i]))
        frontier->unset(i);
    });
    g.shardData[shard].unlockSeq();
    g.shardFrontier[shard].unlockSeq();
  });
}
#endif
