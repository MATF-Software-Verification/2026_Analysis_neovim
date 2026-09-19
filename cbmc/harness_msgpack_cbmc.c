// CBMC harness for the REAL, unmodified msgpack-rpc decode primitives of
// neovim v0.12.5: unpack_integer(), unpack_string(), unpack_array() from
// neovim/src/nvim/msgpack_rpc/unpacker.c, sitting on the vendored
// MessagePack tokenizer (neovim/src/mpack/mpack_core.c, object.c, conv.c).
// See unit_tests/tests/test_msgpack.c for the stub rationale (same set here)
// and ../ProjectAnalysisReport.md for the full writeup.
//
// Scope note: unpack_skip() (the full tree-walking parser in object.c,
// unbounded container depth) is deliberately left out of this bounded proof
// -- it's covered by fuzzing/harness_msgpack.c instead. The three functions
// proved here each call the tokenizer exactly once and only loop internally
// over at most 8 bytes (mpack_rvalue()'s byte-accumulation loop, for a
// uint64/int64 payload), which is why --unwind here can stay small.
//
// Bounds the input to MAX_LEN bytes (small enough for CBMC to unwind
// exhaustively) and lets CBMC choose every byte + length nondeterministically,
// then checks memory-safety properties CBMC instruments automatically
// (--bounds-check, --pointer-check, --*-overflow-check) -- i.e. that these
// functions never read past the attacker-controlled declared length,
// regardless of token content.

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "nvim/lua/executor.h"
#include "nvim/msgpack_rpc/packer.h"
#include "nvim/msgpack_rpc/unpacker.h"

#define MAX_LEN 12

char nondet_char(void);

// Same rationale as base64's harness_cbmc.c: mock the allocator with CBMC's
// own builtin instead of relying on its malloc-name interception.
void *xmalloc(size_t size)
{
  if (size == 0) {
    return NULL;
  }
  return __CPROVER_allocate(size, 0);
}

void *xrealloc(void *ptr, size_t size)
{
  (void)ptr;
  if (size == 0) {
    return NULL;
  }
  return __CPROVER_allocate(size, 0);
}

void xfree(void *ptr)
{
  free(ptr);
}

// None of unpack_integer()/unpack_string()/unpack_array() reach xstrdup or
// any of the stubs below (they belong to unpacker_parse_header/advance,
// unpack_keydict, or packer.c's Lua/arena paths) -- abort() makes that
// scope assumption checkable rather than assumed.
char *xstrdup(const char *str) { (void)str; abort(); }
void api_free_luaref(LuaRef ref) { (void)ref; abort(); }
void api_set_error(Error *err, ErrorType errType, const char *format, ...) { (void)err; (void)errType; (void)format; abort(); }
void *arena_alloc(Arena *arena, size_t size, bool align) { (void)arena; (void)size; (void)align; abort(); }
ArenaMem arena_finish(Arena *arena) { (void)arena; abort(); }
void arena_mem_free(ArenaMem mem) { (void)mem; abort(); }
String arena_printf(Arena *arena, const char *fmt, ...) { (void)arena; (void)fmt; abort(); }
Object handle_ui_client_redraw(uint64_t channel_id, Array args, Arena *arena, Error *error)
{
  (void)channel_id; (void)args; (void)arena; (void)error; abort();
}
MsgpackRpcRequestHandler msgpack_rpc_get_handler_for(const char *name, size_t name_len, Error *error)
{
  (void)name; (void)name_len; (void)error; abort();
}
schar_T schar_from_buf(const char *buf, size_t len) { (void)buf; (void)len; abort(); }
void ui_client_event_grid_line(Array args) { (void)args; abort(); }
UIClientHandler ui_client_get_redraw_handler(const char *name, size_t name_len, Error *error)
{
  (void)name; (void)name_len; (void)error; abort();
}

size_t grid_line_buf_size = 0;
schar_T *grid_line_buf_char = NULL;
sattr_T *grid_line_buf_attr = NULL;

int main(void)
{
  char src[MAX_LEN];
  size_t src_len;
  __CPROVER_assume(src_len <= MAX_LEN);
  for (size_t i = 0; i < MAX_LEN; i++) {
    src[i] = nondet_char();
  }

  {
    const char *data = src;
    size_t size = src_len;
    Integer result;
    unpack_integer(&data, &size, &result);
  }
  {
    const char *data = src;
    size_t size = src_len;
    unpack_string(&data, &size);
  }
  {
    const char *data = src;
    size_t size = src_len;
    unpack_array(&data, &size);
  }

  return 0;
}
