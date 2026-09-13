/* BOOTSTRAP_C_TEST: exact writer suffix from ntasm.c; see pe_oracle-source.json. */
#include "pe_oracle.h"
#include "pe_layout_oracle.h"
#include <stdlib.h>
#include <string.h>
#define NtArtifact NovaOraclePeImage
#define NtBaseReloc NovaOracleBaseReloc
#define nt_make_pe nova_oracle_make_pe
static void put16(uint8_t *p, uint16_t value) {
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
}
static void put32(uint8_t *p, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    p[i] = (uint8_t)(value >> (8 * i));
}
static void put64(uint8_t *p, uint64_t value) {
  for (unsigned i = 0; i < 8; ++i)
    p[i] = (uint8_t)(value >> (8 * i));
}
static uint32_t align32(uint32_t value, uint32_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}
static void pe_section(uint8_t *p, const char *name, uint32_t virtual_size,
                       uint32_t rva, uint32_t raw_size, uint32_t raw,
                       uint32_t flags) {
  memcpy(p, name, strlen(name));
  put32(p + 8, virtual_size);
  put32(p + 12, rva);
  put32(p + 16, raw_size);
  put32(p + 20, raw);
  put32(p + 36, flags);
}
static int compare_rva(const void *left, const void *right) {
  uint32_t a = *(const uint32_t *)left, b = *(const uint32_t *)right;
  return a < b ? -1 : a > b ? 1 : 0;
}
int nt_make_pe(NtArtifact *artifact) {
  if (!artifact || artifact->error.code || !artifact->text.size ||
      artifact->entry >= artifact->text.size ||
      artifact->text.size > 16 * 1024 * 1024 || !artifact->text.bytes ||
      artifact->rdata.size > 16 * 1024 * 1024 ||
      artifact->data.size > 16 * 1024 * 1024 ||
      (artifact->rdata.size && !artifact->rdata.bytes) ||
      (artifact->data.size && !artifact->data.bytes) ||
      artifact->base_relocation_count > 1048576 ||
      (artifact->base_relocation_count && !artifact->base_relocations))
    return 0;
  uint32_t text_size = (uint32_t)artifact->text.size;
  uint32_t text_raw = align32(text_size, 512);
  if (artifact->rdata.size > UINT32_MAX - 8 || artifact->data.size > UINT32_MAX)
    return 0;
  uint32_t rdata_size = 8 + (uint32_t)artifact->rdata.size;
  uint32_t data_size = artifact->data.size ? (uint32_t)artifact->data.size : 8;
  NtPeLayout layout;
  if (!nt_pe_layout(text_size, artifact->rdata.size, artifact->data.size,
                    &layout))
    return 0;
  uint32_t ro_rva = layout.rdata;
  uint32_t data_rva = layout.data;
  uint32_t reloc_rva = layout.reloc;
  size_t rva_count = artifact->base_relocation_count + 1;
  uint32_t *rvas = malloc(rva_count * sizeof(*rvas));
  if (!rvas)
    return 0;
  rvas[0] = ro_rva; /* PE loader's anchor is not part of logical user rdata. */
  for (size_t i = 0; i < artifact->base_relocation_count; i++) {
    const NtBaseReloc *r = artifact->base_relocations + i;
    size_t limit = r->section == 1   ? artifact->text.size
                   : r->section == 2 ? artifact->rdata.size
                   : r->section == 3 ? artifact->data.size
                                     : 0;
    uint32_t base = r->section == 1   ? 4096
                    : r->section == 2 ? ro_rva + 8
                                      : data_rva;
    if (r->section < 1 || r->section > 3 || r->offset > limit ||
        limit - r->offset < 8) {
      free(rvas);
      return 0;
    }
    rvas[i + 1] = base + (uint32_t)r->offset;
  }
  qsort(rvas, rva_count, sizeof(*rvas), compare_rva);
  for (size_t i = 1; i < rva_count; i++)
    if (rvas[i] - rvas[i - 1] < 8) {
      free(rvas);
      return 0;
    }
  uint32_t reloc_size = 0;
  for (size_t i = 0; i < rva_count;) {
    size_t end = i + 1;
    while (end < rva_count && (rvas[end] & ~4095u) == (rvas[i] & ~4095u))
      end++;
    reloc_size += align32(8 + 2 * (uint32_t)(end - i), 4);
    i = end;
  }
  uint32_t reloc_raw_size = align32(reloc_size, 512);
  uint32_t rdata_raw_size = align32(rdata_size, 512);
  uint32_t data_raw_size = align32(data_size, 512);
  uint32_t ro_raw = 1024 + text_raw;
  uint32_t data_raw = ro_raw + rdata_raw_size;
  uint32_t reloc_raw = data_raw + data_raw_size;
  size_t size = (size_t)reloc_raw + reloc_raw_size;
  uint8_t *bytes = (uint8_t *)calloc(size, 1);
  if (!bytes) {
    free(rvas);
    return 0;
  }
  bytes[0] = 'M';
  bytes[1] = 'Z';
  put32(bytes + 60, 128);
  put32(bytes + 128, 0x4550);
  put16(bytes + 132, 0x8664);
  put16(bytes + 134, 4);
  put16(bytes + 148, 240);
  put16(bytes + 150, 0x2022);
  put16(bytes + 152, 0x20b);
  put32(bytes + 156, text_raw);
  put32(bytes + 160, rdata_raw_size + data_raw_size + reloc_raw_size);
  put32(bytes + 168, 4096 + (uint32_t)artifact->entry);
  put32(bytes + 172, 4096);
  put64(bytes + 176, UINT64_C(0x140000000));
  put32(bytes + 184, 4096);
  put32(bytes + 188, 512);
  put16(bytes + 192, 6);
  put16(bytes + 200, 6);
  put32(bytes + 208, align32(reloc_rva + reloc_size, 4096));
  put32(bytes + 212, 1024);
  put16(bytes + 220, 10);
  put16(bytes + 222, 0x160);
  put64(bytes + 224, 0x100000);
  put64(bytes + 232, 0x1000);
  put64(bytes + 240, 0x100000);
  put64(bytes + 248, 0x1000);
  put32(bytes + 260, 16);
  put32(bytes + 304, reloc_rva);
  put32(bytes + 308, reloc_size);
  pe_section(bytes + 392, ".text", text_size, 4096, text_raw, 1024, 0x60000020);
  pe_section(bytes + 432, ".rdata", rdata_size, ro_rva, rdata_raw_size, ro_raw,
             0x40000040);
  pe_section(bytes + 472, ".data", data_size, data_rva, data_raw_size, data_raw,
             0xc0000040);
  pe_section(bytes + 512, ".reloc", reloc_size, reloc_rva, reloc_raw_size,
             reloc_raw, 0x42000040);
  memcpy(bytes + 1024, artifact->text.bytes, artifact->text.size);
  put64(bytes + ro_raw, UINT64_C(0x140001000) + artifact->entry);
  if (artifact->rdata.size)
    memcpy(bytes + ro_raw + 8, artifact->rdata.bytes, artifact->rdata.size);
  if (artifact->data.size)
    memcpy(bytes + data_raw, artifact->data.bytes, artifact->data.size);
  size_t write_at = reloc_raw;
  for (size_t i = 0; i < rva_count;) {
    size_t end = i + 1;
    uint32_t page = rvas[i] & ~4095u;
    while (end < rva_count && (rvas[end] & ~4095u) == page)
      end++;
    uint32_t block_size = align32(8 + 2 * (uint32_t)(end - i), 4);
    put32(bytes + write_at, page);
    put32(bytes + write_at + 4, block_size);
    for (size_t j = i; j < end; j++)
      put16(bytes + write_at + 8 + 2 * (j - i),
            (uint16_t)(0xa000u | (rvas[j] & 4095u)));
    write_at += block_size;
    i = end;
  }
  free(rvas);
  free(artifact->pe.bytes);
  artifact->pe.bytes = bytes;
  artifact->pe.size = size;
  artifact->pe.capacity = size;
  return 1;
}

int nova_oracle_relocation(unsigned kind,uint64_t source,uint64_t target,int64_t addend,uint64_t*value){return nt_relocation_value(kind,source,target,addend,value);}
