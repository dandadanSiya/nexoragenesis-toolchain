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
static void test(const char *app, const char *math, int pass,
                 uint64_t expected) {
  NtFInput in[] = {{"app.nova", app, strlen(app)},
                   {"math.nova", math, strlen(math)}};
  NtArtifact a;
  int ok = nova_compile_many_v2(in, 2, &a);
  CHECK(ok == pass, "module compile result");
  if (ok) {
    NovaContextV2 c;
    NovaHostImage image;
    CHECK(nova_context_init_v2(&c) && nova_host_map(&a, &image),
          "module execution setup");
    uint64_t got = image.entry(&c, NULL, 0);
    CHECK(!c.error && got == expected, "qualified module execution");
    nova_host_unmap(&image);
    nova_context_destroy_v2(&c);
  } else {
    CHECK(a.error.code && !a.text.bytes, "module error is transactional");
    if (pass)
      fprintf(stderr, "E%u %zu:%zu %s\n", a.error.code, a.error.line,
              a.error.column, a.error.message);
  }
  nt_artifact_free(&a);
}
int main(void) {
  test("module app;import math;fn main()->u64{return math.add(19,23);}",
       "module math;export fn add(a:u64,b:u64)->u64{return a+b;}", 1, 42);
  test("module app;fn main()->u64{return math.add(19,23);}",
       "module math;export fn add(a:u64,b:u64)->u64{return a+b;}", 0, 0);
  test("module app;import math;fn main()->u64{return math.add(19,23);}",
       "module math;fn add(a:u64,b:u64)->u64{return a+b;}", 0, 0);
  test("module app;import missing;fn main()->u64{return 42;}",
       "module math;export fn add(a:u64,b:u64)->u64{return a+b;}", 0, 0);
  test("module app;import math;fn add(a:u64,b:u64)->u64{return a-b;}fn "
       "main()->u64{return math.add(19,23)+add(5,5);}",
       "module math;export fn add(a:u64,b:u64)->u64{return a+b;}", 1, 42);
  test("module app;import math;export fn plus(a:u64)->u64{return a+1;}fn "
       "main()->u64{return math.value();}",
       "module math;import app;export fn value()->u64{return app.plus(41);}", 1,
       42);
  printf("NOVA_MODULE_TESTS checks=%u failures=%u\n", checks, failures);
  return failures ? 1 : 0;
}
