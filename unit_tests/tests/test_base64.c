// Unit tests for base64_encode()/base64_decode() from the real, unmodified
// src/nvim/base64.c (neovim v0.12.5) -- the same target function analyzed by
// fuzzing/ and cbmc/, so this project applies three independent techniques to
// one previously-untested (see the coverage discussion in
// ../ProjectAnalysisReport.md) function and compares what each one buys.
//
// No test framework is pulled in on purpose: base64.c only calls
// xmalloc()/xfree() (stubbed below, same convention as fuzzing/harness.c and
// cbmc/harness_cbmc.c) and has a small, fully specified interface, so a plain
// assert()-based runner is enough to drive it and is trivial to build with
// --coverage. A framework like the course's QtTest is built for testing
// classes/objects with setup/teardown and Qt signal wiring -- overkill for two
// free functions with no state.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nvim/base64.h"

static int g_failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
      g_failures++;                                                          \
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

void xfree(void *ptr)
{
  free(ptr);
}

// Encode/decode known test vectors (RFC 4648 examples) -- an oracle
// independent of the implementation under test, not just a round trip.
static void test_known_vectors(void)
{
  struct { const char *plain; size_t plain_len; const char *encoded; } cases[] = {
    { "Man", 3, "TWFu" },
    { "Ma", 2, "TWE=" },
    { "M", 1, "TQ==" },
    { "", 0, "" },
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    char *enc = base64_encode(cases[i].plain, cases[i].plain_len);
    CHECK(strcmp(enc, cases[i].encoded) == 0);
    xfree(enc);

    size_t out_len = 0;
    char *dec = base64_decode(cases[i].encoded, strlen(cases[i].encoded), &out_len);
    CHECK(out_len == cases[i].plain_len);
    if (cases[i].plain_len > 0) {
      CHECK(dec != NULL && memcmp(dec, cases[i].plain, cases[i].plain_len) == 0);
    }
    xfree(dec);
  }
}

// Round-trips every length from 0..32 so the >=8-byte block loop, the 4-byte
// block loop, and all three tail-remainder branches in base64_encode() (and
// the corresponding accumulator paths in base64_decode()) each get exercised.
static void test_roundtrip_all_short_lengths(void)
{
  unsigned char src[32];
  for (size_t len = 0; len <= sizeof(src); len++) {
    for (size_t i = 0; i < len; i++) {
      src[i] = (unsigned char)((i * 37 + len) & 0xFF);
    }

    char *enc = base64_encode((const char *)src, len);
    size_t out_len = 0;
    char *dec = base64_decode(enc, strlen(enc), &out_len);

    CHECK(dec != NULL);
    CHECK(out_len == len);
    if (dec != NULL && out_len == len) {
      CHECK(memcmp(dec, src, len) == 0);
    }

    xfree(enc);
    xfree(dec);
  }
}

static void test_decode_rejects_bad_length(void)
{
  size_t out_len = 123;
  char *dec = base64_decode("ABCDE", 5, &out_len);  // 5 is not a multiple of 4
  CHECK(dec == NULL);
  CHECK(out_len == 0);
}

static void test_decode_rejects_invalid_character(void)
{
  size_t out_len = 123;
  char *dec = base64_decode("!!!!", 4, &out_len);
  CHECK(dec == NULL);
  CHECK(out_len == 0);
}

static void test_decode_rejects_misplaced_padding(void)
{
  size_t out_len = 123;
  // '=' as the very first character: the padding scan expects everything
  // from there to the end to be '=', which "AAA" is not.
  char *dec = base64_decode("=AAA", 4, &out_len);
  CHECK(dec == NULL);
  CHECK(out_len == 0);
}

static void test_decode_rejects_wrong_padding_count(void)
{
  size_t out_len = 123;
  // Only 4 non-padding bits are left over after one 6-bit group, so exactly
  // one '=' is valid here ("TQ=X" swaps the required second '=' for junk).
  char *dec = base64_decode("TQ=X", 4, &out_len);
  CHECK(dec == NULL);
  CHECK(out_len == 0);
}

int main(void)
{
  test_known_vectors();
  test_roundtrip_all_short_lengths();
  test_decode_rejects_bad_length();
  test_decode_rejects_invalid_character();
  test_decode_rejects_misplaced_padding();
  test_decode_rejects_wrong_padding_count();

  if (g_failures == 0) {
    printf("all tests passed\n");
    return 0;
  }
  fprintf(stderr, "%d assertion(s) failed\n", g_failures);
  return 1;
}
