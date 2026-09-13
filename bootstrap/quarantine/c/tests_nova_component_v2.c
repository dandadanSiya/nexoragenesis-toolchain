#define _GNU_SOURCE
#include "nova.h"
#include "nova/run_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#define ABI
#else
#include <sys/mman.h>
#define ABI __attribute__((ms_abi))
#endif
typedef uint64_t(ABI *Function)(const void *, uint64_t, void *, uint64_t);
static unsigned checks, failures;
#define CHECK(c, m)                                                            \
  do {                                                                         \
    checks++;                                                                  \
    if (!(c)) {                                                                \
      failures++;                                                              \
      fprintf(stderr, "FAIL %s\n", m);                                         \
    }                                                                          \
  } while (0)
static NovaContextV2 execution;
static NovaHostImage mapped;
/* ABI adapter only: it registers borrowed test storage, invokes generated
 * Nova code, and releases handles. It does not parse or compute the result. */
static uint64_t ABI invoke(const void *input, uint64_t length, void *output,
                           uint64_t capacity) {
  nova_error_clear_v2(&execution);
  uint64_t in = nova_borrow_v2(&execution, (void *)input, length, 0),
           out = nova_borrow_v2(&execution, output, capacity, 1);
  CHECK(in && out, "ABI2 borrowed handles");
  uint64_t arguments[] = {in, length, out, capacity};
  uint64_t result = mapped.entry(&execution, arguments, 4);
  CHECK(!execution.error, "compiled decimal execution has no runtime error");
  if (!execution.error) {
    execution.runtime->calls[NV2_FREE](&execution, in, length, 0);
    execution.runtime->calls[NV2_FREE](&execution, out, capacity, 0);
    CHECK(!execution.error, "ABI2 borrowed handles released");
  }
  return result;
}
static uint64_t le64(const unsigned char *p) {
  uint64_t v = 0;
  for (unsigned i = 0; i < 8; i++)
    v |= (uint64_t)p[i] << (8 * i);
  return v;
}
/* Independent division-based oracle; compiler input uses threshold+digit split.
 * Error status and value occupy distinct channels, including UINT64_MAX. */
static unsigned reference(const unsigned char *s, size_t n, uint64_t *v,
                          size_t *at) {
  *v = 0;
  *at = 0;
  if (!n)
    return 1;
  for (size_t i = 0; i < n; i++) {
    *at = i;
    if (s[i] < '0' || s[i] > '9') {
      *v = 0;
      return 2;
    }
    uint64_t d = s[i] - '0';
    if (*v > (UINT64_MAX - d) / 10) {
      *v = 0;
      return 3;
    }
    *v = *v * 10 + d;
  }
  *at = n;
  return 0;
}
static void testcase(Function fn, const unsigned char *s, size_t n) {
  unsigned char result[18];
  memset(result, 0xa5, sizeof result);
  uint64_t value;
  size_t at;
  unsigned expected = reference(s, n, &value, &at);
  uint64_t actual = fn(s, n, result + 1, 16);
  CHECK(actual == expected,
        "compiled parser status equals independent reference");
  CHECK(le64(result + 1) == value,
        "compiled parser full u64 value equals reference");
  CHECK(le64(result + 9) == at,
        "compiled parser error/end byte position equals reference");
  CHECK(result[0] == 0xa5 && result[17] == 0xa5,
        "parser preserves output neighbours");
}
int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: component-test decimal-u64.nova [output.efi]\n");
    return 2;
  }
  FILE *f = fopen(argv[1], "rb");
  if (!f)
    return 2;
  char *source = malloc(65536);
  size_t size = fread(source, 1, 65536, f);
  fclose(f);
  NtArtifact a;
  CHECK(nova_compile_v2(source, size, &a), "real decimal-u64.nova compiles");
  free(source);
  if (!a.text.bytes) {
    fprintf(stderr, "E%u %zu:%zu %s\n", a.error.code, a.error.line,
            a.error.column, a.error.message);
    nt_artifact_free(&a);
    return 1;
  }
  CHECK(nt_make_pe(&a), "shared PE writer accepts actual Nova machine code");
  if (argc > 2) {
    FILE *o = fopen(argv[2], "wbx");
    CHECK(o != NULL, "fresh PE output");
    if (o) {
      CHECK(fwrite(a.pe.bytes, 1, a.pe.size, o) == a.pe.size,
            "write complete PE");
      CHECK(fclose(o) == 0, "close PE");
    }
  }
  CHECK(nova_context_init_v2(&execution), "ABI2 context initializes");
  CHECK(nova_host_map(&a, &mapped), "ABI2 text/data mapping");
  Function fn = invoke;
  const char *cases[] = {"",
                         "0",
                         "42",
                         "00000000000000000000000000042",
                         "123456789",
                         "18446744073709551615",
                         "18446744073709551616",
                         "99999999999999999999999999999999999999999",
                         "12a",
                         "a12",
                         "-1",
                         "+2",
                         "1_2",
                         "1 2",
                         "00\n"};
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
    testcase(fn, (const unsigned char *)cases[i], strlen(cases[i]));
  for (unsigned byte = 0; byte < 256; byte++) {
    unsigned char sample[3] = {'1', (unsigned char)byte, '2'};
    testcase(fn, sample, 3);
  }
  uint64_t seed = UINT64_C(0x713948);
  for (unsigned i = 0; i < 1000; i++) {
    seed = seed * UINT64_C(6364136223846793005) + 1;
    char digits[32];
    int n = snprintf(digits, sizeof digits, "%llu", (unsigned long long)seed);
    testcase(fn, (unsigned char *)digits, (size_t)n);
  }
  unsigned char short_buffer[16];
  memset(short_buffer, 0xa5, sizeof short_buffer);
  CHECK(fn("42", 2, short_buffer, 15) == 4,
        "short output rejected before writes");
  for (unsigned i = 0; i < 16; i++)
    CHECK(short_buffer[i] == 0xa5, "short output remains unchanged");
  nova_host_unmap(&mapped);
  CHECK(nova_context_destroy_v2(&execution), "ABI2 resources released");
  printf("NOVA_COMPONENT_V2_TESTS checks=%u failures=%u text=%zu pe=%zu\n",
         checks, failures, a.text.size, a.pe.size);
  nt_artifact_free(&a);
  return failures ? 1 : 0;
}
