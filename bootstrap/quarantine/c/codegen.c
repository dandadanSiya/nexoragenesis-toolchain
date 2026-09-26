#include "codegen.h"
#include "conformance.h"
#include "x64.h"
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FN(p, id) ((p)->functions[(id) - 1])
#define VA(p, id) ((p)->variables[(id) - 1])
#define TY(p, id) ((p)->types[(id) - 1])
#define EX(p, id) ((p)->expressions[(id) - 1])
#define ST(p, id) ((p)->statements[(id) - 1])
#define EXPRESSION_STATE_BYTES 280u
static const unsigned saved_registers[] = {1,  2,  3,  6,  7,  8, 9,
                                           10, 11, 12, 13, 14, 15};

typedef struct {
  size_t displacement_site;
  NtFId target;
} Patch;
typedef struct {
  size_t displacement_site;
  NtFId variable;
  int64_t addend;
} DataPatch;
typedef struct {
  size_t displacement_site;
  NtFId statement;
  NtFSpan span;
} LabelPatch;
typedef struct {
  const NtFProgram *program;
  NtCodeImage *image;
  NtCodeDiagnostic *diagnostic;
  size_t *function_offsets;
  int32_t *variable_offsets;
  size_t *data_offsets;
  unsigned char *data_sections;
  Patch *patches;
  size_t patch_count, patch_capacity;
  DataPatch *data_patches;
  size_t data_patch_count, data_patch_capacity;
  LabelPatch *label_patches;
  size_t label_patch_count, label_patch_capacity;
  size_t *statement_offsets;
  NtFId function;
  unsigned spill_depth, call_depth;
  unsigned call_argument_bytes;
  int32_t spill_base, call_base;
  unsigned expression_level;
  int32_t snapshot_base;
  int preserve_flags;
  int object_mode;
  int failed;
} Generator;

