#include "nova.h"
#include "nova/run_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, failures;
#define CHECK(c, m)                                                            \
  do {                                                                         \
    checks++;                                                                  \
    if (!(c)) {                                                                \
      failures++;                                                              \
      fprintf(stderr, "FAIL %s\n", m);                                         \
    }                                                                          \
  } while (0)
static void execute(const char *source, size_t n, uint64_t want,
                    const char *label) {
  NtArtifact a;
  int ok = nova_compile_v2(source, n, &a);
  CHECK(ok, label);
  if (!ok) {
    fprintf(stderr, "E%u %zu:%zu %s\n", a.error.code, a.error.line,
            a.error.column, a.error.message);
    nt_artifact_free(&a);
    return;
  }
  NovaContextV2 c;
  NovaHostImage image;
  CHECK(nova_context_init_v2(&c) && nova_host_map(&a, &image),
        "capacity execution setup");
  uint64_t result = image.entry(&c, NULL, 0);
  CHECK(result == want && !c.error,
        "large compiled program executes expected result");
  printf("NOVA_CAPACITY_CASE label=%s source=%zu text=%zu result=%llu\n", label,
         n, a.text.size, (unsigned long long)result);
  nova_host_unmap(&image);
  nova_context_destroy_v2(&c);
  nt_artifact_free(&a);
}
int main(void) {
  char *source = malloc(1024 * 1024);
  if (!source)
    return 2;
  size_t n = 0;
  n += (size_t)sprintf(source + n, "fn total(");
  for (unsigned i = 0; i < 64; i++)
    n += (size_t)sprintf(source + n, "%sa%u:u64", i ? "," : "", i);
  n += (size_t)sprintf(source + n, ")->u64{return ");
  for (unsigned i = 0; i < 64; i++)
    n += (size_t)sprintf(source + n, "%sa%u", i ? "+" : "", i);
  n += (size_t)sprintf(source + n, ";}fn main()->u64{return total(");
  for (unsigned i = 0; i < 64; i++)
    n += (size_t)sprintf(source + n, "%s%u", i ? "," : "", i + 1);
  n += (size_t)sprintf(source + n, ");}");
  execute(source, n, 2080, "64-scalar-ABI");
  n = 0;
  for (unsigned f = 0; f < 512; f++) {
    n += (size_t)sprintf(source + n, "fn f%03u(p:u64)->u64{", f);
    for (unsigned i = 0; i < 8; i++)
      n += (size_t)sprintf(source + n, "let v%u:u64=p+%u;", i, i);
    n += (size_t)sprintf(source + n, "return v0+v1+v2+v3+v4+v5+v6+v7;}\n");
  }
  n += (size_t)sprintf(source + n, "fn main()->u64{return f511(1);}");
  execute(source, n, 36, "513-functions-4096-source-locals");
  free(source);
  printf("NOVA_CAPACITY_TESTS checks=%u failures=%u\n", checks, failures);
  return failures ? 1 : 0;
}
