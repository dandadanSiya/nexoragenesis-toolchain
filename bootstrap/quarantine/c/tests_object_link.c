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
#ifdef NT_LINK_ALLOC_TEST
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
static int allocation_probe;
static size_t allocation_calls, allocation_fail = SIZE_MAX;
void *__wrap_malloc(size_t n) {
  if (allocation_probe && allocation_calls++ == allocation_fail)
    return NULL;
  return __real_malloc(n);
}
void *__wrap_calloc(size_t n, size_t s) {
  if (allocation_probe && allocation_calls++ == allocation_fail)
    return NULL;
  return __real_calloc(n, s);
}
#endif
#define CHECK(c, m)                                                            \
  do {                                                                         \
    checks++;                                                                  \
    if (!(c)) {                                                                \
      failures++;                                                              \
      fprintf(stderr, "FAIL %s\n", m);                                         \
    }                                                                          \
  } while (0)
static uint32_t read32(const uint8_t *p) {
  return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
static int executes42(const NtBuffer *b) {
#ifdef _WIN32
  void *p =
      VirtualAlloc(NULL, b->size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (!p)
    return 0;
  memcpy(p, b->bytes, b->size);
  DWORD old;
  if (!VirtualProtect(p, b->size, PAGE_EXECUTE_READ, &old)) {
    VirtualFree(p, 0, MEM_RELEASE);
    return 0;
  }
  FlushInstructionCache(GetCurrentProcess(), p, b->size);
#else
  void *p = mmap(NULL, b->size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED)
    return 0;
  memcpy(p, b->bytes, b->size);
  if (mprotect(p, b->size, PROT_READ | PROT_EXEC)) {
    munmap(p, b->size);
    return 0;
  }
#endif
  uint64_t (*fn)(void);
  memcpy(&fn, &p, sizeof(fn));
  int ok = fn() == 42;
#ifdef _WIN32
  VirtualFree(p, 0, MEM_RELEASE);
#else
  munmap(p, b->size);
#endif
  return ok;
}
int main(void) {
  uint8_t app[] = {0x48, 0x83, 0xec, 0x28, 0xe8, 0,    0,
                   0,    0,    0x48, 0x83, 0xc4, 0x28, 0xc3};
  uint8_t helper[] = {0xb8, 42, 0, 0, 0, 0xc3}, pointer[8] = {0};
  NtObjectSymbol syma[] = {{"app.main", "fn()->u64", 1,
                            NT_OBJECT_FUNCTION | NT_OBJECT_EXPORT, 0,
                            sizeof(app)},
                           {"lib.answer", "fn()->u64", 0,
                            NT_OBJECT_FUNCTION | NT_OBJECT_IMPORT, 0, 0}};
  NtObjectSymbol symb[] = {{"lib.answer", "fn()->u64", 1,
                            NT_OBJECT_FUNCTION | NT_OBJECT_EXPORT, 0,
                            sizeof(helper)}};
  NtObjectReloc rel[] = {{1, 1, 2, 5, 0}, {3, 2, 2, 0, 0}};
  NtObject inputs[2] = {{0}}, linked = {0}, decoded = {0};
  inputs[0].target = inputs[1].target = 1;
  inputs[0].entry_symbol = 1;
  inputs[0].sections[0] = (NtBuffer){app, sizeof(app), sizeof(app)};
  inputs[0].sections[2] = (NtBuffer){pointer, sizeof(pointer), sizeof(pointer)};
  inputs[1].sections[0] = (NtBuffer){helper, sizeof(helper), sizeof(helper)};
  inputs[0].symbols = syma;
  inputs[0].symbol_count = 2;
  inputs[1].symbols = symb;
  inputs[1].symbol_count = 1;
  inputs[0].relocations = rel;
  inputs[0].relocation_count = 2;
  NtDiagnostic error = {0};
  NtBuffer encoded = {0};
  NtArtifact artifact = {0};
  int ok = nt_object_link(inputs, 2, &linked, &error);
  CHECK(ok, "resolves exported import between two objects");
  if (ok) {
    CHECK(linked.symbol_count == 2 && linked.relocation_count == 2 &&
              linked.entry_symbol == 1,
          "closed metadata retained");
    CHECK(linked.symbols[1].offset == 16 && linked.sections[0].size == 22,
          "section placement alignment");
    CHECK(nt_object_write(&linked, &encoded, &error) &&
              nt_object_read(encoded.bytes, encoded.size, &decoded, &error),
          "linked NXO round trip");
    CHECK(nt_object_materialize(&decoded, &artifact, &error),
          "linked object materializes");
    if (artifact.text.bytes) {
      CHECK(read32(artifact.text.bytes + 5) == 7,
            "REL32 spans independent text sections");
      CHECK(executes42(&artifact.text), "actual cross object call returns42");
      CHECK(artifact.base_relocation_count == 1 &&
                artifact.base_relocations[0].section == 3,
            "data pointer carries DIR64 site");
      CHECK(nt_make_pe(&artifact), "linked object produces PE");
    }
    nt_artifact_free(&artifact);
    nt_object_free(&decoded);
    free(encoded.bytes);
    nt_object_free(&linked);
  }
#ifdef NT_LINK_ALLOC_TEST
  allocation_probe = 1;
  allocation_calls = 0;
  ok = nt_object_link(inputs, 2, &linked, &error);
  size_t allocations = allocation_calls;
  allocation_probe = 0;
  CHECK(ok && allocations > 0,
        "allocation failure campaign has successful baseline");
  nt_object_free(&linked);
  for (size_t j = 0; j < allocations; ++j) {
    allocation_calls = 0;
    allocation_fail = j;
    allocation_probe = 1;
    ok = nt_object_link(inputs, 2, &linked, &error);
    allocation_probe = 0;
    CHECK(!ok && error.code == 800 && !linked._owned && !linked.symbols &&
              !linked.relocations && !linked.sections[0].bytes &&
              !linked.sections[1].bytes && !linked.sections[2].bytes,
          "each link allocation failure returns an empty output");
    nt_object_free(&linked);
  }
  allocation_fail = SIZE_MAX;
#endif
  CHECK(!nt_object_link(inputs, 1, &linked, &error) && !linked._owned,
        "unresolved import fails transactionally");
  symb[0].flags = NT_OBJECT_FUNCTION;
  CHECK(!nt_object_link(inputs, 2, &linked, &error),
        "private definition cannot resolve import");
  symb[0].flags = NT_OBJECT_FUNCTION | NT_OBJECT_EXPORT;
  symb[0].contract = "fn()->u32";
  CHECK(!nt_object_link(inputs, 2, &linked, &error),
        "contract mismatch rejected");
  symb[0].contract = "fn()->u64";
  inputs[1].target = 2;
  CHECK(!nt_object_link(inputs, 2, &linked, &error), "mixed targets rejected");
  inputs[1].target = 1;
  inputs[1].entry_symbol = 1;
  CHECK(!nt_object_link(inputs, 2, &linked, &error),
        "multiple entries rejected");
  inputs[1].entry_symbol = 0;
  NtObject duplicate[2] = {inputs[1], inputs[1]};
  CHECK(!nt_object_link(duplicate, 2, &linked, &error),
        "duplicate definitions rejected");
  CHECK(!nt_object_link(inputs, 0, &linked, &error), "empty unit rejected");
  ok = nt_object_link(inputs + 1, 1, &linked, &error);
  CHECK(ok && !linked.entry_symbol, "closed library can link without entry");
  if (ok)
    CHECK(!nt_object_materialize(&linked, &artifact, &error),
          "library requires entry before PE");
  nt_object_free(&linked);
  nt_artifact_free(&artifact);
  CHECK(app[5] == 0 && pointer[0] == 0 && symb[0].offset == 0,
        "input objects unchanged");
  printf("NTASM_OBJECT_LINK checks=%u failures=%u\n", checks, failures);
  return failures ? 1 : 0;
}
