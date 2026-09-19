#include "nova.h"
#include "nova/run_host.h"
#include "nova/runtime_v2.h"
#include "object.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned checks, failures, cases;
#define CHECK(c, m) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %s\n", m); } } while (0)
static const uint64_t guard = UINT64_C(0xcccccccccccccccc);

static char *load(const char *path, size_t *length) {
  FILE *f = fopen(path, "rb"); if (!f) return NULL;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  char *p = n >= 0 ? malloc((size_t)n + 1) : NULL;
  if (!p || fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); fclose(f); return NULL; }
  fclose(f); p[n] = 0; *length = (size_t)n; return p;
}

static uint64_t invoke(const NovaHostImage *image, const NtBuffer *file,
                       uint64_t file_size, uint64_t index, uint64_t *out,
                       uint64_t out_words, uint64_t *runtime_error) {
  NovaContextV2 c = {0}; CHECK(nova_context_init_v2(&c), "context init");
  uint64_t ih = nova_borrow_v2(&c, file->bytes, file_size, 0);
  uint64_t oh = nova_borrow_v2(&c, out, out_words * sizeof(uint64_t), 1);
  uint64_t args[] = {ih, file_size, index, oh, out_words};
  uint64_t returned = image->entry(&c, args, 5); *runtime_error = c.error;
  CHECK(nova_context_destroy_v2(&c), "context cleanup"); return returned;
}

static void reset(uint64_t *out) { for (unsigned i = 0; i < 10; i++) out[i] = guard; }
static void expect_symbol(const NovaHostImage *image, const NtBuffer *file,
                          uint64_t index, const uint64_t expected[8]) {
  uint64_t out[10], runtime = 0; reset(out); cases++;
  uint64_t returned = invoke(image, file, file->size, index, out, 8, &runtime);
  CHECK(returned == 0, "symbol return"); CHECK(runtime == 0, "symbol runtime");
  for (unsigned i = 0; i < 8; i++) CHECK(out[i] == expected[i], "symbol word");
  CHECK(out[8] == guard && out[9] == guard, "symbol guards");
}
static void expect_error(const NovaHostImage *image, const NtBuffer *file,
                         uint64_t size, uint64_t index, uint64_t words,
                         uint64_t code) {
  uint64_t out[10], runtime = 0; reset(out); cases++;
  uint64_t returned = invoke(image, file, size, index, out, words, &runtime);
  CHECK(returned == code, "error code"); CHECK(runtime == 0, "error runtime");
  for (unsigned i = 0; i < 10; i++) CHECK(out[i] == guard, "error transactional");
}

int main(void) {
  const char *paths[] = {
    "toolchain/ntasm-nova/tests/nxo-symbol-entry.nova",
    "toolchain/ntasm-nova/nxo_inspect.nova",
    "toolchain/ntasm-nova/nxo.nova",
    "toolchain/ntasm-nova/pe.nova"};
  size_t sizes[4] = {0}; char *sources[4] = {0}; NtFInput inputs[4];
  for (unsigned i = 0; i < 4; i++) { sources[i] = load(paths[i], sizes + i); inputs[i] = (NtFInput){paths[i], sources[i], sizes[i]}; }
  CHECK(sources[0] && sources[1] && sources[2] && sources[3], "sources load");
  NtArtifact artifact = {0}; int compiled = sources[0] && sources[1] && sources[2] && sources[3] && nova_compile_many_v2(inputs, 4, &artifact);
  CHECK(compiled, "Nova modules compile");
  if (!compiled) fprintf(stderr, "E%u %zu:%zu %s\n", artifact.error.code, artifact.error.line, artifact.error.column, artifact.error.message);
  NovaHostImage image = {0}; CHECK(compiled && nova_host_map(&artifact, &image), "image maps");
  if (image.memory) {
    uint8_t text[8] = {0}, data[8] = {0};
    NtObjectSymbol symbols[] = {{"a", "data:ptr", 3, NT_OBJECT_VARIABLE, 0, 8}, {"b", "fn:u64", 1, NT_OBJECT_FUNCTION, 0, 8}};
    NtObjectReloc reloc = {3, NT_OBJECT_DIR64, 1, 0, 0};
    NtObject object = {0}; object.target = 1; object.entry_symbol = 2;
    object.symbols = symbols; object.symbol_count = 2; object.relocations = &reloc; object.relocation_count = 1;
    object.sections[0] = (NtBuffer){text, 8, 8}; object.sections[2] = (NtBuffer){data, 8, 8};
    NtBuffer file = {0}; NtDiagnostic error = {0}; CHECK(nt_object_write(&object, &file, &error), "oracle writes NXO");
    if (file.bytes) {
      const uint64_t first[] = {0, 1, 3, 4, 0, 8, 1, 8};
      const uint64_t second[] = {9, 1, 1, 2, 0, 8, 10, 6};
      expect_symbol(&image, &file, 0, first); expect_symbol(&image, &file, 1, second);
      expect_error(&image, &file, file.size, 2, 8, 810);
      expect_error(&image, &file, file.size, 0, 7, 809);
      expect_error(&image, &file, file.size - 1, 0, 8, 800);
      free(file.bytes);
    }
  }
  if (image.memory)
    nova_host_unmap(&image);
  if (compiled)
    nt_artifact_free(&artifact);
  for (unsigned i = 0; i < 4; i++) free(sources[i]);
  printf("NTASM_NOVA_NXO_SYMBOL checks=%u failures=%u cases=%u\n", checks, failures, cases);
  return failures ? 1 : 0;
}
