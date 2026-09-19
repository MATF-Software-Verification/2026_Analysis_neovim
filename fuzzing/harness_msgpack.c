// libFuzzer harness for the REAL, unmodified msgpack-rpc wire-format layer of
// neovim v0.12.5: unpack_array()/unpack_skip()/unpack_integer()/unpack_string()
// from neovim/src/nvim/msgpack_rpc/unpacker.c, sitting on the vendored
// MessagePack tokenizer/parser (neovim/src/mpack/mpack_core.c, object.c,
// conv.c). See unit_tests/tests/test_msgpack.c for the full rationale/scope
// note (same stub set, same "real unmodified file" convention as base64.c's
// harnesses) and ../ProjectAnalysisReport.md for the writeup.
//
// Complementary to harness_msgpack_encode.c, which round-trips arbitrary
// bytes through the encoder first. This harness instead feeds raw fuzzer
// bytes straight into the decode side -- the interesting attack surface,
// since it parses untrusted bytes off an RPC socket -- and checks only that
// nothing crashes or trips ASan/UBSan (most random bytes are not valid
// msgpack, so there is no round-trip identity to check here, same caveat
// discussed for base64's decode-first harness.c).

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "nvim/lua/executor.h"
#include "nvim/msgpack_rpc/packer.h"
#include "nvim/msgpack_rpc/unpacker.h"

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

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
  // Each entry point gets its own {ptr, size} view of the same bytes, since a
  // successful parse advances the pointer and shrinks the remaining size.
  {
    const char *data = (const char *)Data;
    size_t size = Size;
    unpack_skip(&data, &size);  // drives the full tokenizer/parser (object.c)
  }
  {
    const char *data = (const char *)Data;
    size_t size = Size;
    unpack_array(&data, &size);
  }
  {
    const char *data = (const char *)Data;
    size_t size = Size;
    Integer result;
    unpack_integer(&data, &size, &result);
  }
  {
    const char *data = (const char *)Data;
    size_t size = Size;
    unpack_string(&data, &size);
  }
  return 0;
}
