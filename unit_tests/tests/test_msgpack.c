// Unit tests for the msgpack-rpc wire-format layer from the real, unmodified
// neovim v0.12.5 sources -- a second, independently-chosen verification target
// alongside base64.c (see ../../ProjectAnalysisReport.md, "msgpack-rpc wire
// format" section, for why this target was picked and what is deliberately
// out of scope).
//
// Targeted functions, all real and unmodified:
//   unpack_integer(), unpack_uint_or_sint(), unpack_string(), unpack_array()
//     from neovim/src/nvim/msgpack_rpc/unpacker.c
//   mpack_integer(), mpack_uint64(), mpack_str()
//     from neovim/src/nvim/msgpack_rpc/packer.c
// plus the vendored MessagePack tokenizer they both sit on top of:
// neovim/src/mpack/{mpack_core,object,conv}.c -- confirmed by trial
// compilation to have zero neovim dependencies of their own.
//
// unpacker.c and packer.c are each compiled here as whole, unmodified
// translation units (same "real file, not excerpts" rule as base64.c), which
// means functions this suite never calls (unpacker_parse_header/advance,
// unpack_keydict, mpack_object/mpack_object_inner -- all of which need the
// arena allocator, the generated API dispatch table, or Lua refs) still need
// to *link*. The stubs below exist purely to satisfy the linker for that
// unreachable code; each one is abort()-bodied so an accidental call would
// fail loudly instead of silently returning nonsense.

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nvim/lua/executor.h"
#include "nvim/msgpack_rpc/packer.h"
#include "nvim/msgpack_rpc/unpacker.h"

static int g_failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
      g_failures++;                                                         \
    }                                                                        \
  } while (0)

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

// Storage for the globals unpacker.c's (unreached) redraw path references --
// EXTERN in ui_client.h resolves to plain `extern` in every TU except
// ui_client.c itself, so something must define them.
size_t grid_line_buf_size = 0;
schar_T *grid_line_buf_char = NULL;
sattr_T *grid_line_buf_attr = NULL;

static void abort_flush(PackerBuffer *packer) { abort(); }

static PackerBuffer make_packer_buffer(char *buf, size_t cap)
{
  return (PackerBuffer){
    .startptr = buf,
    .ptr = buf,
    .endptr = buf + cap,
    .packer_flush = abort_flush,
  };
}

// Decode known byte sequences straight from the MessagePack spec (an oracle
// independent of this codebase, same role RFC 4648 plays for base64) -- each
// sequence is hand-built per the spec's type-tag table, not by round-tripping
// through mpack_integer()/mpack_str(), so this actually exercises unpack_*()
// against a fixed external ground truth.
static void test_known_vectors_decode(void)
{
  struct { unsigned char bytes[9]; size_t len; int64_t expect; } int_cases[] = {
    { {0x00}, 1, 0 },                                          // positive fixint
    { {0x7f}, 1, 127 },                                        // positive fixint, max
    { {0xcc, 0x80}, 2, 128 },                                  // uint8
    { {0xcd, 0x01, 0x00}, 3, 256 },                            // uint16
    { {0xce, 0x00, 0x01, 0x00, 0x00}, 5, 65536 },              // uint32
    { {0xcf, 0, 0, 0, 0, 0x10, 0, 0, 0}, 9, 268435456 },       // uint64
    { {0xff}, 1, -1 },                                         // negative fixint
    { {0xe0}, 1, -32 },                                        // negative fixint, min
    { {0xd0, 0xdf}, 2, -33 },                                  // int8
    { {0xd1, 0xff, 0x7f}, 3, -129 },                           // int16
    { {0xd2, 0x80, 0x00, 0x00, 0x00}, 5, -2147483648LL },      // int32
  };

  for (size_t i = 0; i < sizeof(int_cases) / sizeof(int_cases[0]); i++) {
    const char *data = (const char *)int_cases[i].bytes;
    size_t size = int_cases[i].len;
    Integer result = 0;
    CHECK(unpack_integer(&data, &size, &result));
    CHECK(result == int_cases[i].expect);
    CHECK(size == 0);  // whole token consumed, nothing left
  }

  // fixstr, empty
  {
    unsigned char bytes[] = {0xa0};
    const char *data = (const char *)bytes;
    size_t size = sizeof(bytes);
    String s = unpack_string(&data, &size);
    CHECK(s.data != NULL);
    CHECK(s.size == 0);
  }
  // fixstr, "a"
  {
    unsigned char bytes[] = {0xa1, 'a'};
    const char *data = (const char *)bytes;
    size_t size = sizeof(bytes);
    String s = unpack_string(&data, &size);
    CHECK(s.data != NULL && s.size == 1 && s.data[0] == 'a');
  }
  // str8 tag used explicitly for a 2-byte string: the spec permits any width
  // tag regardless of whether a shorter one would fit, and a decoder must
  // accept this non-minimal-but-valid encoding.
  {
    unsigned char bytes[] = {0xd9, 0x02, 'h', 'i'};
    const char *data = (const char *)bytes;
    size_t size = sizeof(bytes);
    String s = unpack_string(&data, &size);
    CHECK(s.data != NULL && s.size == 2 && memcmp(s.data, "hi", 2) == 0);
  }
}

