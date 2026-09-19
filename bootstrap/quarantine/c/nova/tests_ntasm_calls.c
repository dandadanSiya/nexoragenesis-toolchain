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
  CHECK(nova_context_destroy_v2(&context), "calls context cleanup");
  return r;
}

static void expect_ok(const NovaHostImage *image, const char *name,
                      const char *source, uint64_t decls, uint64_t calls) {
  Run r = run(image, source, 4); cases++; checks++;
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
  Run r = run(image, source, 4); cases++; checks++;
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
  const char *paths[] = {"toolchain/ntasm-nova/tests/calls-entry.nova",
    "toolchain/ntasm-nova/calls.nova", "toolchain/ntasm-nova/semantics.nova",
    "toolchain/ntasm-nova/refs.nova",
    "toolchain/ntasm-nova/parser.nova", "toolchain/ntasm-nova/ast.nova",
    "toolchain/ntasm-nova/lexer.nova"};
  char *sources[7] = {0}; NtFInput inputs[7];
  for (size_t i = 0; i < 7; i++) { size_t n = 0; sources[i] = load(paths[i], &n); inputs[i] = (NtFInput){paths[i], sources[i], n}; }
  CHECK(sources[0] && sources[1] && sources[2] && sources[3] && sources[4] && sources[5] && sources[6], "calls sources load");
  NtArtifact artifact = {0};
  int compiled = sources[0] && sources[1] && sources[2] && sources[3] && sources[4] && sources[5] && sources[6] && nova_compile_many_v2(inputs, 7, &artifact);
  CHECK(compiled, "calls Nova modules compile");
  if (!compiled) fprintf(stderr, "Nova compile E%u %zu:%zu %s\n", artifact.error.code, artifact.error.line, artifact.error.column, artifact.error.message);
  NovaHostImage image = {0}; CHECK(compiled && nova_host_map(&artifact, &image), "calls image maps");
  if (image.memory) {
    const char *zero_arg = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn helper()->u64\neffects {}\nclobbers {} {\nreturn 1\n}\nfn main()->u64\neffects {}\nclobbers {} {\ncall helper()\nreturn 0\n}\n}\n";
    expect_ok(&image, "zero_arg", zero_arg, 2, 1);
    const char *many_args = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn helper(in x:u64 @rcx,in y:u64 @rdx)->u64\neffects {}\nclobbers {} {\nreturn x\n}\nfn main()->u64\neffects {}\nclobbers {} {\ncall helper(1,2)\nreturn 0\n}\n}\n";
    expect_ok(&image, "many_args", many_args, 2, 1);
    const char *import_alias = "module m\ntarget x86_64-nexora-none\nimport math.add as add: fn(in x:u64 @rcx,in y:u64 @rdx)->u64\neffects {}\nclobbers {}\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\ncall add(1,2)\nreturn 0\n}\n}\n";
    expect_ok(&image, "import_alias", import_alias, 2, 1);
    const char *expr_call = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn helper()->u64\neffects {}\nclobbers {} {\nreturn 1\n}\nfn main()->u64\neffects {}\nclobbers {} {\nreturn helper()\n}\n}\n";
    expect_ok(&image, "expr_call", expr_call, 2, 1);
    const char *too_few = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn helper(in x:u64 @rcx)->u64\neffects {}\nclobbers {} {\nreturn x\n}\nfn main()->u64\neffects {}\nclobbers {} {\ncall helper()\nreturn 0\n}\n}\n";
    expect_code(&image, "too_few", too_few, 311);
    const char *too_many = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn helper()->u64\neffects {}\nclobbers {} {\nreturn 1\n}\nfn main()->u64\neffects {}\nclobbers {} {\ncall helper(1,2)\nreturn 0\n}\n}\n";
    expect_code(&image, "too_many", too_many, 311);
    const char *absent = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\ncall absent()\nreturn 0\n}\n}\n";
    expect_code(&image, "absent", absent, 310);
    expect_untouched(&image, "short", zero_arg, 2);
  }
  if (image.memory) nova_host_unmap(&image);
  if (compiled) nt_artifact_free(&artifact);
  for (size_t i = 0; i < 7; i++) free(sources[i]);
  printf("NTASM_NOVA_CALLS checks=%u failures=%u cases=%u\n", checks, failures, cases);
  return failures ? 1 : 0;
}
