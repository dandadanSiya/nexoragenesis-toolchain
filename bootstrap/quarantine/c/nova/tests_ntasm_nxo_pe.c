/* BOOTSTRAP_C_TEST: NXO -> PE oracle harness for the Nova ntasm_nxo_pe
 * adapter. It reads a frozen NXOV0001 image with the snapshot object.c,
 * materializes the executable artifact exactly as object_materialize.c does
 * (deep copy, REL32/DIR64 arithmetic, DIR64 base relocations) and lays it out
 * with the frozen pe_oracle.c writer. The Nova adapter is compiled from
 * toolchain/ntasm-nova/{nxo_pe,nxo,pe}.nova with the same snapshot compiler
 * and must reproduce the oracle byte for byte. The materializer is mirrored
 * here rather than linked from the live tree so the whole oracle stays inside
 * the frozen snapshot; object_materialize.c is not part of that snapshot. */
#include "nova.h"
#include "nova/run_host.h"
#include "nova/pe_oracle.h"
#include "nova/pe_layout_oracle.h"
#include "object.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, failures, cases;
#define CHECK(c, m)                                                            \
  do {                                                                         \
    checks++;                                                                  \
    if (!(c)) {                                                                \
      failures++;                                                              \
      fprintf(stderr, "FAIL %s\n", m);                                         \
    }                                                                          \
  } while (0)
