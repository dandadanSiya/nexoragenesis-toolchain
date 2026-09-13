#ifndef GENESIS_NTASM_CODEGEN_H
#define GENESIS_NTASM_CODEGEN_H
#include "frontend.h"
#include <stddef.h>
#include <stdint.h>

/* Direct verified-IR to x86-64 lowering. No assembler, compiler, linker, or
 * subprocess is invoked. A named parameter denotes an immutable snapshot of
 * its bound register at function entry when its direction is IN; an explicit
 * register expression reads the current machine register. This distinction
 * remains true after a body instruction overwrites that register. Calls use
 * nx64-abi-v0 register binding and reserve the 32-byte x64 shadow area.
 * stack_aligned(N) denotes the function body state after the generated
 * prologue; a function carrying that requirement cannot use the raw-entry leaf
 * shortcut because raw x64 entry RSP is 8 mod 16.
 *
 * Explicit register leaves in one expression observe an immutable snapshot at
 * expression entry. Calls execute left-to-right. Evaluator temporaries and
 * argument bindings are transparent; genuine callee clobbers, including its
 * RAX result, update the live state retained after the expression. Thus both
 * f()+rax and rax+f() read the entry RAX but the next statement observes f's
 * real clobbers. let/if preserve machine registers and flags except those
 * real calls change. Direct machine instructions act on live hardware.
 *
 * nx64-abi-v0 supports at most14 distinct explicit GPR bindings, excluding
 * rsp/rbp. Omitted bindings1..4 use rcx,rdx,r8,r9; later ones require @.
 * No implicit stack arguments are introduced by this bootstrap profile.
 * efi-x64-v0 separately supports up to64 scalar/pointer inputs: fixed first
 * four GPR positions followed by8-byte stack slots after32-byte shadow space.
 * NX64 integer OUT parameters consume no call argument; INOUT consumes one.
 * Their names read the live bound register at expression entry, and output
 * registers survive the epilogue independently of the callee clobber set.
 * RAX/RSP/RBP outputs are reserved; pointer/bool output proof is still closed.
 *
 * generated_stub=1 is a closed vector6 UD2 witness profile, never ordinary
 * entry code. It adjusts only saved RIP in the 64-bit hardware no-error frame
 * (RIP,CS,RFLAGS,RSP,SS), preserves RAX, and exits via IRETQ. Every tag, ABI,
 * effect, footprint and operand is revalidated before its18 raw bytes are
 * emitted. It is not a general exception handler or a CPL transition proof. */
typedef struct {
  uint8_t *bytes;
  size_t size, capacity;
} NtCodeBuffer;
enum {
  NT_CODE_SECTION_TEXT = 1,
  NT_CODE_SECTION_RDATA = 2,
  NT_CODE_SECTION_DATA = 3
};
enum { NT_CODE_RELOC_REL32 = 1, NT_CODE_RELOC_DIR64 = 2 };
typedef struct {
  char *name, *module;
  uint32_t section, flags;
  size_t offset, size;
  NtFId function_id, variable_id;
} NtCodeSymbol;
typedef struct {
  uint32_t source_section, kind;
  size_t offset;
  NtFId target_function, target_variable;
  int64_t addend;
} NtCodeReloc;
typedef struct {
  uint8_t *bytes;
  size_t size, capacity, entry;
  NtCodeBuffer rdata, data;
  NtCodeSymbol *symbols;
  size_t symbol_count, symbol_capacity;
  NtCodeReloc *relocations;
  size_t relocation_count, relocation_capacity;
} NtCodeImage;
/* REL32 fields are patched for the canonical relative section layout and also
 * retain symbolic metadata. DIR64 initializer slots deliberately contain zero:
 * the PE/NXO materializer must resolve their typed targets at the chosen image
 * base and emit base-relocation sites before such pointers may be dereferenced.
 * NTF_X_ADDRESS is a symbolic constant and must never be folded as integer0. */
typedef struct {
  unsigned code;
  NtFSpan span;
  char message[192];
} NtCodeDiagnostic;
enum {
  NTCG_E_UNVERIFIED = 700,
  NTCG_E_ENTRY = 701,
  NTCG_E_UNSUPPORTED = 702,
  NTCG_E_ENCODE = 703,
  NTCG_E_RANGE = 704,
  NTCG_E_OOM = 705,
  NTCG_E_INTERNAL = 706
};

/* Failure leaves image empty and returns one stable diagnostic. */
int nt_codegen_x64(const NtFProgram *program, NtCodeImage *image,
                   NtCodeDiagnostic *diagnostic);
/* Relocatable mode permits declared unresolved imports and no executable
 * entry (image.entry=SIZE_MAX). Undefined imports have section0, flags8. */
int nt_codegen_x64_object(const NtFProgram *program, NtCodeImage *image,
                          NtCodeDiagnostic *diagnostic);
void nt_code_image_free(NtCodeImage *image);
#endif
