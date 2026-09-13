#ifndef GENESIS_NXO_OBJECT_H
#define GENESIS_NXO_OBJECT_H
#include "ntasm.h"
enum { NT_OBJECT_TEXT = 1, NT_OBJECT_RDATA = 2, NT_OBJECT_DATA = 3 };
enum {
  NT_OBJECT_EXPORT = 1,
  NT_OBJECT_FUNCTION = 2,
  NT_OBJECT_VARIABLE = 4,
  NT_OBJECT_IMPORT = 8
};
enum { NT_OBJECT_REL32 = 1, NT_OBJECT_DIR64 = 2 };
typedef struct {
  char *name, *contract;
  uint32_t section, flags;
  uint64_t offset, size;
} NtObjectSymbol;
typedef struct {
  uint32_t section, kind, target; /* target is a one-based symbol index */
  uint64_t offset;
  int64_t addend; /* REL32 = S+A-(P+4); DIR64 = S+A */
} NtObjectReloc;
typedef struct {
  uint32_t target, entry_symbol; /* target1=UEFI,2=bare x64; entry0=library */
  NtBuffer sections[3];
  NtObjectSymbol *symbols;
  size_t symbol_count;
  NtObjectReloc *relocations;
  size_t relocation_count;
  int _owned;
} NtObject;
/* NXOV0001 is the C bootstrap's versioned relocatable format. Structural
 * validity does not authenticate a producer or prove semantic contracts.
 * Inputs to write may be borrowed. Read outputs own all memory; call free.
 * Output arguments must be zero-initialized. Failed reads remain empty. */
int nt_object_write(const NtObject *object, NtBuffer *output,
                    NtDiagnostic *error);
int nt_object_read(const uint8_t *bytes, size_t length, NtObject *output,
                   NtDiagnostic *error);
void nt_object_free(NtObject *object);
int nt_object_compile(const NtFInput *inputs,size_t count,NtObject *output,NtDiagnostic *error);
int nt_object_validate(const NtObject *object,NtDiagnostic *error);
int nt_object_materialize(const NtObject *object,NtArtifact *output,NtDiagnostic *error);
/* Resolve a set of objects using exact qualified names and contracts.
 * Imports require exported definitions. Output owns a closed linked object. */
int nt_object_link(const NtObject *objects, size_t count, NtObject *output,
                   NtDiagnostic *error);
#endif
