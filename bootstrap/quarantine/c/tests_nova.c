#define _GNU_SOURCE
#include "nova.h"
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
    ++checks;                                                                  \
    if (!(c)) {                                                                \
      ++failures;                                                              \
      fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, m);               \
    }                                                                          \
  } while (0)

static uint64_t execute2(const NtArtifact *artifact, const void *input,
                         uint64_t length) {
  uint64_t result = 0;
#ifdef _WIN32
  void *memory = VirtualAlloc(NULL, artifact->text.size,
                              MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  DWORD old = 0;
  CHECK(memory != NULL, "Nova JIT allocation");
  if (!memory)
    return 0;
  memcpy(memory, artifact->text.bytes, artifact->text.size);
  CHECK(VirtualProtect(memory, artifact->text.size, PAGE_EXECUTE_READ, &old),
        "Nova JIT RX");
  FlushInstructionCache(GetCurrentProcess(), memory, artifact->text.size);
  result = ((uint64_t (*)(const void *, uint64_t))(
      (uint8_t *)memory + artifact->entry))(input, length);
  VirtualFree(memory, 0, MEM_RELEASE);
#else
  void *memory = mmap(NULL, artifact->text.size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(memory != MAP_FAILED, "Nova JIT allocation");
  if (memory == MAP_FAILED)
    return 0;
  memcpy(memory, artifact->text.bytes, artifact->text.size);
  CHECK(mprotect(memory, artifact->text.size, PROT_READ | PROT_EXEC) == 0,
        "Nova JIT RX");
  result = ((uint64_t(__attribute__((ms_abi)) *)(const void *, uint64_t))(
      (uint8_t *)memory + artifact->entry))(input, length);
  munmap(memory, artifact->text.size);
#endif
  return result;
}
static uint64_t compile_run(const char *name, const char *source,
                            const void *input, uint64_t length,
                            NtArtifact *keep) {
  NtArtifact artifact;
  int ok = nova_compile(source, strlen(source), &artifact);
  CHECK(ok, name);
  if (!ok)
    fprintf(stderr, " Nova E%u %zu:%zu %s\n", artifact.error.code,
            artifact.error.line, artifact.error.column, artifact.error.message);
  uint64_t result = ok ? execute2(&artifact, input, length) : 0;
  if (keep)
    *keep = artifact;
  else
    nt_artifact_free(&artifact);
  return result;
}
static void basic_language(void) {
  const char *literal = "fn main() -> u64 { return 42; }\n";
  NtArtifact artifact;
  CHECK(compile_run("literal Nova", literal, NULL, 0, &artifact) == 42,
        "Nova literal executes");
  CHECK(nt_make_pe(&artifact),
        "Nova literal packages through shared PE writer");
  nt_artifact_free(&artifact);
  const char *functions = "fn add(a:u64,b:u64) -> u64 { return a+b; }\n"
                          "fn main() -> u64 { return add(19,23); }\n";
  CHECK(compile_run("Nova functions", functions, NULL, 0, NULL) == 42,
        "Nova named function call executes");
  const char *loop = "fn main() -> u64 {\n"
                     " let mut i:u64=0;\n let mut total:u64=0;\n"
                     " while i<10 { i=i+1; total=total+i; }\n"
                     " if total==55 { return total; } else { return 0; }\n}\n";
  CHECK(compile_run("Nova loop", loop, NULL, 0, NULL) == 55,
        "Nova mutable locals, while and if execute");
}
static void decimal_parser_component(void) {
  const char *source =
      "fn main(input:buf<u8>) -> u64 {\n let length:u64=len(input);\n"
      " let mut value:u64=0;\n let mut i:u64=0;\n"
      " if length==0 { return 0x8000000100000000; }\n"
      " while i<length {\n"
      "  let ch:u8=input[i];\n"
      "  if ch<48 { return 0x8000000200000000|i; }\n"
      "  if ch>57 { return 0x8000000200000000|i; }\n"
      "  let digit:u64=ch-48;\n"
      "  if value>1844674407370955161 { return 0x8000000300000000|i; }\n"
      "  if value==1844674407370955161 {\n"
      "   if digit>5 { return 0x8000000300000000|i; }\n"
      "  }\n"
      "  value=value*10+digit;\n i=i+1;\n"
      " }\n return value;\n}\n";
  NtArtifact artifact;
  int ok = nova_compile(source, strlen(source), &artifact);
  CHECK(ok, "Nova decimal-u64 parser compiles");
  if (!ok) {
    fprintf(stderr, " Nova parser E%u %zu:%zu %s\n", artifact.error.code,
            artifact.error.line, artifact.error.column, artifact.error.message);
    nt_artifact_free(&artifact);
    return;
  }
  struct Case {
    const char *text;
    uint64_t expected;
  } cases[] = {{"0", 0},
               {"42", 42},
               {"123456789", 123456789},
               {"", UINT64_C(0x8000000100000000)},
               {"12a", UINT64_C(0x8000000200000002)},
               {"18446744073709551615", UINT64_MAX},
               {"18446744073709551616", UINT64_C(0x8000000300000013)}};
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
    CHECK(execute2(&artifact, cases[i].text, strlen(cases[i].text)) ==
              cases[i].expected,
          "Nova decimal parser corpus result");
  nt_artifact_free(&artifact);
}
static void negative_is_transactional(void) {
  const char *bad = "fn main() -> u64 { let x:u64=true; return x; }\n";
  NtArtifact artifact;
  CHECK(!nova_compile(bad, strlen(bad), &artifact) && artifact.error.code,
        "Nova rejects type mismatch");
  CHECK(!artifact.text.bytes && !artifact.pe.bytes,
        "Nova failure emits no partial artifact");
  nt_artifact_free(&artifact);
}
static void extended_language(void) {
  CHECK(compile_run("explicit u8 cast", "fn main()->u64{return u8(298);}", NULL,
                    0, NULL) == 42,
        "u8 cast truncates explicitly");
  CHECK(compile_run("short circuit",
                    "fn main(b:buf<u8>)->u64{if false && b[0]==1{return 1;} if "
                    "true || b[0]==1{return 42;} return 0;}",
                    NULL, 0, NULL) == 42,
        "logical operators short circuit bounds-invalid RHS");
  CHECK(compile_run("bounds", "fn main(b:buf<u8>)->u64{return b[0];}", NULL, 0,
                    NULL) == NOVA_BOUNDS_ERROR,
        "zero-length buffer is rejected before access");
  CHECK(compile_run("null bounds", "fn main(b:buf<u8>)->u64{return b[0];}",
                    NULL, 1, NULL) == NOVA_BOUNDS_ERROR,
        "null pointer with false length rejected");
  unsigned char b[3] = {1, 2, 3};
  unsigned char order[1] = {0};
  CHECK(compile_run("assignment evaluation order",
                    "fn index(b:buf<u8>)->u64{b[0]=u8(b[0]+1);return 0;}fn "
                    "value(b:buf<u8>)->u8{return b[0];}fn "
                    "main(b:buf<u8>)->u64{b[index(b)]=value(b);return b[0];}",
                    order, 1, NULL) == 1,
        "buffer index evaluates before assigned expression");
  CHECK(compile_run("buffer mutation",
                    "fn main(b:buf<u8>)->u64{b[1]=42;return b[1];}", b, 3,
                    NULL) == 42 &&
            b[0] == 1 && b[1] == 42 && b[2] == 3,
        "bounded buffer write preserves neighbours");
  CHECK(compile_run("loop shadow",
                    "fn main()->u64{let mut n:u64=0;while n<3{{let n:u64=10; "
                    "if n!=10{return 1;}}n=n+1;}return n;}",
                    NULL, 0, NULL) == 3,
        "scope shadow and loop re-evaluation");
  const char *bad[] = {"fn main()->u64{let x:u64=1;x=2;return x;}",
                       "fn main()->u64{let x:u8=256;return x;}",
                       "fn main()->u64{return 18446744073709551616;}",
                       "fn main()->u64{return 0x10000000000000000;}",
                       "fn main()->u64{return 0x;}",
                       "fn main()->u64{return 1garbage;}",
                       "fn main()->u64{if 1{return 2;}return 0;}",
                       "fn main()->u64{if true{return 2;}}",
                       "fn main()->u64{{let x:u64=1;}return x;}",
                       "fn main()->u64{let x:u64=1;let x:u64=2;return x;}",
                       "fn x(a:u64)->u64{return a;}fn main()->u64{return x();}",
                       "fn main()->u64{return missing(42);}",
                       "fn main()->u64{return true+1;}",
                       "fn main()->u64{return len(1);}",
                       "fn main()->u64{return 1;}fn main()->u64{return 2;}",
                       "fn main(a:buf<u8>,b:buf<u8>,c:u64)->u64{return 0;}"};
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
    NtArtifact a;
    CHECK(!nova_compile(bad[i], strlen(bad[i]), &a), "Nova negative rejected");
    CHECK(a.error.code && a.error.line == 1 && a.error.column > 0 &&
              !a.text.bytes && !a.pe.bytes,
          "positioned transactional failure");
    nt_artifact_free(&a);
  }
  NtFInput inputs[] = {
      {"helper.nova", "fn helper(x:u64)->u64{return x+2;}", 40},
      {"main.nova", "fn main()->u64{return helper(40);}", 34}};
  inputs[0].length = strlen(inputs[0].text);
  inputs[1].length = strlen(inputs[1].text);
  NtArtifact multi;
  CHECK(nova_compile_many(inputs, 2, &multi), "Nova multi-file function link");
  if (multi.text.bytes)
    CHECK(execute2(&multi, NULL, 0) == 42, "multi-file actual execution");
  nt_artifact_free(&multi);
  NtFInput split[] = {{"broken-one.nova", "fn main()->u64{", 15},
                      {"broken-two.nova", "return 42;}", 11}};
  CHECK(!nova_compile_many(split, 2, &multi) && !multi.text.bytes,
        "a declaration cannot cross a source-file boundary");
  nt_artifact_free(&multi);
}
int main(void) {
  basic_language();
  decimal_parser_component();
  negative_is_transactional();
  extended_language();
  printf("NOVA_TESTS checks=%u failures=%u\n", checks, failures);
  return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
