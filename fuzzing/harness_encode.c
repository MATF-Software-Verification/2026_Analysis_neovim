// libFuzzer harness for the REAL, unmodified src/nvim/base64.c from neovim v0.12.5.
//
// Complementary to harness.c, which feeds raw fuzzer bytes into base64_decode()
// first (the interesting attack surface, since decode parses untrusted input, and
// most random bytes are malformed base64). This harness instead feeds raw fuzzer
// bytes into base64_encode() -- which accepts any byte sequence unconditionally,
// so there is no rejection step to gate on -- and checks that decoding the result
// reproduces the original bytes exactly (an encode->decode fidelity round trip).
//
// Property checked: for any input, decoding the base64_encode() of that input
// must reproduce the exact same bytes -- and none of this may ever crash, leak,
// or trigger UB/ASAN/UBSAN.

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
  char *encoded = base64_encode((const char *)Data, Size);

  size_t decoded_len = 0;
  char *decoded = base64_decode(encoded, strlen(encoded), &decoded_len);

  if (decoded == NULL || decoded_len != Size
      || (Size > 0 && memcmp(decoded, Data, Size) != 0)) {
    abort();  // round-trip property violated
  }

  free(encoded);
  free(decoded);
  return 0;
}
