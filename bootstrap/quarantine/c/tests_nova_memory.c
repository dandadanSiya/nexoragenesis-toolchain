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
static void run(const char *s, uint64_t wanted, uint64_t error) {
  NtArtifact a;
  int ok = nova_compile_v2(s, strlen(s), &a);
  CHECK(ok, "memory source compiles");
  if (!ok) {
    fprintf(stderr, "E%u %s\n", a.error.code, a.error.message);
    nt_artifact_free(&a);
    return;
  }
  NovaContextV2 c;
  NovaHostImage image;
  CHECK(nova_context_init_v2(&c) && nova_host_map(&a, &image),
        "memory entry setup");
  uint64_t got = image.entry(&c, NULL, 0);
  CHECK(got == wanted && c.error == error, "checked memory operation result");
  if (got != wanted || c.error != error)
    fprintf(stderr, "got=%llu error=%llu\n", (unsigned long long)got,
            (unsigned long long)c.error);
  nova_host_unmap(&image);
  nova_context_destroy_v2(&c);
  nt_artifact_free(&a);
}
int main(void) {
  run("fn main()->u64{let b:buf<u8>=[1,2,3,4,5];let d:buf<u8>=slice(b,1,4);let "
      "s:buf<u8>=slice(b,0,4);rt_copy(runtime(),d,s,4);return "
      "b[0]*10000+b[1]*1000+b[2]*100+b[3]*10+b[4];}",
      11234, 0);
  run("fn main()->u64{let b:buf<u8>=[1,2,3,4,5];let d:buf<u8>=slice(b,0,4);let "
      "s:buf<u8>=slice(b,1,4);rt_copy(runtime(),d,s,4);return "
      "b[0]*10000+b[1]*1000+b[2]*100+b[3]*10+b[4];}",
      23455, 0);
  run("fn main()->u64{let d:buf<u8>=\"42\";let "
      "s:buf<u8>=[1,2];rt_copy(runtime(),d,s,2);return 42;}",
      0, NV2_READONLY);
  run("fn main()->u64{let d:buf<u8>=alloc(runtime(),4);let "
      "s:buf<u8>=[1];rt_copy(runtime(),d,s,2);return 42;}",
      0, NV2_BOUNDS);
  run("fn main()->u64{let d:buf<u8>=alloc(runtime(),0);let "
      "s:buf<u8>=alloc(runtime(),0);rt_free(runtime(),s);rt_copy(runtime(),d,s,"
      "0);return 42;}",
      0, NV2_LIFETIME);
  run("fn main()->u64{let b:buf<u8>=alloc(runtime(),33554432);let "
      "d:buf<u8>=slice(b,33554430,2);let "
      "s:buf<u8>=\"42\";rt_copy(runtime(),d,s,2);return "
      "b[33554430]+b[33554431]-60;}",
      42, 0);
  run("fn main()->u64{let b:buf<u8>=alloc(runtime(),67108865);return len(b);}",
      0, NV2_BOUNDS);
  printf("NOVA_MEMORY_TESTS checks=%u failures=%u\n", checks, failures);
  return failures ? 1 : 0;
}
