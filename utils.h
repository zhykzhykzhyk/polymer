#ifndef UTILS_H
#define UTILS_H
#include <new>
#include <utility>

class BitSet {
 public:
  void set(int i) { data[i / bits_per_long] |= 1 << (i % bits_per_long); }
  void unset(int i) { data[i / bits_per_long] &= ~(1 << (i % bits_per_long)); }
  bool get(int i) { return data[i / bits_per_long] & (1 << (i % bits_per_long)); }

  template <typename F>
  void for_each(F&& f) {
    auto size = this->size / bits_per_long;
    for (int i = 0; i < size; i++) {
      auto m = data[i];
      while (auto x = __builtin_ffs(m)) {
        m ^= 1 << --x;
        std::forward<F>(f)(i * bits_per_long + x);
      }
    }
  }

  constexpr static int bits_per_long = sizeof(long) * 8;
  static BitSet *create(size_t size) {
    static_assert(std::is_pod<BitSet>(), "BitSet should be a POD");
    void *buf = operator new((size + 7) / 8);
    return new(buf) BitSet{size};
  }

 private:
  BitSet() = default;
  BitSet(size_t size) : size(size) {}
  size_t size;
  unsigned long data[];
};
#endif
