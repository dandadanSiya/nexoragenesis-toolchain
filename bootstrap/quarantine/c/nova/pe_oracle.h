#ifndef NG_NOVA_PE_ORACLE_H
#define NG_NOVA_PE_ORACLE_H
/* BOOTSTRAP_C_TEST: isolated current C writer oracle, not used by Nova code. */
#include "../ntasm.h"
typedef struct {
  uint32_t section;
  uint64_t offset;
} NovaOracleBaseReloc;
typedef struct {
  NtBuffer text, pe;
  size_t entry;
  NtDiagnostic error;
  NtBuffer rdata, data;
  NovaOracleBaseReloc *base_relocations;
  size_t base_relocation_count;
} NovaOraclePeImage;
int nova_oracle_make_pe(NovaOraclePeImage *);
int nova_oracle_relocation(unsigned, uint64_t, uint64_t, int64_t, uint64_t *);
#endif
