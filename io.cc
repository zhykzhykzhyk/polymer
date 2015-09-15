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
}

void File::open(const char *fname) {
}

void FileBuffer::open() {
  static int fileid = 0;
  int fd = ::open(std::to_string(fileid++).c_str(), O_RDWR | O_CREAT | O_TRUNC, 0600);
  if (fd < 0)
    OSError::raise("open");

  File &file = *this;
  file = File{fd};
}

void FileBuffer::resize(size_t size) {
  if (!is_open()) {
    if (size == 0)
      return (void)(size_ = 0);

    open();
  }
  if (ftruncate(get(), size) < 0)
    OSError::raise("ftruncate");

  size_ = size;
}

void *FileBuffer::freeze() {
  if (data_ != NULL)
    return data_;

  if (!is_open())
    return NULL;

  data_ = mmap(NULL, size_, PROT_READ, MAP_SHARED /*| MAP_HUGETLB*/, get(), 0);
  if (data_ == MAP_FAILED) {
    data_ = NULL;
    OSError::raise("mmap");
  }

  close();
  return data_;
}

FileBuffer::~FileBuffer() {
  close();

  if (data_) {
    if (munmap(data_, size_) < 0)
      OSError::raise("munmap");

    data_ = NULL;
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
  freeze();
  mprotect(data_, size_, PROT_READ | PROT_WRITE);
  madvise(data_, size_, MADV_RANDOM);
  return data_;
}

void *FileBuffer::lockSeq() {
  freeze();
  mprotect(data_, size_, PROT_READ | PROT_WRITE);
  madvise(data_, size_, MADV_SEQUENTIAL);
  return data_;
}

void FileBuffer::unlockSeq() { }
void FileBuffer::unlock() { }
