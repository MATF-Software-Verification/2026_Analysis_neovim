// CBMC harness for base64_decode()/base64_encode() from the real, unmodified
// src/nvim/base64.c (neovim v0.12.5). Bounds the input to MAX_LEN bytes (small enough
// for CBMC to unwind exhaustively) and lets CBMC choose every byte + length
// nondeterministically, then checks memory-safety properties CBMC instruments
// automatically (--bounds-check, --pointer-check, --*-overflow-check).

#include <stddef.h>
#include <stdlib.h>

#include "nvim/base64.h"

#define MAX_LEN 12

// Never defined -- a call to a declared-but-undefined function is CBMC's
// standard idiom for "return a fully nondeterministic value of this type".
// The declaration matters: without it, C's implicit-function-declaration
// rule would assume an `int` return (truncated to `char` on assignment
// below), which happens to cover the same value range here but isn't
// conforming C.
char nondet_char(void);

// Old implementation: called the real `malloc` and assumed it succeeds.
// This worked, but relied on CBMC's name-based interception of `malloc`
// (CBMC never actually links/executes glibc's allocator -- by default it
// substitutes its own builtin, which never fails unless --malloc-may-fail
// is passed). The __CPROVER_assume below was only there to guard against
// that flag.
//
//   void *xmalloc(size_t size)
//   {
//     void *p = malloc(size);
//     __CPROVER_assume(size == 0 || p != NULL);
//     return p;
//   }
//
// New implementation: mock malloc explicitly with the same builtin CBMC's
// own `malloc` stub uses internally, so the harness doesn't depend on
// CBMC's name-based interception or its command-line flags -- it makes
// clear that we assume the allocator is correct and want CBMC to reason
// about base64_decode()/base64_encode() only.
void *xmalloc(size_t size)
{
  if (size == 0) {
    return NULL;
  }
  return __CPROVER_allocate(size, 0);
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
