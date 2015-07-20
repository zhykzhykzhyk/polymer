/*
 * 
 */

#ifndef POLYMER_H
#define POLYMER_H
#include <cstdint>
#include <cstddef>
#include <climits>
#include <vector>

#include "parallel.h"
#include "io.h"
#include "utils.h"

struct Empty {
  Empty() = default;

  template <typename... Args>
  Empty(Args&&... ) {}

  typedef Empty task_data_type;
};

struct Edge {
  typedef unsigned long vertex_id_type;
  typedef Empty edge_data_type;

  vertex_id_type from;
  vertex_id_type to;
};

template <typename WeightT = unsigned int>
struct WeightedEdge {
  typedef unsigned long vertex_id_type;
  typedef WeightT edge_data_type;

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
    op.update(*value, v);
  }

 private:
  T* value;
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
class Frontiers {
 public:
  Frontiers();

 private:
  unsigned long *bitset;
  typename VertexT::vertex_id_type *vlist;
  size_t size;
  bool isDense;
};
/*
template <typename GraphT, typename F>
void vertexMap(GraphT& g, const F& f);

template <typename HashF, typename EdgeIterT, typename GraphT>
void initGraph(GraphT& g, size_t shards, size_t vertices, EdgeIterT first, EdgeIterT last, HashF f);
*/
template <typename T>
constexpr size_t SizeOf() { return sizeof(T); }

template <>
constexpr size_t SizeOf<void>() { return 0; }

template <typename EdgeT, typename VertexT>
class PolymerGraph {
 public:
  typedef typename VertexT::vertex_data_type vertex_data_type;
  constexpr static size_t vertex_data_size = SizeOf<vertex_data_type>();
  typedef typename VertexT::vertex_id_type vertex_id_type;
  typedef typename EdgeT::edge_data_type edge_data_type;

  PolymerGraph() = default;
  PolymerGraph(const PolymerGraph&) = delete;

  void resize(int shards) {
    n_shards_ = shards;
    shardEdges.resize(shards);
    shardVertices.resize(shards);
    shardData.resize(shards);
    shardFrontiers.resize(shards);
    shardNextFrontiers.resize(shards);
  }

  int n_shards() const { return n_shards_; }

  void put_edge(uint16_t shard, uint32_t offset, EdgeT data) {
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

  uint32_t get_edge(void *&buffer, edge_data_type& data) const {
    auto j = *reinterpret_cast<uint32_t *&>(buffer)++;
    memcpy(&data, buffer, sizeof(data));
    reinterpret_cast<char *&>(buffer) += sizeof(data);
    return j;
  }

  template <typename F>
  decltype(auto) parallel_shards(int priority, AbstractTaskGroup& tg, F&& f) {
    tg.start(n_shards_, std::forward<F>(f));
    defaultPool.queue(priority, &tg);
    // tg();
    // TODO: wait for tg done
  }

  template <typename F>
  decltype(auto) parallel_shards(F&& f) {
    TaskGroup<Empty> tg;
    parallel_shards(INT_MAX, tg, std::forward<F>(f));
  }


 private:
  int n_shards_;
  std::vector<FileBuffer> shardVertices;
  std::vector<FileBuffer> shardEdges;
  std::vector<FileBuffer> shardData;
  std::vector<FileBuffer> shardFrontiers;
  std::vector<FileBuffer> shardNextFrontiers;

  template <typename GraphT, typename F>
  friend void vertexMap(GraphT& g, const F& f);

  template <typename HashF, typename EdgeIterT, typename GraphT>
  friend void initGraph(GraphT& g, size_t shards, size_t vertices, EdgeIterT first, EdgeIterT last, HashF f);
  
  template <typename ViewT, typename GraphT, typename F>
  friend void edgeMap(GraphT& g, const F& f);
};

class DefaultHashF {
 public:
  DefaultHashF(size_t shards) : shards(shards), vertex_per_shard{}  { }
  
