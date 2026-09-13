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
static void run(const char *s, int pass, uint64_t value, uint64_t error) {
  NtArtifact a;
  int ok = nova_compile_v2(s, strlen(s), &a);
  CHECK(ok == pass, "integer compile expectation");
  if (ok) {
    NovaContextV2 c;
    NovaHostImage image;
    CHECK(nova_context_init_v2(&c) && nova_host_map(&a, &image),
          "integer execution setup");
    uint64_t result = image.entry(&c, NULL, 0);
    CHECK(result == value && c.error == error, "integer machine result");
    if (result != value || c.error != error)
      fprintf(stderr, "got=%llu err=%llu expected=%llu/%llu\n",
              (unsigned long long)result, (unsigned long long)c.error,
              (unsigned long long)value, (unsigned long long)error);
    nova_host_unmap(&image);
    nova_context_destroy_v2(&c);
  } else if (pass)
    fprintf(stderr, "E%u %zu:%zu %s\n", a.error.code, a.error.line,
            a.error.column, a.error.message);
  nt_artifact_free(&a);
}
int main(void) {
  run("fn main()->u64{let "
      "b:buf<u16>=alloc(runtime(),2);b[0]=65535;b[1]=42;return u64(b[1]);}",
      1, 42, 0);
  run("fn main()->u64{let b:buf<i32>=alloc(runtime(),1);b[0]=-42;return "
      "u64(-b[0]);}",
      1, 42, 0);
  run("struct S{n:i16}fn main()->u64{let s:S=S{n:-1};s.n=-42;return "
      "u64(-s.n);}",
      1, 42, 0);
  run("fn main()->u64{let x:i64=-40;if x<0{return u64(-x)+2;}return 0;}", 1, 42,
      0);
  run("fn main()->u64{let x:i8=-1;return u64(x);}", 1, UINT64_MAX, 0);
  run("fn main()->u64{let x:i16=-32768;return u64(x);}", 1, UINT64_MAX - 32767,
      0);
  run("fn main()->u64{let x:i32=-2147483648;return u64(x);}", 1,
      UINT64_MAX - 2147483647, 0);
  run("fn main()->u64{let x:i64=-9;return u64(x/2);}", 1, UINT64_MAX - 3, 0);
  run("fn main()->u64{let x:i64=-9;return u64(x%2);}", 1, UINT64_MAX, 0);
  run("fn main()->u64{let x:i64=-9;return u64(x>>1);}", 1, UINT64_MAX - 4, 0);
  run("fn main()->u64{return u64(u32(4294967297));}", 1, 1, 0);
  run("fn main()->u64{return u64(u16(65578));}", 1, 42, 0);
  run("fn main()->u64{let x:i64=-9223372036854775808;return u64(x / -1);}", 1,
      0, 11);
  run("fn main()->u64{let x:u64=42;return x<<64;}", 1, 0, 12);
  run("fn main()->u64{let x:i8=128;return 0;}", 0, 0, 0);
  run("fn main()->u64{let x:i8=-129;return 0;}", 0, 0, 0);
  run("fn main()->u64{let x:i64=-1;let y:u64=2;return u64(x+y);}", 0, 0, 0);
  printf("NOVA_INTEGER_TESTS checks=%u failures=%u\n", checks, failures);
  return failures ? 1 : 0;
}
