// libFuzzer harness for the REAL, unmodified msgpack-rpc wire-format layer of
// neovim v0.12.5. Complementary to harness_msgpack.c: this one is encode-first
// (mpack_integer()/mpack_str() from neovim/src/nvim/msgpack_rpc/packer.c
// accept any value/bytes unconditionally, so there is no rejection step to
// gate on) and checks the encode->decode fidelity round trip, the same shape
// as base64's harness_encode.c.
//
// Property checked: for any input, decoding the mpack_integer()/mpack_str()
// encoding of that input must reproduce the exact same value/bytes -- and
// none of this may ever crash, leak, or trigger UB/ASan/UBSan.
//
// See unit_tests/tests/test_msgpack.c for the stub rationale (same set here).

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

static void abort_flush(PackerBuffer *packer) { abort(); }

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
  if (Size < sizeof(int64_t)) {
    return 0;
  }

  int64_t original_int;
  memcpy(&original_int, Data, sizeof(original_int));

  {
    char buf[16];
    PackerBuffer packer = {
      .startptr = buf, .ptr = buf, .endptr = buf + sizeof(buf),
      .packer_flush = abort_flush,
    };
    mpack_integer(&packer.ptr, original_int);

    const char *data = buf;
    size_t size = (size_t)(packer.ptr - buf);
    Integer decoded_int = 0;
    if (!unpack_integer(&data, &size, &decoded_int) || decoded_int != original_int || size != 0) {
      abort();  // round-trip property violated
    }
  }

  const uint8_t *str_data = Data + sizeof(int64_t);
  size_t str_len = Size - sizeof(int64_t);

  // +64: same mpack_check_buffer() prefetch headroom as test_msgpack.c's
  // string sweep -- big enough that its post-write "room for a next write"
  // check never fires (there is no flush wired up here to handle it).
  size_t cap = str_len + 64;
  char *buf = xmalloc(cap);
  PackerBuffer packer = {
    .startptr = buf, .ptr = buf, .endptr = buf + cap,
    .packer_flush = abort_flush,
  };
  String str = { .data = (char *)str_data, .size = str_len };
  mpack_str(str, &packer);

  const char *data = buf;
  size_t size = (size_t)(packer.ptr - buf);
  String decoded_str = unpack_string(&data, &size);
  if (decoded_str.data == NULL || decoded_str.size != str_len
      || (str_len > 0 && memcmp(decoded_str.data, str_data, str_len) != 0)
      || size != 0) {
    abort();  // round-trip property violated
  }

  xfree(buf);
  return 0;
}
