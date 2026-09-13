#define efi_main probe_efi_main
#include "gp_probe.c"
#undef efi_main
static unsigned checks, failures;
#define CHECK(c, n)                                                            \
  do {                                                                         \
    (void)(n);                                                                 \
    checks++;                                                                  \
    if (!(c))                                                                  \
      failures++;                                                              \
  } while (0)
static int same(const uint8_t *a, const uint8_t *b, size_t size) {
  while (size--)
    if (*a++ != *b++)
      return 0;
  return 1;
}
int main(void) {
  static const uint8_t sidt[] = {0x0f, 0x01, 0x09, 0xc3};
  static const uint8_t lidt[] = {0x0f, 0x01, 0x19, 0xc3};
  static const uint8_t cs[] = {0x31, 0xc0, 0x66, 0x8c, 0xc8, 0xc3};
  static const uint8_t flags[] = {0x9c, 0x58, 0xc3};
  static const uint8_t gp[] = {
      0xb9, 0xff, 0xff, 0xff, 0xff, 0x31, 0xc0, 0x31, 0xd2, 0x9c,
      0x41, 0x58, 0x0f, 0x30, 0x9c, 0x58, 0x4c, 0x31, 0xc0, 0xc3};
  CHECK(sizeof(sidt_code) == sizeof(sidt) &&
            same(sidt_code, sidt, sizeof(sidt)),
        "SIDT [rcx]; ret encoding");
  CHECK(sizeof(lidt_code) == sizeof(lidt) &&
            same(lidt_code, lidt, sizeof(lidt)),
        "LIDT [rcx]; ret encoding");
  CHECK(sizeof(cs_code) == sizeof(cs) && same(cs_code, cs, sizeof(cs)),
        "read CS encoding");
  CHECK(sizeof(flags_code) == sizeof(flags) &&
            same(flags_code, flags, sizeof(flags)),
        "read RFLAGS encoding");
  CHECK(sizeof(gp_code) == sizeof(gp) && same(gp_code, gp, sizeof(gp)),
        "reserved WRMSR #GP flags observer encoding");
  return failures ? 1 : 0;
}
