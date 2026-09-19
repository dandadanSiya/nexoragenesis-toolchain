#include "nova.h"
#include "nova/run_host.h"
#include "nova/runtime_v2.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures, cases;
static const uint64_t guard = UINT64_C(0xcccccccccccccccc);
#define CHECK(c, m) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %s\n", m); } } while (0)

static char *load(const char *path, size_t *size) {
  FILE *file = fopen(path, "rb");
  if (!file) return NULL;
  fseek(file, 0, SEEK_END);
  long length = ftell(file);
  fseek(file, 0, SEEK_SET);
  char *bytes = length >= 0 ? malloc((size_t)length + 1) : NULL;
  if (!bytes || fread(bytes, 1, (size_t)length, file) != (size_t)length) {
    free(bytes); fclose(file); return NULL;
  }
  fclose(file); bytes[length] = 0; *size = (size_t)length; return bytes;
}

typedef struct { uint64_t returned, runtime, out[4]; } Run;

static Run run(const NovaHostImage *image, const char *source, uint64_t words) {
  Run result = {.out = {guard, guard, guard, guard}};
  NovaContextV2 context = {0};
  CHECK(nova_context_init_v2(&context), "context init");
  uint64_t source_handle = nova_borrow_v2(&context, (void *)source, strlen(source), 0);
  uint64_t out_handle = nova_borrow_v2(&context, result.out, words * 8, 1);
  uint64_t args[] = {source_handle, strlen(source), out_handle, words};
  result.returned = image->entry(&context, args, 4);
  result.runtime = context.error;
  CHECK(nova_context_destroy_v2(&context), "context cleanup");
  return result;
}

static void ok(const NovaHostImage *image, const char *name, const char *source,
               uint64_t sets, uint64_t entries, uint64_t memory,
               uint64_t scoped) {
  Run result = run(image, source, 4); cases++; checks++;
  if (result.returned || result.runtime || result.out[0] != sets ||
      result.out[1] != entries || result.out[2] != memory ||
      result.out[3] != scoped) {
    failures++; fprintf(stderr, "FAIL ok %s ret=%llu out=%llu,%llu,%llu,%llu\n",
      name, (unsigned long long)result.returned,
      (unsigned long long)result.out[0], (unsigned long long)result.out[1],
      (unsigned long long)result.out[2], (unsigned long long)result.out[3]);
  }
}

static void error(const NovaHostImage *image, const char *name,
                  const char *source, uint64_t words, uint64_t code,
                  uint64_t reason, int untouched) {
  Run result = run(image, source, words); cases++; checks++;
  int bad = result.returned != code || result.runtime;
  if (untouched) bad |= result.out[0] != guard || result.out[1] != guard;
  else bad |= result.out[0] == guard || result.out[1] == guard ||
              (reason && result.out[2] != reason);
  if (bad) {
    failures++; fprintf(stderr, "FAIL error %s ret=%llu out=%llu,%llu,%llu\n",
      name, (unsigned long long)result.returned,
      (unsigned long long)result.out[0], (unsigned long long)result.out[1],
      (unsigned long long)result.out[2]);
  }
}

