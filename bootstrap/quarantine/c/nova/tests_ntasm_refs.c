#include "nova.h"
#include "nova/run_host.h"
#include "nova/runtime_v2.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures, cases;
#define CHECK(c, m) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %s\n", m); } } while (0)
static const uint64_t guard = UINT64_C(0xcccccccccccccccc);

static char *load(const char *path, size_t *length) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
  char *data = size >= 0 ? malloc((size_t)size + 1) : NULL;
  if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) {
    free(data); fclose(f); return NULL;
  }
  fclose(f); data[size] = 0; *length = (size_t)size; return data;
}

typedef struct { uint64_t returned, runtime_error, out[4]; } Run;
static Run run(const NovaHostImage *image, const char *source, uint64_t words) {
  Run r = {.out = {guard, guard, guard, guard}};
  NovaContextV2 context;
  if (!nova_context_init_v2(&context)) { r.runtime_error = UINT64_MAX; return r; }
  uint64_t sh = nova_borrow_v2(&context, (void *)source, strlen(source), 0);
  uint64_t oh = nova_borrow_v2(&context, r.out, words * sizeof(uint64_t), 1);
  uint64_t args[] = {sh, strlen(source), oh, words};
  r.returned = image->entry(&context, args, 4); r.runtime_error = context.error;
  CHECK(nova_context_destroy_v2(&context), "refs context cleanup");
  return r;
}

static void expect_ok(const NovaHostImage *image, const char *name,
                      const char *source, uint64_t decls, uint64_t calls) {
  Run r = run(image, source, 3); cases++; checks++;
  if (r.returned || r.runtime_error || r.out[0] != decls ||
      r.out[1] != calls || r.out[2] != calls) {
    failures++; fprintf(stderr, "FAIL ok %s ret=%llu runtime=%llu out=%llu,%llu,%llu\n",
      name, (unsigned long long)r.returned, (unsigned long long)r.runtime_error,
      (unsigned long long)r.out[0], (unsigned long long)r.out[1],
      (unsigned long long)r.out[2]);
  }
}

static void expect_code(const NovaHostImage *image, const char *name,
                        const char *source, uint64_t code) {
  Run r = run(image, source, 3); cases++; checks++;
  if (r.returned != code || r.runtime_error || r.out[0] == guard) {
    failures++; fprintf(stderr, "FAIL code %s ret=%llu runtime=%llu out=%llu,%llu\n",
      name, (unsigned long long)r.returned, (unsigned long long)r.runtime_error,
      (unsigned long long)r.out[0], (unsigned long long)r.out[1]);
  }
}

static void expect_untouched(const NovaHostImage *image, const char *name,
                             const char *source, uint64_t words) {
  Run r = run(image, source, words); cases++; checks++;
  if (r.returned != 800 || r.runtime_error || r.out[0] != guard || r.out[1] != guard) {
    failures++; fprintf(stderr, "FAIL untouched %s ret=%llu runtime=%llu\n", name,
      (unsigned long long)r.returned, (unsigned long long)r.runtime_error);
  }
}

int main(void) {
  const char *paths[] = {"toolchain/ntasm-nova/tests/refs-entry.nova",
    "toolchain/ntasm-nova/refs.nova", "toolchain/ntasm-nova/semantics.nova",
    "toolchain/ntasm-nova/parser.nova", "toolchain/ntasm-nova/ast.nova",
    "toolchain/ntasm-nova/lexer.nova"};
  char *sources[6] = {0}; NtFInput inputs[6];
  for (size_t i = 0; i < 6; i++) { size_t n = 0; sources[i] = load(paths[i], &n); inputs[i] = (NtFInput){paths[i], sources[i], n}; }
  CHECK(sources[0] && sources[1] && sources[2] && sources[3] && sources[4] && sources[5], "refs sources load");
  NtArtifact artifact = {0};
  int compiled = sources[0] && sources[1] && sources[2] && sources[3] && sources[4] && sources[5] && nova_compile_many_v2(inputs, 6, &artifact);
  CHECK(compiled, "refs Nova modules compile");
  if (!compiled) fprintf(stderr, "Nova compile E%u %zu:%zu %s\n", artifact.error.code, artifact.error.line, artifact.error.column, artifact.error.message);
  NovaHostImage image = {0}; CHECK(compiled && nova_host_map(&artifact, &image), "refs image maps");
  if (image.memory) {
    const char *simple = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn helper()->u64\neffects {}\nclobbers {} {\nreturn 1\n}\nfn main()->u64\neffects {}\nclobbers {} {\ncall helper()\nreturn 0\n}\n}\n";
    expect_ok(&image, "simple", simple, 2, 1);
    const char *many = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn helper()->u64\neffects {}\nclobbers {} {\nreturn 1\n}\nfn main()->u64\neffects {}\nclobbers {} {\ncall helper()\ncall helper()\nreturn 0\n}\n}\n";
    expect_ok(&image, "multiple", many, 2, 2);
    const char *imported = "module m\ntarget x86_64-nexora-none\nimport math.add as add: fn(in x:u64 @rcx)->u64\neffects {}\nclobbers {}\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\ncall add(1)\nreturn 0\n}\n}\n";
    expect_ok(&image, "import_alias", imported, 2, 1);
    const char *expr_return = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn helper()->u64\neffects {}\nclobbers {} {\nreturn 1\n}\nfn main()->u64\neffects {}\nclobbers {} {\nreturn helper()\n}\n}\n";
    expect_ok(&image, "expr_return", expr_return, 2, 1);
    const char *expr_let = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn helper()->u64\neffects {}\nclobbers {} {\nreturn 1\n}\nfn main()->u64\neffects {}\nclobbers {} {\nlet x:u64=helper();\nreturn x\n}\n}\n";
    expect_ok(&image, "expr_let", expr_let, 2, 1);
    const char *missing = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\ncall absent()\nreturn 0\n}\n}\n";
    expect_code(&image, "missing", missing, 310);
    const char *duplicate = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {}\nclobbers {} {\nreturn 0\n}\nfn f()->u64\neffects {}\nclobbers {} {\nreturn 0\n}\n}\n";
    expect_code(&image, "duplicate", duplicate, 300);
    expect_untouched(&image, "short", simple, 2);
  }
  if (image.memory) nova_host_unmap(&image);
  if (compiled) nt_artifact_free(&artifact);
  for (size_t i = 0; i < 6; i++) free(sources[i]);
  printf("NTASM_NOVA_REFS checks=%u failures=%u cases=%u\n", checks, failures, cases);
  return failures ? 1 : 0;
}
