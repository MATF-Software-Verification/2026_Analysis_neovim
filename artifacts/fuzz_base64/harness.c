// libFuzzer harness for the REAL, unmodified src/nvim/base64.c from neovim v0.12.5.
//
// base64.c only calls xmalloc()/xfree() from nvim/memory.h. Rather than linking the
// full nvim/memory.c (which pulls in the garbage collector, logging, etc.), we provide
// minimal standalone definitions here and compile+link base64.c directly as a TU.
//
// Property checked: for any input, if base64_decode() accepts it, then re-encoding the
// decoded bytes and decoding that again must reproduce the exact same bytes (round-trip
// invariant) -- and none of this may ever crash, leak, or trigger UB/ASAN/UBSAN.

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "nvim/base64.h"

void *xmalloc(size_t size)
{
  void *p = malloc(size);
  if (!p && size) {
    abort();
  }
  return p;
}

void xfree(void *ptr)
{
  free(ptr);
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
  size_t out_len = 0;
  char *decoded = base64_decode((const char *)Data, Size, &out_len);
  if (decoded == NULL) {
    return 0;
  }

  char *reencoded = base64_encode(decoded, out_len);
  size_t out_len2 = 0;
  char *redecoded = base64_decode(reencoded, strlen(reencoded), &out_len2);

  if (redecoded == NULL || out_len2 != out_len
      || (out_len > 0 && memcmp(decoded, redecoded, out_len) != 0)) {
    abort();  // round-trip property violated
  }

  free(decoded);
  free(reencoded);
  free(redecoded);
  return 0;
}
