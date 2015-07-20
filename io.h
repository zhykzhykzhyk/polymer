#ifndef IO_H
#define IO_H

#include <cstdlib>
#include <utility>

struct OSError {
  static void raise(const char *);
  const char *reason;
  int err;
};

class Freezed{};

class File {
 public:
  explicit File(int fd) : fd{fd} { }
  File() : fd{-1} {}

  File(const File&) = delete;
  File(File&& o) : File{} { std::swap(fd, o.fd); }
  File& operator=(const File&) = delete;
  File& operator=(File&& o) { std::swap(fd, o.fd); return *this; }

  bool is_open() const { return fd >= 0; }

  void open(int fd);
  void open(const char *fname);
  void close();

  void write(void *data, size_t size);
  int get() const { return fd; }

  ~File() { close(); }

 private:
  int fd;
};

class FileBuffer : File {
 public:
  FileBuffer() : data_{}, size_{} { }
  FileBuffer(const FileBuffer&) = delete;
  FileBuffer(FileBuffer&& buf) : FileBuffer() { swap(buf); }
  FileBuffer& operator=(const FileBuffer&) = delete;
  FileBuffer& operator=(FileBuffer&& buf) { swap(buf); return *this; }
  FileBuffer(File&& f) : File{std::move(f)} { }

  void write(void *data, size_t size) {
    if (freezed())
      throw Freezed{};
    if (!is_open())
      open();

    size_ += size;
    File::write(data, size);
  }

  void resize(size_t size);
  size_t size() { return size_; }

  bool freezed() { return data_; }
  void *freeze();

  void *lock();
  void unlock();

  void *lockSeq();
  void unlockSeq();

  size_t tell() const { return size_; }

  void swap(FileBuffer& buf) {
    using std::swap;
    swap(data_, buf.data_);
    swap(size_, buf.size_);
    swap(static_cast<File&>(buf), static_cast<File&>(*this));
  }

  ~FileBuffer();

 private:
  void open();

  void *data_;
  size_t size_;
};

#endif
