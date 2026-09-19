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
  FILE *f = fopen(path, "rb"); if (!f) return NULL;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  char *p = n >= 0 ? malloc((size_t)n + 1) : NULL;
  if (!p || fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); fclose(f); return NULL; }
  fclose(f); p[n] = 0; *length = (size_t)n; return p;
}

typedef struct { uint64_t returned, runtime_error, out[64]; } Run;
static Run run(const NovaHostImage *image, const char *source, uint64_t words) {
  Run r = {0}; for (unsigned i = 0; i < 64; i++) r.out[i] = guard;
  NovaContextV2 c = {0}; CHECK(nova_context_init_v2(&c), "context init");
  uint64_t sh = nova_borrow_v2(&c, (void *)source, strlen(source), 0);
  uint64_t oh = nova_borrow_v2(&c, r.out, words * sizeof(uint64_t), 1);
  uint64_t args[] = {sh, strlen(source), oh, words};
  r.returned = image->entry(&c, args, 4); r.runtime_error = c.error;
  CHECK(nova_context_destroy_v2(&c), "context cleanup"); return r;
}
static void expect_code(const NovaHostImage *image, const char *name,
                        const char *source, uint64_t words, uint64_t code,
                        int untouched) {
  Run r = run(image, source, words); cases++; checks++;
  if (r.returned != code || r.runtime_error ||
      (untouched && (r.out[0] != guard || r.out[1] != guard))) {
    failures++; fprintf(stderr, "FAIL %s ret=%llu runtime=%llu out=%llu,%llu\n",
      name, (unsigned long long)r.returned, (unsigned long long)r.runtime_error,
      (unsigned long long)r.out[0], (unsigned long long)r.out[1]);
  }
}

int main(void) {
  const char *paths[] = {"toolchain/ntasm-nova/tests/decls-entry.nova",
    "toolchain/ntasm-nova/decls.nova",
    "toolchain/ntasm-nova/semantics.nova",
    "toolchain/ntasm-nova/parser.nova",
    "toolchain/ntasm-nova/ast.nova",
    "toolchain/ntasm-nova/lexer.nova"};
  char *sources[6] = {0}; NtFInput inputs[6];
  for (unsigned i = 0; i < 6; i++) { size_t n = 0; sources[i] = load(paths[i], &n); inputs[i] = (NtFInput){paths[i], sources[i], n}; }
  CHECK(sources[0] && sources[1] && sources[2] && sources[3] && sources[4] && sources[5], "sources load");
  NtArtifact artifact = {0}; int compiled = sources[0] && sources[1] && sources[2] && sources[3] && sources[4] && sources[5] && nova_compile_many_v2(inputs, 6, &artifact);
  CHECK(compiled, "Nova modules compile");
  if (!compiled) fprintf(stderr, "E%u %zu:%zu %s\n", artifact.error.code, artifact.error.line, artifact.error.column, artifact.error.message);
  NovaHostImage image = {0}; CHECK(compiled && nova_host_map(&artifact, &image), "image maps");
  if (image.memory) {
    const char *valid = "module m\ntarget x86_64-nexora-none\nimport math.add as add: fn(in a:u64 @rcx,in b:u64 @rdx)->u64\neffects {}\nclobbers {}\nsection .rdata {\ndata answer:u64=42\n}\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\nreturn 0\n}\n}\n";
    Run r = run(&image, valid, 19); cases++; checks++;
    if (r.returned || r.runtime_error || r.out[0] != 3 || r.out[1] != 9 ||
        r.out[2] != 0 || r.out[5] != 2 || r.out[7] != 24 || r.out[8] == 0 ||
        r.out[11] != 0 || r.out[13] != 18 || r.out[14] == 0 || r.out[17] != 0) {
      failures++; fprintf(stderr, "FAIL valid declaration table\n");
    }
    const char *params = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn f(in a:u64 @rcx,in b:u64 @rdx)->u64\neffects {}\nclobbers {} {\nreturn a\n}\n}\n";
    r = run(&image, params, 7); cases++; checks++;
    if (r.returned || r.runtime_error || r.out[0] != 1 || r.out[1] != 18 || r.out[5] != 2) { failures++; fprintf(stderr, "FAIL function arity\n"); }
    const char *two_sections = "module m\ntarget x86_64-nexora-none\nsection .rdata {\ndata same:u64=1\n}\nsection .text {\nfn same()->u64\neffects {}\nclobbers {} {\nreturn 0\n}\n}\n";
    r = run(&image, two_sections, 13); cases++; checks++;
    if (r.returned || r.runtime_error || r.out[0] != 2 || r.out[1] != 24 || r.out[7] != 18 || r.out[2] == r.out[8]) { failures++; fprintf(stderr, "FAIL section tracking\n"); }
    const char *duplicate = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {}\nclobbers {} {\nreturn 0\n}\nfn f()->u64\neffects {}\nclobbers {} {\nreturn 0\n}\n}\n";
    expect_code(&image, "duplicate", duplicate, 19, 300, 0);
    expect_code(&image, "short", valid, 18, 812, 1);
    expect_code(&image, "invalid", "target x86_64-nexora-none\n", 19, 800, 1);
  }
  if (image.memory)
    nova_host_unmap(&image);
  if (compiled)
    nt_artifact_free(&artifact);
  for (unsigned i = 0; i < 6; i++) free(sources[i]);
  printf("NTASM_NOVA_DECLS checks=%u failures=%u cases=%u\n", checks, failures, cases);
  return failures ? 1 : 0;
}
