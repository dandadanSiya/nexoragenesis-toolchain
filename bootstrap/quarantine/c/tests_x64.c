#define _GNU_SOURCE
#include "x64.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__unix__) && defined(__x86_64__)
#include <sys/mman.h>
#include <unistd.h>
#endif
static unsigned cases, failures;
static NtX64Context all = {NT_X64_ALL_FEATURES, 0};
static NtX64Operand r(unsigned n, unsigned w) {
  NtX64Operand o = {0};
  o.kind = NT_X64_REG;
  o.reg = n;
  o.width = w;
  return o;
}
static NtX64Operand u(uint64_t x) {
  NtX64Operand o = {0};
  o.kind = NT_X64_IMM;
  o.imm = x;
  return o;
}
static NtX64Operand s(int64_t x) {
  NtX64Operand o = u((uint64_t)x);
  o.is_signed = 1;
  return o;
}
static NtX64Operand m(int b, int i, unsigned sc, int64_t d, unsigned w) {
  NtX64Operand o = {0};
  o.kind = NT_X64_MEM;
  o.base = b;
  o.index = i;
  o.scale = sc;
  o.disp = d;
  o.width = w;
  return o;
}
static NtX64Operand rel(int64_t x) {
  NtX64Operand o = s(x);
  o.kind = NT_X64_REL;
  return o;
}
static NtX64Operand cr(unsigned n) {
  NtX64Operand o = r(n, 64);
  o.kind = NT_X64_CR;
  return o;
}
static void golden(const char *n, NtX64Operand a, NtX64Operand b,
                   NtX64Operand c, size_t count, const char *hex) {
  NtX64Operand ops[3] = {a, b, c};
  NtX64Instruction got;
  uint8_t expected[15];
  size_t len = 0;
  NtX64Error e;
  unsigned v;
  memset(&got, 0xa5, sizeof got);
  while (*hex) {
    if (sscanf(hex, "%2x", &v) != 1)
      abort();
    expected[len++] = (uint8_t)v;
    hex += 2;
  }
  e = nt_x64_encode(n, ops, count, all, &got);
  cases++;
  if (e || got.length != len || memcmp(got.bytes, expected, len)) {
    failures++;
    printf("FAIL golden %s error=%u len=%zu expected=%zu bytes=", n, e,
           got.length, len);
    if (!e)
      for (size_t i = 0; i < got.length; i++)
        printf("%02x", got.bytes[i]);
    puts("");
  }
  NtX64Effects footprint;
  cases++;
  NtX64Error semantic = nt_x64_effects(n, ops, count, all, &footprint);
  if (semantic || (footprint.flags_written & footprint.flags_undefined)) {
    failures++;
    printf("FAIL footprint %s error=%u\n", n, semantic);
  }
}
static void reject(const char *n, NtX64Operand a, NtX64Operand b,
                   NtX64Operand c, size_t count, NtX64Context ctx,
                   NtX64Error want) {
  NtX64Operand ops[3] = {a, b, c};
  NtX64Instruction got, before;
  NtX64Error e;
  memset(&got, 0xa5, sizeof got);
  memcpy(&before, &got, sizeof got);
  e = nt_x64_encode(n, ops, count, ctx, &got);
  cases++;
  if (e != want || memcmp(&got, &before, sizeof got)) {
    failures++;
    printf("FAIL reject %s error=%u expected=%u preserved=%d\n", n, e, want,
           !memcmp(&got, &before, sizeof got));
  }
}
#define N ((NtX64Operand){0})
int main(void) {
  golden("nop", N, N, N, 0, "90");
  const char *names[] = {
      "pause",   "ud2",     "cpuid",  "cbw",   "cwde",   "cdqe",   "cwd",
      "cdq",     "cqo",     "pushfq", "popfq", "rdtsc",  "lfence", "sfence",
      "mfence",  "cli",     "sti",    "hlt",   "swapgs", "rdmsr",  "wrmsr",
      "syscall", "sysretq", "iretq",  "ret",   "return"};
  const char *bytes[] = {"f390",   "0f0b",   "0fa2",   "6698", "98",     "4898",
                         "6699",   "99",     "4899",   "9c",   "9d",     "0f31",
                         "0faee8", "0faef8", "0faef0", "fa",   "fb",     "f4",
                         "0f01f8", "0f32",   "0f30",   "0f05", "480f07", "48cf",
                         "c3",     "c3"};
  for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
    golden(names[i], N, N, N, 0, bytes[i]);
    reject(names[i], r(0, 64), N, N, 1, all, NT_X64_COUNT);
    reject(names[i], N, N, N, 0, (NtX64Context){0, 0}, NT_X64_FEATURE);
  }
  reject("hlt", N, N, N, 0, (NtX64Context){31, 3}, NT_X64_PRIVILEGE);
  reject("rdtsc", N, N, N, 0, (NtX64Context){1, 0}, NT_X64_FEATURE);
  reject("wrmsr", N, N, N, 0, (NtX64Context){1, 0}, NT_X64_FEATURE);
  reject("syscall", N, N, N, 0, (NtX64Context){1, 0}, NT_X64_FEATURE);
  reject("lfence", N, N, N, 0, (NtX64Context){1, 0}, NT_X64_FEATURE);
  reject("nop", N, N, N, 0, (NtX64Context){31, 4}, NT_X64_PRIVILEGE);
  reject("unknown", N, N, N, 0, all, NT_X64_UNKNOWN);
  golden("mov", r(0, 64), r(9, 64), N, 2, "4c89c8");
  golden("mov", r(8, 8), r(4, 8), N, 2, "4188e0");
  golden("mov", r(9, 16), r(14, 16), N, 2, "664589f1");
  golden("mov", r(2, 32), r(7, 32), N, 2, "89fa");
  golden("mov", r(15, 64), u(UINT64_MAX), N, 2, "49bfffffffffffffffff");
  golden("mov", r(4, 8), u(255), N, 2, "40b4ff");
  golden("mov", r(8, 16), s(-32768), N, 2, "6641b80080");
  golden("mov", r(10, 32), u(UINT32_MAX), N, 2, "41baffffffff");
  NtX64Operand ah = r(0, 8);
  ah.high8 = 1;
  golden("mov", ah, r(1, 8), N, 2, "88cc");
  golden("mov", ah, u(3), N, 2, "b403");
  reject("mov", ah, r(8, 8), N, 2, all, NT_X64_HIGH8_REX);
  reject("mov", ah, r(4, 8), N, 2, all, NT_X64_HIGH8_REX);
  reject("mov", r(16, 64), r(0, 64), N, 2, all, NT_X64_REGISTER);
  reject("mov", r(0, 32), r(0, 64), N, 2, all, NT_X64_WIDTH);
  reject("mov", r(0, 8), u(256), N, 2, all, NT_X64_RANGE);
  reject("mov", r(0, 8), s(-129), N, 2, all, NT_X64_RANGE);
  reject("mov", r(0, 7), u(2), N, 2, all, NT_X64_WIDTH);
  reject("mov", r(0, 64), N, N, 1, all, NT_X64_COUNT);
  reject("mov", r(0, 64), r(1, 64), N, 2, (NtX64Context){0, 0}, NT_X64_FEATURE);
  golden("load", r(8, 64), m(13, 12, 8, 0, 64), N, 2, "4f8b44e500");
  golden("load", r(0, 32), m(-2, -1, 1, -4, 32), N, 2, "8b05fcffffff");
  golden("load", r(0, 8), m(-1, 1, 4, 0x1234, 8), N, 2, "8a048d34120000");
  golden("load", r(3, 16), m(-1, -1, 1, -32, 16), N, 2, "668b1c25e0ffffff");
  golden("store", m(4, -1, 1, 127, 8), ah, N, 2, "8864247f");
  golden("store", m(5, -1, 1, -128, 32), r(0, 32), N, 2, "894580");
  golden("store", m(5, -1, 1, 128, 64), r(15, 64), N, 2, "4c89bd80000000");
  golden("store", m(0, -1, 1, 0, 64), s(-1), N, 2, "48c700ffffffff");
  golden("store", m(0, -1, 1, 0, 8), u(255), N, 2, "c600ff");
  golden("store", m(0, -1, 1, 0, 16), u(65535), N, 2, "66c700ffff");
  golden("store", m(0, -1, 1, 0, 32), u(UINT32_MAX), N, 2, "c700ffffffff");
  golden("lea", r(9, 64), m(-2, -1, 1, 32, 64), N, 2, "4c8d0d20000000");
  NtX64Operand fs = m(12, -1, 1, 0, 16);
  fs.segment = NT_X64_FS;
  golden("load", r(8, 16), fs, N, 2, "6466458b0424");
  fs.segment = NT_X64_GS;
  golden("load", r(8, 16), fs, N, 2, "6566458b0424");
  reject("load", r(0, 64), m(0, 4, 1, 0, 64), N, 2, all, NT_X64_ADDRESS);
  reject("load", r(0, 64), m(-2, 1, 1, 0, 64), N, 2, all, NT_X64_ADDRESS);
  reject("load", r(0, 64), m(0, 1, 3, 0, 64), N, 2, all, NT_X64_ADDRESS);
  reject("load", r(0, 64), m(0, -1, 1, INT64_MAX, 64), N, 2, all,
         NT_X64_ADDRESS);
  reject("store", m(0, -1, 1, 0, 64), u(UINT32_MAX), N, 2, all, NT_X64_RANGE);
  reject("store", m(0, -1, 1, 0, 64), s(INT32_MIN - INT64_C(1)), N, 2, all,
         NT_X64_RANGE);
  reject("load", ah, m(8, -1, 1, 0, 8), N, 2, all, NT_X64_HIGH8_REX);
  reject("load", r(0, 64), m(0, -1, 1, 0, 32), N, 2, all, NT_X64_WIDTH);
  reject("lea", r(0, 32), m(0, -1, 1, 0, 32), N, 2, all, NT_X64_WIDTH);
  fs.segment = (NtX64Segment)9;
  reject("load", r(8, 16), fs, N, 2, all, NT_X64_SEGMENT);
  golden("add", r(0, 64), r(1, 64), N, 2, "4801c8");
  golden("add", r(0, 64), u(2), N, 2, "4883c002");
  golden("add", r(0, 64), s(2), N, 2, "4883c002");
  golden("add", r(0, 32), u(127), N, 2, "83c07f");
  golden("add", r(0, 32), u(128), N, 2, "0580000000");
  golden("sub", r(0, 8), s(-1), N, 2, "2cff");
  golden("add", r(8, 64), s(127), N, 2, "4983c07f");
  golden("sub", r(0, 64), r(2, 64), N, 2, "4829d0");
  golden("and", r(0, 32), u(255), N, 2, "25ff000000");
  golden("or", r(9, 64), r(10, 64), N, 2, "4d09d1");
  golden("xor", r(0, 64), r(0, 64), N, 2, "4831c0");
  golden("cmp", r(0, 64), r(1, 64), N, 2, "4839c8");
  golden("adc", r(0, 64), r(1, 64), N, 2, "4811c8");
  golden("sbb", r(0, 64), r(1, 64), N, 2, "4819c8");
  golden("inc", r(0, 64), N, N, 1, "48ffc0");
  golden("dec", r(0, 64), N, N, 1, "48ffc8");
  golden("test", r(0, 64), r(0, 64), N, 2, "4885c0");
  golden("neg", r(0, 64), N, N, 1, "48f7d8");
  golden("not", r(8, 64), N, N, 1, "49f7d0");
  golden("shl", r(0, 64), u(3), N, 2, "48c1e003");
  golden("shr", r(8, 64), u(1), N, 2, "49d1e8");
  golden("sar", r(0, 64), u(1), N, 2, "48d1f8");
  golden("rol", r(0, 64), u(1), N, 2, "48d1c0");
  golden("ror", r(0, 64), u(1), N, 2, "48d1c8");
  golden("imul", r(0, 64), r(1, 64), N, 2, "480fafc1");
  golden("imul", r(0, 16), r(1, 16), s(-1), 3, "666bc1ff");
  golden("imul", r(0, 64), m(13, -1, 1, 8, 64), u(128), 3, "4969450880000000");
  golden("imul", r(9, 64), m(12, 13, 4, 128, 64), s(-129), 3,
         "4f698cac800000007fffffff");
  reject("imul", r(0, 8), r(1, 8), u(1), 3, all, NT_X64_WIDTH);
  reject("imul", r(0, 64), r(1, 64), u(UINT64_C(2147483648)), 3, all,
         NT_X64_RANGE);
  golden("add", m(4, -1, 1, 8, 8), r(1, 8), N, 2, "004c2408");
  golden("adc", r(9, 16), m(12, -1, 1, 0, 16), N, 2, "6645130c24");
  golden("sbb", m(13, -1, 1, 0, 64), s(-1), N, 2, "49835d00ff");
  golden("test", m(0, -1, 1, 0, 16), u(65535), N, 2, "66f700ffff");
  golden("neg", m(12, -1, 1, 0, 8), N, N, 1, "41f61c24");
  golden("inc", m(12, -1, 1, 0, 8), N, N, 1, "41fe0424");
  golden("dec", m(12, -1, 1, 0, 16), N, N, 1, "6641ff0c24");
  golden("sar", m(13, -1, 1, 0, 32), r(1, 8), N, 2, "41d37d00");
  golden("mul", r(10, 64), N, N, 1, "49f7e2");
  golden("div", r(10, 64), N, N, 1, "49f7f2");
  golden("idiv", r(10, 64), N, N, 1, "49f7fa");
  golden("push", r(0, 64), N, N, 1, "50");
  golden("push", r(8, 64), N, N, 1, "4150");
  golden("pop", r(8, 64), N, N, 1, "4158");
  golden("push", m(5, -1, 1, -8, 64), N, N, 1, "ff75f8");
  golden("push", s(-1), N, N, 1, "6aff");
  golden("push", u(128), N, N, 1, "6880000000");
  golden("pop", m(5, -1, 1, 0, 64), N, N, 1, "8f4500");
  golden("call", rel(10), N, N, 1, "e80a000000");
  golden("call", m(-2, -1, 1, 0, 64), N, N, 1, "ff1500000000");
  golden("call", r(15, 64), N, N, 1, "41ffd7");
  golden("jmp", m(12, -1, 1, 0, 64), N, N, 1, "41ff2424");
  golden("jmp", rel(-5), N, N, 1, "e9fbffffff");
  golden("je", rel(10), N, N, 1, "0f840a000000");
  golden("jne", rel(-6), N, N, 1, "0f85faffffff");
  static const char *suffixes[] = {"o", "no", "b", "ae", "e", "ne", "be", "a",
                                   "s", "ns", "p", "np", "l", "ge", "le", "g"};
  for (unsigned cc = 0; cc < 16; cc++) {
    char mnemonic[16], hex[40];
    snprintf(mnemonic, sizeof(mnemonic), "j%s", suffixes[cc]);
    snprintf(hex, sizeof(hex), "0f%02x04000000", 0x80 + cc);
    golden(mnemonic, rel(4), N, N, 1, hex);
    snprintf(mnemonic, sizeof(mnemonic), "set%s", suffixes[cc]);
    snprintf(hex, sizeof(hex), "0f%02xc0", 0x90 + cc);
    golden(mnemonic, r(0, 8), N, N, 1, hex);
    snprintf(mnemonic, sizeof(mnemonic), "cmov%s", suffixes[cc]);
    snprintf(hex, sizeof(hex), "480f%02xc1", 0x40 + cc);
    golden(mnemonic, r(0, 64), r(1, 64), N, 2, hex);
    reject(mnemonic, r(0, 8), r(1, 8), N, 2, all, NT_X64_WIDTH);
  }
  golden("read_cr", r(0, 64), cr(3), N, 2, "0f20d8");
  golden("write_cr", cr(3), r(0, 64), N, 2, "0f22d8");
  golden("read_cr", r(9, 64), cr(8), N, 2, "450f20c1");
  golden("write_cr", cr(8), r(9, 64), N, 2, "450f22c1");
  golden("movzx", r(0, 64), r(1, 8), N, 2, "480fb6c1");
  golden("movsx", r(9, 64), m(12, -1, 1, 0, 8), N, 2, "4d0fbe0c24");
  golden("movsxd", r(0, 64), r(1, 32), N, 2, "4863c1");
  golden("sete", r(0, 8), N, N, 1, "0f94c0");
  golden("setne", m(0, -1, 1, 0, 8), N, N, 1, "0f9500");
  golden("cmove", r(0, 64), r(1, 64), N, 2, "480f44c1");
  golden("cmovg", r(15, 16), m(13, -1, 1, 0, 16), N, 2, "66450f4f7d00");
  golden("setle", m(12, 9, 2, -128, 8), N, N, 1, "430f9e444c80");
  golden("bswap", r(0, 64), N, N, 1, "480fc8");
  golden("bswap", r(9, 32), N, N, 1, "410fc9");
  golden("bt", r(0, 64), r(1, 64), N, 2, "480fa3c8");
  golden("bts", r(0, 64), u(3), N, 2, "480fbae803");
  golden("btr", r(0, 64), u(3), N, 2, "480fbaf003");
  golden("btc", r(0, 64), u(3), N, 2, "480fbaf803");
  golden("bsf", r(0, 64), r(1, 64), N, 2, "480fbcc1");
  golden("bsr", r(0, 32), m(8, -1, 1, 0, 32), N, 2, "410fbd00");
  golden("xchg", r(0, 64), r(1, 64), N, 2, "4887c8");
  golden("xchg", r(0, 64), m(4, -1, 1, 0, 64), N, 2, "48870424");
  golden("lgdt", m(0, -1, 1, 0, 80), N, N, 1, "0f0110");
  golden("lidt", m(0, -1, 1, 0, 80), N, N, 1, "0f0118");
  golden("sgdt", m(0, -1, 1, 0, 80), N, N, 1, "0f0100");
  golden("sidt", m(0, -1, 1, 0, 80), N, N, 1, "0f0108");
  golden("ltr", r(0, 16), N, N, 1, "0f00d8");
  golden("invlpg", m(0, -1, 1, 0, 0), N, N, 1, "0f0138");
  golden("in", r(0, 8), u(0x60), N, 2, "e460");
  golden("in", r(0, 32), r(2, 16), N, 2, "ed");
  golden("out", u(0x60), r(0, 8), N, 2, "e660");
  golden("out", r(2, 16), r(0, 32), N, 2, "ef");
  golden("in", r(0, 16), r(2, 16), N, 2, "66ed");
  golden("out", r(2, 16), r(0, 16), N, 2, "66ef");
  reject("add", r(0, 64), u(UINT32_MAX), N, 2, all, NT_X64_RANGE);
  reject("div", r(0, 7), N, N, 1, all, NT_X64_WIDTH);
  reject("shl", r(0, 64), u(256), N, 2, all, NT_X64_RANGE);
  reject("read_cr", r(0, 64), cr(1), N, 2, all, NT_X64_REGISTER);
  reject("movzx", r(0, 16), r(1, 16), N, 2, all, NT_X64_WIDTH);
  reject("sete", r(0, 64), N, N, 1, all, NT_X64_WIDTH);
  reject("cmove", r(0, 8), r(1, 8), N, 2, all, NT_X64_WIDTH);
  reject("lgdt", m(0, -1, 1, 0, 80), N, N, 1, (NtX64Context){31, 3},
         NT_X64_PRIVILEGE);
  reject("in", r(1, 8), u(3), N, 2, all, NT_X64_REGISTER);
  printf("X64_TESTS cases=%u failures=%u\n", cases, failures);
  return failures ? 1 : 0;
}