  std::pair<uint16_t, uint32_t> hash(size_t v) {
    // shuffle kills locality
    return { v % shards, v / shards };
    // return { vertex_per_shard, v % vertex_per_shard };
  }
/*
  size_t unhash(uint16_t x, uint32_t y) {
    return x + y * shards;
  }
*/
 private:
  const size_t shards;
  const size_t vertex_per_shard;
};

template <typename HashF, typename EdgeIterT, typename GraphT>
void initGraph(GraphT& g, size_t shards, size_t vertices, EdgeIterT first, EdgeIterT last, HashF f = HashF{}) {
  g.resize(shards);
  auto vertices_per_shard = (vertices + shards - 1) / shards;
  g.parallel_shards([&](int shard) {
    g.shardData[shard].resize(vertices_per_shard * GraphT::vertex_data_size);
    g.shardFrontiers[shard].resize(BitSet::allocate_size(vertices_per_shard));
    g.shardNextFrontiers[shard].resize(BitSet::allocate_size(vertices_per_shard));
  });

  // need external sorting if parallel
  for (; first != last; ++first) {
    int shard, offset;
    std::tie(shard, offset) = f.hash(first->to);
    g.put_edge(shard, offset, *first);
  }
} 

template <typename HashF = DefaultHashF, typename EdgeIterT, typename GraphT>
void initGraph(GraphT& g, size_t shards, size_t vertices, EdgeIterT first, EdgeIterT last) {
  initGraph(g, shards, vertices, first, last, HashF{shards});
}

template <typename GraphT, typename F>
void vertexMap(GraphT& g, const F& f) {
  g.parallel_shards([&](int shard) {
    auto data = static_cast<typename GraphT::vertex_data_type*>(g.shardData[shard].lockSeq());
    auto frontiers = static_cast<BitSet*>(g.shardFrontiers[shard].lockSeq());
    frontiers->for_each([=](int i) {
      // TODO: lock data on page boundaries
      if (!f(data[i]))
        frontiers->unset(i);
    });
    g.shardData[shard].unlockSeq();
    g.shardFrontiers[shard].unlockSeq();
  });
}

template <typename GraphT, typename F>
void vertexMapPrepareEdgeMap(GraphT g, const F& f) {
  g.parallel_shards([&](int shard) {
    auto data = static_cast<typename GraphT::vertex_data_type*>(g.shardData[shard].lockSeq());
    auto frontiers = static_cast<BitSet*>(g.shardFrontiers[shard].lockSeq());
    auto nextFrontiers = static_cast<BitSet*>(g.shardNextFrontiers[shard].lockSeq());
    nextFrontiers->clear();
    frontiers->for_each([=](int i) {
      // TODO: lock data on page boundaries
      if (!f(data[i]))
        frontiers->unset(i);
    });
    g.shardData[shard].unlockSeq();
    g.shardFrontiers[shard].unlockSeq();
  });
}

template <typename ViewT>
class ShardView {
 public:
  typedef size_t task_data_type;

  ShardView(TaskGroup<ShardView> *tg) : views(tg->data()), tg(tg) { }

  void apply(typename ViewT::data_type *data) {
    tg->reduce([views = std::move(views), data]() {
      for (int i = 0; i < views.size(); i++)
        views[i].apply(data[i]);
    });
  }

  std::vector<ViewT> views;

 private:
  TaskGroup<ShardView> * const tg;
};

template <typename ViewT, typename GraphT, typename F>
void edgeMap(GraphT& g, const F& f) {
  g.parallel_shards([&](int shard) {
    auto localData = static_cast<typename GraphT::vertex_data_type*>(g.shardData[shard].lockSeq());
    auto nextFrontiers = static_cast<BitSet*>(g.shardNextFrontiers[shard].lockSeq());
    auto edges = g.shardEdges[shard].lockSeq();
    auto esize = g.shardEdges[shard].size();
    auto vertices = static_cast<typename GraphT::vertex_id_type*>(g.shardVertices[shard].lockSeq());
    auto nVertices = g.shardVertices[shard].size() / sizeof(*vertices);

    TaskGroup<ShardView<ViewT>> tg(g.shardData[shard].size() / sizeof(*localData));

    g.parallel_shards(0, tg, [&](int remoteShard) {
      auto frontiers = static_cast<BitSet*>(g.shardFrontiers[remoteShard].lockSeq());
      auto remoteData = static_cast<typename GraphT::vertex_data_type*>(g.shardData[remoteShard].lockSeq());
      auto& views = tg.view().views;

      frontiers->for_each([=, &g](size_t i) {
        void *first = reinterpret_cast<char *>(edges) + vertices[i],
             *last = reinterpret_cast<char *>(edges) + (i < nVertices ? vertices[i + 1] : esize);

        auto data = remoteData[i];
        
        // for each edge e
        while (first != last) {
          typename GraphT::edge_data_type edge_data;
          auto j = g.get_edge(first, edge_data);
          if (f(data, edge_data, views[j]))
            nextFrontiers->set(j);
        }
      });
    });
    g.shardData[shard].unlockSeq();
    g.shardFrontiers[shard].unlockSeq();
  });
}
#endif