static char *load(const char *path, size_t *length) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  if (fseek(f, 0, SEEK_END)) { fclose(f); return NULL; }
  long n = ftell(f);
  if (n < 0 || n > 4 * 1024 * 1024 || fseek(f, 0, SEEK_SET)) { fclose(f); return NULL; }
  char *p = malloc((size_t)n + 1);
  if (!p) { fclose(f); return NULL; }
  size_t got = fread(p, 1, (size_t)n, f);
  fclose(f);
  if (got != (size_t)n) { free(p); return NULL; }
  p[got] = 0;
  *length = got;
  return p;
}
static int oracle_materialize(const NtObject *o, NovaOraclePeImage *a) {
  NtDiagnostic error;
  memset(&error, 0, sizeof(error));
  if (!nt_object_validate(o, &error))
    return 0;
  if (o->target != 1 || !o->entry_symbol)
    return 0;
  for (size_t j = 0; j < o->symbol_count; j++)
    if (o->symbols[j].flags & NT_OBJECT_IMPORT)
      return 0;
  NtPeLayout layout;
  if (!nt_pe_layout(o->sections[0].size, o->sections[1].size,
                    o->sections[2].size, &layout))
    return 0;
  NtBuffer *parts[3] = {&a->text, &a->rdata, &a->data};
  for (unsigned j = 0; j < 3; j++) {
    parts[j]->size = parts[j]->capacity = o->sections[j].size;
    if (parts[j]->size) {
      parts[j]->bytes = malloc(parts[j]->size);
      if (!parts[j]->bytes)
        return 0;
      memcpy(parts[j]->bytes, o->sections[j].bytes, parts[j]->size);
    }
  }
  a->entry = (size_t)o->symbols[o->entry_symbol - 1].offset;
  size_t absolute = 0;
  for (size_t j = 0; j < o->relocation_count; j++)
    if (o->relocations[j].kind == NT_OBJECT_DIR64)
      absolute++;
  if (absolute) {
    a->base_relocations = calloc(absolute, sizeof(*a->base_relocations));
    if (!a->base_relocations)
      return 0;
  }
  for (size_t j = 0; j < o->relocation_count; j++) {
    const NtObjectReloc *r = &o->relocations[j];
    const NtObjectSymbol *s = &o->symbols[r->target - 1];
    uint64_t source = nt_pe_logical_rva(&layout, r->section) + r->offset;
    uint64_t target = nt_pe_logical_rva(&layout, s->section) + s->offset, value;
    if (!nt_relocation_value(r->kind, source, target, r->addend, &value))
      return 0;
    NtBuffer *sec = r->section == 1 ? &a->text
                    : r->section == 2 ? &a->rdata
                                      : &a->data;
    nt_relocation_store(sec->bytes + (size_t)r->offset,
                        r->kind == NT_OBJECT_DIR64 ? 8 : 4, value);
    if (r->kind == NT_OBJECT_DIR64)
      a->base_relocations[a->base_relocation_count++] =
          (NovaOracleBaseReloc){r->section, r->offset};
  }
  return 1;
}
static void parity(NovaHostImage *image, const uint8_t *input, size_t size,
                   unsigned mode) {
  NtObject reference = {0};
  NtDiagnostic error = {0};
  int readable = nt_object_read(input, size, &reference, &error);
  if (readable && mode) {
    if (mode == 1)
      reference.entry_symbol = 0;
    if (mode == 2)
      reference.entry_symbol = 65537;
    if (mode == 3 && reference.relocation_count)
      reference.relocations[0].kind = 0;
  }
  NovaOraclePeImage oracle = {0};
  uint8_t *out = NULL;
  size_t capacity = size + 65536;
  out = malloc(capacity + 16);
  CHECK(out != NULL, "output allocation");
  if (out) {
    memset(out, 0xcc, capacity + 16);
    int wanted = readable && oracle_materialize(&reference, &oracle) &&
                 nova_oracle_make_pe(&oracle);
    NovaContextV2 context = {0};
    int ready = nova_context_init_v2(&context);
    CHECK(ready, "context init");
    if (ready) {
      uint64_t in_handle = nova_borrow_v2(&context, (void *)input, size, 0);
      uint64_t out_handle = nova_borrow_v2(&context, out, capacity, 1);
      uint64_t args[] = {in_handle, size, out_handle, capacity, mode};
      uint64_t returned = image->entry(&context, args, 5);
      cases++;
      int failed = returned >= (UINT64_C(1) << 63);
      int match = !context.error &&
                  (wanted ? (returned == oracle.pe.size && oracle.pe.bytes &&
                             !memcmp(out, oracle.pe.bytes, (size_t)oracle.pe.size))
                          : failed);
      if (!match)
        fprintf(stderr, "case%u size%zu mode%u expected%d ret%" PRIu64
                        " runtime%" PRIu64 "\n",
                cases, size, mode, wanted, returned, context.error);
      CHECK(match, "Nova NXO->PE matches oracle");
      int intact = 1;
      size_t guard = wanted ? (size_t)oracle.pe.size : 0;
      for (size_t j = guard; j < capacity + 16; j++)
        if (out[j] != 0xcc)
          intact = 0;
      CHECK(intact, "failure transaction and output guards");
      CHECK(nova_context_destroy_v2(&context), "context cleanup");
    }
    free(out);
  }
  free(oracle.text.bytes);
  free(oracle.rdata.bytes);
  free(oracle.data.bytes);
  free(oracle.base_relocations);
  free(oracle.pe.bytes);
  nt_object_free(&reference);
}
static void run_fixture(NovaHostImage *image, const NtObject *o,
                        const unsigned *modes, unsigned mode_count) {
  NtBuffer file = {0};
  NtDiagnostic error = {0};
  if (!nt_object_write(o, &file, &error)) {
    fprintf(stderr, "fixture serialization E%u %s\n", error.code, error.message);
    CHECK(0, "fixture serialization");
    return;
  }
  CHECK(file.bytes != NULL, "fixture bytes");
  if (!file.bytes)
    return;
  for (unsigned i = 0; i < mode_count; i++)
    parity(image, file.bytes, file.size, modes[i]);
  for (size_t at = 0; at < file.size; at++) {
    file.bytes[at] ^= 0x80;
    parity(image, file.bytes, file.size, 0);
    file.bytes[at] ^= 0x80;
  }
  for (size_t length = 0; length < file.size; length += 17)
    parity(image, file.bytes, length, 0);
  free(file.bytes);
}
int main(void) {
  size_t sizes[4] = {0};
  char *sources[4] = {
      load("toolchain/ntasm-nova/tests/nxo-pe-entry.nova", sizes),
      load("toolchain/ntasm-nova/nxo_pe.nova", sizes + 1),
      load("toolchain/ntasm-nova/nxo.nova", sizes + 2),
      load("toolchain/ntasm-nova/pe.nova", sizes + 3)};
  int all = sources[0] && sources[1] && sources[2] && sources[3];
  CHECK(all, "Nova modules load");
  NtFInput inputs[] = {{"nxo-pe-entry.nova", sources[0], sizes[0]},
                       {"nxo_pe.nova", sources[1], sizes[1]},
                       {"nxo.nova", sources[2], sizes[2]},
                       {"pe.nova", sources[3], sizes[3]}};
  NtArtifact artifact = {0};
  NovaHostImage image = {0};
  int compiled = all && nova_compile_many_v2(inputs, 4, &artifact);
  CHECK(compiled, "NXO->PE Nova modules compile");
  if (!compiled)
    fprintf(stderr, "E%u %zu:%zu %s\n", artifact.error.code,
            artifact.error.line, artifact.error.column, artifact.error.message);
  if (compiled)
    CHECK(nova_host_map(&artifact, &image), "map NXO->PE machine code");
  if (image.memory) {
    static uint8_t code[16] = {0x48, 0x8b, 0x01, 0x48, 0x89, 0x01, 0x48,
                               0x31, 0xc0, 0x48, 0x8b, 0x41, 0x08, 0xc3,
                               0x90, 0x90};
    static uint8_t tail[16] = {0};
    static uint8_t ro[16] = {0};
    unsigned modes[] = {0, 1, 2, 3};
    /* Executable: REL32 call plus DIR64 pointers in text/data. */
    NtObjectSymbol symbolsA[] = {{"a", "data:ptr", 3, NT_OBJECT_VARIABLE, 0, 8},
                                 {"b", "fn:u64", 1, NT_OBJECT_FUNCTION, 0, 16}};
    NtObjectReloc relocsA[] = {{1, NT_OBJECT_REL32, 2, 0, 0},
                               {1, NT_OBJECT_DIR64, 1, 8, 0},
                               {3, NT_OBJECT_DIR64, 1, 0, 0}};
    NtObject exec = {0};
    exec.target = 1;
    exec.entry_symbol = 2;
    exec.symbols = symbolsA;
    exec.symbol_count = 2;
    exec.relocations = relocsA;
    exec.relocation_count = 3;
    exec.sections[0] = (NtBuffer){code, sizeof(code), sizeof(code)};
    exec.sections[2] = (NtBuffer){tail, sizeof(tail), sizeof(tail)};
    run_fixture(&image, &exec, modes, 4);
    /* Executable: text/rdata/data with three DIR64 sites on distinct pages. */
    NtObjectSymbol symbolsB[] = {{"a", "data:ptr", 2, NT_OBJECT_VARIABLE, 0, 8},
                                 {"b", "fn:u64", 1, NT_OBJECT_FUNCTION, 0, 16},
                                 {"c", "data:ptr", 3, NT_OBJECT_VARIABLE, 0, 8}};
    NtObjectReloc relocsB[] = {{1, NT_OBJECT_REL32, 2, 0, 0},
                               {1, NT_OBJECT_DIR64, 3, 8, 0},
                               {2, NT_OBJECT_DIR64, 3, 8, 0},
                               {3, NT_OBJECT_DIR64, 1, 0, 0}};
    NtObject exec2 = {0};
    exec2.target = 1;
    exec2.entry_symbol = 2;
    exec2.symbols = symbolsB;
    exec2.symbol_count = 3;
    exec2.relocations = relocsB;
    exec2.relocation_count = 4;
    exec2.sections[0] = (NtBuffer){code, sizeof(code), sizeof(code)};
    exec2.sections[1] = (NtBuffer){ro, sizeof(ro), sizeof(ro)};
    exec2.sections[2] = (NtBuffer){tail, sizeof(tail), sizeof(tail)};
    run_fixture(&image, &exec2, modes, 4);
    /* Library without entry: UEFI target, no entry symbol. */
    NtObjectSymbol symbolsC[] = {{"a", "data:ptr", 2, NT_OBJECT_VARIABLE, 0, 8}};
    NtObject library = {0};
    library.target = 1;
    library.entry_symbol = 0;
    library.symbols = symbolsC;
    library.symbol_count = 1;
    library.sections[0] = (NtBuffer){code, sizeof(code), sizeof(code)};
    library.sections[1] = (NtBuffer){ro, sizeof(ro), sizeof(ro)};
    unsigned only0[] = {0};
    run_fixture(&image, &library, only0, 1);
    /* Bare x64 library: non-UEFI target cannot materialize a PE. */
    NtObject bare = {0};
    bare.target = 2;
    bare.entry_symbol = 0;
    bare.symbols = symbolsC;
    bare.symbol_count = 1;
    bare.sections[0] = (NtBuffer){code, sizeof(code), sizeof(code)};
    bare.sections[1] = (NtBuffer){ro, sizeof(ro), sizeof(ro)};
    bare.sections[2] = (NtBuffer){tail, sizeof(tail), sizeof(tail)};
    run_fixture(&image, &bare, only0, 1);
    /* Unresolved import: valid NXO, but no PE without a linker. */
    NtObjectSymbol symbolsE[] = {{"a", "fn:u64", 0,
                                  NT_OBJECT_IMPORT | NT_OBJECT_FUNCTION, 0, 0},
                                 {"b", "fn:u64", 1, NT_OBJECT_FUNCTION, 0, 16}};
    NtObject imported = {0};
    imported.target = 1;
    imported.entry_symbol = 2;
    imported.symbols = symbolsE;
    imported.symbol_count = 2;
    imported.sections[0] = (NtBuffer){code, sizeof(code), sizeof(code)};
    run_fixture(&image, &imported, only0, 1);
  }
  nova_host_unmap(&image);
  nt_artifact_free(&artifact);
  for (unsigned i = 0; i < 4; i++)
    free(sources[i]);
  printf("NTASM_NOVA_NXO_PE checks=%u failures=%u cases=%u\n", checks, failures,
         cases);
  return failures ? 1 : 0;
}
