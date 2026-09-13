#ifndef GENESIS_NTASM_FRONTEND_H
#define GENESIS_NTASM_FRONTEND_H
#include <stddef.h>
#include <stdint.h>

/* NTASM v0 C bootstrap frontend, API 1. All IDs are one-based; zero is null.
 * Inputs are borrowed only during compile. All strings/nodes in the result are
 * owned by the result, including when compilation fails. Never emit a program
 * unless verified is true. Limits and unsupported constructs fail explicitly.
 * This API performs no I/O and invokes no external producer.
 *
 * Bootstrap grammar delta: this C frontend accepts if <bool-expression>
 * so ordinary typed control flow can be lowered before the registered
 * predicate catalogue is complete. The astra-r1 candidate's registered
 * predicate form remains accepted where its expression is representable.
 * It also admits `expand macro(...)` as an expression primary and as a type
 * form because astra-r1 declares ast.expr/ast.type macro results but provides
 * no consumer production for them. Expansion clones typed AST directly and
 * has no internal terminator.
 * This bootstrap extension is not a normative specification change. */
typedef uint32_t NtFId;
typedef struct {
  const char *name, *text;
  size_t length;
} NtFInput;
typedef struct {
  uint32_t source, line, column;
  size_t start, end;
} NtFSpan;
typedef struct {
  unsigned code;
  NtFSpan span;
  char message[192];
} NtFDiagnostic;
enum {
  NTF_E_LEX = 100,
  NTF_E_SYNTAX = 200,
  NTF_E_LIMIT = 201,
  NTF_E_DUPLICATE = 300,
  NTF_E_NAME = 301,
  NTF_E_IMPORT = 302,
  NTF_E_TARGET = 303,
  NTF_E_TYPE = 400,
  NTF_E_LITERAL = 401,
  NTF_E_REGISTER = 402,
  NTF_E_ABI = 403,
  NTF_E_FLOW = 500,
  NTF_E_EFFECT = 501,
  NTF_E_CLOBBER = 502,
  NTF_E_CONTRACT = 503,
  NTF_E_MEMORY = 504,
  NTF_E_UNSUPPORTED = 600,
  NTF_E_OOM = 900
};
enum {
  NTF_COMPTIME_FUEL_MAX = 1048576,
  NTF_COMPTIME_CALL_DEPTH_MAX = 256,
  NTF_COMPTIME_ARENA_BYTES_MAX = 16777216,
  NTF_COMPTIME_AST_NODES_MAX = 1048576,
  NTF_COMPTIME_EXPANSION_DEPTH_MAX = 128,
  NTF_COMPTIME_CONSTANT_BYTES_MAX = 16777216
};
typedef enum {
  NTF_T_INVALID,
  NTF_T_U8,
  NTF_T_U16,
  NTF_T_U32,
  NTF_T_U64,
  NTF_T_I8,
  NTF_T_I16,
  NTF_T_I32,
  NTF_T_I64,
  NTF_T_BOOL,
  NTF_T_NEVER,
  NTF_T_PTR,
  NTF_T_ARRAY
} NtFTypeKind;
typedef enum {
  NTF_SPACE_NONE,
  NTF_SPACE_USER,
  NTF_SPACE_KERNEL,
  NTF_SPACE_PHYSICAL,
  NTF_SPACE_MMIO,
  NTF_SPACE_DEVICE,
  NTF_SPACE_FIRMWARE
} NtFSpace;
typedef struct {
  NtFTypeKind kind;
  NtFSpace space;
  NtFId element;
  uint64_t count;
  unsigned bits;
} NtFType;
/* GPR family: rax=0 rcx=1 rdx=2 rbx=3 rsp=4 rbp=5 rsi=6 rdi=7,
 * r8..r15=8..15; rip=16, rflags=17, gsbase=18, fsbase=19.
 * family=-1 means absent. Subregister aliases share the same family. */
