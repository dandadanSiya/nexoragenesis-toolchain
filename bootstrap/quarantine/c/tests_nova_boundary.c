#include "nova.h"
#include "nova/run_host.h"
#include <stdio.h>
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
static void execute(const char *s, const uint64_t *args, size_t count,
                    uint64_t wanted, uint64_t error) {
  NtArtifact a;
  int ok = nova_compile_v2(s, strlen(s), &a);
  CHECK(ok, "boundary source compiles");
  if (!ok) {
    nt_artifact_free(&a);
    return;
  }
  NovaContextV2 c;
  NovaHostImage image;
  CHECK(nova_context_init_v2(&c) && nova_host_map(&a, &image),
        "boundary entry setup");
  uint64_t got = image.entry(&c, args, count);
  CHECK(got == wanted && c.error == error, "public typed argument boundary");
  if (got != wanted || c.error != error)
    fprintf(stderr, "actual=%llu,error=%llu wanted=%llu,error=%llu\n",
            (unsigned long long)got, (unsigned long long)c.error,
            (unsigned long long)wanted, (unsigned long long)error);
  nova_host_unmap(&image);
  nova_context_destroy_v2(&c);
  nt_artifact_free(&a);
}
int main(void) {
  char s[8192];
  uint64_t args[64];
  size_t n = 0;
  n += (size_t)sprintf(s + n, "fn main(");
  for (unsigned i = 0; i < 64; i++) {
    args[i] = i + 1;
    n += (size_t)sprintf(s + n, "%sa%u:u64", i ? "," : "", i);
  }
  n += (size_t)sprintf(s + n, ")->u64{return ");
  for (unsigned i = 0; i < 64; i++)
    n += (size_t)sprintf(s + n, "%sa%u", i ? "+" : "", i);
  n += (size_t)sprintf(s + n, ";}");
  execute(s, args, 64, 2080, 0);
  n = 0;
  n += (size_t)sprintf(s + n, "fn main(");
  for (unsigned i = 0; i < 65; i++)
    n += (size_t)sprintf(s + n, "%sa%u:u64", i ? "," : "", i);
  n += (size_t)sprintf(s + n, ")->u64{return 0;}");
  NtArtifact bad;
  CHECK(!nova_compile_v2(s, n, &bad) && bad.error.code == 403 &&
            !bad.text.bytes,
        "65 scalar slots rejected transactionally");
  nt_artifact_free(&bad);
  uint64_t too16 = 65536, too8 = 255, minus1 = UINT64_MAX,
           too32 = UINT64_C(0x100000000);
  execute("fn main(x:u16)->u64{return u64(x);}", &too16, 1, 0, NV2_ARGUMENT);
  execute("fn main(x:u32)->u64{return u64(x);}", &too32, 1, 0, NV2_ARGUMENT);
  execute("fn main(x:i8)->u64{return u64(x);}", &too8, 1, 0, NV2_ARGUMENT);
  execute("fn main(x:i8)->u64{return u64(x);}", &minus1, 1, UINT64_MAX, 0);
  printf("NOVA_BOUNDARY_TESTS checks=%u failures=%u\n", checks, failures);
  return failures ? 1 : 0;
}
