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
  int fd = ::open(std::to_string(fileid++).c_str(), O_RDWR | O_CREAT, 0600);
  if (fd < 0)
    OSError::raise("open");

  File &file = *this;
  file = File{fd};
}

void FileBuffer::resize(size_t size) {
  if (ftruncate(get(), size) < 0)
    OSError::raise("ftruncate");

  this->size = size;
}

void *FileBuffer::freeze() {
  if (data != NULL)
    return data;

  if (!is_open())
    return NULL;

  data = mmap(NULL, size, PROT_READ, MAP_SHARED /*| MAP_HUGETLB*/, get(), 0);
  if (data == MAP_FAILED) {
    data = NULL;
    OSError::raise("mmap");
  }

  close();
  return data;
}

FileBuffer::~FileBuffer() {
  if (data) {
    if (munmap(data, size) < 0)
      OSError::raise("munmap");
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
}

void *FileBuffer::lock() {
  freeze();
  madvise(data, size, MADV_RANDOM);
  return data;
}

void *FileBuffer::lockSeq() {
  freeze();
  madvise(data, size, MADV_SEQUENTIAL);
  return data;
}

void FileBuffer::unlockSeq() { }
void FileBuffer::unlock() { }
