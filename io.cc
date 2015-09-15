#include "io.h"

#include <errno.h>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <assert.h>
#include <string>
#include <cstdio>

static_assert(sizeof(off_t) >= 8, "off_t should equals to off64_t");

void OSError::raise(const char *reason) {
  throw OSError{reason, errno};
  abort();
}

void File::open(const char *fname) {
  close();
  fd = ::open(fname, O_RDONLY);
  if (fd < 0)
    OSError::raise("open");
}

void FileBuffer::open() {
  static int fileid = 0;
  char fname[20];
  sprintf(fname, "%d", fileid++);
  int fd = ::open(fname, O_RDWR | O_CREAT | O_TRUNC, 0600);
  if (fd < 0)
    OSError::raise("open");

  File &file = *this;
  file = File{fd};
}

size_t File::size() const {
  struct stat buf;
  if (fstat(fd, &buf) < 0)
    OSError::raise("fstat");

  return buf.st_size;
}

void FileBuffer::resize(size_t size) {
  if (!is_open()) {
    if (size == 0)
      return (void)(size_ = 0);

    open();
  }
  if (ftruncate(get(), size) < 0)
    OSError::raise("ftruncate");

  cap_ = size;
  size_ = size;

  freeze();
  assert(data_ != MAP_FAILED);
}

void *FileBuffer::freeze() {
  // XXX:
  if (data_ == MAP_FAILED)
    data_ = NULL;

  assert(data_ != MAP_FAILED);

  if (data_ != NULL)
    return data_;

  if (!is_open())
    return NULL;

  auto data = mmap(NULL, size_, PROT_READ, MAP_SHARED /*| MAP_HUGETLB*/, get(), 0);
  if (data == MAP_FAILED)
    OSError::raise("mmap");

  data_ = data;
  close();

  assert(data_ != MAP_FAILED);
  assert(data_ != NULL);
  return data_;
}

void FileBuffer::init(size_t ncap) {
  auto cap = 4096;
  while (cap < ncap) cap *= 2;
  ncap = cap;

  if (ftruncate(get(), ncap) < 0)
    OSError::raise("ftruncate");

  auto ndata = mmap(NULL, ncap, PROT_READ | PROT_WRITE, MAP_SHARED, get(), 0);
  if (ndata == MAP_FAILED)
    OSError::raise("map");

  data_ = ndata;
  cap_ = ncap;
  assert(data_ != MAP_FAILED);
}

void FileBuffer::write(void *data, size_t size) {
  // if (freezed())
  //  throw Freezed{};

  if (!is_open())
    open();

  if (cap_ <= 0)
    init(size_ + size);
  else if (size_ + size > cap_)
    grow(size_ + size);

  memcpy((char*)data_ + size_, data, size);
  size_ += size;
}

void FileBuffer::grow(size_t ncap) {
  auto cap = cap_;
  while (cap < ncap) cap *= 2;
  ncap = cap;

  if (ftruncate(get(), ncap) < 0)
    OSError::raise("ftruncate");

  auto ndata = mmap(NULL, ncap, PROT_READ | PROT_WRITE, MAP_SHARED, get(), 0);
  if (ndata == MAP_FAILED)
    OSError::raise("map");

  if (munmap(data_, cap_) < 0)
    OSError::raise("munmap");

  data_ = ndata;
  cap_ = ncap;
  assert(data_ != MAP_FAILED);
}

FileBuffer::~FileBuffer() {
  close();

  if (data_) {
    if (munmap(data_, cap_) < 0)
      OSError::raise("munmap");

    data_ = NULL;
    assert(data_ != MAP_FAILED);
  }
}

void File::write(void *data, size_t size) {
  ::write(fd, data, size);
}

void File::close() {
  if (fd < 0)
    return;

  if (::close(fd) < 0)
    OSError::raise("close");

  fd = -1;
}

void *FileBuffer::lock() {
  // freeze();
  mprotect(data_, size_, PROT_READ | PROT_WRITE);
  madvise(data_, size_, MADV_RANDOM);
  return data_;
}

void *FileBuffer::lockSeq() {
  // freeze();
  mprotect(data_, size_, PROT_READ | PROT_WRITE);
  madvise(data_, size_, MADV_SEQUENTIAL);
  return data_;
}

void FileBuffer::unlockSeq() { }
void FileBuffer::unlock() { }
