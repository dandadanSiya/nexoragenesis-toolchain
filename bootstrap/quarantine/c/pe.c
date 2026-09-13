#include "pe.h"
#include <stdio.h>
#include <string.h>
static uint16_t r16(const uint8_t *p) {
  return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
static uint32_t r32(const uint8_t *p) {
  return r16(p) | (uint32_t)r16(p + 2) << 16;
}
static uint64_t r64(const uint8_t *p) {
  return r32(p) | (uint64_t)r32(p + 4) << 32;
}
static int within(size_t at, size_t count, size_t n) {
  return at <= n && count <= n - at;
}
static int bad(NtDiagnostic *e, size_t at, const char *message) {
  e->code = 601;
  e->offset = at;
  snprintf(e->message, sizeof(e->message), "%s", message);
  return 0;
}
typedef struct {
  uint32_t va, vs, raw, rs, flags;
} Section;
static int map(const Section *sections, unsigned count, uint32_t rva,
               size_t size, size_t *offset) {
  for (unsigned j = 0; j < count; ++j) {
    const Section *s = sections + j;
    if (rva >= s->va && within(rva - s->va, size, s->vs) &&
        within(rva - s->va, size, s->rs)) {
      *offset = (size_t)s->raw + rva - s->va;
      return 1;
    }
  }
  return 0;
}
int nt_pe_inspect(const uint8_t *b, size_t n, NtPeInfo *i, NtDiagnostic *e) {
  if (!i || !e)
    return 0;
  memset(i, 0, sizeof(*i));
  memset(e, 0, sizeof(*e));
  if (!b || n < 64)
    return bad(e, 0, "truncated DOS header");
  if (r16(b) != 0x5a4d)
    return bad(e, 0, "invalid DOS signature");
  uint32_t nt = r32(b + 60);
  if (nt < 64 || !within(nt, 264, n))
    return bad(e, 60, "invalid PE header offset");
  if (r32(b + nt) != 0x4550)
    return bad(e, nt, "invalid PE signature");
  if (r16(b + nt + 4) != 0x8664 || r16(b + nt + 20) != 240 ||
      r16(b + nt + 24) != 0x20b)
    return bad(e, nt + 4, "expected AMD64 PE32+ header");
  unsigned count = r16(b + nt + 6);
  size_t table = (size_t)nt + 264;
  if (!count || count > 96 || !within(table, 40 * count, n))
    return bad(e, nt + 6, "invalid section table");
  if (r32(b + nt + 8) || r32(b + nt + 12) || r32(b + nt + 16))
    return bad(e, nt + 8, "timestamp and COFF symbols must be zero");
  const uint8_t *o = b + nt + 24;
  uint32_t section_align = r32(o + 32), file_align = r32(o + 36),
           image = r32(o + 56), headers = r32(o + 60), entry = r32(o + 16);
  if (section_align != 4096 || file_align != 512 || !image || (image % 4096) ||
      headers % 512 || !within(table, 40 * count, headers) || headers > n)
    return bad(e, nt + 56, "invalid canonical alignment or image size");
  if (r16(o + 68) != 10 || r32(o + 64) || r32(o + 108) != 16)
    return bad(e, nt + 88, "invalid EFI subsystem, checksum or directories");
  for (unsigned d = 0; d < 16; ++d)
    if (d != 5 && (r32(o + 112 + 8 * d) || r32(o + 116 + 8 * d)))
      return bad(e, nt + 136 + 8 * d, "unsupported external directory");
  Section s[96];
  uint64_t next_virtual = 4096;
  size_t next_raw = headers;
  int executable_entry = 0;
  for (unsigned j = 0; j < count; ++j) {
    const uint8_t *h = b + table + 40 * j;
    s[j] = (Section){r32(h + 12), r32(h + 8), r32(h + 20), r32(h + 16),
                     r32(h + 36)};
    uint64_t end = (uint64_t)s[j].va + s[j].vs;
    if (s[j].va != next_virtual || s[j].raw != next_raw || s[j].rs % 512 ||
        s[j].vs > s[j].rs || !within(s[j].raw, s[j].rs, n) || end > image)
      return bad(e, table + 40 * j,
                 "noncanonical or overlapping section layout");
    if ((s[j].flags & 0xa0000000u) == 0xa0000000u)
      return bad(e, table + 40 * j + 36, "writable executable section");
    if (r32(h + 24) || r32(h + 28) || r32(h + 32))
      return bad(e, table + 40 * j + 24, "COFF relocation metadata in image");
    if (entry >= s[j].va && (uint64_t)entry < end &&
        (s[j].flags & 0xe0000000u) == 0x60000000u)
      executable_entry = 1;
    next_raw += s[j].rs;
    next_virtual = (end + 4095) & ~UINT64_C(4095);
  }
  if (next_raw != n || next_virtual != image)
    return bad(e, n, "overlay or incorrect image size");
  if (!executable_entry)
    return bad(e, nt + 40, "entry is not within executable read-only code");
  uint32_t reloc_rva = r32(o + 152), reloc_size = r32(o + 156), relocations = 0;
  size_t at;
  if (!reloc_size || !map(s, count, reloc_rva, reloc_size, &at))
    return bad(e, nt + 176, "missing or invalid base relocations");
  size_t consumed = 0;
  uint32_t previous_page = 0;
  int first_page = 1;
  while (consumed < reloc_size) {
    if (!within(consumed, 8, reloc_size))
      return bad(e, at + consumed, "truncated relocation header");
    uint32_t page = r32(b + at + consumed), block = r32(b + at + consumed + 4);
    if (page % 4096 || block < 8 || block % 4 ||
        !within(consumed, block, reloc_size) ||
        (!first_page && page <= previous_page))
      return bad(e, at + consumed, "invalid relocation block");
    first_page = 0;
    previous_page = page;
    unsigned previous_offset = 0;
    int first_offset = 1;
    for (size_t k = 8; k < block; k += 2) {
      uint16_t r = r16(b + at + consumed + k);
      unsigned type = r >> 12, off = r & 4095;
      size_t site;
      if (!type) {
        if (r)
          return bad(e, at + consumed + k, "nonzero relocation padding");
        continue;
      }
      if (type != 10 || page > UINT32_MAX - off ||
          !map(s, count, page + off, 8, &site) ||
          (!first_offset && off <= previous_offset))
        return bad(e, at + consumed + k, "invalid DIR64 relocation site");
      first_offset = 0;
      previous_offset = off;
      ++relocations;
    }
    consumed += block;
  }
  if (!relocations)
    return bad(e, at, "no actual DIR64 relocation");
  *i = (NtPeInfo){entry, image, headers, count, r64(o + 24), relocations};
  return 1;
}
