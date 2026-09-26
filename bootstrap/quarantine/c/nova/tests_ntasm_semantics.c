#include "nova.h"
#include "nova/run_host.h"
#include "nova/runtime_v2.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures, cases;
#define CHECK(c, m)                                                            \
  do {                                                                         \
    checks++;                                                                  \
    if (!(c)) {                                                                \
      failures++;                                                              \
      fprintf(stderr, "FAIL %s\n", m);                                        \
    }                                                                          \
  } while (0)

static char *load(const char *path, size_t *length) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *data = size >= 0 ? (char *)malloc((size_t)size + 1) : NULL;
  if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) {
    free(data);
    fclose(f);
    return NULL;
  }
  fclose(f);
  data[size] = 0;
  *length = (size_t)size;
  return data;
}

typedef struct {
  uint64_t returned, runtime_error;
  uint64_t out[16];
} Run;

/* Runs semantics-entry.nova main(source,out) over the mapped host image.
   The out buffer is pre-zeroed so transactional behaviour is observable. */
static Run run(const NovaHostImage *image, const char *source, size_t length,
               uint64_t out_words) {
  Run result = {0};
  NovaContextV2 context;
  if (!nova_context_init_v2(&context)) {
    result.runtime_error = UINT64_MAX;
    return result;
  }
  uint64_t source_handle =
      nova_borrow_v2(&context, (void *)source, (uint64_t)length, 0);
  uint64_t output_handle =
      nova_borrow_v2(&context, result.out, out_words * sizeof(uint64_t), 1);
  uint64_t args[] = {source_handle, length, output_handle, out_words};
  result.returned = image->entry(&context, args, 4);
  result.runtime_error = context.error;
  CHECK(nova_context_destroy_v2(&context), "semantics context cleanup");
  return result;
}

/* Line (1-based) and column (1-based) of a byte offset in source. */
static void line_col(const char *source, size_t offset, uint64_t *line,
                     uint64_t *column) {
  uint64_t l = 1, c = 1;
  for (size_t i = 0; i < offset; i++) {
    if (source[i] == '\n') {
      l++;
      c = 1;
    } else
      c++;
  }
  *line = l;
  *column = c;
}

static size_t second_occurrence(const char *source, const char *needle) {
  const char *first = strstr(source, needle);
  if (!first)
    return 0;
  const char *second = strstr(first + 1, needle);
  return second ? (size_t)(second - source) : 0;
}

static void expect_ok(const NovaHostImage *image, const char *name,
                      const char *source, uint64_t imports, uint64_t functions,
                      uint64_t data) {
  Run r = run(image, source, strlen(source), 16);
  checks++;
  cases++;
  if (r.returned != 0 || r.runtime_error != 0 || r.out[0] != imports ||
      r.out[1] != functions || r.out[2] != data) {
    failures++;
    fprintf(stderr,
            "FAIL ok %s return=%llu runtime=%llu out=%llu,%llu,%llu "
            "want=%llu,%llu,%llu\n",
            name, (unsigned long long)r.returned,
            (unsigned long long)r.runtime_error,
            (unsigned long long)r.out[0], (unsigned long long)r.out[1],
            (unsigned long long)r.out[2], (unsigned long long)imports,
            (unsigned long long)functions, (unsigned long long)data);
  }
}

static void expect_duplicate(const NovaHostImage *image, const char *name,
                             const char *source, size_t second_name_offset) {
  uint64_t line, column;
  line_col(source, second_name_offset, &line, &column);
  Run r = run(image, source, strlen(source), 16);
  checks++;
  cases++;
  if (r.returned != 300 || r.runtime_error != 0 || r.out[0] != line ||
      r.out[1] != column) {
    failures++;
    fprintf(stderr,
            "FAIL duplicate %s return=%llu runtime=%llu out=%llu,%llu "
            "want=%llu,%llu\n",
            name, (unsigned long long)r.returned,
            (unsigned long long)r.runtime_error,
            (unsigned long long)r.out[0], (unsigned long long)r.out[1],
            (unsigned long long)line, (unsigned long long)column);
  }
}

static void expect_invalid_tree(const NovaHostImage *image, const char *name,
                                const char *source) {
  Run r = run(image, source, strlen(source), 16);
  checks++;
  cases++;
  if (r.returned != 800 || r.runtime_error != 0 || r.out[0] != 0 ||
      r.out[1] != 0 || r.out[2] != 0) {
    failures++;
    fprintf(stderr,
            "FAIL invalid_tree %s return=%llu runtime=%llu "
            "out=%llu,%llu,%llu want untouched 800\n",
            name, (unsigned long long)r.returned,
            (unsigned long long)r.runtime_error,
            (unsigned long long)r.out[0], (unsigned long long)r.out[1],
            (unsigned long long)r.out[2]);
  }
}