static int fail(Generator *g, unsigned code, NtFSpan span, const char *format,
                ...) {
  if (!g->failed) {
    g->diagnostic->code = code;
    g->diagnostic->span = span;
    va_list ap;
    va_start(ap, format);
    vsnprintf(g->diagnostic->message, sizeof(g->diagnostic->message), format,
              ap);
    va_end(ap);
  }
  g->failed = 1;
  return 0;
}
static int reserve(Generator *g, size_t extra) {
  if (extra > SIZE_MAX - g->image->size)
    return fail(g, NTCG_E_RANGE, (NtFSpan){0}, "machine code size overflow");
  size_t needed = g->image->size + extra;
  if (needed > g->image->capacity) {
    size_t capacity = g->image->capacity ? g->image->capacity * 2 : 256;
    while (capacity < needed) {
      if (capacity > SIZE_MAX / 2)
        return fail(g, NTCG_E_RANGE, (NtFSpan){0},
                    "machine code capacity overflow");
      capacity *= 2;
    }
    uint8_t *bytes = (uint8_t *)realloc(g->image->bytes, capacity);
    if (!bytes)
      return fail(g, NTCG_E_OOM, (NtFSpan){0},
                  "machine code allocation failed");
    g->image->bytes = bytes;
    g->image->capacity = capacity;
  }
  return 1;
}
static int raw(Generator *g, const void *data, size_t size) {
  if (!reserve(g, size))
    return 0;
  memcpy(g->image->bytes + g->image->size, data, size);
  g->image->size += size;
  return 1;
}
static int buffer_append(Generator *g, NtCodeBuffer *buffer, const void *data,
                         size_t size, NtFSpan span) {
  if (size > SIZE_MAX - buffer->size)
    return fail(g, NTCG_E_RANGE, span, "data section size overflow");
  size_t needed = buffer->size + size;
  if (needed > buffer->capacity) {
    size_t capacity = buffer->capacity ? buffer->capacity * 2 : 64;
    while (capacity < needed) {
      if (capacity > SIZE_MAX / 2)
        return fail(g, NTCG_E_RANGE, span, "data capacity overflow");
      capacity *= 2;
    }
    uint8_t *bytes = (uint8_t *)realloc(buffer->bytes, capacity);
    if (!bytes)
      return fail(g, NTCG_E_OOM, span, "data allocation failed");
    buffer->bytes = bytes;
    buffer->capacity = capacity;
  }
  memcpy(buffer->bytes + buffer->size, data, size);
  buffer->size += size;
  return 1;
}
static char *metadata_text(const char *text) {
  size_t length = strlen(text);
  char *copy = (char *)malloc(length + 1);
  if (copy)
    memcpy(copy, text, length + 1);
  return copy;
}
static int add_symbol(Generator *g, const char *name, const char *module,
                      uint32_t section, size_t offset, size_t size,
                      uint32_t flags, NtFId function, NtFId variable,
                      NtFSpan span) {
  NtCodeImage *image = g->image;
  if (image->symbol_count == image->symbol_capacity) {
    size_t capacity = image->symbol_capacity ? image->symbol_capacity * 2 : 16;
    NtCodeSymbol *next =
        (NtCodeSymbol *)realloc(image->symbols, capacity * sizeof(*next));
    if (!next)
      return fail(g, NTCG_E_OOM, span, "symbol allocation failed");
    image->symbols = next;
    image->symbol_capacity = capacity;
  }
  char *owned_name = metadata_text(name), *owned_module = metadata_text(module);
  if (!owned_name || !owned_module) {
    free(owned_name);
    free(owned_module);
    return fail(g, NTCG_E_OOM, span, "symbol name allocation failed");
  }
  image->symbols[image->symbol_count++] =
      (NtCodeSymbol){owned_name, owned_module, section,  flags,
                     offset,     size,         function, variable};
  return 1;
}
static int add_relocation(Generator *g, uint32_t section, size_t offset,
                          uint32_t kind, NtFId function, NtFId variable,
                          int64_t addend, NtFSpan span) {
  NtCodeImage *image = g->image;
  if (image->relocation_count == image->relocation_capacity) {
    size_t capacity =
        image->relocation_capacity ? image->relocation_capacity * 2 : 16;
    NtCodeReloc *next =
        (NtCodeReloc *)realloc(image->relocations, capacity * sizeof(*next));
    if (!next)
      return fail(g, NTCG_E_OOM, span, "relocation metadata allocation failed");
    image->relocations = next;
    image->relocation_capacity = capacity;
  }
  image->relocations[image->relocation_count++] =
      (NtCodeReloc){section, kind, offset, function, variable, addend};
  return 1;
}
static NtX64Operand reg64(unsigned reg) {
  NtX64Operand o = {0};
  o.kind = NT_X64_REG;
  o.width = 64;
  o.reg = reg;
  return o;
}
static NtX64Operand reg8(unsigned reg) {
  NtX64Operand o = {0};
  o.kind = NT_X64_REG;
  o.width = 8;
  o.reg = reg;
  return o;
}
static NtX64Operand reg_width(unsigned reg, unsigned width) {
  NtX64Operand o = reg64(reg);
  o.width = width;
  return o;
}
static NtX64Operand imm(uint64_t value, int signed_value) {
  NtX64Operand o = {0};
  o.kind = NT_X64_IMM;
  o.imm = value;
  o.is_signed = (unsigned)signed_value;
  return o;
}
static NtX64Operand mem_rbp(int32_t displacement) {
  NtX64Operand o = {0};
  o.kind = NT_X64_MEM;
  o.width = 64;
  o.base = 5;
  o.index = -1;
  o.scale = 1;
  o.disp = displacement;
  return o;
}
static NtX64Operand relative(int32_t displacement) {
  NtX64Operand o = {0};
  o.kind = NT_X64_REL;
  o.imm = (uint64_t)(int64_t)displacement;
  o.is_signed = 1;
  return o;
}
static int instruction(Generator *g, NtFSpan span, const char *name,
                       const NtX64Operand *operands, size_t count) {
  NtX64Instruction encoded;
  NtX64Context context = {NT_X64_ALL_FEATURES, 0};
  NtX64Error error = nt_x64_encode(name, operands, count, context, &encoded);
  if (error)
    return fail(g, NTCG_E_ENCODE, span, "%s: %s", name,
                nt_x64_error_string(error));
  return raw(g, encoded.bytes, encoded.length);
}
static int ins0(Generator *g, NtFSpan s, const char *n) {
  return instruction(g, s, n, NULL, 0);
}
static int ins1(Generator *g, NtFSpan s, const char *n, NtX64Operand a) {
  return instruction(g, s, n, &a, 1);
}
static int ins2(Generator *g, NtFSpan s, const char *n, NtX64Operand a,
                NtX64Operand b) {
  NtX64Operand o[2] = {a, b};
  return instruction(g, s, n, o, 2);
}
static int add_patch(Generator *g, size_t site, NtFId target, NtFSpan span) {
  if (g->patch_count == g->patch_capacity) {
    size_t capacity = g->patch_capacity ? g->patch_capacity * 2 : 16;
    Patch *next = (Patch *)realloc(g->patches, capacity * sizeof(*next));
    if (!next)
      return fail(g, NTCG_E_OOM, span, "relocation allocation failed");
    g->patches = next;
    g->patch_capacity = capacity;
  }
  g->patches[g->patch_count++] = (Patch){site, target};
  return add_relocation(g, NT_CODE_SECTION_TEXT, site, NT_CODE_RELOC_REL32,
                        target, 0, 0, span);
}
static int add_data_patch(Generator *g, size_t site, NtFId variable,
                          int64_t addend, NtFSpan span) {
  if (g->data_patch_count == g->data_patch_capacity) {
    size_t capacity = g->data_patch_capacity ? g->data_patch_capacity * 2 : 16;
    DataPatch *next =
        (DataPatch *)realloc(g->data_patches, capacity * sizeof(*next));
    if (!next)
      return fail(g, NTCG_E_OOM, span, "data relocation allocation failed");
    g->data_patches = next;
    g->data_patch_capacity = capacity;
  }
  g->data_patches[g->data_patch_count++] = (DataPatch){site, variable, addend};
  return add_relocation(g, NT_CODE_SECTION_TEXT, site, NT_CODE_RELOC_REL32, 0,
                        variable, addend, span);
}
static int add_label_patch(Generator *g, size_t site, NtFId statement,
                           NtFSpan span) {
  if (g->label_patch_count == g->label_patch_capacity) {
    size_t capacity =
        g->label_patch_capacity ? g->label_patch_capacity * 2 : 16;
    LabelPatch *next =
        (LabelPatch *)realloc(g->label_patches, capacity * sizeof(*next));
    if (!next)
      return fail(g, NTCG_E_OOM, span, "label relocation allocation failed");
    g->label_patches = next;
    g->label_patch_capacity = capacity;
  }
  g->label_patches[g->label_patch_count++] =
      (LabelPatch){site, statement, span};
  return 1;
}
static void put32(uint8_t *p, int32_t value) {
  uint32_t v = (uint32_t)value;
  for (unsigned i = 0; i < 4; i++)
    p[i] = (uint8_t)(v >> (8 * i));
}
static int patch_local(Generator *g, size_t site, size_t target, NtFSpan span) {
  int64_t displacement = (int64_t)target - (int64_t)(site + 4);
  if (displacement < INT32_MIN || displacement > INT32_MAX)
    return fail(g, NTCG_E_RANGE, span, "branch displacement outside rel32");
  put32(g->image->bytes + site, (int32_t)displacement);
  return 1;
}
static uint64_t literal_bits(const NtFExpr *x) {
  return x->negative ? UINT64_C(0) - x->value : x->value;
}
static const NtFExpr *symbolic_address(const NtFProgram *p, NtFId id) {
  size_t steps = 0;
  while (id && id <= p->expression_count && ++steps <= p->variable_count + 1) {
    const NtFExpr *x = &EX(p, id);
    if (x->kind == NTF_X_ADDRESS)
      return x;
    if (x->kind != NTF_X_NAME || !x->resolved ||
        x->resolved > p->variable_count ||
        VA(p, x->resolved).direction != NTF_CONST)
      return NULL;
    id = VA(p, x->resolved).initializer;
  }
  return NULL;
}
static int evaluate_constant(const NtFProgram *p, NtFId id, uint64_t *value) {
  const NtFExpr *x = &EX(p, id);
  if (symbolic_address(p, id))
    return 0; /* symbolic constant, never integer zero */
  if (x->constant) {
    *value = literal_bits(x);
    return 1;
  }
  if (x->kind == NTF_X_LITERAL) {
    *value = literal_bits(x);
    return 1;
  }
  if (x->kind == NTF_X_NAME && x->resolved &&
      VA(p, x->resolved).direction == NTF_CONST)
    return evaluate_constant(p, VA(p, x->resolved).initializer, value);
  return 0;
}
static unsigned expression_depth(const NtFProgram *p, NtFId id) {
  if (!id)
    return 0;
  const NtFExpr *x = &EX(p, id);
  unsigned left = expression_depth(p, x->left),
           right = expression_depth(p, x->right), args = 0;
  for (NtFId a = x->first_arg; a; a = EX(p, a).next) {
    unsigned d = expression_depth(p, a);
    if (d > args)
      args = d;
  }
  unsigned nested = left > right ? left : right;
  if (args > nested)
    nested = args;
  return 1 + nested;
}
static unsigned call_depth(const NtFProgram *p, NtFId id) {
  if (!id)
    return 0;
  const NtFExpr *x = &EX(p, id);
  unsigned depth = x->kind == NTF_X_CALL ? 1 : 0;
  unsigned child = call_depth(p, x->left);
  if (call_depth(p, x->right) > child)
    child = call_depth(p, x->right);
  for (NtFId a = x->first_arg; a; a = EX(p, a).next) {
    unsigned d = call_depth(p, a);
    if (d > child)
      child = d;
  }
  return depth + child;
}
static int is_efi(const NtFFunction *function) {
  return function->abi && !strcmp(function->abi, "efi-x64-v0");
}
static void argument_requirements(const NtFProgram *p, NtFId id,
                                  unsigned *count, unsigned *stack) {
  if (!id)
    return;
  const NtFExpr *x = &EX(p, id);
  if (x->kind == NTF_X_CALL && x->resolved) {
    const NtFFunction *target = &FN(p, x->resolved);
    if (target->param_count > *count)
      *count = target->param_count;
    if (is_efi(target) && target->param_count > 4 &&
        target->param_count - 4 > *stack)
      *stack = target->param_count - 4;
  }
  argument_requirements(p, x->left, count, stack);
  argument_requirements(p, x->right, count, stack);
  for (NtFId a = x->first_arg; a; a = EX(p, a).next)
    argument_requirements(p, a, count, stack);
}
static void statement_requirements(const NtFProgram *p, NtFId id,
                                   unsigned *expr, unsigned *calls,
                                   unsigned *args, unsigned *stack) {
  for (; id; id = ST(p, id).next) {
    const NtFStmt *s = &ST(p, id);
    if (s->kind == NTF_S_BLOCK)
      statement_requirements(p, s->first, expr, calls, args, stack);
    argument_requirements(p, s->expression, args, stack);
    argument_requirements(p, s->condition, args, stack);
    unsigned d = expression_depth(p, s->expression);
    if (expression_depth(p, s->condition) > d)
      d = expression_depth(p, s->condition);
    unsigned c = call_depth(p, s->expression);
    if (call_depth(p, s->condition) > c)
      c = call_depth(p, s->condition);
    if (d > *expr)
      *expr = d;
    if (c > *calls)
      *calls = c;
    if (s->then_branch)
      statement_requirements(p, ST(p, s->then_branch).first, expr, calls, args,
                             stack);
    if (s->else_branch)
      statement_requirements(p, ST(p, s->else_branch).first, expr, calls, args,
                             stack);
  }
}
static int emit_expression(Generator *, NtFId);
static int high_level(Generator *g) {
  return FN(g->program, g->function).high_level_expressions != 0;
}
static int32_t expression_register_slot(Generator *g, unsigned reg,
                                        int restore) {
  return -(g->snapshot_base + (int32_t)(reg * 8u) + (restore ? 128 : 0));
}
static int32_t expression_flags_slot(Generator *g, int restore) {
  return -(g->snapshot_base + (restore ? 264 : 256));
}
static int capture_expression_flags(Generator *g, NtFSpan span, int restore) {
  if (high_level(g))
    return 1;
  return ins0(g, span, "pushfq") &&
         ins1(g, span, "pop", mem_rbp(expression_flags_slot(g, restore)));
}
static int restore_expression_flags(Generator *g, NtFSpan span) {
  if (high_level(g))
    return 1;
  return ins1(g, span, "push", mem_rbp(expression_flags_slot(g, 1))) &&
         ins0(g, span, "popfq");
}
static int restore_expression_rax(Generator *g, NtFSpan span) {
  if (high_level(g))
    return 1;
  return ins2(g, span, "load", reg64(0),
              mem_rbp(expression_register_slot(g, 0, 1)));
}
static int normalize_type(Generator *g, NtFId type, NtFSpan span) {
  NtFType *t = &TY(g->program, type);
  if (t->kind == NTF_T_BOOL)
    return ins2(g, span, "and", reg64(0), imm(1, 0));
  if (t->kind >= NTF_T_U8 && t->kind <= NTF_T_U64) {
    if (t->bits == 64)
      return 1;
    if (t->bits == 32)
      return ins2(g, span, "mov", reg_width(0, 32), reg_width(0, 32));
    return ins2(g, span, "movzx", reg_width(0, 32), reg_width(0, t->bits));
  }
  if (t->kind >= NTF_T_I8 && t->kind <= NTF_T_I64) {
    if (t->bits == 64)
      return 1;
    return ins2(g, span, t->bits == 32 ? "movsxd" : "movsx", reg64(0),
                reg_width(0, t->bits));
  }
  if (t->kind == NTF_T_PTR)
    return 1;
  return fail(g, NTCG_E_UNSUPPORTED, span,
              "type '%s' cannot be lowered as a scalar",
              nt_frontend_type_name(t->kind));
}
static int emit_call(Generator *g, const NtFExpr *x) {
  const NtFFunction *callee = &FN(g->program, x->resolved);
  NtFId target_id = callee->imported ? callee->resolved : x->resolved;
  if (!target_id && g->object_mode && callee->imported)
    target_id = x->resolved;
  if (!target_id)
    return fail(g, NTCG_E_INTERNAL, x->span, "import has no resolved target");
  int efi = is_efi(callee);
  if (callee->param_count > (efi ? 64u : 14u))
    return fail(g, NTCG_E_UNSUPPORTED, x->span,
                "parameter count exceeds its ABI profile");
  unsigned level = g->call_depth++;
  NtFId arg = x->first_arg;
  for (uint32_t i = 0; i < callee->param_count; i++) {
    if (VA(g->program, callee->first_param + i).direction == NTF_OUT)
      continue;
    if (!arg)
      return fail(g, NTCG_E_INTERNAL, x->span, "missing input argument");
    if (!emit_expression(g, arg))
      return 0;
    int32_t offset =
        -(g->call_base + (int32_t)(level * g->call_argument_bytes + i * 8));
    if (!ins2(g, x->span, "store", mem_rbp(offset), reg64(0)))
      return 0;
    arg = EX(g->program, arg).next;
  }
  static const unsigned defaults[] = {1, 2, 8, 9};
  if (efi)
    for (uint32_t i = 4; i < callee->param_count; i++) {
      int32_t offset =
          -(g->call_base + (int32_t)(level * g->call_argument_bytes + i * 8));
      NtX64Operand outgoing = mem_rbp((int32_t)(32 + (i - 4) * 8));
      outgoing.base = 4;
      if (!ins2(g, x->span, "load", reg64(0), mem_rbp(offset)) ||
          !ins2(g, x->span, "store", outgoing, reg64(0)))
        return 0;
    }
  for (uint32_t i = 0; i < callee->param_count; i++) {
    if (efi && i >= 4)
      continue;
    const NtFVariable *parameter = &VA(g->program, callee->first_param + i);
    if (parameter->direction == NTF_OUT)
      continue;
    if (parameter->reg.family < 0 && i >= 4)
      return fail(g, NTCG_E_UNSUPPORTED, x->span,
                  "parameter after fourth requires register binding");
    unsigned target = parameter->reg.family >= 0
                          ? (unsigned)parameter->reg.family
                          : defaults[i];
    if (target == 4 || target == 5 || target > 15)
      return fail(g, NTCG_E_UNSUPPORTED, x->span,
                  "parameter bound to reserved register");
    int32_t offset =
        -(g->call_base + (int32_t)(level * g->call_argument_bytes + i * 8));
    unsigned width = parameter->reg.family >= 0
                         ? parameter->reg.bits
                         : TY(g->program, parameter->type).bits;
    if (width == 1)
      width = 8;
    NtX64Operand destination = reg_width(target, width),
                 argument = mem_rbp(offset);
    destination.high8 = (unsigned)parameter->reg.high8;
    argument.width = width;
    if (!ins2(g, x->span, "load", destination, argument))
      return 0;
  }
  size_t before = g->image->size;
  if (!ins1(g, x->span, "call", relative(0)))
    return 0;
  if (!add_patch(g, before + 1, target_id, x->span))
    return 0;
  /* Only genuine callee writes update the restoration state. Argument moves
   * and evaluator temporaries must not become observable source-level writes.
   * RAX is the ABI result even when absent from the callee clobber set. */
  uint64_t changed =
      high_level(g)
          ? 0
          : callee->clobbers | UINT64_C(1) |
                nt_conformance_output_registers(g->program, x->resolved);
  for (unsigned reg = 0; reg < 16; reg++)
    if ((changed & (UINT64_C(1) << reg)) && reg != 4 && reg != 5 &&
        !ins2(g, x->span, "store", mem_rbp(expression_register_slot(g, reg, 1)),
              reg64(reg)))
      return 0;
  if ((changed & (UINT64_C(1) << 17)) &&
      !capture_expression_flags(g, x->span, 1))
    return 0;
  --g->call_depth;
  return normalize_type(g, x->type, x->span);
}
static const char *binary_name(NtFOp op) {
  switch (op) {
  case NTF_OP_ADD:
    return "add";
  case NTF_OP_SUB:
    return "sub";
  case NTF_OP_MUL:
    return "imul";
  case NTF_OP_AND:
    return "and";
  case NTF_OP_OR:
    return "or";
  case NTF_OP_XOR:
    return "xor";
  case NTF_OP_SHL:
    return "shl";
  case NTF_OP_SHR:
    return "shr";
  default:
    return NULL;
  }
}
static const char *condition_name(NtFOp op, int signed_values) {
  switch (op) {
  case NTF_OP_EQ:
    return "je";
  case NTF_OP_NE:
    return "jne";
  case NTF_OP_LT:
    return signed_values ? "jl" : "jb";
  case NTF_OP_LE:
    return signed_values ? "jle" : "jbe";
  case NTF_OP_GT:
    return signed_values ? "jg" : "ja";
  case NTF_OP_GE:
    return signed_values ? "jge" : "jae";
  default:
    return NULL;
  }
}
static int label_branch(const char *name) {
  static const char *names[] = {"jmp", "je", "jne", "jb", "jbe", "ja",
                                "jae", "jl", "jle", "jg", "jge", "jo",
                                "jno", "js", "jns", "jp", "jnp"};
  for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
    if (!strcmp(name, names[i]))
      return 1;
  return 0;
}
static int emit_comparison(Generator *g, const NtFExpr *x, NtX64Operand left) {
  if (!ins2(g, x->span, "cmp", left, reg64(0)))
    return 0;
  NtFTypeKind operand_kind = TY(g->program, EX(g->program, x->left).type).kind;
  int signed_values = operand_kind >= NTF_T_I8 && operand_kind <= NTF_T_I64;
  const char *condition = condition_name(x->op, signed_values);
  size_t conditional = g->image->size;
  if (!ins1(g, x->span, condition, relative(0)))
    return 0;
  if (!ins2(g, x->span, "mov", reg64(0), imm(0, 0)))
    return 0;
  size_t jump = g->image->size;
  if (!ins1(g, x->span, "jmp", relative(0)))
    return 0;
  size_t true_target = g->image->size;
  if (!ins2(g, x->span, "mov", reg64(0), imm(1, 0)))
    return 0;
  size_t end = g->image->size;
  return patch_local(g, conditional + 2, true_target, x->span) &&
         patch_local(g, jump + 1, end, x->span);
}
static int emit_expression_inner(Generator *g, NtFId id) {
  const NtFExpr *x = &EX(g->program, id);
  uint64_t value;
  if (evaluate_constant(g->program, id, &value))
    return ins2(g, x->span, "mov", reg64(0), imm(value, 0));
  switch (x->kind) {
  case NTF_X_ADDRESS: {
    if (!x->resolved || x->resolved > g->program->variable_count ||
        VA(g->program, x->resolved).direction != NTF_DATA)
      return fail(g, NTCG_E_INTERNAL, x->span,
                  "invalid symbolic address target");
    NtX64Operand address = {0};
    address.kind = NT_X64_MEM;
    address.width = 64;
    address.base = -2;
    address.index = -1;
    address.scale = 1;
    if (!ins2(g, x->span, "lea", reg64(0), address))
      return 0;
    return add_data_patch(g, g->image->size - 4, x->resolved,
                          x->memory.displacement, x->span);
  }
  case NTF_X_NAME: {
    const NtFVariable *v = &VA(g->program, x->resolved);
    if (v->direction == NTF_OUT || v->direction == NTF_INOUT) {
      NtX64Operand source =
          high_level(g) ? reg_width((unsigned)v->reg.family, v->reg.bits)
                        : mem_rbp(expression_register_slot(
                              g, (unsigned)v->reg.family, 0));
      if (high_level(g))
        source.high8 = (unsigned)v->reg.high8;
      else {
        source.width = v->reg.bits;
        if (v->reg.high8)
          source.disp++;
      }
      if (v->reg.bits < 32) {
        if (!ins2(g, x->span, "movzx", reg_width(0, 32), source))
          return 0;
      } else if (!ins2(g, x->span, high_level(g) ? "mov" : "load",
                       reg_width(0, v->reg.bits), source))
        return 0;
      return normalize_type(g, v->type, x->span);
    }
    if (v->direction == NTF_CONST) {
      if (symbolic_address(g->program, v->initializer))
        return emit_expression(g, v->initializer);
      return fail(g, NTCG_E_INTERNAL, x->span, "nonconstant const expression");
    }
    unsigned width = TY(g->program, v->type).bits;
    if (width == 1)
      width = 8;
    NtX64Operand slot = mem_rbp(g->variable_offsets[x->resolved - 1]);
    slot.width = width;
    if (!ins2(g, x->span, "load", reg_width(0, width), slot))
      return 0;
    return normalize_type(g, v->type, x->span);
  }
  case NTF_X_REGISTER:
    if (x->reg.family < 0 || x->reg.family > 15)
      return fail(g, NTCG_E_UNSUPPORTED, x->span,
                  "non-GPR register expression");
    {
      if (high_level(g)) {
        NtX64Operand source = reg_width((unsigned)x->reg.family, x->reg.bits);
        source.high8 = (unsigned)x->reg.high8;
        if (x->reg.bits < 32) {
          if (!ins2(g, x->span, "movzx", reg_width(0, 32), source))
            return 0;
        } else if (!ins2(g, x->span, "mov", reg_width(0, x->reg.bits), source))
          return 0;
        return normalize_type(g, x->type, x->span);
      }
      NtX64Operand source =
          mem_rbp(expression_register_slot(g, (unsigned)x->reg.family, 0));
      source.width = x->reg.bits;
      if (x->reg.high8)
        source.disp++;
      if (x->reg.bits < 32) {
        if (!ins2(g, x->span, "movzx", reg_width(0, 32), source))
          return 0;
      } else if (!ins2(g, x->span, "load", reg_width(0, x->reg.bits), source))
        return 0;
      return normalize_type(g, x->type, x->span);
    }
  case NTF_X_CALL:
    return emit_call(g, x);
  case NTF_X_UNARY:
    if (!emit_expression(g, x->left))
      return 0;
    if (x->op == NTF_OP_POS)
      return 1;
    if (x->op == NTF_OP_NEG)
      return ins1(g, x->span, "neg", reg64(0)) &&
             normalize_type(g, x->type, x->span);
    if (x->op == NTF_OP_NOT)
      return ins1(g, x->span, "not", reg64(0)) &&
             normalize_type(g, x->type, x->span);
    break;
  case NTF_X_BINARY: {
    unsigned level = g->spill_depth++;
    if (!emit_expression(g, x->left))
      return 0;
    int32_t offset = -(g->spill_base + (int32_t)(level * 8));
    if (!ins2(g, x->span, "store", mem_rbp(offset), reg64(0)) ||
        !emit_expression(g, x->right))
      return 0;
    NtX64Operand left = reg64(10);
    if (!ins2(g, x->span, "load", left, mem_rbp(offset)))
      return 0;
    --g->spill_depth;
    if (x->op >= NTF_OP_EQ && x->op <= NTF_OP_GE)
      return emit_comparison(g, x, left);
    if (x->op == NTF_OP_DIV || x->op == NTF_OP_MOD) {
      if (!ins2(g, x->span, "mov", reg64(11), reg64(0)) ||
          !ins2(g, x->span, "mov", reg64(0), left))
        return 0;
      NtFTypeKind kind = TY(g->program, x->type).kind;
      int signed_type = kind >= NTF_T_I8 && kind <= NTF_T_I64;
      if (signed_type) {
        if (!ins0(g, x->span, "cqo") || !ins1(g, x->span, "idiv", reg64(11)))
          return 0;
      } else {
        if (!ins2(g, x->span, "xor", reg64(2), reg64(2)) ||
            !ins1(g, x->span, "div", reg64(11)))
          return 0;
      }
      if (x->op == NTF_OP_MOD && !ins2(g, x->span, "mov", reg64(0), reg64(2)))
        return 0;
      return normalize_type(g, x->type, x->span);
    }
    const char *operation = binary_name(x->op);
    if (!operation)
      return fail(g, NTCG_E_UNSUPPORTED, x->span,
                  "binary operation cannot be lowered");
    if (x->op == NTF_OP_SHL || x->op == NTF_OP_SHR) {
      if (!ins2(g, x->span, "mov", reg64(1), reg64(0)) ||
          !ins2(g, x->span, operation, left, reg8(1)))
        return 0;
    } else if (!ins2(g, x->span, operation, left, reg64(0)))
      return 0;
    return ins2(g, x->span, "mov", reg64(0), left) &&
           normalize_type(g, x->type, x->span);
  }
  default:
    break;
  }
  return fail(g, NTCG_E_UNSUPPORTED, x->span,
              "expression kind cannot be lowered");
}
static int emit_expression(Generator *g, NtFId id) {
  NtFSpan span = EX(g->program, id).span;
  int outer = g->expression_level == 0;
  if (outer && !high_level(g)) {
    for (unsigned reg = 0; reg < 16; reg++)
      if (!ins2(g, span, "store", mem_rbp(expression_register_slot(g, reg, 0)),
                reg64(reg)) ||
          !ins2(g, span, "store", mem_rbp(expression_register_slot(g, reg, 1)),
                reg64(reg)))
        return 0;
    if (!capture_expression_flags(g, span, 0) ||
        !capture_expression_flags(g, span, 1))
      return 0;
  }
  g->expression_level++;
  int ok = emit_expression_inner(g, id);
  g->expression_level--;
  if (!ok || !outer || high_level(g))
    return ok;
  for (unsigned reg = 1; reg < 16; reg++)
    if (reg != 4 && reg != 5 &&
        !ins2(g, span, "load", reg64(reg),
              mem_rbp(expression_register_slot(g, reg, 1))))
      return 0;
  return restore_expression_flags(g, span);
}
static int epilogue(Generator *g, NtFSpan span) {
  if (!ins2(g, span, "mov", reg64(4), reg64(5)) ||
      !ins1(g, span, "pop", reg64(5)))
    return 0;
  for (size_t i = sizeof saved_registers / sizeof saved_registers[0]; i; i--) {
    unsigned reg = saved_registers[i - 1];
    const NtFFunction *function = &FN(g->program, g->function);
    const NtFVariable *output = NULL;
    for (uint32_t p = 0; p < function->param_count; p++) {
      const NtFVariable *v = &VA(g->program, function->first_param + p);
      if ((v->direction == NTF_OUT || v->direction == NTF_INOUT) &&
          v->reg.family == (int)reg)
        output = v;
    }
    if (output && output->reg.bits < 32) {
      NtX64Operand saved = mem_rbp(output->reg.high8 ? 1 : 0);
      saved.base = 4;
      saved.width = output->reg.bits;
      NtX64Operand value = reg_width(reg, output->reg.bits);
      value.high8 = (unsigned)output->reg.high8;
      if (!ins2(g, span, "store", saved, value) ||
          !ins1(g, span, "pop", reg64(reg)))
        return 0;
      continue;
    }
    if (output && output->reg.bits == 32 &&
        !ins2(g, span, "mov", reg_width(reg, 32), reg_width(reg, 32)))
      return 0;
    if ((FN(g->program, g->function).clobbers |
         nt_conformance_output_registers(g->program, g->function)) &
        (UINT64_C(1) << reg)) {
      NtX64Operand skip = mem_rbp(8);
      skip.base = 4;
      if (!ins2(g, span, "lea", reg64(4), skip))
        return 0;
    } else if (!ins1(g, span, "pop", reg64(reg)))
      return 0;
  }
  if (g->preserve_flags && !ins0(g, span, "popfq"))
    return 0;
  return ins0(g, span, "ret");
}
static int emit_block(Generator *, NtFId);
/* Convert one already typed machine operand. Expressions requiring temporary
 * registers are handled separately; direct operands never perturb another
 * source register or the flags merely to encode an instruction. */
