#include "object.h"
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
static uint32_t r32(const uint8_t *p) {
  return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
int main(void) {
  uint8_t code[] = {0x48, 0xb8, 42, 0, 0, 0, 0, 0, 0, 0, 0xc3};
  uint8_t data[] = {42, 0, 0, 0, 0, 0, 0, 0};
  NtObjectSymbol symbols[] = {
      {"app.answer", "data:u64", 2, NT_OBJECT_VARIABLE, 0, 8},
      {"app.main", "fn()->u64;abi=nx64-abi-v0", 1,
       NT_OBJECT_EXPORT | NT_OBJECT_FUNCTION, 0, 11}};
  NtObject input = {0};
  input.target = 1;
  input.entry_symbol = 2;
  input.symbols = symbols;
  input.symbol_count = 2;
  input.sections[0] = (NtBuffer){code, sizeof code, sizeof code};
  input.sections[1] = (NtBuffer){data, sizeof data, sizeof data};
  NtBuffer bytes = {0};
  NtDiagnostic error;
  NtObject decoded = {0};
  int wrote = nt_object_write(&input, &bytes, &error);
  CHECK(wrote, "serialize code/data/symbols into NXO");
  if (wrote) {
    CHECK(bytes.size >= 400 && !memcmp(bytes.bytes, "NXOV0001", 8) &&
              r32(bytes.bytes + 8) == 1,
          "NXO magic and format version");
    CHECK(r32(bytes.bytes + 20) == 3 && r32(bytes.bytes + 24) == 2 &&
              r32(bytes.bytes + 88) == 2,
          "NXO independent header counts and entry symbol");
    CHECK(nt_object_read(bytes.bytes, bytes.size, &decoded, &error),
          "read structurally valid NXO");
    CHECK(decoded.symbol_count == 2 &&
              !strcmp(decoded.symbols[0].name, "app.answer") &&
              !strcmp(decoded.symbols[1].contract, symbols[1].contract),
          "typed symbol contracts roundtrip");
    CHECK(decoded.sections[0].size == sizeof code &&
              !memcmp(decoded.sections[0].bytes, code, sizeof code),
          "machine code payload roundtrip");
    CHECK(decoded.sections[1].size == 8 && decoded.sections[1].bytes[0] == 42,
          "data payload roundtrip");
  }
  nt_object_free(&decoded);
  free(bytes.bytes);
  printf("NTASM_NXO_TESTS checks=%u failures=%u\n", checks, failures);
  return failures ? 1 : 0;
}
