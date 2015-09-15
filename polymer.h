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

  void apply(void *) {}

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

  void resize(int shards, size_t vertices) {
    n_shards_ = shards;
    n_vertices_ = vertices;
    shardEdges.resize(shards);
    shardVertices.resize(shards);
    shardData.resize(shards);
    shardActive.resize(shards);
    shardFrontiers.resize(shards);
  }

  size_t vertices_of_shard(int shard) {
    return (n_vertices_ / n_shards_) + (n_vertices_ % n_shards_ < shard);
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
  decltype(auto) parallel_shards(int priority, std::shared_ptr<AbstractTaskGroup> tg, F&& f) {
    tg->start(n_shards_, std::forward<F>(f));
    defaultPool.queue(priority, std::move(tg));
    // tg();
    // TODO: wait for tg done
  }

  template <typename F>
  decltype(auto) parallel_shards(F&& f) {
    auto tg = std::make_shared<TaskGroup<Empty>>();
    parallel_shards(INT_MAX, tg, std::forward<F>(f));
    tg->wait();
  }

  void activeAll() {
    parallel_shards([&](int shard) {
      auto active = static_cast<BitSet*>(shardActive[shard].lockSeq());
      active->set();
      shardActive[shard].unlockSeq();
    });
    puts("THE END");
  }


 private:
  int n_shards_;
  int n_vertices_;

  std::vector<FileBuffer> shardVertices;
  std::vector<FileBuffer> shardEdges;
  std::vector<FileBuffer> shardData;
  std::vector<FileBuffer> shardActive;
  std::vector<FileBuffer> shardFrontiers;

  template <typename TG, typename GraphT, typename F>
  friend void vertexMap(std::shared_ptr<TG>, GraphT& g, const F& f);

  template <typename HashF, typename EdgeIterT, typename GraphT>
  friend void initGraph(GraphT& g, size_t shards, size_t vertices, EdgeIterT first, EdgeIterT last, HashF f);
  
  template <typename ViewT, typename TG, typename GraphT, typename F>
  friend void edgeMap(std::shared_ptr<TG>, GraphT& g, const F& f);
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
  g.resize(shards, vertices);
  for (int shard = 0; shard < shards; shard++) {
  // g.parallel_shards([&g, vertices](int shard) {
    auto vertices = g.vertices_of_shard(shard);
    g.shardData[shard].resize(vertices * GraphT::vertex_data_size);

    auto asize = BitSet::allocate_size(vertices);

    g.shardActive[shard].resize(asize);
    auto active = static_cast<BitSet*>(g.shardActive[shard].lockSeq());
    std::cout << "vertices: " << vertices << std::endl;
    active->resize(vertices);
    // assert(active->size() == vertices);
    // active->set();
    g.shardActive[shard].unlockSeq();

    g.shardFrontiers[shard].resize(asize);
    auto frontiers = static_cast<BitSet*>(g.shardFrontiers[shard].lockSeq());
    frontiers->resize(vertices);
    g.shardFrontiers[shard].unlockSeq();
  } // );

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

template <typename TG = Empty, typename GraphT, typename F>
void vertexMap(std::shared_ptr<TG> tg, GraphT& g, const F& f) {
  g.parallel_shards(INT_MAX, tg, [&](int shard) {
    auto frontiers = static_cast<BitSet*>(g.shardFrontiers[shard].lockSeq());
    frontiers->clear();
    g.shardFrontiers[shard].unlockSeq();

    auto data = static_cast<typename GraphT::vertex_data_type*>(g.shardData[shard].lockSeq());
    auto active = static_cast<BitSet*>(g.shardActive[shard].lockSeq());
    active->for_each([=](int i) {
      // TODO: lock data on page boundaries
      if (!f(data[i]))
        active->unset(i);
    });
    g.shardData[shard].unlockSeq();
    g.shardActive[shard].unlockSeq();
  });
  tg->wait();
}

template <typename GraphT, typename F>
void vertexMap(GraphT& g, const F& f) {
  vertexMap(std::make_shared<TaskGroup<Empty>>(), g, f);
}

/*
template <typename GraphT, typename F>
void vertexMapPrepareEdgeMap(GraphT g, const F& f) {
  g.parallel_shards([&](int shard) {
    auto data = static_cast<typename GraphT::vertex_data_type*>(g.shardData[shard].lockSeq());
    auto frontiers = static_cast<BitSet*>(g.shardActive[shard].lockSeq());
    auto frontiers = static_cast<BitSet*>(g.shardFrontiers[shard].lockSeq());
    assert(frontiers->size() == 0);
    frontiers->clear();
    frontiers->for_each([=](int i) {
      // TODO: lock data on page boundaries
      if (!f(data[i]))
        frontiers->unset(i);
    });
    g.shardData[shard].unlockSeq();
    g.shardActive[shard].unlockSeq();
  });
}*/

template <typename ViewT>
class ShardView {
 public:
  struct task_data_type {
    size_t vertices;
    typename ViewT::data_type *data;
    BitSet *frontiers;
  };

  ShardView(TaskGroup<ShardView> *tg) : 
    views(tg->data().vertices), 
    frontierView(BitSet::create(views.size())) { }

  void apply(TaskGroup<ShardView> *tg) const {
    auto&& data = tg->data();
    tg->reduce([views = std::move(views), 
                frontierView = std::move(frontierView),
                data = data.data, frontiers = data.frontiers]() {
      for (int i = 0; i < views.size(); i++)
        views[i].apply(data[i]);
      
      *frontiers |= *frontierView;
    });
  }

  ~ShardView() { delete frontierView; }

  std::vector<ViewT> views;
  BitSet *frontierView;
};

template <typename ViewT, typename TG = Empty, typename GraphT, typename F>
void edgeMap(std::shared_ptr<TG> tg, GraphT& g, const F& f) {
  g.parallel_shards(INT_MAX, tg, [&](int shard) {
    auto localData = static_cast<typename GraphT::vertex_data_type*>(g.shardData[shard].lockSeq());
    auto frontiers = static_cast<BitSet*>(g.shardFrontiers[shard].lockSeq());
    auto edges = g.shardEdges[shard].lockSeq();
    auto esize = g.shardEdges[shard].size();
    auto vertices = static_cast<typename GraphT::vertex_id_type*>(g.shardVertices[shard].lockSeq());
    auto nVertices = g.vertices_of_shard(shard);

    auto tg = std::make_shared<TaskGroup<ShardView<ViewT>>>(
        typename ShardView<ViewT>::task_data_type{g.vertices_of_shard(shard), localData, frontiers});

    g.parallel_shards(0, tg, [=, &g](int remoteShard) {
      auto active = static_cast<BitSet*>(g.shardActive[remoteShard].lockSeq());
      auto remoteData = static_cast<typename GraphT::vertex_data_type*>(g.shardData[remoteShard].lockSeq());
      auto& views = tg->view().views;
      auto& frontierView = tg->view().frontierView;

      active->for_each([=, &g, &views, &frontierView](size_t i) {
        void *first = reinterpret_cast<char *>(edges) + vertices[i],
             *last = reinterpret_cast<char *>(edges) + (i < nVertices ? vertices[i + 1] : esize);

        auto data = remoteData[i];
        
        // for each edge e
        while (first != last) {
          typename GraphT::edge_data_type edge_data;
          auto j = g.get_edge(first, edge_data);
          if (f(data, edge_data, views[j]))
            frontierView->set(j);
        }
      });
    });
    /*
    tg->wait();
    g.shardData[shard].unlockSeq();
    g.shardActive[shard].unlockSeq();*/
  });
  tg->wait();
}

template <typename ViewT, typename GraphT, typename F>
void edgeMap(GraphT& g, const F& f) {
  edgeMap<ViewT>(std::make_shared<TaskGroup<Empty>>(), g, f);
}
#endif
