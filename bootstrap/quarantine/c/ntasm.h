#ifndef GENESIS_NTASM_H
#define GENESIS_NTASM_H
#include "frontend.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
  unsigned code;
  uint32_t source_index; /* one-based NtFInput index; zero for global errors */
  size_t offset, line, column;
  char message[192];
} NtDiagnostic;
typedef struct {
  uint8_t *bytes;
  size_t size, capacity;
} NtBuffer;
typedef struct {
  uint32_t section; /* 1=text,2=user rdata excluding PE anchor,3=data */
  uint64_t offset;
} NtBaseReloc;
typedef struct {
  NtBuffer text, pe;
  size_t entry;
  NtDiagnostic error;
  NtBuffer rdata, data;
  NtBaseReloc *base_relocations;
  size_t base_relocation_count;
} NtArtifact;

/* In-memory producer. Owns no input memory; result is always releasable.
 * No output is usable on error. No external translator is ever invoked. */
int nt_compile(const char *source, size_t length, NtArtifact *out);
int nt_compile_many(const NtFInput *inputs, size_t count, NtArtifact *out);
int nt_make_pe(NtArtifact *artifact);
void nt_artifact_free(NtArtifact *artifact);
#endif