static void expect_short_output(const NovaHostImage *image, const char *name,
                                const char *source) {
  /* Valid source, two-word output: contract says 800 with no write. */
  Run r = run(image, source, strlen(source), 2);
  checks++;
  cases++;
  if (r.returned != 800 || r.runtime_error != 0 || r.out[0] != 0 ||
      r.out[1] != 0) {
    failures++;
    fprintf(stderr,
            "FAIL short_output %s return=%llu runtime=%llu out=%llu,%llu "
            "want untouched 800\n",
            name, (unsigned long long)r.returned,
            (unsigned long long)r.runtime_error,
            (unsigned long long)r.out[0], (unsigned long long)r.out[1]);
  }
}

int main(void) {
  const char *paths[] = {"toolchain/ntasm-nova/tests/semantics-entry.nova", "toolchain/ntasm-nova/semantics.nova",
                         "toolchain/ntasm-nova/parser.nova", "toolchain/ntasm-nova/ast.nova", "toolchain/ntasm-nova/lexer.nova"};
  char *sources[5] = {0};
  NtFInput inputs[5];
  for (size_t i = 0; i < 5; i++) {
    size_t length = 0;
    sources[i] = load(paths[i], &length);
    inputs[i] = (NtFInput){paths[i], sources[i], length};
  }
  CHECK(sources[0] && sources[1] && sources[2] && sources[3] && sources[4],
        "semantics Nova sources load");
  NtArtifact artifact;
  int compiled = sources[0] && sources[1] && sources[2] && sources[3] &&
                 sources[4] &&
                 nova_compile_many_v2(inputs, 5, &artifact);
  CHECK(compiled, "semantics Nova modules compile");
  if (!compiled)
    fprintf(stderr, "Nova compile E%u %zu:%zu %s\n", artifact.error.code,
            artifact.error.line, artifact.error.column, artifact.error.message);
  NovaHostImage image = {0};
  CHECK(compiled && nova_host_map(&artifact, &image), "semantics image maps");
  if (image.memory) {
    const char *valid =
        "module demo\ntarget x86_64-nexora-none\n"
        "import math.add as add: fn(in a:u64 @rcx,in b:u64 @rdx)->u64\n"
        "effects {}\nclobbers {}\n"
        "section .rdata {\nexport data answer:u64=42\n}\n"
        "section .text {\nfn main()->u64\neffects {}\nclobbers {} {\n"
        "return 0\n}\n}\n";
    expect_ok(&image, "valid_multisection", valid, 1, 1, 1);

    const char *dup_fn =
        "module dupfn\ntarget x86_64-nexora-none\nsection .text {\n"
        "fn main()->u64\neffects {}\nclobbers {} {\nreturn 0\n}\n"
        "fn main()->u64\neffects {}\nclobbers {} {\nreturn 1\n}\n}\n";
    expect_duplicate(&image, "duplicate_function", dup_fn,
                     second_occurrence(dup_fn, "main"));

    const char *dup_data =
        "module dupdata\ntarget x86_64-nexora-none\nsection .rdata {\n"
        "data answer:u64=1\ndata answer:u64=2\n}\nsection .text {\n"
        "fn main()->u64\neffects {}\nclobbers {} {\nreturn 0\n}\n}\n";
    expect_duplicate(&image, "duplicate_data", dup_data,
                     second_occurrence(dup_data, "answer"));

    const char *two_sections =
        "module twosec\ntarget x86_64-nexora-none\nsection .rdata {\n"
        "data answer:u64=1\n}\nsection .text {\n"
        "fn answer()->u64\neffects {}\nclobbers {} {\nreturn 0\n}\n}\n";
    expect_ok(&image, "same_name_two_sections", two_sections, 0, 1, 1);

    expect_invalid_tree(&image, "invalid_tree",
                        "target x86_64-nexora-none\n");
    expect_short_output(&image, "short_output", valid);
  }
  if (image.memory)
    nova_host_unmap(&image);
  if (compiled)
    nt_artifact_free(&artifact);
  for (size_t i = 0; i < 5; i++)
    free(sources[i]);
  printf("NTASM_NOVA_SEMANTICS checks=%u failures=%u cases=%u\n", checks,
         failures, cases);
  return failures ? 1 : 0;
}
