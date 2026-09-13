#include "nova.h"
#include "nova/pe_oracle.h"
#include "nova/run_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, failures;
static NovaHostImage program;
static NovaContextV2 context;
#define CHECK(c, m)                                                            \
  do {                                                                         \
    checks++;                                                                  \
    if (!(c)) {                                                                \
      failures++;                                                              \
      fprintf(stderr, "FAIL %s\n", m);                                         \
    }                                                                          \
  } while (0)
static uint64_t read64(const unsigned char *p) {
  uint64_t n = 0;
  for (unsigned i = 0; i < 8; i++)
    n |= (uint64_t)p[i] << (i * 8);
  return n;
}
static char *load(const char *name, size_t *length) {
  FILE *f = fopen(name, "rb");
  if (!f)
    return NULL;
  if (fseek(f, 0, SEEK_END)) {
    fclose(f);
    return NULL;
  }
  long n = ftell(f);
  if (n < 0 || n > 4 * 1024 * 1024 || fseek(f, 0, SEEK_SET)) {
    fclose(f);
    return NULL;
  }
  char *s = malloc((size_t)n + 1);
  if (!s) {
    fclose(f);
    return NULL;
  }
  *length = fread(s, 1, (size_t)n, f);
  fclose(f);
  s[*length] = 0;
  return s;
}
static void compare(unsigned kind, uint64_t source, uint64_t target,
                    int64_t addend) {
  uint64_t expected = 0;
  int valid = nova_oracle_relocation(kind, source, target, addend, &expected);
  nova_error_clear_v2(&context);
  uint64_t args[] = {kind, source, target, (uint64_t)addend};
  uint64_t h = program.entry(&context, args, 4);
  unsigned char *p =
      (unsigned char *)(uintptr_t)context.runtime->calls[NV2_RESOLVE](&context,
                                                                      h, 0, 16);
  int pass = p && !context.error && ((read64(p) == 0) == valid) &&
             (!valid || read64(p + 8) == expected);
  CHECK(pass, "relocation value matches C oracle");
  if (!pass)
    fprintf(stderr,
            "kind=%u source=%llx target=%llx add=%lld valid=%d expected=%llx "
            "error=%llu\n",
            kind, (unsigned long long)source, (unsigned long long)target,
            (long long)addend, valid, (unsigned long long)expected,
            (unsigned long long)context.error);
  if (!context.error)
    context.runtime->calls[NV2_FREE](&context, h, 16, 0);
}
int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  size_t a, b;
  char *entry = load(argv[1], &a), *library = load(argv[2], &b);
  if (!entry || !library) {
    free(entry);
    free(library);
    return 2;
  }
  NtFInput input[] = {{argv[1], entry, a}, {argv[2], library, b}};
  NtArtifact artifact;
  int ok = nova_compile_many_v2(input, 2, &artifact);
  CHECK(ok, "relocation module compiles");
  if (!ok) {
    fprintf(stderr, "E%u %zu:%zu %s\n", artifact.error.code,
            artifact.error.line, artifact.error.column, artifact.error.message);
    nt_artifact_free(&artifact);
    free(entry);
    free(library);
    return 1;
  }
  CHECK(nova_context_init_v2(&context) && nova_host_map(&artifact, &program),
        "relocation execution setup");
  compare(1, 4096, 4116, 0);
  compare(1, 8192, 4096, 0);
  compare(1, 0, UINT64_C(0x80000003), 0);
  compare(1, 0, UINT64_C(0x80000004), 0);
  compare(1, UINT64_C(0x7ffffffc), 0, 0);
  compare(1, UINT64_C(0x7ffffffd), 0, 0);
  compare(1, 0, 0, INT64_MIN);
  compare(1, 0, INT64_MAX, 1);
  compare(1, UINT64_MAX, 0, 0);
  compare(1, 0, UINT64_MAX, 0);
  compare(2, 0, 8192, 0);
  compare(2, 0, 0, -1);
  compare(2, 0, 0, -INT64_C(0x140000000));
  compare(2, 0, 0, -INT64_C(0x140000001));
  compare(2, 0, 0, INT64_MIN);
  compare(2, 0, INT64_MAX, INT64_MAX);
  compare(0, 0, 0, 0);
  compare(3, 0, 0, 0);
  uint64_t seed = 42;
  for (unsigned i = 0; i < 2000; i++) {
    seed = seed * UINT64_C(6364136223846793005) + 1;
    uint64_t source = seed & UINT32_MAX;
    seed = seed * UINT64_C(6364136223846793005) + 1;
    uint64_t target = seed & UINT32_MAX;
    int64_t addend =
        (int64_t)((seed >> 32) & UINT64_C(0x1fffffff)) - INT64_C(0xfffffff);
    compare(i % 2 + 1, source, target, addend);
  }
  nova_host_unmap(&program);
  CHECK(nova_context_destroy_v2(&context), "relocation context cleanup");
  nt_artifact_free(&artifact);
  free(entry);
  free(library);
  printf("NOVA_PE_RELOCATION_TESTS checks=%u failures=%u\n", checks, failures);
  return failures ? 1 : 0;
}