int main(void) {
  const char *paths[] = {
    "toolchain/ntasm-nova/tests/effects-entry.nova",
    "toolchain/ntasm-nova/effects.nova",
    "toolchain/ntasm-nova/callable_names.nova",
    "toolchain/ntasm-nova/semantics.nova",
    "toolchain/ntasm-nova/parser.nova",
    "toolchain/ntasm-nova/ast.nova",
    "toolchain/ntasm-nova/lexer.nova"
  };
  char *sources[7] = {0}; NtFInput inputs[7];
  for (unsigned i = 0; i < 7; i++) {
    size_t size = 0; sources[i] = load(paths[i], &size);
    inputs[i] = (NtFInput){paths[i], sources[i], size};
  }
  int all = 1;
  for (unsigned i = 0; i < 7; i++) if (!sources[i]) all = 0;
  CHECK(all, "sources load");
  NtArtifact artifact = {0};
  int compiled = all && nova_compile_many_v2(inputs, 7, &artifact);
  CHECK(compiled, "modules compile");
  if (!compiled) fprintf(stderr, "E%u %zu:%zu %s\n", artifact.error.code,
                         artifact.error.line, artifact.error.column,
                         artifact.error.message);
  NovaHostImage image = {0};
  CHECK(compiled && nova_host_map(&artifact, &image), "image maps");
  if (image.memory) {
    const char *empty = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {}\nclobbers {} {\nreturn 0\n}\n}\n";
    ok(&image, "empty", empty, 1, 0, 0, 0);
    const char *atoms = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {privileged,changes_flags,mmio,io_port,msr,control,interrupt_state,no_return,memory_ordering,reads_clock,suspends}\nclobbers {} {\nreturn 0\n}\n}\n";
    ok(&image, "atoms", atoms, 1, 11, 0, 0);
    const char *memory = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {reads_mem,writes_mem,reads_mem(user),writes_mem(kernel)}\nclobbers {} {\nreturn 0\n}\n}\n";
    ok(&image, "memory", memory, 1, 4, 4, 2);
    const char *multiple = "module m\ntarget x86_64-nexora-none\nimport a.f as f: fn()->u64\neffects {privileged}\nclobbers {}\nsection .text {\nfn g()->u64\neffects {changes_flags}\nclobbers {} {\nreturn 0\n}\n}\n";
    ok(&image, "multiple", multiple, 2, 2, 0, 0);
    const char *spaces = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {reads_mem(user),reads_mem(kernel)}\nclobbers {} {\nreturn 0\n}\n}\n";
    ok(&image, "different spaces", spaces, 1, 2, 2, 2);
    const char *all_spaces = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {reads_mem(user),reads_mem(kernel),reads_mem(physical),reads_mem(mmio),reads_mem(device),reads_mem(firmware)}\nclobbers {} {\nreturn 0\n}\n}\n";
    ok(&image, "all spaces", all_spaces, 1, 6, 6, 6);
    const char *separate = "module m\ntarget x86_64-nexora-none\nimport a.f as f: fn()->u64\neffects {msr}\nclobbers {}\nsection .text {\nfn g()->u64\neffects {msr}\nclobbers {} {\nreturn 0\n}\n}\n";
    ok(&image, "same separate", separate, 2, 2, 0, 0);
    const char *unknown = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {telepathy}\nclobbers {} {\nreturn 0\n}\n}\n";
    error(&image, "unknown", unknown, 4, 321, 1, 0);
    const char *atom_arg = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {privileged(user)}\nclobbers {} {\nreturn 0\n}\n}\n";
    error(&image, "atom argument", atom_arg, 4, 321, 2, 0);
    const char *bad_space = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {reads_mem(telepathy)}\nclobbers {} {\nreturn 0\n}\n}\n";
    error(&image, "space", bad_space, 4, 321, 3, 0);
    const char *dup_atom = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {msr,msr}\nclobbers {} {\nreturn 0\n}\n}\n";
    error(&image, "duplicate atom", dup_atom, 4, 322, 0, 0);
    const char *dup_memory = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {reads_mem(user),reads_mem(user)}\nclobbers {} {\nreturn 0\n}\n}\n";
    error(&image, "duplicate memory", dup_memory, 4, 322, 0, 0);
    const char *dup_unscoped = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {writes_mem,writes_mem}\nclobbers {} {\nreturn 0\n}\n}\n";
    error(&image, "duplicate unscoped", dup_unscoped, 4, 322, 0, 0);
    const char *qualified_effect = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {cpu.privileged}\nclobbers {} {\nreturn 0\n}\n}\n";
    error(&image, "qualified effect", qualified_effect, 4, 321, 4, 0);
    const char *qualified_space = "module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {reads_mem(cpu.user)}\nclobbers {} {\nreturn 0\n}\n}\n";
    error(&image, "qualified space", qualified_space, 4, 321, 4, 0);
    error(&image, "short", empty, 3, 800, 0, 1);
    error(&image, "invalid", "target x86_64-nexora-none\n", 4, 800, 0, 1);
  }
  if (image.memory) nova_host_unmap(&image);
  if (compiled) nt_artifact_free(&artifact);
  for (unsigned i = 0; i < 7; i++) free(sources[i]);
  printf("NTASM_NOVA_EFFECTS checks=%u failures=%u cases=%u\n",
         checks, failures, cases);
  return failures ? 1 : 0;
}
