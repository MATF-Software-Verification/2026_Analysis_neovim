// CBMC harness for base64_decode()/base64_encode() from the real, unmodified
// src/nvim/base64.c (neovim v0.12.5). Bounds the input to MAX_LEN bytes (small enough
// for CBMC to unwind exhaustively) and lets CBMC choose every byte + length
// nondeterministically, then checks memory-safety properties CBMC instruments
// automatically (--bounds-check, --pointer-check, --*-overflow-check).

#include <stddef.h>
#include <stdlib.h>

#include "nvim/base64.h"

#define MAX_LEN 12

void *xmalloc(size_t size)
{
  void *p = malloc(size);
  __CPROVER_assume(size == 0 || p != NULL);
  return p;
}

void xfree(void *ptr)
{
  free(ptr);
}

int main(void)
{
  char src[MAX_LEN];
  size_t src_len;
  __CPROVER_assume(src_len <= MAX_LEN);
  for (size_t i = 0; i < MAX_LEN; i++) {
    src[i] = nondet_char();
  }

  size_t out_len;
  char *decoded = base64_decode(src, src_len, &out_len);
  if (decoded != NULL) {
    char *reencoded = base64_encode(decoded, out_len);
    xfree(reencoded);
    xfree(decoded);
  }

  return 0;
}
