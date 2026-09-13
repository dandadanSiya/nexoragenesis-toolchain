#include "x64.h"
#include <stdio.h>
#include <string.h>
static unsigned checks, failures;
#define CHECK(x)                                                               \
  do {                                                                         \
    checks++;                                                                  \
    if (!(x)) {                                                                \
      failures++;                                                              \
      fprintf(stderr, "FAIL conformance line %d: %s\n", __LINE__, #x);         \
    }                                                                          \
  } while (0)
static NtX64Operand r(unsigned n, unsigned width) {
  NtX64Operand o = {0};
  o.kind = NT_X64_REG;
  o.reg = n;
  o.width = width;
  return o;
}
static NtX64Operand m(void) {
  NtX64Operand o = {0};
  o.kind = NT_X64_MEM;
  o.width = 64;
  o.base = 12;
  o.index = 9;
  o.scale = 4;
  return o;
}
int main(void) {
  NtX64Context context = {NT_X64_ALL_FEATURES, 0};
  NtX64Effects e;
  NtX64Operand a[] = {r(0, 64), m()};
  CHECK(nt_x64_effects("adc", a, 2, context, &e) == NT_X64_OK);
  CHECK(e.flags_read == NT_X64_FLAG_CF);
  CHECK(e.flags_written == NT_X64_FLAGS_ARITH);
  CHECK(e.memory_read && !e.memory_write);
  CHECK(e.registers_read == ((1u << 0) | (1u << 12) | (1u << 9) | (1u << 17)));
  CHECK(e.registers_written == ((1u << 0) | (1u << 17)));
  CHECK(nt_x64_effects("lea", a, 2, context, &e) == NT_X64_OK);
  CHECK(!e.memory_read && !e.memory_write && !e.flags_written);
  CHECK(e.registers_read == ((1u << 12) | (1u << 9)));
  CHECK(nt_x64_effects("div", a, 1, context, &e) == NT_X64_OK);
  CHECK(e.flags_undefined == NT_X64_FLAGS_ARITH && !e.flags_written);
  CHECK(e.may_trap &&
        e.registers_written == ((1u << 0) | (1u << 2) | (1u << 17)));
  CHECK(nt_x64_effects("push", a, 1, context, &e) == NT_X64_OK);
  CHECK(e.stack_write && !e.stack_read && e.registers_written == (1u << 4));
  CHECK(nt_x64_effects("cpuid", NULL, 0, context, &e) == NT_X64_OK);
  CHECK(e.registers_read == ((1u << 0) | (1u << 1)) &&
        e.registers_written == 15);
  CHECK(nt_x64_effects("hlt", NULL, 0, context, &e) == NT_X64_OK);
  CHECK(!e.terminal && (e.effects & NT_X64_EFFECT_SUSPENDS));
  CHECK(nt_x64_effects("ud2", NULL, 0, context, &e) == NT_X64_OK);
  CHECK(e.terminal && e.may_trap);
  memset(&e, 0xa5, sizeof(e));
  NtX64Effects original = e;
  CHECK(nt_x64_effects("missing", NULL, 0, context, &e) == NT_X64_UNKNOWN);
  CHECK(!memcmp(&e, &original, sizeof(e)));
  NtX64Operand store[] = {m(), {0}};
  store[1].kind = NT_X64_IMM;
  store[1].imm = 42;
  store[0].base = -2;
  store[0].index = -1;
  store[0].scale = 1;
  NtX64Instruction instruction;
  CHECK(nt_x64_encode("store", store, 2, context, &instruction) == NT_X64_OK);
  CHECK(instruction.displacement_size == 4 &&
        instruction.displacement_offset == 3 && instruction.length == 11);
  NtX64Operand shift[] = {r(0, 64), r(1, 8)};
  CHECK(nt_x64_effects("shl", shift, 2, context, &e) == NT_X64_OK);
  CHECK(!e.flags_written && e.flags_undefined == NT_X64_FLAGS_ARITH);
  printf("CONFORMANCE_TESTS checks=%u failures=%u\n", checks, failures);
  return failures ? 1 : 0;
}
