#include "object.h"
#include "pe_layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int failure(NtDiagnostic *e, const char *text) {
  memset(e, 0, sizeof(*e));
  e->code = 800;
  snprintf(e->message, sizeof(e->message), "%s", text);
  return 0;
}
static int clone(const NtBuffer *source, NtBuffer *destination) {
  destination->size = destination->capacity = source->size;
  if (!source->size)
    return 1;
  destination->bytes = malloc(source->size);
  if (!destination->bytes)
    return 0;
  memcpy(destination->bytes, source->bytes, source->size);
  return 1;
}
int nt_object_materialize(const NtObject *o, NtArtifact *a, NtDiagnostic *e) {
  if (!a || !e)
    return 0;
  memset(e, 0, sizeof(*e));
  if (a->text.bytes || a->pe.bytes || a->rdata.bytes || a->data.bytes ||
      a->base_relocations)
    return failure(e, "artifact output must be empty");
  if (!nt_object_validate(o, e))
    return 0;
  if (o->target != 1)
    return failure(e, "PE materialization currently requires the UEFI target");
  if (!o->entry_symbol)
    return failure(e, "object library has no executable entry");
  for (size_t j = 0; j < o->symbol_count; j++)
    if (o->symbols[j].flags & NT_OBJECT_IMPORT)
      return failure(e, "unresolved object import");
  NtPeLayout layout;
  if (!nt_pe_layout(o->sections[0].size, o->sections[1].size,
                    o->sections[2].size, &layout))
    return failure(e, "section size exceeds PE profile");
  if (!clone(o->sections, &a->text) || !clone(o->sections + 1, &a->rdata) ||
      !clone(o->sections + 2, &a->data)) {
    failure(e, "artifact allocation failed");
    goto invalid;
  }
  a->entry = (size_t)o->symbols[o->entry_symbol - 1].offset;
  size_t absolute_count = 0;
  for (size_t j = 0; j < o->relocation_count; j++)
    if (o->relocations[j].kind == NT_OBJECT_DIR64)
      absolute_count++;
  if (absolute_count) {
    a->base_relocations = calloc(absolute_count, sizeof(*a->base_relocations));
    if (!a->base_relocations) {
      failure(e, "base relocation allocation failed");
      goto invalid;
    }
  }
  for (size_t j = 0; j < o->relocation_count; j++) {
    const NtObjectReloc *r = o->relocations + j;
    const NtObjectSymbol *s = o->symbols + r->target - 1;
    uint64_t source = nt_pe_logical_rva(&layout, r->section) + r->offset,
             target = nt_pe_logical_rva(&layout, s->section) + s->offset, value;
    if (!nt_relocation_value(r->kind, source, target, r->addend, &value)) {
      failure(e, "relocation arithmetic or displacement out of range");
      goto invalid;
    }
    NtBuffer *section = r->section == 1   ? &a->text
                        : r->section == 2 ? &a->rdata
                                          : &a->data;
    nt_relocation_store(section->bytes + (size_t)r->offset,
                        r->kind == NT_OBJECT_DIR64 ? 8 : 4, value);
    if (r->kind == NT_OBJECT_DIR64)
      a->base_relocations[a->base_relocation_count++] =
          (NtBaseReloc){r->section, r->offset};
  }
  return 1;
invalid:
  nt_artifact_free(a);
  return 0;
}