static int machine_operand(Generator *g, NtFId id, const char *mnemonic,
                           NtX64Operand *operand) {
  const NtFExpr *x = &EX(g->program, id);
  memset(operand, 0, sizeof(*operand));
  if (x->kind == NTF_X_REGISTER) {
    if (x->reg.family < 0 || x->reg.family > 15)
      return fail(g, NTCG_E_UNSUPPORTED, x->span, "non-GPR machine operand");
    *operand = reg_width((unsigned)x->reg.family, x->reg.bits);
    operand->high8 = (unsigned)x->reg.high8;
    return 1;
  }
  if (x->kind == NTF_X_MEMORY) {
    if (!x->type || TY(g->program, x->type).kind != NTF_T_PTR)
      return fail(g, NTCG_E_INTERNAL, x->span,
                  "memory operand lacks pointer type");
    NtFType *pointer = &TY(g->program, x->type);
    operand->kind = NT_X64_MEM;
    operand->width = TY(g->program, pointer->element).bits;
    if (!strcmp(mnemonic, "invlpg"))
      operand->width = 0;
    if ((!strcmp(mnemonic, "lgdt") || !strcmp(mnemonic, "lidt") ||
         !strcmp(mnemonic, "sgdt") || !strcmp(mnemonic, "sidt")) &&
        TY(g->program, pointer->element).kind == NTF_T_ARRAY &&
        TY(g->program, pointer->element).count == 10 &&
        TY(g->program, TY(g->program, pointer->element).element).kind ==
            NTF_T_U8)
      operand->width = 80;
    operand->base = x->memory.symbol ? -2 : x->memory.base.family;
    operand->index = x->memory.index.family;
    operand->scale = x->memory.scale;
    operand->disp = x->memory.symbol ? 0 : x->memory.displacement;
    operand->segment = x->memory.segment == 1   ? NT_X64_FS
                       : x->memory.segment == 2 ? NT_X64_GS
                                                : NT_X64_SEG_NONE;
    if (x->memory.symbol && x->memory.segment)
      return fail(g, NTCG_E_UNSUPPORTED, x->span,
                  "segmented symbol requires an absolute relocation profile");
    return 1;
  }
  if (x->kind == NTF_X_NAME && x->name && strlen(x->name) == 3 &&
      x->name[0] == 'c' && x->name[1] == 'r' &&
      (x->name[2] == '0' || x->name[2] == '2' || x->name[2] == '3' ||
       x->name[2] == '4' || x->name[2] == '8')) {
    operand->kind = NT_X64_CR;
    operand->width = 64;
    operand->reg = (unsigned)(x->name[2] - '0');
    return 1;
  }
  uint64_t value;
  if (evaluate_constant(g->program, id, &value)) {
    int negative = x->negative;
    if (x->type) {
      NtFTypeKind kind = TY(g->program, x->type).kind;
      if (kind >= NTF_T_I8 && kind <= NTF_T_I64 && value > INT64_MAX)
        negative = 1;
    }
    *operand = imm(value, negative);
    return 1;
  }
  return fail(g, NTCG_E_UNSUPPORTED, x->span,
              "machine operand must be register, typed memory or constant");
}
static int emit_machine_instruction(Generator *g, const NtFStmt *s) {
  const char *machine_name =
      !strcmp(s->name, "call_indirect") ? "call" : s->name;
  if (!strcmp(s->name, "mov") && s->expression) {
    NtFId source = EX(g->program, s->expression).next;
    uint64_t constant;
    if (source && !EX(g->program, source).next &&
        EX(g->program, s->expression).kind == NTF_X_REGISTER &&
        EX(g->program, source).kind != NTF_X_REGISTER &&
        EX(g->program, source).kind != NTF_X_MEMORY &&
        !evaluate_constant(g->program, source, &constant)) {
      NtX64Operand destination;
      if (!machine_operand(g, s->expression, s->name, &destination) ||
          !emit_expression(g, source))
        return 0;
      NtX64Operand result = mem_rbp(-(g->snapshot_base + 272));
      if (!ins2(g, s->span, "store", result, reg64(0)) ||
          !restore_expression_rax(g, s->span))
        return 0;
      result.width = destination.width;
      return ins2(g, s->span, "load", destination, result);
    }
  }
  NtX64Operand operands[3];
  size_t count = 0;
  const NtFExpr *symbolic = NULL;
  for (NtFId id = s->expression; id; id = EX(g->program, id).next) {
    if (count == 3)
      return fail(g, NTCG_E_ENCODE, s->span,
                  "instruction exceeds three operands");
    if (!machine_operand(g, id, machine_name, &operands[count]))
      return 0;
    const NtFExpr *x = &EX(g->program, id);
    if (x->kind == NTF_X_MEMORY && x->memory.symbol) {
      if (symbolic)
        return fail(g, NTCG_E_ENCODE, s->span,
                    "multiple symbolic memory operands");
      symbolic = x;
    }
    count++;
  }
  NtX64Instruction encoded;
  const NtFFunction *function = &FN(g->program, g->function);
  NtX64Context context = {g->program->modules[function->module - 1].features,
                          (function->effects.flags & NTF_F_PRIVILEGED) ? 0u
                                                                       : 3u};
  NtX64Error error =
      nt_x64_encode(machine_name, operands, count, context, &encoded);
  if (error)
    return fail(g, NTCG_E_ENCODE, s->span, "%s: %s", s->name,
                nt_x64_error_string(error));
  size_t before = g->image->size;
  if (!raw(g, encoded.bytes, encoded.length))
    return 0;
  if (symbolic) {
    if (encoded.displacement_size != 4)
      return fail(g, NTCG_E_INTERNAL, s->span,
                  "symbol has no displacement field");
    size_t tail = encoded.length - encoded.displacement_offset - 4;
    return add_data_patch(
        g, before + encoded.displacement_offset, symbolic->resolved,
        symbolic->memory.displacement - (int64_t)tail, s->span);
  }
  return 1;
}
static int emit_statement(Generator *g, NtFId id) {
  const NtFStmt *s = &ST(g->program, id);
  if (s->kind == NTF_S_LABEL) {
    g->statement_offsets[id - 1] = g->image->size;
    return 1;
  }
  if (s->kind == NTF_S_BLOCK)
    return emit_block(g, id);
  if (s->kind == NTF_S_LET) {
    if (!emit_expression(g, s->expression))
      return 0;
    return ins2(g, s->span, "store",
                mem_rbp(g->variable_offsets[s->variable - 1]), reg64(0)) &&
           restore_expression_rax(g, s->span);
  }
  if (s->kind == NTF_S_RETURN)
    return emit_expression(g, s->expression) && epilogue(g, s->span);
  if (s->kind == NTF_S_CALL)
    return emit_expression(g, s->expression);
  if (s->kind == NTF_S_IF) {
    if (!emit_expression(g, s->condition) ||
        !ins2(g, s->span, "test", reg64(0), reg64(0)))
      return 0;
    size_t branch = g->image->size;
    if (!ins1(g, s->span, "je", relative(0)))
      return 0;
    if (!restore_expression_rax(g, s->span) ||
        !restore_expression_flags(g, s->span))
      return 0;
    if (!emit_block(g, s->then_branch))
      return 0;
    if (s->else_branch) {
      size_t jump = g->image->size;
      if (!ins1(g, s->span, "jmp", relative(0)))
        return 0;
      size_t else_target = g->image->size;
      if (!patch_local(g, branch + 2, else_target, s->span) ||
          !restore_expression_rax(g, s->span) ||
          !restore_expression_flags(g, s->span) ||
          !emit_block(g, s->else_branch))
        return 0;
      return patch_local(g, jump + 1, g->image->size, s->span);
    }
    size_t jump = g->image->size;
    if (!ins1(g, s->span, "jmp", relative(0)) ||
        !patch_local(g, branch + 2, g->image->size, s->span) ||
        !restore_expression_rax(g, s->span) ||
        !restore_expression_flags(g, s->span))
      return 0;
    return patch_local(g, jump + 1, g->image->size, s->span);
  }
  if (s->kind == NTF_S_INSTRUCTION) {
    if (label_branch(s->name)) {
      size_t before = g->image->size;
      if (!ins1(g, s->span, s->name, relative(0)))
        return 0;
      size_t site = before + (!strcmp(s->name, "jmp") ? 1 : 2);
      return add_label_patch(g, site, s->variable, s->span);
    }
    return emit_machine_instruction(g, s);
  }
  return fail(g, NTCG_E_UNSUPPORTED, s->span,
              "statement kind cannot be lowered");
}
static int emit_block(Generator *g, NtFId block) {
  for (NtFId id = ST(g->program, block).first; id; id = ST(g->program, id).next)
    if (!emit_statement(g, id))
      return 0;
  return 1;
}
static int raw_rax(const NtFProgram *p, NtFId id) {
  if (!id || id > p->expression_count)
    return 0;
  const NtFExpr *x = &EX(p, id);
  return x->kind == NTF_X_REGISTER && x->reg.family == 0 && x->reg.bits == 64 &&
         !x->reg.high8;
}
static int raw_saved_rip(const NtFProgram *p, NtFId id, int displacement) {
  if (!id || id > p->expression_count)
    return 0;
  const NtFExpr *x = &EX(p, id);
  if (x->kind != NTF_X_MEMORY || !x->type || x->type > p->type_count)
    return 0;
  const NtFType *t = &TY(p, x->type);
  if (t->kind != NTF_T_PTR || t->space != NTF_SPACE_KERNEL || !t->element ||
      t->element > p->type_count)
    return 0;
  return TY(p, t->element).kind == NTF_T_U64 && x->memory.base.family == 4 &&
         x->memory.base.bits == 64 && x->memory.index.family == -1 &&
         x->memory.scale == 1 && x->memory.segment == 0 &&
         x->memory.displacement == displacement && !x->memory.symbol &&
         !x->resolved;
}
static int emit_raw_interrupt_stub(Generator *g, NtFId id, size_t start) {
  const NtFProgram *p = g->program;
  const NtFFunction *f = &FN(p, id);
  int error_frame = f->generated_stub == 2;
  unsigned count = error_frame ? 7u : 6u;
  const char *abi = error_frame ? "nx64-interrupt-same-cpl-error-v0"
                                : "nx64-interrupt-same-cpl-v0";
  const uint32_t effects =
      NTF_F_PRIVILEGED | NTF_F_CHANGES_FLAGS | NTF_F_CONTROL | NTF_F_NO_RETURN;
  const uint64_t writes =
      UINT64_C(1) | (UINT64_C(1) << 4) | (UINT64_C(1) << 17);
  if ((f->generated_stub != 1 && f->generated_stub != 2) ||
      f->stub_vector != (error_frame ? 13u : 6u) || !f->abi ||
      strcmp(f->abi, abi) || f->param_count || f->clobbers ||
      f->inferred_clobbers || f->requires || f->ensures || !f->return_type ||
      f->return_type > p->type_count ||
      TY(p, f->return_type).kind != NTF_T_NEVER ||
      f->effects.flags != effects ||
      f->effects.reads != (1u << NTF_SPACE_KERNEL) ||
      f->effects.writes != (1u << NTF_SPACE_KERNEL) ||
      f->write_footprint != writes || !f->body ||
      f->body > p->statement_count || ST(p, f->body).kind != NTF_S_BLOCK)
    return fail(
        g, NTCG_E_UNSUPPORTED, f->span,
        "raw interrupt ABI does not match its verified witness profile");
  const char *names[] = {"push",  "load", "add",
                         "store", "pop",  error_frame ? "add" : "iretq",
                         "iretq"};
  NtFId statements[7], statement = ST(p, f->body).first;
  for (unsigned i = 0; i < count; i++) {
    if (!statement || statement > p->statement_count)
      return fail(g, NTCG_E_UNSUPPORTED, f->span,
                  "raw interrupt body is incomplete");
    const NtFStmt *s = &ST(p, statement);
    statements[i] = statement;
    if (s->kind != NTF_S_INSTRUCTION || !s->name || strcmp(s->name, names[i]) ||
        s->condition || s->then_branch || s->else_branch || s->variable)
      return fail(g, NTCG_E_UNSUPPORTED, s->span,
                  "raw interrupt body differs from its proven sequence");
    NtFId a = s->expression,
          b = a && a <= p->expression_count ? EX(p, a).next : 0;
    if (a > p->expression_count || b > p->expression_count ||
        (b && EX(p, b).next))
      return fail(g, NTCG_E_UNSUPPORTED, s->span,
                  "invalid raw interrupt operand list");
    int valid = 0;
    if (i == 0 || i == 4)
      valid = raw_rax(p, a) && !b;
    if (i == 1)
      valid = raw_rax(p, a) && raw_saved_rip(p, b, error_frame ? 16 : 8);
    if (i == 2)
      valid = raw_rax(p, a) && b && EX(p, b).kind == NTF_X_LITERAL &&
              EX(p, b).value == 2 && !EX(p, b).negative;
    if (i == 3)
      valid = raw_saved_rip(p, a, error_frame ? 16 : 8) && raw_rax(p, b);
    if (i == 5 && error_frame)
      valid = a && EX(p, a).kind == NTF_X_REGISTER &&
              EX(p, a).reg.family == 4 && EX(p, a).reg.bits == 64 &&
              !EX(p, a).reg.high8 && b && EX(p, b).kind == NTF_X_LITERAL &&
              EX(p, b).value == 8 && !EX(p, b).negative;
    if (i == count - 1)
      valid = !a && s->terminal;
    if (!valid)
      return fail(g, NTCG_E_UNSUPPORTED, s->span,
                  "raw interrupt operands violate the frame profile");
    statement = s->next;
  }
  if (statement)
    return fail(g, NTCG_E_UNSUPPORTED, f->span,
                "raw interrupt body has trailing statements");
  for (unsigned i = 0; i < count; i++)
    if (!emit_machine_instruction(g, &ST(p, statements[i])))
      return 0;
  return add_symbol(g, f->name, p->modules[f->module - 1].name,
                    NT_CODE_SECTION_TEXT, start, g->image->size - start,
                    f->exported ? 1u : 0u, id, 0, f->span);
}
static int emit_function(Generator *g, NtFId id) {
  const NtFFunction *f = &FN(g->program, id);
  if (f->imported)
    return 1;
  g->function = id;
  if (f->high_level_expressions > 1 ||
      (f->generated_stub && f->high_level_expressions))
    return fail(g, NTCG_E_UNSUPPORTED, f->span,
                "invalid high-level expression mode");
  size_t function_start = g->image->size;
  g->function_offsets[id - 1] = function_start;
  if (f->generated_stub ||
      (f->abi && (!strcmp(f->abi, "nx64-interrupt-same-cpl-v0") ||
                  !strcmp(f->abi, "nx64-interrupt-same-cpl-error-v0"))))
    return emit_raw_interrupt_stub(g, id, function_start);
  NtFId first_statement = ST(g->program, f->body).first;
  uint64_t folded_value;
  int requires_aligned_body = 0;
  for (NtFId predicate = f->requires; predicate;
       predicate = EX(g->program, predicate).next)
    if (!strcmp(EX(g->program, predicate).name, "stack_aligned"))
      requires_aligned_body = 1;
  if (!requires_aligned_body && !f->param_count && first_statement &&
      ST(g->program, first_statement).kind == NTF_S_RETURN &&
      !ST(g->program, first_statement).next &&
      evaluate_constant(g->program, ST(g->program, first_statement).expression,
                        &folded_value)) {
    if (!ins2(g, f->span, "mov", reg64(0), imm(folded_value, 0)))
      return 0;
    if (!ins0(g, f->span, "ret"))
      return 0;
    return add_symbol(g, f->name, g->program->modules[f->module - 1].name,
                      NT_CODE_SECTION_TEXT, function_start,
                      g->image->size - function_start, f->exported ? 1u : 0u,
                      id, 0, f->span);
  }
  unsigned locals = 0;
  for (size_t i = 0; i < g->program->variable_count; i++)
    if (g->program->variables[i].function == id)
      locals++;
  unsigned expr_depth = 0, calls = 0, args = 0, stack_args = 0;
  statement_requirements(g->program, ST(g->program, f->body).first, &expr_depth,
                         &calls, &args, &stack_args);
  g->call_argument_bytes = (args ? args : 1) * 8;
  uint64_t needed = 32u + (uint64_t)locals * 8u + (uint64_t)expr_depth * 8u +
                    (uint64_t)calls * g->call_argument_bytes +
                    EXPRESSION_STATE_BYTES + (uint64_t)stack_args * 8u;
  g->preserve_flags = !(f->clobbers & (UINT64_C(1) << 17));
  uint64_t frame = g->preserve_flags ? (needed + 15u) & ~UINT64_C(15)
                                     : ((needed + 7u) & ~UINT64_C(15)) + 8u;
  if (frame > INT32_MAX)
    return fail(g, NTCG_E_RANGE, f->span, "function frame too large");
  unsigned slot = 0;
  for (size_t i = 0; i < g->program->variable_count; i++)
    if (g->program->variables[i].function == id)
      g->variable_offsets[i] = -(int32_t)(8 * (++slot));
  g->snapshot_base = (int32_t)(slot * 8 + 8);
  g->spill_base = g->snapshot_base + (int32_t)EXPRESSION_STATE_BYTES;
  g->call_base = g->spill_base + (int32_t)(expr_depth * 8);
  if (g->preserve_flags && !ins0(g, f->span, "pushfq"))
    return 0;
  for (size_t i = 0; i < sizeof saved_registers / sizeof saved_registers[0];
       i++)
    if (!ins1(g, f->span, "push", reg64(saved_registers[i])))
      return 0;
  if (!ins1(g, f->span, "push", reg64(5)) ||
      !ins2(g, f->span, "mov", reg64(5), reg64(4)))
    return 0;
  /* Probe each new page rather than jumping over Windows stack guards. */
  uint64_t remaining = frame;
  while (remaining > 4096) {
    NtX64Operand touch = mem_rbp(0);
    touch.base = 4;
    touch.width = 8;
    if (!ins2(g, f->span, "sub", reg64(4), imm(4096, 0)) ||
        !ins2(g, f->span, "store", touch, imm(0, 0)))
      return 0;
    remaining -= 4096;
  }
  if (remaining && !ins2(g, f->span, "sub", reg64(4), imm(remaining, 0)))
    return 0;
  static const unsigned defaults[] = {1, 2, 8, 9};
  int efi = is_efi(f);
  if (efi && f->param_count > 4 &&
      !ins2(g, f->span, "store", mem_rbp(expression_register_slot(g, 0, 1)),
            reg64(0)))
    return 0;
  for (uint32_t i = 0; i < f->param_count; i++) {
    const NtFVariable *parameter = &VA(g->program, f->first_param + i);
    if (parameter->direction == NTF_OUT)
      continue;
    if (efi && i >= 4) {
      int32_t incoming =
          (int32_t)(sizeof(saved_registers) / sizeof(saved_registers[0]) * 8 +
                    8 + (g->preserve_flags ? 8 : 0) + 40 + (i - 4) * 8);
      if (!ins2(g, parameter->span, "load", reg64(0), mem_rbp(incoming)) ||
          !ins2(g, parameter->span, "store",
                mem_rbp(g->variable_offsets[f->first_param + i - 1]), reg64(0)))
        return 0;
      continue;
    }
    if (parameter->reg.family < 0 && i >= 4)
      return fail(g, NTCG_E_UNSUPPORTED, parameter->span,
                  "parameter after fourth requires register binding");
    unsigned source = parameter->reg.family >= 0
                          ? (unsigned)parameter->reg.family
                          : defaults[i];
    unsigned width = parameter->reg.family >= 0
                         ? parameter->reg.bits
                         : TY(g->program, parameter->type).bits;
    if (width == 1)
      width = 8;
    NtX64Operand input = reg_width(source, width);
    input.high8 = (unsigned)parameter->reg.high8;
    NtX64Operand slot = mem_rbp(g->variable_offsets[f->first_param + i - 1]);
    slot.width = width;
    if (!ins2(g, parameter->span, "store", slot, input))
      return 0;
  }
  if (efi && f->param_count > 4 && !restore_expression_rax(g, f->span))
    return 0;
  if (!emit_block(g, f->body))
    return 0;
  return add_symbol(g, f->name, g->program->modules[f->module - 1].name,
                    NT_CODE_SECTION_TEXT, function_start,
                    g->image->size - function_start, f->exported ? 1u : 0u, id,
                    0, f->span);
}
static int function_before(const NtFProgram *program, NtFId a, NtFId b) {
  const NtFFunction *left = &FN(program, a);
  const NtFFunction *right = &FN(program, b);
  const char *left_module = program->modules[left->module - 1].name;
  const char *right_module = program->modules[right->module - 1].name;
  int module_order = strcmp(left_module, right_module);
  if (module_order)
    return module_order < 0;
  int name_order = strcmp(left->name, right->name);
  if (name_order)
    return name_order < 0;
  return a < b;
}
static int data_before(const NtFProgram *program, NtFId a, NtFId b) {
  const NtFVariable *left = &VA(program, a);
  const NtFVariable *right = &VA(program, b);
  const char *left_module = program->modules[left->module - 1].name;
  const char *right_module = program->modules[right->module - 1].name;
  int module_order = strcmp(left_module, right_module);
  if (module_order)
    return module_order < 0;
  int name_order = strcmp(left->name, right->name);
  if (name_order)
    return name_order < 0;
  return a < b;
}
static int emit_static_data(Generator *g) {
  unsigned char *emitted = (unsigned char *)calloc(
      g->program->variable_count ? g->program->variable_count : 1, 1);
  if (!emitted)
    return fail(g, NTCG_E_OOM, (NtFSpan){0},
                "static data ordering allocation failed");
  for (;;) {
    NtFId chosen = 0;
    for (size_t i = 0; i < g->program->variable_count; i++) {
      if (emitted[i] || g->program->variables[i].direction != NTF_DATA)
        continue;
      NtFId candidate = (NtFId)(i + 1);
      if (!chosen || data_before(g->program, candidate, chosen))
        chosen = candidate;
    }
    if (!chosen)
      break;
    emitted[chosen - 1] = 1;
    const NtFVariable *variable = &VA(g->program, chosen);
    NtFType *type = &TY(g->program, variable->type);
    int string_array =
        type->kind == NTF_T_ARRAY &&
        TY(g->program, type->element).kind == NTF_T_U8 &&
        EX(g->program, variable->initializer).kind == NTF_X_STRING;
    const NtFExpr *initializer =
        symbolic_address(g->program, variable->initializer);
    int address_initializer = type->kind == NTF_T_PTR && initializer;
    if (!string_array && !address_initializer &&
        (type->kind < NTF_T_U8 || type->kind > NTF_T_BOOL)) {
      free(emitted);
      return fail(g, NTCG_E_UNSUPPORTED, variable->span,
                  "static data type '%s' is not implemented",
                  nt_frontend_type_name(type->kind));
    }
    size_t width = string_array               ? (size_t)type->count
                   : type->kind == NTF_T_BOOL ? 1
                                              : type->bits / 8;
    uint64_t value = 0;
    if (!string_array && !address_initializer &&
        !evaluate_constant(g->program, variable->initializer, &value)) {
      free(emitted);
      return fail(g, NTCG_E_INTERNAL, variable->span,
                  "verified static initializer is not constant");
    }
    NtCodeBuffer *section;
    unsigned section_id;
    if (!strcmp(variable->section, ".rdata")) {
      section = &g->image->rdata;
      section_id = 1;
    } else if (!strcmp(variable->section, ".data")) {
      section = &g->image->data;
      section_id = 2;
    } else {
      free(emitted);
      return fail(g, NTCG_E_UNSUPPORTED, variable->span,
                  "data must be in .rdata or .data");
    }
    size_t alignment = width > 8 ? 8 : width;
    while (alignment && section->size % alignment) {
      uint8_t zero = 0;
      if (!buffer_append(g, section, &zero, 1, variable->span)) {
        free(emitted);
        return 0;
      }
    }
    g->data_offsets[chosen - 1] = section->size;
    g->data_sections[chosen - 1] = (unsigned char)section_id;
    if (address_initializer) {
      if (!initializer->resolved ||
          initializer->resolved > g->program->variable_count ||
          VA(g->program, initializer->resolved).direction != NTF_DATA ||
          width != 8) {
        free(emitted);
        return fail(g, NTCG_E_INTERNAL, initializer->span,
                    "invalid static symbolic pointer");
      }
      if (!add_relocation(
              g, section_id == 1 ? NT_CODE_SECTION_RDATA : NT_CODE_SECTION_DATA,
              section->size, NT_CODE_RELOC_DIR64, 0, initializer->resolved,
              initializer->memory.displacement, initializer->span)) {
        free(emitted);
        return 0;
      }
    }
    if (string_array) {
      const NtFExpr *string = &EX(g->program, variable->initializer);
      for (size_t i = 0; i < string->string_length; i++) {
        unsigned char byte = (unsigned char)string->name[i];
        if (byte == '\\') {
          byte = (unsigned char)string->name[++i];
          if (byte == 'n')
            byte = '\n';
          else if (byte == 'r')
            byte = '\r';
          else if (byte == 't')
            byte = '\t';
          else if (byte == '0')
            byte = 0;
          else if (byte == 'x') {
            unsigned char high = (unsigned char)string->name[++i];
            unsigned char low = (unsigned char)string->name[++i];
            unsigned high_value =
                high <= '9' ? high - '0' : (high | 32) - 'a' + 10;
            unsigned low_value = low <= '9' ? low - '0' : (low | 32) - 'a' + 10;
            byte = (unsigned char)((high_value << 4) | low_value);
          }
        }
        if (!buffer_append(g, section, &byte, 1, variable->span)) {
          free(emitted);
          return 0;
        }
      }
    } else {
      uint8_t bytes[8];
      for (size_t i = 0; i < width; i++)
        bytes[i] = (uint8_t)(value >> (8 * i));
      if (!buffer_append(g, section, bytes, width, variable->span)) {
        free(emitted);
        return 0;
      }
    }
    if (!add_symbol(
            g, variable->name, g->program->modules[variable->module - 1].name,
            section_id == 1 ? NT_CODE_SECTION_RDATA : NT_CODE_SECTION_DATA,
            g->data_offsets[chosen - 1], width, variable->exported ? 1u : 0u, 0,
            chosen, variable->span)) {
      free(emitted);
      return 0;
    }
  }
  free(emitted);
  return 1;
}
static int generate(const NtFProgram *program, NtCodeImage *image,
                    NtCodeDiagnostic *diagnostic, int object_mode) {
  if (!image || !diagnostic)
    return 0;
  memset(image, 0, sizeof(*image));
  memset(diagnostic, 0, sizeof(*diagnostic));
  Generator g;
  memset(&g, 0, sizeof(g));
  g.program = program;
  g.image = image;
  g.diagnostic = diagnostic;
  g.object_mode = object_mode;
  if (!program || !program->verified)
    return fail(&g, NTCG_E_UNVERIFIED, (NtFSpan){0},
                "frontend program is not verified");
  g.function_offsets = (size_t *)calloc(
      program->function_count ? program->function_count : 1, sizeof(size_t));
  g.variable_offsets = (int32_t *)calloc(
      program->variable_count ? program->variable_count : 1, sizeof(int32_t));
  g.data_offsets = (size_t *)calloc(
      program->variable_count ? program->variable_count : 1, sizeof(size_t));
  g.data_sections = (unsigned char *)calloc(
      program->variable_count ? program->variable_count : 1, 1);
  g.statement_offsets = (size_t *)malloc(
      (program->statement_count ? program->statement_count : 1) *
      sizeof(size_t));
  if (g.statement_offsets)
    for (size_t i = 0; i < program->statement_count; i++)
      g.statement_offsets[i] = SIZE_MAX;
  unsigned char *emitted = (unsigned char *)calloc(
      program->function_count ? program->function_count : 1,
      sizeof(unsigned char));
  if (!g.function_offsets || !g.variable_offsets || !g.data_offsets ||
      !g.data_sections || !g.statement_offsets || !emitted) {
    fail(&g, NTCG_E_OOM, (NtFSpan){0}, "codegen metadata allocation failed");
    free(emitted);
    goto done;
  }
  NtFId entry = 0;
  unsigned entry_count = 0;
  for (size_t i = 0; i < program->function_count; i++)
    if (program->functions[i].exported &&
        !strcmp(program->functions[i].name, "main")) {
      entry = (NtFId)(i + 1);
      ++entry_count;
    }
  if (entry_count > 1 || (!object_mode && !entry_count)) {
    fail(&g, NTCG_E_ENTRY, (NtFSpan){0},
         entry_count ? "multiple exported main functions"
                     : "exported main function is required");
    free(emitted);
    goto done;
  }
  if (entry && FN(program, entry).generated_stub) {
    fail(&g, NTCG_E_ENTRY, FN(program, entry).span,
         "interrupt ABI requires a hardware frame and cannot be the executable "
         "main entry");
    free(emitted);
    goto done;
  }
  for (size_t i = 0; i < program->function_count && !g.failed; i++) {
    const NtFFunction *fn = program->functions + i;
    if (!fn->imported || fn->resolved)
      continue;
    if (!object_mode) {
      fail(&g, NTCG_E_UNVERIFIED, fn->span,
           "unresolved import requires object mode");
      break;
    }
    const char *dot = fn->import_name ? strrchr(fn->import_name, '.') : NULL;
    if (!dot || dot == fn->import_name || !dot[1]) {
      fail(&g, NTCG_E_INTERNAL, fn->span, "invalid qualified import name");
      break;
    }
    size_t length = (size_t)(dot - fn->import_name);
    char *module = malloc(length + 1);
    if (!module) {
      fail(&g, NTCG_E_OOM, fn->span, "import module allocation failed");
      break;
    }
    memcpy(module, fn->import_name, length);
    module[length] = 0;
    add_symbol(&g, dot + 1, module, 0, 0, 0, 8, (NtFId)(i + 1), 0, fn->span);
    free(module);
  }
  if (g.failed) {
    free(emitted);
    goto done;
  }
  if (!emit_static_data(&g)) {
    free(emitted);
    goto done;
  }
  for (size_t emitted_count = 0;
       emitted_count < program->function_count && !g.failed;) {
    NtFId chosen = 0;
    for (size_t i = 0; i < program->function_count; i++) {
      NtFId candidate = (NtFId)(i + 1);
      if (emitted[i] || program->functions[i].imported)
        continue;
      if (!chosen || function_before(program, candidate, chosen))
        chosen = candidate;
    }
    if (!chosen)
      break;
    emitted[chosen - 1] = 1;
    ++emitted_count;
    emit_function(&g, chosen);
  }
  free(emitted);
  for (size_t i = 0; i < g.patch_count && !g.failed; i++) {
    Patch *patch = &g.patches[i];
    if (object_mode && FN(program, patch->target).imported &&
        !FN(program, patch->target).resolved)
      continue; /* Leave zero displacement plus typed external relocation. */
    patch_local(&g, patch->displacement_site,
                g.function_offsets[patch->target - 1],
                FN(program, patch->target).span);
  }
  for (size_t i = 0; i < g.label_patch_count && !g.failed; i++) {
    LabelPatch *patch = &g.label_patches[i];
    if (!patch->statement || patch->statement > program->statement_count ||
        g.statement_offsets[patch->statement - 1] == SIZE_MAX) {
      fail(&g, NTCG_E_INTERNAL, patch->span, "label target was not emitted");
      break;
    }
    patch_local(&g, patch->displacement_site,
                g.statement_offsets[patch->statement - 1], patch->span);
  }
  if (!g.failed) {
    size_t rdata_base = (image->size + 4095u) & ~(size_t)4095u;
    size_t data_base =
        (rdata_base + 8u + image->rdata.size + 4095u) & ~(size_t)4095u;
    for (size_t i = 0; i < g.data_patch_count && !g.failed; i++) {
      DataPatch *patch = &g.data_patches[i];
      unsigned section = g.data_sections[patch->variable - 1];
      size_t base = section == 1 ? rdata_base + 8 : data_base;
      size_t offset = g.data_offsets[patch->variable - 1];
      if (!section || offset > SIZE_MAX - base) {
        fail(&g, NTCG_E_INTERNAL, (NtFSpan){0},
             "invalid static data relocation");
        break;
      }
      int64_t target = (int64_t)(base + offset) + patch->addend;
      int64_t displacement = target - (int64_t)(patch->displacement_site + 4);
      if (displacement < INT32_MIN || displacement > INT32_MAX) {
        fail(&g, NTCG_E_RANGE, (NtFSpan){0},
             "static data relocation outside rel32");
        break;
      }
      put32(image->bytes + patch->displacement_site, (int32_t)displacement);
    }
  }
  if (!g.failed)
    image->entry = entry ? g.function_offsets[entry - 1] : SIZE_MAX;
done:
  free(g.function_offsets);
  free(g.variable_offsets);
  free(g.data_offsets);
  free(g.data_sections);
  free(g.statement_offsets);
  free(g.patches);
  free(g.data_patches);
  free(g.label_patches);
  if (g.failed) {
    free(image->bytes);
    free(image->rdata.bytes);
    free(image->data.bytes);
    for (size_t i = 0; i < image->symbol_count; i++) {
      free(image->symbols[i].name);
      free(image->symbols[i].module);
    }
    free(image->symbols);
    free(image->relocations);
    memset(image, 0, sizeof(*image));
    return 0;
  }
  return 1;
}
int nt_codegen_x64(const NtFProgram *program, NtCodeImage *image,
                   NtCodeDiagnostic *diagnostic) {
  return generate(program, image, diagnostic, 0);
}
int nt_codegen_x64_object(const NtFProgram *program, NtCodeImage *image,
                          NtCodeDiagnostic *diagnostic) {
  return generate(program, image, diagnostic, 1);
}
void nt_code_image_free(NtCodeImage *image) {
  if (!image)
    return;
  free(image->bytes);
  free(image->rdata.bytes);
  free(image->data.bytes);
  for (size_t i = 0; i < image->symbol_count; i++) {
    free(image->symbols[i].name);
    free(image->symbols[i].module);
  }
  free(image->symbols);
  free(image->relocations);
  memset(image, 0, sizeof(*image));
}
