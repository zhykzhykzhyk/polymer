#include "parallel.h"
#include <stdio.h>

int main()
{
  parallel_for_each_iter(0, 100, [](int i) {
      printf("%d\n", i);
  });
  return 0;
}

