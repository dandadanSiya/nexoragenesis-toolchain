#include "object.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MAX_FILE (64u * 1024u * 1024u)
#define MAX_SYMBOLS 65536u
#define MAX_RELOCS 1048576u
static uint32_t r32(const uint8_t *p) {
  return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
static uint64_t r64(const uint8_t *p) {
  return r32(p) | (uint64_t)r32(p + 4) << 32;
}
static void w32(uint8_t *p, uint32_t x) {
  for (unsigned k = 0; k < 4; k++)
    p[k] = (uint8_t)(x >> (8 * k));
}
static void w64(uint8_t *p, uint64_t x) {
  for (unsigned k = 0; k < 8; k++)
    p[k] = (uint8_t)(x >> (8 * k));
}
static size_t aligned(size_t n) { return (n + 15) & ~(size_t)15; }
static int span(uint64_t at, uint64_t size, uint64_t bound) {
  return at <= bound && size <= bound - at;
}
static int fail(NtDiagnostic *e, size_t at, const char *text) {
  memset(e, 0, sizeof(*e));
  e->code = 800;
  e->offset = at;
  snprintf(e->message, sizeof(e->message), "%s", text);
  return 0;
}
static int text_length(const char *s, size_t maximum, size_t *length) {
  if (!s)
    return 0;
  for (size_t n = 0; n <= maximum; n++) {
    unsigned char c = (unsigned char)s[n];
    if (!c) {
      *length = n;
      return n != 0;
    }
    if (c >= 128)
      return 0;
  }
  return 0;
}
int nt_object_validate(const NtObject *o, NtDiagnostic *e) {
  if (!o || o->target < 1 || o->target > 2 || o->symbol_count > MAX_SYMBOLS ||
      o->relocation_count > MAX_RELOCS)
    return fail(e, 0, "invalid target or object counts");
  if ((o->symbol_count && !o->symbols) ||
      (o->relocation_count && !o->relocations))
    return fail(e, 0, "missing object tables");
  for (unsigned j = 0; j < 3; j++)
    if (o->sections[j].size > MAX_FILE ||
        (o->sections[j].size && !o->sections[j].bytes))
      return fail(e, 0, "invalid section payload");
  for (size_t j = 0; j < o->symbol_count; j++) {
    const NtObjectSymbol *s = o->symbols + j;
    size_t nl, cl;
    unsigned role = s->flags & (NT_OBJECT_FUNCTION | NT_OBJECT_VARIABLE);
    if (!text_length(s->name, 512, &nl) ||
        !text_length(s->contract, 65536, &cl) ||
        (j && strcmp(o->symbols[j - 1].name, s->name) >= 0))
      return fail(e, j, "invalid, duplicated or unsorted symbol name/contract");
    if ((s->flags & ~15u) ||
        (role != NT_OBJECT_FUNCTION && role != NT_OBJECT_VARIABLE) ||
        s->section > 3)
      return fail(e, j, "invalid symbol kind");
    if (s->flags & NT_OBJECT_IMPORT) {
      if (s->section || s->offset || s->size || (s->flags & NT_OBJECT_EXPORT))
        return fail(e, j, "invalid import symbol");
    } else if (!s->section ||
               !span(s->offset, s->size, o->sections[s->section - 1].size) ||
               (role == NT_OBJECT_FUNCTION && (s->section != 1 || !s->size)))
      return fail(e, j, "symbol outside its section");
  }
  if (o->entry_symbol) {
    if (o->entry_symbol > o->symbol_count)
      return fail(e, 88, "invalid entry symbol");
    const NtObjectSymbol *s = o->symbols + o->entry_symbol - 1;
    if (s->section != 1 || !(s->flags & NT_OBJECT_FUNCTION))
      return fail(e, 88, "entry is not a defined function");
  }
  for (size_t j = 0; j < o->relocation_count; j++) {
    const NtObjectReloc *r = o->relocations + j;
    unsigned width = r->kind == NT_OBJECT_REL32   ? 4
                     : r->kind == NT_OBJECT_DIR64 ? 8
                                                  : 0;
    if (!width || r->section < 1 || r->section > 3 || !r->target ||
        r->target > o->symbol_count ||
        !span(r->offset, width, o->sections[r->section - 1].size))
      return fail(e, j, "invalid relocation kind, site or target");
    if (j) {
      const NtObjectReloc *p = r - 1;
      unsigned pw = p->kind == NT_OBJECT_REL32 ? 4 : 8;
      if (r->section < p->section ||
          (r->section == p->section && r->offset < p->offset + pw))
        return fail(e, j, "relocations overlap or are not canonical");
    }
  }
  return 1;
}
int nt_object_write(const NtObject *o, NtBuffer *out, NtDiagnostic *e) {
  if (!out || !e)
    return 0;
  memset(e, 0, sizeof(*e));
  if (out->bytes || out->size)
    return fail(e, 0, "output must be empty");
  if (!nt_object_validate(o, e))
    return 0;
  size_t strings = 0;
  for (size_t j = 0; j < o->symbol_count; j++) {
    size_t nl, cl;
    text_length(o->symbols[j].name, 512, &nl);
    text_length(o->symbols[j].contract, 65536, &cl);
    if (nl + cl > MAX_FILE - strings)
      return fail(e, 0, "string table limit");
    strings += nl + cl;
  }
  size_t sym_at = 240, rel_at = sym_at + o->symbol_count * 64,
         str_at = rel_at + o->relocation_count * 40;
  if (str_at > MAX_FILE || strings > MAX_FILE - str_at)
    return fail(e, 0, "object table limit");
  size_t payload = aligned(str_at + strings), offsets[3], size = payload;
  for (unsigned j = 0; j < 3; j++) {
    size = aligned(size);
    offsets[j] = size;
    if (size > MAX_FILE || o->sections[j].size > MAX_FILE - size)
      return fail(e, 0, "object payload limit");
    size += o->sections[j].size;
  }
  uint8_t *b = calloc(size, 1);
  if (!b)
    return fail(e, 0, "object allocation failed");
  memcpy(b, "NXOV0001", 8);
  w32(b + 8, 1);
  w32(b + 12, o->target);
  w32(b + 16, 1);
  w32(b + 20, 3);
  w32(b + 24, (uint32_t)o->symbol_count);
  w32(b + 28, (uint32_t)o->relocation_count);
  w64(b + 32, 96);
  w64(b + 40, sym_at);
  w64(b + 48, rel_at);
  w64(b + 56, str_at);
  w64(b + 64, strings);
  w64(b + 72, payload);
  w64(b + 80, size);
  w32(b + 88, o->entry_symbol);
  const unsigned permissions[] = {5, 1, 3};
  for (unsigned j = 0; j < 3; j++) {
    uint8_t *s = b + 96 + j * 48;
    w32(s, j + 1);
    w32(s + 4, permissions[j]);
    w64(s + 8, j ? 8 : 16);
    w64(s + 16, o->sections[j].size);
    w64(s + 24, offsets[j]);
    if (o->sections[j].size)
      memcpy(b + offsets[j], o->sections[j].bytes, o->sections[j].size);
  }
  size_t text_at = 0;
  for (size_t j = 0; j < o->symbol_count; j++) {
    const NtObjectSymbol *s = o->symbols + j;
    size_t nl, cl;
    text_length(s->name, 512, &nl);
    text_length(s->contract, 65536, &cl);
    uint8_t *r = b + sym_at + j * 64;
    w32(r, (uint32_t)text_at);
    w32(r + 4, (uint32_t)nl);
    memcpy(b + str_at + text_at, s->name, nl);
    text_at += nl;
    w32(r + 8, s->section);
    w32(r + 12, s->flags);
    w64(r + 16, s->offset);
    w64(r + 24, s->size);
    w32(r + 32, (uint32_t)text_at);
    w32(r + 36, (uint32_t)cl);
    memcpy(b + str_at + text_at, s->contract, cl);
    text_at += cl;
    w32(r + 40, 1);
  }
  for (size_t j = 0; j < o->relocation_count; j++) {
    const NtObjectReloc *r = o->relocations + j;
    uint8_t *d = b + rel_at + j * 40;
    w32(d, r->section);
    w32(d + 4, r->kind);
    w64(d + 8, r->offset);
    w32(d + 16, r->target);
    w64(d + 24, (uint64_t)r->addend);
  }
  out->bytes = b;
  out->size = out->capacity = size;
  return 1;
}
void nt_object_free(NtObject *o) {
  if (!o)
    return;
  if (o->_owned) {
    for (unsigned j = 0; j < 3; j++)
      free(o->sections[j].bytes);
    if (o->symbols)
      for (size_t j = 0; j < o->symbol_count; j++) {
        free(o->symbols[j].name);
        free(o->symbols[j].contract);
      }
    free(o->symbols);
    free(o->relocations);
  }
  memset(o, 0, sizeof(*o));
}
static int zero_bytes(const uint8_t *p, size_t size) {
  for (size_t j = 0; j < size; j++)
    if (p[j])
      return 0;
  return 1;
}
static char *copy_text(const uint8_t *p, size_t length) {
  char *s = malloc(length + 1);
  if (!s)
    return NULL;
  memcpy(s, p, length);
  s[length] = 0;
  return s;
}
int nt_object_read(const uint8_t *b, size_t n, NtObject *o, NtDiagnostic *e) {
  if (!o || !e)
    return 0;
  memset(e, 0, sizeof(*e));
  if (o->_owned || o->symbols || o->relocations)
    return fail(e, 0, "read output must be empty");
  if (!b || n < 240 || n > MAX_FILE)
    return fail(e, 0, "invalid NXO file size");
  if (memcmp(b, "NXOV0001", 8) || r32(b + 8) != 1 || r32(b + 16) != 1 ||
      r32(b + 20) != 3 || r32(b + 92))
    return fail(e, 0, "unknown NXO contract version or reserved field");
  uint32_t ns = r32(b + 24), nr = r32(b + 28);
  uint64_t sa = r64(b + 40), ra = r64(b + 48), ta = r64(b + 56),
           ts = r64(b + 64), pa = r64(b + 72);
  if (ns > MAX_SYMBOLS || nr > MAX_RELOCS || r64(b + 32) != 96 || sa != 240 ||
      ra != sa + (uint64_t)ns * 64 || ta != ra + (uint64_t)nr * 40 ||
      !span(ta, ts, n) || pa != aligned((size_t)(ta + ts)) || pa > n ||
      r64(b + 80) != n)
    return fail(e, 24, "invalid NXO table layout");
  if (!zero_bytes(b + ta + ts, (size_t)(pa - ta - ts)))
    return fail(e, (size_t)(ta + ts), "nonzero table padding");
  uint64_t raw = pa;
  const unsigned permissions[] = {5, 1, 3};
  for (unsigned j = 0; j < 3; j++) {
    const uint8_t *s = b + 96 + j * 48;
    uint64_t len = r64(s + 16), at = r64(s + 24), next = aligned((size_t)raw);
    if (r32(s) != j + 1 || r32(s + 4) != permissions[j] ||
        r64(s + 8) != (j ? 8u : 16u) || r64(s + 32) || r64(s + 40) ||
        at != next || !span(at, len, n) ||
        !zero_bytes(b + raw, (size_t)(at - raw)))
      return fail(e, 96 + j * 48, "invalid NXO section layout or permissions");
    raw = at + len;
  }
  if (raw != n)
    return fail(e, (size_t)raw, "NXO overlay or truncated payload");
  o->_owned = 1;
  o->target = r32(b + 12);
  o->entry_symbol = r32(b + 88);
  o->symbol_count = ns;
  o->relocation_count = nr;
  if (ns) {
    o->symbols = calloc(ns, sizeof(*o->symbols));
    if (!o->symbols)
      goto oom;
  }
  if (nr) {
    o->relocations = calloc(nr, sizeof(*o->relocations));
    if (!o->relocations)
      goto oom;
  }
  size_t cursor = 0;
  for (size_t j = 0; j < ns; j++) {
    const uint8_t *r = b + sa + j * 64;
    uint32_t no = r32(r), nl = r32(r + 4), co = r32(r + 32), cl = r32(r + 36);
    if (no != cursor || !nl || nl > 512 || !cl || cl > 65536 ||
        co != (uint64_t)no + nl || !span(no, nl, ts) || !span(co, cl, ts) ||
        memchr(b + ta + no, 0, nl) || memchr(b + ta + co, 0, cl) ||
        r32(r + 40) != 1 || r32(r + 44) || r64(r + 48) || r64(r + 56)) {
      fail(e, (size_t)sa + j * 64, "invalid NXO symbol record");
      goto invalid;
    }
    NtObjectSymbol *s = o->symbols + j;
    s->name = copy_text(b + ta + no, nl);
    s->contract = copy_text(b + ta + co, cl);
    if (!s->name || !s->contract)
      goto oom;
    s->section = r32(r + 8);
    s->flags = r32(r + 12);
    s->offset = r64(r + 16);
    s->size = r64(r + 24);
    cursor = (size_t)co + cl;
  }
  if (cursor != ts) {
    fail(e, (size_t)ta + cursor, "unused NXO string bytes");
    goto invalid;
  }
  for (size_t j = 0; j < nr; j++) {
    const uint8_t *r = b + ra + j * 40;
    NtObjectReloc *d = o->relocations + j;
    if (r32(r + 20) || r64(r + 32)) {
      fail(e, (size_t)ra + j * 40, "nonzero relocation reserved field");
      goto invalid;
    }
    d->section = r32(r);
    d->kind = r32(r + 4);
    d->offset = r64(r + 8);
    d->target = r32(r + 16);
    uint64_t bits = r64(r + 24);
    memcpy(&d->addend, &bits, 8);
  }
  for (unsigned j = 0; j < 3; j++) {
    const uint8_t *s = b + 96 + j * 48;
    size_t len = (size_t)r64(s + 16), at = (size_t)r64(s + 24);
    o->sections[j].size = o->sections[j].capacity = len;
    if (len) {
      o->sections[j].bytes = malloc(len);
      if (!o->sections[j].bytes)
        goto oom;
      memcpy(o->sections[j].bytes, b + at, len);
    }
  }
  if (!nt_object_validate(o, e))
    goto invalid;
  return 1;
oom:
  fail(e, 0, "NXO allocation failed");
invalid:
  nt_object_free(o);
  return 0;
}
