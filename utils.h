#ifndef UTILS_H
#define UTILS_H
#include <new>
#include <utility>
#include <string.h>

class BitSet {
 public:
  void set(int i) { data[i / bits_per_long] |= 1 << (i % bits_per_long); }
  void set() { 
    memset(data, -1, size / bits_per_long);
    if (auto r = size % bits_per_long)
      data[size / bits_per_long] = (1 << r) - 1;
  }

  BitSet& operator |= (const BitSet& rhs) {
    assert(size >= rhs.size);
    for (size_t i = 0; i < rhs.size; i++) {
      data[i] |= rhs.data[i];
    }
    return *this;
  }

  void unset(int i) { data[i / bits_per_long] &= ~(1 << (i % bits_per_long)); }
  void clear() { memset(data, 0, buf_size(size)); }
  void resize(size_t s) { size = s; }
  bool get(int i) { return data[i / bits_per_long] & (1 << (i % bits_per_long)); }

  template <typename F>
  void for_each(F&& f) {
    auto size = buf_size(this->size) / sizeof(long);
    for (size_t i = 0; i < size; i++) {
      auto m = data[i];
      while (auto x = __builtin_ffs(m)) {
        m ^= 1 << --x;
        std::forward<F>(f)(i * bits_per_long + x);
      }
    }
  }

  constexpr static int bits_per_long = sizeof(long) * 8;
  constexpr static size_t buf_size(size_t bits) {
    return (bits + bits_per_long - 1) / bits_per_long / 8;
  }

  static BitSet *create(size_t size) {
    static_assert(std::is_pod<BitSet>(), "BitSet should be a POD");
    void *buf = operator new(sizeof(size_t) + buf_size(size));
    memset(buf, 0, sizeof(size_t) + buf_size(size));
    return new(buf) BitSet{size};
  }

  static size_t allocate_size(size_t bits) {
    return sizeof(size_t) + buf_size(bits);
  }

 private:
  BitSet() = default;
  BitSet(size_t size) : size(size) {}
  size_t size;
  unsigned long data[];
};

template <typename T, class Container = std::vector<T> >
class atomic_read_queue : Container {
 public:
  using Container::Container;

  T pop() {
    auto idx = next_get++;
    if (idx < this->size())
      return std::move((*this)[idx]);
    else
      return nullptr;
  }
  
 private:
  std::atomic<size_t> next_get;
};

struct non_copyable { };

template <typename F>
class unique_function_helper {
 public:
  unique_function_helper(const unique_function_helper&) {
    throw non_copyable{};
  }
  
  unique_function_helper& operator =(const unique_function_helper&) {
    throw non_copyable{};
  }

  unique_function_helper(F&&) : f(std::move(f)) { }

  template <typename... Args>
  decltype(auto) operator()(Args... args) {
    return std::move(f)(std::forward<Args>(args)...);
  }

 private:
  F f;
};

template <typename T>
class unique_function : public std::function<T> {
  template <typename F>
  unique_function(F&& f) : function(unique_function_helper<F>(std::forward<F>(f)))  {
  }

  typedef std::function<T> function;

  struct killer {
    killer(function& f) : f(f) { }
    ~killer() { f = nullptr; }
    function& f;
  };

  template <typename... Args>
  decltype(auto) operator()(Args... args) {
    killer k(*this);
    return function::operator()(std::forward<Args>(args)...);
  }
};

#endif