typedef struct {
  int family;
  unsigned bits;
  int high8;
} NtFRegister;
typedef enum {
  NTF_IN,
  NTF_OUT,
  NTF_INOUT,
  NTF_LOCAL,
  NTF_CONST,
  NTF_DATA
} NtFDirection;
typedef struct {
  const char *name;
  NtFSpan span;
  NtFId type, function, module, initializer;
  NtFDirection direction;
  NtFRegister reg;
  int exported;
  const char *section;
  NtFId scope;
} NtFVariable;
typedef enum {
  NTF_X_LITERAL,
  NTF_X_BOOL,
  NTF_X_STRING,
  NTF_X_NAME,
  NTF_X_REGISTER,
  NTF_X_UNARY,
  NTF_X_BINARY,
  NTF_X_CALL,
  NTF_X_MEMORY,
  NTF_X_PREDICATE,
  NTF_X_ADDRESS
} NtFExprKind;
typedef enum {
  NTF_OP_NONE,
  NTF_OP_ADD,
  NTF_OP_SUB,
  NTF_OP_MUL,
  NTF_OP_DIV,
  NTF_OP_MOD,
  NTF_OP_AND,
  NTF_OP_OR,
  NTF_OP_XOR,
  NTF_OP_SHL,
  NTF_OP_SHR,
  NTF_OP_EQ,
  NTF_OP_NE,
  NTF_OP_LT,
  NTF_OP_LE,
  NTF_OP_GT,
  NTF_OP_GE,
  NTF_OP_NEG,
  NTF_OP_NOT,
  NTF_OP_POS
} NtFOp;
typedef struct {
  NtFRegister base, index;
  unsigned scale, segment;
  int64_t displacement;
  const char *symbol;
  NtFId provenance;
} NtFMemory;
typedef struct {
  NtFExprKind kind;
  NtFSpan span;
  NtFId type, left, right, first_arg, next, resolved;
  const char *name;
  uint64_t value;
  NtFOp op;
  NtFRegister reg;
  NtFMemory memory;
  int constant, negative;
  size_t string_length;
  NtFSpan origin, invocation;
} NtFExpr;
typedef enum {
  NTF_S_BLOCK,
  NTF_S_LET,
  NTF_S_RETURN,
  NTF_S_CALL,
  NTF_S_IF,
  NTF_S_INSTRUCTION,
  NTF_S_LABEL
} NtFStmtKind;
typedef struct {
  NtFStmtKind kind;
  NtFSpan span;
  NtFId next, first, condition, then_branch, else_branch, expression, variable;
  const char *name;
  int explicit_args, terminal;
  NtFSpan origin, invocation;
  uint32_t flags_read, flags_written, flags_undefined;
} NtFStmt;
enum {
  NTF_F_PRIVILEGED = 1u << 0,
  NTF_F_CHANGES_FLAGS = 1u << 1,
  NTF_F_MMIO = 1u << 2,
  NTF_F_IO_PORT = 1u << 3,
  NTF_F_MSR = 1u << 4,
  NTF_F_CONTROL = 1u << 5,
  NTF_F_INTERRUPT_STATE = 1u << 6,
  NTF_F_NO_RETURN = 1u << 7,
  NTF_F_MEMORY_ORDERING = 1u << 8,
  NTF_F_READS_CLOCK = 1u << 9,
  NTF_F_SUSPENDS = 1u << 10
};
typedef struct {
  uint32_t flags, reads, writes;
} NtFEffects;
typedef struct {
  const char *name, *abi, *section;
  NtFSpan span;
  NtFId module, return_type, first_param, body,
    requires,
  ensures;
  uint32_t param_count;
  NtFEffects effects, inferred_effects;
  uint64_t clobbers, write_footprint, inferred_clobbers;
  int exported, imported;
  const char *import_name;
  NtFId resolved;
  uint32_t generated_stub, stub_vector;
  uint32_t high_level_expressions;
} NtFFunction;
/* stack_aligned(N) describes RSP at the first source statement, after any
 * compiler-generated prologue. A backend must materialize that prologue even
 * for an otherwise foldable leaf when the predicate is present. It does not
 * claim that raw x86-64 call-entry RSP is 16-byte aligned. */
typedef struct {
  const char *name, *target, *default_abi;
  NtFSpan span;
  uint32_t features;
} NtFModule;
typedef struct {
  NtFModule *modules;
  size_t module_count;
  NtFFunction *functions;
  size_t function_count;
  NtFVariable *variables;
  size_t variable_count;
  NtFType *types;
  size_t type_count;
  NtFExpr *expressions;
  size_t expression_count;
  NtFStmt *statements;
  size_t statement_count;
  NtFDiagnostic *diagnostics;
  size_t diagnostic_count;
  int verified;
  void *_private;
} NtFProgram;
int nt_frontend_compile(const NtFInput *inputs, size_t count, NtFProgram *out);
/* Separate-object mode keeps a declared external import unresolved after its
 * signature, ABI and call sites verify. It never fabricates a function body. */
int nt_frontend_compile_object(const NtFInput *inputs, size_t count,
                               NtFProgram *out);
void nt_frontend_free(NtFProgram *program);
NtFRegister nt_frontend_register(const char *name);
const char *nt_frontend_type_name(NtFTypeKind kind);
#endif
