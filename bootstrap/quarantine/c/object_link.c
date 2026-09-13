#include "object.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define LINK_BYTES (64u * 1024u * 1024u)
typedef struct {
  const NtObjectSymbol *symbol;
  size_t object, index;
} Definition;
static int failure(NtDiagnostic *error, const char *message) {
  memset(error, 0, sizeof(*error));
  error->code = 800;
  snprintf(error->message, sizeof(error->message), "%s", message);
  return 0;
}
static int compare(const void *a, const void *b) {
  return strcmp(((const Definition *)a)->symbol->name,
                ((const Definition *)b)->symbol->name);
}
static char *copy(const char *s) {
  size_t n = strlen(s) + 1;
  char *p = malloc(n);
  if (p)
    memcpy(p, s, n);
  return p;
}
static uint32_t find(const NtObject *o, const char *name) {
  size_t lo = 0, hi = o->symbol_count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    int c = strcmp(o->symbols[mid].name, name);
    if (c < 0)
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo < o->symbol_count && !strcmp(o->symbols[lo].name, name)
             ? (uint32_t)(lo + 1)
             : 0;
}
static int reloc_compare(const void *a, const void *b) {
  const NtObjectReloc *x = a, *y = b;
  if (x->section != y->section)
    return x->section < y->section ? -1 : 1;
  return x->offset < y->offset ? -1 : x->offset > y->offset;
}
int nt_object_link(const NtObject *objects, size_t count, NtObject *output,
                   NtDiagnostic *error) {
  if (!output || !error)
    return 0;
  memset(error, 0, sizeof(*error));
  if (!objects || !count || count > 64)
    return failure(error, "invalid link unit count");
  if (output->_owned || output->symbols || output->relocations ||
      output->symbol_count || output->relocation_count)
    return failure(error, "link output must be empty");
  for (unsigned k = 0; k < 3; k++)
    if (output->sections[k].bytes || output->sections[k].size)
      return failure(error, "link output must be empty");
  size_t bases[64][3] = {{0}}, totals[3] = {0}, prefixes[65] = {0};
  size_t definitions = 0, relocations = 0, entries = 0;
  for (size_t i = 0; i < count; i++) {
    const NtObject *o = objects + i;
    if (!nt_object_validate(o, error))
      return 0;
    if (o->target != objects[0].target)
      return failure(error, "mixed object targets");
    if (o->entry_symbol && ++entries > 1)
      return failure(error, "multiple executable entries");
    if (o->symbol_count > 65536 - prefixes[i] ||
        o->relocation_count > 1048576 - relocations)
      return failure(error, "link metadata limit");
    prefixes[i + 1] = prefixes[i] + o->symbol_count;
    relocations += o->relocation_count;
    for (size_t j = 0; j < o->symbol_count; j++)
      if (!(o->symbols[j].flags & NT_OBJECT_IMPORT))
        definitions++;
    for (unsigned k = 0; k < 3; k++) {
      size_t base = (totals[k] + 15) & ~(size_t)15;
      if (base > LINK_BYTES || o->sections[k].size > LINK_BYTES - base)
        return failure(error, "linked section size limit");
      bases[i][k] = base;
      totals[k] = base + o->sections[k].size;
    }
  }
  if (totals[0] + totals[1] + totals[2] > LINK_BYTES)
    return failure(error, "linked payload limit");
  Definition *order = calloc(definitions ? definitions : 1, sizeof(*order));
  uint32_t *map = calloc(prefixes[count] ? prefixes[count] : 1, sizeof(*map));
  NtObject result = {0};
  result._owned = 1;
  result.target = objects[0].target;
  result.symbol_count = definitions;
  result.relocation_count = relocations;
  if (!order || !map)
    goto oom;
  result.symbols =
      calloc(definitions ? definitions : 1, sizeof(*result.symbols));
  result.relocations =
      calloc(relocations ? relocations : 1, sizeof(*result.relocations));
  if (!result.symbols || !result.relocations)
    goto oom;
  for (unsigned k = 0; k < 3; k++) {
    if (totals[k]) {
      result.sections[k].bytes = calloc(totals[k], 1);
      if (!result.sections[k].bytes)
        goto oom;
    }
    result.sections[k].size = result.sections[k].capacity = totals[k];
  }
  size_t cursor = 0;
  for (size_t i = 0; i < count; i++) {
    const NtObject *o = objects + i;
    for (unsigned k = 0; k < 3; k++)
      if (o->sections[k].size)
        memcpy(result.sections[k].bytes + bases[i][k], o->sections[k].bytes,
               o->sections[k].size);
    for (size_t j = 0; j < o->symbol_count; j++)
      if (!(o->symbols[j].flags & NT_OBJECT_IMPORT))
        order[cursor++] = (Definition){o->symbols + j, i, j};
  }
  if (definitions > 1)
    qsort(order, definitions, sizeof(*order), compare);
  for (size_t j = 0; j < definitions; j++) {
    Definition *d = order + j;
    const NtObjectSymbol *s = d->symbol;
    if (j && !strcmp(order[j - 1].symbol->name, s->name)) {
      failure(error, "duplicate symbol definition");
      goto invalid;
    }
    NtObjectSymbol *dest = result.symbols + j;
    dest->name = copy(s->name);
    dest->contract = copy(s->contract);
    if (!dest->name || !dest->contract)
      goto oom;
    dest->section = s->section;
    dest->flags = s->flags;
    dest->size = s->size;
    dest->offset = s->offset + bases[d->object][s->section - 1];
    map[prefixes[d->object] + d->index] = (uint32_t)(j + 1);
  }
  for (size_t i = 0; i < count; i++) {
    const NtObject *o = objects + i;
    for (size_t j = 0; j < o->symbol_count; j++) {
      const NtObjectSymbol *s = o->symbols + j;
      if (!(s->flags & NT_OBJECT_IMPORT))
        continue;
      uint32_t target = find(&result, s->name);
      if (!target || !(result.symbols[target - 1].flags & NT_OBJECT_EXPORT)) {
        failure(error, "unresolved import or definition not exported");
        goto invalid;
      }
      const NtObjectSymbol *d = result.symbols + target - 1;
      if ((s->flags & (NT_OBJECT_FUNCTION | NT_OBJECT_VARIABLE)) !=
              (d->flags & (NT_OBJECT_FUNCTION | NT_OBJECT_VARIABLE)) ||
          strcmp(s->contract, d->contract)) {
        failure(error, "import kind or canonical contract mismatch");
        goto invalid;
      }
      map[prefixes[i] + j] = target;
    }
    if (o->entry_symbol)
      result.entry_symbol = map[prefixes[i] + o->entry_symbol - 1];
  }
  cursor = 0;
  for (size_t i = 0; i < count; i++)
    for (size_t j = 0; j < objects[i].relocation_count; j++) {
      NtObjectReloc r = objects[i].relocations[j];
      r.offset += bases[i][r.section - 1];
      r.target = map[prefixes[i] + r.target - 1];
      result.relocations[cursor++] = r;
    }
  if (relocations > 1)
    qsort(result.relocations, relocations, sizeof(*result.relocations),
          reloc_compare);
  if (!nt_object_validate(&result, error))
    goto invalid;
  free(order);
  free(map);
  *output = result;
  return 1;
oom:
  failure(error, "link allocation failed");
invalid:
  free(order);
  free(map);
  nt_object_free(&result);
  return 0;
}
