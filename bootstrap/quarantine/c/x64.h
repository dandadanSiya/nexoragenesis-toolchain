#ifndef GENESIS_X64_H
#define GENESIS_X64_H
#include <stddef.h>
#include <stdint.h>

/* This is an encoder, not a semantic verifier. Typed calls, stack state,
 * effects, relocations and control-flow proofs belong to its caller. */
enum {
  NT_X64_LM = 1u,
  NT_X64_MSR = 2u,
  NT_X64_SYSCALL = 4u,
  NT_X64_SSE2 = 8u,
  NT_X64_TSC = 16u,
  NT_X64_ALL_FEATURES = 31u
};
typedef enum {
  NT_X64_NONE,
  NT_X64_REG,
  NT_X64_IMM,
  NT_X64_MEM,
  NT_X64_REL,
  NT_X64_CR
} NtX64Kind;
typedef enum { NT_X64_SEG_NONE, NT_X64_FS, NT_X64_GS } NtX64Segment;
typedef struct {
  NtX64Kind kind;
  unsigned width; /* bits. Memory descriptor=80, invlpg address-only=0. */
  unsigned reg;   /* REG=0..15; CR=0,2,3,4,8. high8 selects AH..BH via 0..3. */
  unsigned high8;
  uint64_t imm; /* Signed operands contain their full uint64 two's-complement
                   bits. */
  unsigned is_signed;
  int base;       /* MEM: -1=no base, -2=RIP, 0..15=64-bit GPR. */
  int index;      /* MEM: -1=none, 0..15 (rsp=4 cannot be index; r12 can). */
  unsigned scale; /* 1,2,4,8 (use 1 for no index). */
  int64_t disp;
  NtX64Segment segment;
} NtX64Operand;
typedef struct {
  uint32_t features;
  unsigned cpl;
} NtX64Context;
typedef struct {
  uint8_t bytes[15];
  size_t length;
  /* Explicit displacement location for relocatable RIP operands. A trailing
   * immediate may follow it; consumers must never assume length - 4. */
  size_t displacement_offset;
  unsigned displacement_size;
} NtX64Instruction;
typedef enum {
  NT_X64_OK = 0,
  NT_X64_UNKNOWN = 401,
  NT_X64_COUNT = 402,
  NT_X64_WIDTH = 403,
  NT_X64_FEATURE = 404,
  NT_X64_PRIVILEGE = 405,
  NT_X64_REGISTER = 406,
  NT_X64_RANGE = 407,
  NT_X64_HIGH8_REX = 408,
  NT_X64_FORM = 409,
  NT_X64_SEGMENT = 410,
  NT_X64_ADDRESS = 411,
  NT_X64_ARGUMENT = 412
} NtX64Error;

/* On failure *out is byte-for-byte unchanged. No allocation or external tools.
 * REL uses imm/is_signed, always rel32 measured from the next instruction.
 * Immediate width is optional (0); a nonzero width must be 8/16/32/64.
 * Contextual privilege rules are conservative: rdtsc/popflags/descriptors/I/O/
 * iretq require CPL0 because this API has no CR4/IOPL/runtime-state proof.
 * call accepts REL or indirect REG/MEM64; return/ret accepts no operands.
 * Typed argument/value lowering is the caller's responsibility. */
NtX64Error nt_x64_encode(const char *mnemonic, const NtX64Operand *operands,
                         size_t count, NtX64Context context,
                         NtX64Instruction *out);
const char *nt_x64_error_string(NtX64Error error);
/* Architectural footprint of the exact validated operand form. Register mask
 * uses GPR bits0..15; bit17=RFLAGS,18=GSBASE,19=FSBASE. Flags masks use x86
 * RFLAGS bit positions. Explicit memory and implicit stack accesses are
 * separate so the frontend can retain address-space provenance. Undefined
 * outputs must never satisfy a subsequent proof of a defined flag/value. */
enum {
  NT_X64_FLAG_CF = 1,
  NT_X64_FLAG_PF = 4,
  NT_X64_FLAG_AF = 16,
  NT_X64_FLAG_ZF = 64,
  NT_X64_FLAG_SF = 128,
  NT_X64_FLAG_OF = 2048,
  NT_X64_FLAGS_ARITH = 2261
};
enum {
  NT_X64_EFFECT_PRIVILEGED = 1u << 0,
  NT_X64_EFFECT_CHANGES_FLAGS = 1u << 1,
  NT_X64_EFFECT_MMIO = 1u << 2,
  NT_X64_EFFECT_IO_PORT = 1u << 3,
  NT_X64_EFFECT_MSR = 1u << 4,
  NT_X64_EFFECT_CONTROL = 1u << 5,
  NT_X64_EFFECT_INTERRUPT_STATE = 1u << 6,
  NT_X64_EFFECT_NO_RETURN = 1u << 7,
  NT_X64_EFFECT_MEMORY_ORDERING = 1u << 8,
  NT_X64_EFFECT_READS_CLOCK = 1u << 9,
  NT_X64_EFFECT_SUSPENDS = 1u << 10
};
typedef struct {
  uint64_t registers_read, registers_written;
  uint32_t flags_read, flags_written, flags_undefined, effects;
  uint32_t required_features;
  unsigned max_cpl;
  unsigned memory_read, memory_write, stack_read, stack_write;
  unsigned terminal, conditional, may_trap, destination_requires_nonzero;
} NtX64Effects;
/* Validates exactly as nt_x64_encode. On failure *out remains unchanged. */
NtX64Error nt_x64_effects(const char *mnemonic, const NtX64Operand *operands,
                          size_t count, NtX64Context context,
                          NtX64Effects *out);
#endif
