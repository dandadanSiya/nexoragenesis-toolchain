#define _GNU_SOURCE
#include "nova.h"
#include "nova/run_host.h"
#include "nova/runtime_v2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
static unsigned checks, failures;
#define CHECK(c, m)                                                            \
  do {                                                                         \
    checks++;                                                                  \
    if (!(c)) {                                                                \
      failures++;                                                              \
      fprintf(stderr, "FAIL %s\n", m);                                         \
    }                                                                          \
  } while (0)
static uint64_t run(NovaContextV2 *c, const char *s, const uint64_t *args,
                    size_t count) {
  NtArtifact a;
  int ok = nova_compile_v2(s, strlen(s), &a);
  CHECK(ok, "Nova v2 compiles");
  if (!ok) {
    fprintf(stderr, "E%u %zu:%zu %s\n", a.error.code, a.error.line,
            a.error.column, a.error.message);
    nt_artifact_free(&a);
    return 0;
  }
  NovaHostImage image;
  if (!nova_host_map(&a, &image))
    exit(2);
  uint64_t value = image.entry(c, args, count);
  nova_host_unmap(&image);
  nt_artifact_free(&a);
  return value;
}
static void expect(NovaContextV2 *c, const char *s, const uint64_t *args,
                   size_t n, uint64_t value, uint64_t error) {
  nova_error_clear_v2(c);
  uint64_t got = run(c, s, args, n);
  CHECK(got == value, "v2 value channel");
  CHECK(c->error == error, "v2 independent propagated error channel");
}
int main(void) {
  NovaContextV2 c;
  CHECK(nova_context_init_v2(&c), "context v2 initializes");
  expect(&c, "fn main()->u64{return 18446744073709551615;}", NULL, 0,
         UINT64_MAX, 0);
  expect(&c,
         "fn text()->buf<u8>{return \"42\";}fn main()->u64{let "
         "b:buf<u8>=text();return (b[0]-48)*10+b[1]-48;}",
         NULL, 0, 42, 0);
  expect(&c,
         "fn main()->u64{let b:buf<u8>=\"A\\n\\x42\\0\";if len(b)==4 && "
         "b[1]==10 && b[2]==66 && b[3]==0{return 42;}return 0;}",
         NULL, 0, 42, 0);
  expect(&c, "fn main()->u64{let b:buf<u8>=[40,2];b[1]=3;return b[0]+b[1];}",
         NULL, 0, 43, 0);
  expect(&c, "fn main()->u64{let b:buf<u8>=\"42\";b[0]=0;return 1;}", NULL, 0,
         0, NV2_READONLY);
  unsigned char b[2] = {42, 7};
  nova_error_clear_v2(&c);
  uint64_t h = nova_borrow_v2(&c, b, 2, 1);
  CHECK(h != 0, "borrowed buffer handle");
  uint64_t args[] = {h, 2};
  expect(&c, "fn main(b:buf<u8>)->u64{return b[0];}", args, 2, 42, 0);
  expect(&c,
         "fn bad(b:buf<u8>)->u8{return b[4];}fn main(b:buf<u8>)->u64{let "
         "x:u64=bad(b);b[0]=0;return x;}",
         args, 2, 0, NV2_BOUNDS);
  CHECK(b[0] == 42, "error aborts caller before subsequent mutation");
  expect(&c, "fn div(a:u64)->u8{return u8(7/a);}fn main()->u64{return div(0);}",
         NULL, 0, 0, NV2_DIVZERO);
  expect(&c,
         "fn main()->u64{let b:buf<u8>=alloc(runtime(),4);let "
         "alias:buf<u8>=b;rt_free(runtime(),b);return alias[0];}",
         NULL, 0, 0, NV2_LIFETIME);
  expect(
      &c,
      "fn drop(b:buf<u8>)->u8{rt_free(runtime(),b);return 1;}fn "
      "main()->u64{let b:buf<u8>=alloc(runtime(),4);b[0]=drop(b);return 42;}",
      NULL, 0, 0, NV2_LIFETIME);
  expect(&c,
         "fn main()->u64{let b:buf<u8>=alloc(runtime(),4);let "
         "part:buf<u8>=slice(b,1,2);rt_free(runtime(),b);return part[0];}",
         NULL, 0, 0, NV2_LIFETIME);
  expect(&c,
         "fn main()->u64{let b:buf<u8>=alloc(runtime(),4);let "
         "part:buf<u8>=slice(b,1,2);part[0]=42;let "
         "x:u64=b[1];rt_free(runtime(),part);rt_free(runtime(),b);return x;}",
         NULL, 0, 42, 0);
  nova_error_clear_v2(&c);
  uint64_t ro = nova_borrow_v2(&c, b, 2, 0);
  uint64_t readonly[] = {ro, 2};
  expect(&c, "fn main(b:buf<u8>)->u64{b[0]=1;return 0;}", readonly, 2, 0,
         NV2_READONLY);
  CHECK(b[0] == 42, "readonly borrowed memory preserved");
  expect(&c, "fn main(x:u64)->u64{return x;}", NULL, 0, 0, NV2_ARGUMENT);
  expect(
      &c,
      "module mercury_buffer_field;\n"
      "struct Holder { bytes:buf<u8> }\n"
      "fn duplicate(source:buf<u8>)->buf<u8>{\n"
      "    let size:u64=len(source);\n"
      "    let out:buf<u8>=alloc(runtime(),size);\n"
      "    if size>0 { rt_copy(runtime(),out,source,size); }\n"
      "    return out;\n"
      "}\n"
      "fn main()->u64{\n"
      "    let input:buf<u8>=\"42\";\n"
      "    let holder:Holder=Holder{bytes:input};\n"
      "    let copy:buf<u8>=duplicate(holder.bytes);\n"
      "    let size:u64=len(copy);\n"
      "    rt_free(runtime(),copy);\n"
      "    rt_free(runtime(),holder);\n"
      "    return size;\n"
      "}\n",
      NULL, 0, 2, 0);
  expect(
      &c,
      "struct Holder{bytes:buf<u8>}"
      "fn main()->u64{let b:buf<u8>=alloc(runtime(),4);"
      "let holder:Holder=Holder{bytes:b};"
      "let view:buf<u8>=slice(holder.bytes,1,2);"
      "view[0]=42;let val:u64=view[0];"
      "rt_free(runtime(),view);rt_free(runtime(),holder);"
      "rt_free(runtime(),b);return val;}",
      NULL, 0, 42, 0);
  expect(
      &c,
      "struct Holder{bytes:buf<u8>}"
      "fn main()->u64{let b:buf<u8>=alloc(runtime(),4);"
      "let holder:Holder=Holder{bytes:b};"
      "let view:buf<u8>=slice(holder.bytes,1,2);"
      "rt_free(runtime(),b);return view[0];}",
      NULL, 0, 0, NV2_LIFETIME);
  expect(
      &c,
      "struct Holder{bytes:buf<u8>}"
      "fn duplicate(source:buf<u8>)->u64{return len(source);}"
      "fn main()->u64{let b:buf<u8>=alloc(runtime(),2);"
      "let holder:Holder=Holder{bytes:b};rt_free(runtime(),b);"
      "return duplicate(holder.bytes);}",
      NULL, 0, 0, NV2_LIFETIME);
  CHECK(nova_context_destroy_v2(&c), "context v2 cleans owned buffers/views");
  printf("NOVA_V2_TESTS checks=%u failures=%u\n", checks, failures);
  return failures ? 1 : 0;
}
