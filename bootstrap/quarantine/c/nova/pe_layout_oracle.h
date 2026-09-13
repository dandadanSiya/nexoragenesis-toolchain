#ifndef GENESIS_PE_LAYOUT_H
#define GENESIS_PE_LAYOUT_H
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#define NT_PE_IMAGE_BASE UINT64_C(0x140000000)
typedef struct {
  uint32_t text, rdata, data, reloc;
} NtPeLayout;
static inline uint32_t nt_align_u32(uint32_t n, uint32_t a) {
  return (n + a - 1) & ~(a - 1);
}
static inline int nt_pe_layout(size_t text, size_t rdata, size_t data,
                               NtPeLayout *l) {
  if (!l || !text || text > 16 * 1024 * 1024 || rdata > 16 * 1024 * 1024 ||
      data > 16 * 1024 * 1024)
    return 0;
  l->text = 4096;
  l->rdata = nt_align_u32(4096 + (uint32_t)text, 4096);
  l->data = nt_align_u32(l->rdata + 8 + (uint32_t)rdata, 4096);
  l->reloc = nt_align_u32(l->data + (data ? (uint32_t)data : 8), 4096);
  return 1;
}
static inline uint64_t nt_pe_logical_rva(const NtPeLayout *l,
                                         uint32_t section) {
  return section == 1   ? l->text
         : section == 2 ? (uint64_t)l->rdata + 8
         : section == 3 ? l->data
                        : UINT64_MAX;
}
static inline int nt_relocation_value(unsigned kind, uint64_t source,
                                      uint64_t target, int64_t addend,
                                      uint64_t *value) {
  if (!value || source > INT64_MAX - 4 || target > INT64_MAX)
    return 0;
  if (kind == 1) {
    int64_t t = (int64_t)target, p = (int64_t)source + 4;
    if ((addend > 0 && t > INT64_MAX - addend) ||
        (addend < 0 && t < INT64_MIN - addend))
      return 0;
    t += addend;
    if (t < INT64_MIN + p)
      return 0;
    t -= p;
    if (t < INT32_MIN || t > INT32_MAX)
      return 0;
    *value = (uint32_t)(int32_t)t;
    return 1;
  }
  if (kind == 2) {
    if (target > UINT64_MAX - NT_PE_IMAGE_BASE)
      return 0;
    uint64_t t = NT_PE_IMAGE_BASE + target;
    if (addend < 0) {
      uint64_t magnitude = (uint64_t)(-(addend + 1)) + 1;
      if (magnitude > t)
        return 0;
      t -= magnitude;
    } else {
      if ((uint64_t)addend > UINT64_MAX - t)
        return 0;
      t += (uint64_t)addend;
    }
    *value = t;
    return 1;
  }
  return 0;
}
static inline void nt_relocation_store(uint8_t *destination, unsigned width,
                                       uint64_t value) {
  for (unsigned j = 0; j < width; j++)
    destination[j] = (uint8_t)(value >> (8 * j));
}
#endif
