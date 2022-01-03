#include "safe_malloc.h"
#include <stdlib.h>

void *safe_malloc(size_t size) {
  void *ptr = malloc(size);

  if (ptr == NULL)
    exit(1);

  return ptr;
}
