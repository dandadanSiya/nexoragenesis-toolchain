#ifndef GENESIS_PE_INSPECT_H
#define GENESIS_PE_INSPECT_H
#include "ntasm.h"
typedef struct {
  uint32_t entry_rva, image_size, header_size, section_count;
  uint64_t image_base;
  uint32_t relocation_count;
} NtPeInfo;
/* Inspect bytes only, without loading or executing them. */
int nt_pe_inspect(const uint8_t *bytes, size_t length, NtPeInfo *info,
                  NtDiagnostic *error);
#endif
