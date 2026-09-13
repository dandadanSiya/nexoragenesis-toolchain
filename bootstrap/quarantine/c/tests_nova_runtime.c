#define _GNU_SOURCE
#include "nova.h"
#include "nova/runtime.h"
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
static uint64_t execute(const NtArtifact *a, NovaRuntimeV1 *r, const void *p,
                        uint64_t n) {
  void *mem;
#ifdef _WIN32
  mem = VirtualAlloc(0, a->text.size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!mem)
    exit(2);
  DWORD old;
  memcpy(mem, a->text.bytes, a->text.size);
  if (!VirtualProtect(mem, a->text.size, PAGE_EXECUTE_READ, &old))
    exit(2);
  FlushInstructionCache(GetCurrentProcess(), mem, a->text.size);
#else
  mem = mmap(0, a->text.size, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mem == MAP_FAILED)
    exit(2);
  memcpy(mem, a->text.bytes, a->text.size);
  if (mprotect(mem, a->text.size, PROT_READ | PROT_EXEC))
    exit(2);
#endif
  uint64_t value =
      ((uint64_t(NOVA_ABI *)(NovaRuntimeV1 *, const void *, uint64_t))(
          (unsigned char *)mem + a->entry))(r, p, n);
#ifdef _WIN32
  VirtualFree(mem, 0, MEM_RELEASE);
#else
  munmap(mem, a->text.size);
#endif
  return value;
}
static uint64_t run(NovaRuntimeV1 *r, const char *s, const void *p,
                    uint64_t n) {
  NtArtifact a;
  int ok = nova_compile(s, strlen(s), &a);
  CHECK(ok, "runtime Nova source compiles");
  if (!ok)
    fprintf(stderr, "E%u %zu:%zu %s\n", a.error.code, a.error.line,
            a.error.column, a.error.message);
  uint64_t result = ok ? execute(&a, r, p, n) : 0;
  nt_artifact_free(&a);
  return result;
}
int main(int argc, char **argv) {
  if (argc != 2 && argc != 3) {
    fprintf(stderr, "supply fresh output file path [runtime-roundtrip.nova]\n");
    return 2;
  }
  NovaRuntimeV1 r;
  int ok = nova_runtime_init(&r);
  CHECK(ok, "runtime initializes");
  if (ok) {
    if (argc == 3) {
      FILE *source_file = fopen(argv[2], "rb");
      CHECK(source_file != NULL, "open real runtime-roundtrip.nova");
      if (source_file) {
        char *source = malloc(65536);
        if (!source) {
          fclose(source_file);
          nova_runtime_destroy(&r);
          return 2;
        }
        size_t size = fread(source, 1, 65535, source_file);
        source[size] = 0;
        fclose(source_file);
        char *path = malloc(strlen(argv[1]) + 9);
        if (!path) {
          free(source);
          nova_runtime_destroy(&r);
          return 2;
        }
        sprintf(path, "%s.example", argv[1]);
        CHECK(run(&r, source, path, strlen(path)) == 42,
              "real .nova runtime roundtrip file compiles and executes");
        free(path);
        free(source);
      }
    }
    CHECK(run(&r,
              "fn main(rt:runtime)->u64{let "
              "b:buf<u8>=alloc(rt,4);b[0]=40;b[1]=2;let "
              "result:u64=b[0]+b[1];rt_free(rt,b);return result;}",
              NULL, 0) == 42,
          "compiled Nova allocates, computes, frees");
    CHECK(run(&r,
              "fn main(rt:runtime)->u64{let b:buf<u8>=alloc(rt,4);b[2]=42;let "
              "part:buf<u8>=slice(b,2,1);let "
              "result:u64=part[0];rt_free(rt,b);return result;}",
              NULL, 0) == 42,
          "compiled Nova bounded slice");
    const char *write = "fn main(rt:runtime,path:buf<u8>)->u64{let "
                        "b:buf<u8>=alloc(rt,2);b[0]=52;b[1]=50;let "
                        "h:u64=rt_open_new(rt,path);if "
                        "h==18446744073709551615{rt_free(rt,b);return h;}let "
                        "count:u64=rt_write(rt,h,b);let "
                        "closed:u64=rt_close(rt,h);rt_free(rt,b);if "
                        "closed!=0{return closed;}return count;}";
    CHECK(run(&r, write, argv[1], strlen(argv[1])) == 2,
          "compiled Nova creates file and writes bytes");
    CHECK(run(&r, write, argv[1], strlen(argv[1])) == UINT64_MAX,
          "runtime refuses overwrite");
    const char *read =
        "fn main(rt:runtime,path:buf<u8>)->u64{let b:buf<u8>=alloc(rt,16);let "
        "h:u64=rt_open_read(rt,path);if "
        "h==18446744073709551615{rt_free(rt,b);return h;}let "
        "count:u64=rt_read(rt,h,b);rt_close(rt,h);let "
        "used:buf<u8>=slice(b,0,count);let "
        "value:u64=(used[0]-48)*10+(used[1]-48);rt_free(rt,b);return value;}";
    CHECK(run(&r, read, argv[1], strlen(argv[1])) == 42,
          "compiled Nova reads actual file and computes decimal");
    uint64_t pointer = r.allocate(r.context, 16, 0, 0);
    CHECK(pointer != 0, "direct allocation");
    CHECK(r.release(r.context, pointer, 16, 0) == 0,
          "direct release owned allocation");
    CHECK(r.release(r.context, pointer, 16, 0) == UINT64_MAX,
          "double release rejected");
    CHECK(r.allocate(r.context, UINT64_MAX, 0, 0) == 0, "allocation limit");
    CHECK(r.close(r.context, 1, 0, 0) == UINT64_MAX,
          "invalid file handle rejected");
    CHECK(run(&r,
              "fn main(rt:runtime)->u64{let b:buf<u8>=alloc(rt,4);let "
              "part:buf<u8>=slice(b,3,2);return len(part);}",
              NULL, 0) == NOVA_BOUNDS_ERROR,
          "slice overflow rejected");
    r.version = 2;
    CHECK(run(&r,
              "fn main(rt:runtime)->u64{let b:buf<u8>=alloc(rt,4);return "
              "len(b);}",
              NULL, 0) == UINT64_C(0x8000000600000000),
          "compiled runtime version guard");
    r.version = 1;
    r.size = 16;
    CHECK(run(&r,
              "fn main(rt:runtime)->u64{let b:buf<u8>=alloc(rt,4);return "
              "len(b);}",
              NULL, 0) == UINT64_C(0x8000000600000000),
          "compiled runtime size guard");
    r.size = 88;
    NovaRuntimeCall saved_allocate = r.allocate;
    r.allocate = NULL;
    CHECK(run(&r,
              "fn main(rt:runtime)->u64{let b:buf<u8>=alloc(rt,4);return "
              "len(b);}",
              NULL, 0) == UINT64_C(0x8000000600000000),
          "compiled null callback guard");
    r.allocate = saved_allocate;
    CHECK(run(NULL,
              "fn main(rt:runtime)->u64{let b:buf<u8>=alloc(rt,4);return "
              "len(b);}",
              NULL, 0) == UINT64_C(0x8000000600000000),
          "compiled null runtime guard");
  }
  CHECK(nova_runtime_destroy(&r), "runtime destroy cleans owned resources");
  printf("NOVA_RUNTIME_TESTS checks=%u failures=%u\n", checks, failures);
  return failures ? 1 : 0;
}
