#include "object.h"
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
static int run(const NtArtifact *a) {
#ifdef _WIN32
  void *p = VirtualAlloc(NULL, a->text.size, MEM_COMMIT | MEM_RESERVE,
                         PAGE_READWRITE);
  if (!p)
    return 0;
  memcpy(p, a->text.bytes, a->text.size);
  DWORD old;
  if (!VirtualProtect(p, a->text.size, PAGE_EXECUTE_READ, &old)) {
    VirtualFree(p, 0, MEM_RELEASE);
    return 0;
  }
  FlushInstructionCache(GetCurrentProcess(), p, a->text.size);
#else
  void *p = mmap(NULL, a->text.size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED)
    return 0;
  memcpy(p, a->text.bytes, a->text.size);
  if (mprotect(p, a->text.size, PROT_READ | PROT_EXEC)) {
    munmap(p, a->text.size);
    return 0;
  }
#endif
  void *entry = (uint8_t *)p + a->entry;
  uint64_t (*fn)(void);
  memcpy(&fn, &entry, sizeof(fn));
  int ok = fn() == 42;
#ifdef _WIN32
  VirtualFree(p, 0, MEM_RELEASE);
#else
  munmap(p, a->text.size);
#endif
  return ok;
}
static const char *app =
    "module app\ntarget x86_64-nexora-uefi\nimport math.add as add: fn(in "
    "a:u64 @rcx,in b:u64 @rdx)->u64\neffects {}\nclobbers {}\nsection .text "
    "{\nexport fn main()->u64\neffects {}\nclobbers {} {\nreturn "
    "add(20,22);\n}\n}\n";
static const char *math =
    "module math\ntarget x86_64-nexora-uefi\nsection .text {\nexport fn add(in "
    "a:u64 @rcx,in b:u64 @rdx)->u64\neffects {}\nclobbers {} {\nreturn "
    "a+b;\n}\n}\n";
int main(void) {
  NtFInput inputs[] = {{"app.ntasm", app, strlen(app)},
                       {"math.ntasm", math, strlen(math)}};
  NtObject objects[2] = {{0}}, loaded[2] = {{0}}, linked = {0};
  NtBuffer bytes[2] = {{0}};
  NtDiagnostic e = {0};
  NtArtifact a = {0};
  NtFProgram whole = {0};
  CHECK(nt_frontend_compile(inputs, 2, &whole),
        "source fixtures valid as a closed unit");
  nt_frontend_free(&whole);
  int oka = nt_object_compile(inputs, 1, objects, &e);
  if (!oka)
    fprintf(stderr, "app E%u: %s\n", e.code, e.message);
  CHECK(oka, "app source compiles with unresolved typed import");
  int okb = nt_object_compile(inputs + 1, 1, objects + 1, &e);
  if (!okb)
    fprintf(stderr, "library E%u: %s\n", e.code, e.message);
  CHECK(okb, "library source compiles without main");
  if (oka && okb) {
    CHECK(objects[0].symbol_count == 2 && objects[0].relocation_count == 1 &&
              (objects[0].symbols[1].flags & NT_OBJECT_IMPORT),
          "app retains import symbol and call relocation");
    CHECK(objects[1].entry_symbol == 0 && objects[1].symbol_count == 1,
          "library metadata has no invented entry");
    for (unsigned j = 0; j < 2; j++)
      CHECK(nt_object_write(objects + j, bytes + j, &e) &&
                nt_object_read(bytes[j].bytes, bytes[j].size, loaded + j, &e),
            "separate source object roundtrip");
    CHECK(!nt_object_link(loaded, 1, &linked, &e),
          "missing library remains unresolved");
    int ok = nt_object_link(loaded, 2, &linked, &e) &&
             nt_object_materialize(&linked, &a, &e);
    CHECK(ok, "independent source objects link and materialize");
    if (ok) {
      CHECK(run(&a), "independently compiled cross-module call returns42");
      CHECK(nt_make_pe(&a), "separate compilation creates PE");
    }
    nt_artifact_free(&a);
    nt_object_free(&linked);
    NtObject reversed[] = {loaded[1], loaded[0]};
    ok = nt_object_link(reversed, 2, &linked, &e) &&
         nt_object_materialize(&linked, &a, &e);
    CHECK(ok && a.entry > 0, "library-first link carries real entry offset");
    if (ok)
      CHECK(run(&a), "library-first executable returns42");
    nt_artifact_free(&a);
    nt_object_free(&linked);
  }
  NtFProgram p = {0};
  CHECK(!nt_frontend_compile(inputs, 1, &p),
        "ordinary closed frontend still rejects unresolved import");
  nt_frontend_free(&p);
  CHECK(!nt_compile_many(inputs + 1, 1, &a),
        "ordinary executable compiler still requires main");
  nt_artifact_free(&a);
  for (unsigned j = 0; j < 2; j++) {
    nt_object_free(objects + j);
    nt_object_free(loaded + j);
    free(bytes[j].bytes);
  }
  printf("NTASM_SEPARATE_SOURCE checks=%u failures=%u\n", checks, failures);
  return failures ? 1 : 0;
}
