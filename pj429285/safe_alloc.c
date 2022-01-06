#include "safe_alloc.h"
#include "err.h"

#include <stdlib.h>

void* safe_malloc(size_t size) {
  void* ptr = malloc(size);

  if (ptr == NULL)
    fatal("Error in allocation.");

  return ptr;
}

void* safe_calloc(size_t num, size_t size) {
  void* ptr = calloc(num, size);

  if (ptr == NULL)
    fatal("Error in allocation.");

  return ptr;
}
