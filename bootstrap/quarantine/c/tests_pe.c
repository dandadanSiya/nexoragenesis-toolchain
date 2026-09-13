#include "pe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, failures;
#define CHECK(c, m)                                                            \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(c)) {                                                                \
      ++failures;                                                              \
      fprintf(stderr, "FAIL: %s\n", m);                                        \
    }                                                                          \
  } while (0)
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  FILE *f = fopen(argv[1], "rb");
  if (!f)
    return 2;
  uint8_t b[4096];
  size_t n = fread(b, 1, sizeof(b), f);
  fclose(f);
  NtPeInfo i;
  NtDiagnostic e;
  CHECK(nt_pe_inspect(b, n, &i, &e), "inspect actual compiler-produced PE");
  CHECK(i.entry_rva == 4096 && i.image_size == 20480 && i.section_count == 4 &&
            i.image_base == UINT64_C(0x140000000) && i.relocation_count == 1,
        "independent PE metadata and DIR64 count");
  struct {
    size_t at;
    uint8_t value;
    const char *name;
  } mutations[] = {{0, 0, "bad DOS signature"},
                   {128, 0, "bad PE signature"},
                   {132, 0, "wrong CPU"},
                   {136, 1, "timestamp prohibited"},
                   {152, 0, "wrong optional format"},
                   {220, 3, "not UEFI"},
                   {60, 255, "out-of-bounds NT header"},
                   {134, 0, "empty section table"},
                   {170, 1, "entry outside code"},
                   {431, 0xe0, "writable executable code"},
                   {471, 0xe0, "writable executable data"},
                   {412, 1, "unaligned raw code"},
                   {2569, 0xb0, "unknown relocation"},
                   {2564, 7, "undersized relocation block"}};
  for (size_t k = 0; k < sizeof(mutations) / sizeof(mutations[0]); ++k) {
    uint8_t old = b[mutations[k].at];
    b[mutations[k].at] = mutations[k].value;
    CHECK(!nt_pe_inspect(b, n, &i, &e) && e.code == 601, mutations[k].name);
    b[mutations[k].at] = old;
  }
  CHECK(!nt_pe_inspect(b, n - 1, &i, &e) && e.code == 601,
        "truncated final section");
  b[n] = 0;
  CHECK(!nt_pe_inspect(b, n + 1, &i, &e) && e.code == 601, "overlay rejected");
  printf("NTASM_PE_INSPECT_TESTS checks=%u failures=%u\n", checks, failures);
  return failures ? 1 : 0;
}