// Encode every integer at every mpack_integer() size-class boundary and
// decode it back with unpack_integer(), forcing every encoding-width branch
// deterministically -- the msgpack analogue of test_roundtrip_all_short_lengths
// in test_base64.c.
//
// Note: mpack_integer()'s actual uint64-tag threshold is 0xfffffff (2^28-1,
// ~268M), not 0xffffffff/2^32-1 as the "uint8/16/32/64" naming might suggest
// -- verified by trial-compiling and dumping real encodings. Values from
// 0x10000000 up to 0xffffffff get the full 8-byte uint64 (0xcf) tag instead
// of the 4-byte uint32 (0xce) one. Still spec-compliant (msgpack decoders
// must accept any valid width for a value), just non-minimal; the boundary
// list below is built around the *real* threshold, not the assumed one.
static void test_roundtrip_integer_boundaries(void)
{
  const int64_t values[] = {
    0, 1, 0x7f, 0x80, 0xff, 0x100, 0xffff, 0x10000,
    0x0fffffff, 0x10000000, 0xffffffffLL, 0x100000000LL,
    -1, -32, -33, -128, -129, -32768, -32769,
    -2147483648LL, -2147483649LL,
    INT64_MIN, INT64_MAX,
  };

  for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
    char buf[16];
    PackerBuffer packer = make_packer_buffer(buf, sizeof(buf));
    mpack_integer(&packer.ptr, values[i]);

    const char *data = buf;
    size_t size = (size_t)(packer.ptr - buf);
    Integer decoded = 0;
    CHECK(unpack_integer(&data, &size, &decoded));
    CHECK(decoded == values[i]);
    CHECK(size == 0);
  }
}

// Same idea for mpack_str()/unpack_string(): sweep every length across the
// fixstr (0-31) / str8 (32-254) / str16 (255+) boundaries.
static void test_roundtrip_string_lengths(void)
{
  char src[300];
  size_t lengths[] = {0, 1, 30, 31, 32, 33, 254, 255, 256, 299};

  for (size_t li = 0; li < sizeof(lengths) / sizeof(lengths[0]); li++) {
    size_t len = lengths[li];
    for (size_t i = 0; i < len; i++) {
      src[i] = (char)((i * 37 + len) & 0xFF);
    }

    // +64 gives mpack_check_buffer() (called after every write, to prefetch
    // room for a *next* write that never comes) clear headroom above its
    // 2*MPACK_ITEM_SIZE=18-byte threshold, so it never calls packer_flush
    // (which this harness never wired up, on purpose -- there is nothing
    // else to flush).
    char buf[300 + 64];
    PackerBuffer packer = make_packer_buffer(buf, len + 64);
    String str = { .data = src, .size = len };
    mpack_str(str, &packer);

    const char *data = buf;
    size_t size = (size_t)(packer.ptr - buf);
    String decoded = unpack_string(&data, &size);
    CHECK(decoded.data != NULL);
    CHECK(decoded.size == len);
    if (decoded.data != NULL && decoded.size == len && len > 0) {
      CHECK(memcmp(decoded.data, src, len) == 0);
    }
    CHECK(size == 0);
  }
}

static void test_decode_rejects_truncated_token(void)
{
  // uint16 tag (0xcd) with no payload bytes at all.
  unsigned char bytes[] = {0xcd};
  const char *data = (const char *)bytes;
  size_t size = sizeof(bytes);
  Integer result = 999;
  CHECK(!unpack_integer(&data, &size, &result));
}

static void test_decode_rejects_wrong_type(void)
{
  // An empty fixstr token where an integer is expected.
  unsigned char bytes[] = {0xa0};
  const char *data = (const char *)bytes;
  size_t size = sizeof(bytes);
  Integer result = 999;
  CHECK(!unpack_integer(&data, &size, &result));
}

static void test_decode_rejects_declared_length_past_buffer(void)
{
  // str8 declares a 5-byte payload but only 2 bytes are actually present.
  unsigned char bytes[] = {0xd9, 0x05, 'a', 'b'};
  const char *data = (const char *)bytes;
  size_t size = sizeof(bytes);
  String result = unpack_string(&data, &size);
  CHECK(result.data == NULL);
}

static void test_decode_array_rejects_non_array(void)
{
  unsigned char bytes[] = {0x00};  // fixint, not an array
  const char *data = (const char *)bytes;
  size_t size = sizeof(bytes);
  CHECK(unpack_array(&data, &size) == -1);
}

int main(void)
{
  test_known_vectors_decode();
  test_roundtrip_integer_boundaries();
  test_roundtrip_string_lengths();
  test_decode_rejects_truncated_token();
  test_decode_rejects_wrong_type();
  test_decode_rejects_declared_length_past_buffer();
  test_decode_array_rejects_non_array();

  if (g_failures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d assertion(s) failed\n", g_failures);
  return 1;
}
