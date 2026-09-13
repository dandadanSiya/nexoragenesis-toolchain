#include "conformance.h"
#include "x64.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CFG_BUDGET (64u * 1024u * 1024u)
#define ST(p, id) ((p)->statements[(id) - 1])
#define EX(p, id) ((p)->expressions[(id) - 1])
typedef struct {
  NtFId edge[2];
  unsigned count, built, reachable, queued;
  uint32_t flags;
  uint64_t initialized[16];
  NtFId pointers[16];
  unsigned char writable[16];
} Node;
typedef struct {
  const NtFProgram *p;
  NtFId function;
  NtFDiagnostic *diagnostic;
  Node *nodes;
  NtFId *queue;
  uint32_t *local_index;
  uint64_t *states, *temporary;
  size_t rows, words, local_count;
  size_t head, tail;
  int failed;
  int output_bindings;
  uint64_t required_outputs[16];
  NtFProgram *annotate;
  const unsigned char *current_writable;
  size_t permission_fuel;
} Graph;
static int branch(const NtFStmt *s);
static uint64_t register_bits(NtFRegister reg) {
  if (reg.bits == 64 || reg.bits == 32)
    return UINT64_MAX;
  if (!reg.bits || reg.bits > 64)
    return 0;
  return ((UINT64_C(1) << reg.bits) - 1) << (reg.high8 ? 8 : 0);
}
static uint64_t register_read_bits(NtFRegister reg) {
  if (reg.bits == 32)
    return UINT32_MAX;
  return register_bits(reg);
}
uint64_t nt_conformance_output_registers(const NtFProgram *p, NtFId function) {
  if (!p || !function || function > p->function_count)
    return 0;
  const NtFFunction *f = &p->functions[function - 1];
  uint64_t mask = 0;
  for (uint32_t i = 0; i < f->param_count; i++) {
    const NtFVariable *v = &p->variables[f->first_param + i - 1];
    if ((v->direction == NTF_OUT || v->direction == NTF_INOUT) &&
        v->reg.family >= 0 && v->reg.family < 16)
      mask |= UINT64_C(1) << v->reg.family;
  }
  return mask;
}
static int outputs_expression(Graph *g, NtFId id, const uint64_t *before,
                              uint64_t *after, unsigned depth) {
  if (!id)
    return 1;
  if (id > g->p->expression_count || depth > 128)
    return 0;
  const NtFExpr *x = &EX(g->p, id);
  NtFRegister reg = {-1, 0, 0};
  if (x->kind == NTF_X_REGISTER)
    reg = x->reg;
  if (x->kind == NTF_X_NAME && x->resolved &&
      x->resolved <= g->p->variable_count) {
    const NtFVariable *v = &g->p->variables[x->resolved - 1];
    if (v->direction == NTF_OUT || v->direction == NTF_INOUT)
      reg = v->reg;
  }
  if (before && reg.family >= 0 && reg.family < 16 &&
      (register_read_bits(reg) & ~before[reg.family]))
    return 0;
  if (!outputs_expression(g, x->left, before, after, depth + 1) ||
      !outputs_expression(g, x->right, before, after, depth + 1))
    return 0;
  size_t steps = 0;
  for (NtFId a = x->first_arg; a; a = EX(g->p, a).next) {
    if (++steps > g->p->expression_count ||
        !outputs_expression(g, a, before, after, depth + 1))
      return 0;
  }
  if (after && x->kind == NTF_X_CALL && x->resolved &&
      x->resolved <= g->p->function_count) {
    const NtFFunction *f = &g->p->functions[x->resolved - 1];
    for (uint32_t i = 0; i < f->param_count; i++) {
      const NtFVariable *v = &g->p->variables[f->first_param + i - 1];
      if ((v->direction == NTF_OUT || v->direction == NTF_INOUT) &&
          v->reg.family >= 0 && v->reg.family < 16)
        after[v->reg.family] |= register_read_bits(v->reg);
    }
  }
  return 1;
}
static int outputs_instruction(Graph *g, const NtFStmt *s,
                               const uint64_t *before, uint64_t *after) {
  if (branch(s))
    return 1;
  NtX64Operand operands[3];
  NtFId ids[3];
  size_t count = 0;
  uint64_t explicit_families = 0;
  for (NtFId id = s->expression; id; id = EX(g->p, id).next) {
    if (count == 3 || id > g->p->expression_count)
      return 0;
    const NtFExpr *x = &EX(g->p, id);
    NtX64Operand *o = &operands[count];
    ids[count++] = id;
    memset(o, 0, sizeof(*o));
    if (x->kind == NTF_X_REGISTER) {
      o->kind = NT_X64_REG;
      o->width = x->reg.bits;
      o->reg = (unsigned)x->reg.family;
      o->high8 = (unsigned)x->reg.high8;
      explicit_families |= UINT64_C(1) << o->reg;
    } else if (x->kind == NTF_X_MEMORY) {
      o->kind = NT_X64_MEM;
      o->width = g->p->types[g->p->types[x->type - 1].element - 1].bits;
      o->base = x->memory.symbol ? -2 : x->memory.base.family;
      o->index = x->memory.index.family;
      o->scale = x->memory.scale;
      o->disp = x->memory.displacement;
    } else {
      o->kind = NT_X64_IMM;
      o->imm = x->kind == NTF_X_LITERAL ? x->value : 0;
      o->is_signed = (unsigned)x->negative;
    }
  }
  NtX64Effects facts;
  NtX64Context context = {NT_X64_ALL_FEATURES, 0};
  if (nt_x64_effects(s->name, operands, count, context, &facts))
    return 0;
  uint64_t reads[16] = {0}, writes[16] = {0};
  for (unsigned r = 0; r < 16; r++) {
    if ((facts.registers_read & ~explicit_families) & (UINT64_C(1) << r))
      reads[r] = UINT64_MAX;
    if (facts.registers_written & (UINT64_C(1) << r))
      writes[r] = UINT64_MAX;
  }
  int overwrite = !strcmp(s->name, "mov") || !strcmp(s->name, "load") ||
                  !strcmp(s->name, "lea") || !strcmp(s->name, "movzx") ||
                  !strcmp(s->name, "movsx") || !strcmp(s->name, "movsxd") ||
                  !strncmp(s->name, "set", 3);
  int zeroing =
      count == 2 && (!strcmp(s->name, "xor") || !strcmp(s->name, "sub")) &&
      operands[0].kind == NT_X64_REG && operands[1].kind == NT_X64_REG &&
      operands[0].reg == operands[1].reg &&
      operands[0].width == operands[1].width &&
      operands[0].high8 == operands[1].high8;
  for (size_t i = 0; i < count; i++) {
    const NtFExpr *x = &EX(g->p, ids[i]);
    if (operands[i].kind == NT_X64_REG) {
      unsigned r = operands[i].reg;
      if (!zeroing && !(i == 0 && overwrite))
        reads[r] |= register_read_bits(x->reg);
      if (facts.registers_written & (UINT64_C(1) << r))
        writes[r] = register_bits(x->reg);
    } else if (!(i == 0 && overwrite) && before &&
               !outputs_expression(g, ids[i], before, NULL, 0))
      return 0;
  }
  if (count && (!strcmp(s->name, "mul") || !strcmp(s->name, "div") ||
                !strcmp(s->name, "idiv"))) {
    NtFRegister part = {0, operands[0].width == 8 ? 16 : operands[0].width, 0};
    writes[0] = register_bits(part);
    if (operands[0].width != 8)
      writes[2] = register_bits(part);
  }
  if (!strcmp(s->name, "cwd"))
    writes[2] = UINT16_MAX;
  for (unsigned r = 0; r < 16; r++) {
    if (before && (reads[r] & ~before[r]))
      return 0;
    if (after)
      after[r] |= writes[r];
  }
  return 1;
}
static int fail(Graph *g, unsigned code, NtFSpan span, const char *message) {
  if (!g->failed) {
    g->diagnostic->code = code;
    g->diagnostic->span = span;
    snprintf(g->diagnostic->message, sizeof(g->diagnostic->message), "%s",
             message);
  }
  g->failed = 1;
  return 0;
}
static NtFId pointer_type(const NtFProgram *p, NtFId type) {
  return type && type <= p->type_count && p->types[type - 1].kind == NTF_T_PTR
             ? type
             : 0;
}
static int same_type(const NtFProgram *p, NtFId a, NtFId b, unsigned depth) {
  if (a == b)
    return 1;
  if (!a || !b || a > p->type_count || b > p->type_count || depth > 128)
    return 0;
  const NtFType *x = &p->types[a - 1], *y = &p->types[b - 1];
  return x->kind == y->kind && x->space == y->space && x->bits == y->bits &&
         x->count == y->count &&
         ((!x->element && !y->element) ||
          same_type(p, x->element, y->element, depth + 1));
}
static NtFRegister parameter_register(const NtFProgram *p, const NtFFunction *f,
                                      unsigned index) {
  const NtFVariable *v = &p->variables[f->first_param + index - 1];
  NtFRegister r = v->reg;
  if (r.family < 0 && index < 4) {
    static const int defaults[] = {1, 2, 8, 9};
    r.family = defaults[index];
    r.bits = p->types[v->type - 1].bits;
    if (r.bits == 1)
      r.bits = 8;
  }
  return r;
}
static int writable_value(Graph *, NtFId, const unsigned char *, NtFId,
                          const unsigned char *, unsigned);
static int writable_returns(Graph *g, NtFId block, NtFId owner,
                            const unsigned char *parameters, unsigned depth,
                            unsigned *found) {
  if (g->failed || !block || block > g->p->statement_count || depth > 64)
    return 0;
  size_t steps = 0;
  for (NtFId id = ST(g->p, block).first; id; id = ST(g->p, id).next) {
    if (id > g->p->statement_count || ++steps > g->p->statement_count)
      return 0;
    const NtFStmt *s = &ST(g->p, id);
    if (s->kind == NTF_S_RETURN) {
      (*found)++;
      if (!writable_value(g, s->expression, NULL, owner, parameters, depth + 1))
        return 0;
    }
    if (s->kind == NTF_S_BLOCK &&
        !writable_returns(g, id, owner, parameters, depth + 1, found))
      return 0;
    if (s->then_branch && !writable_returns(g, s->then_branch, owner,
                                            parameters, depth + 1, found))
      return 0;
    if (s->else_branch && !writable_returns(g, s->else_branch, owner,
                                            parameters, depth + 1, found))
      return 0;
  }
  return 1;
}
static int writable_value(Graph *g, NtFId id, const unsigned char *registers,
                          NtFId owner, const unsigned char *parameters,
                          unsigned depth) {
  if (g->failed || !id || id > g->p->expression_count || depth > 64)
    return 0;
  const NtFExpr *x = &EX(g->p, id);
  if (!g->permission_fuel) {
    fail(g, NTF_E_LIMIT, x->span, "write-permission proof budget exhausted");
    return 0;
  }
  g->permission_fuel--;
  if (x->kind == NTF_X_ADDRESS && x->resolved &&
      x->resolved <= g->p->variable_count) {
    const NtFVariable *v = &g->p->variables[x->resolved - 1];
    return v->section && !strcmp(v->section, ".data");
  }
  if (x->kind == NTF_X_REGISTER)
    return registers && x->reg.family >= 0 && x->reg.family < 16 &&
                   x->reg.bits == 64
               ? registers[x->reg.family] != 0
               : 0;
  if (x->kind == NTF_X_NAME && x->resolved &&
      x->resolved <= g->p->variable_count) {
    const NtFVariable *v = &g->p->variables[x->resolved - 1];
    if (!pointer_type(g->p, v->type))
      return 0;
    if (v->direction == NTF_OUT || v->direction == NTF_INOUT)
      return registers && v->reg.family >= 0 && v->reg.family < 16
                 ? registers[v->reg.family] != 0
                 : 0;
    if (v->direction == NTF_IN) {
      if (!parameters)
        return v->function == owner;
      const NtFFunction *f = &g->p->functions[owner - 1];
      return v->function == owner && x->resolved >= f->first_param &&
                     x->resolved < f->first_param + f->param_count
                 ? parameters[x->resolved - f->first_param] != 0
                 : 0;
    }
    if (v->direction == NTF_CONST || v->direction == NTF_LOCAL) {
      if (v->direction == NTF_LOCAL && v->function &&
          g->p->functions[v->function - 1].high_level_expressions)
        return 0;
      return writable_value(g, v->initializer, NULL, owner, parameters,
                            depth + 1);
    }
  }
  if (x->kind == NTF_X_CALL && x->resolved &&
      x->resolved <= g->p->function_count) {
    NtFId target = x->resolved;
    const NtFFunction *f = &g->p->functions[target - 1];
    if (f->param_count > 64 || !pointer_type(g->p, f->return_type))
      return 0;
    unsigned char arguments[64] = {0};
    NtFId arg = x->first_arg;
    for (uint32_t i = 0; i < f->param_count; i++) {
      const NtFVariable *v = &g->p->variables[f->first_param + i - 1];
      if (v->direction == NTF_OUT)
        continue;
      if (!arg)
        return 0;
      arguments[i] = (unsigned char)writable_value(g, arg, registers, owner,
                                                   parameters, depth + 1);
      arg = EX(g->p, arg).next;
    }
    if (f->imported) {
      if (!f->resolved || f->resolved > g->p->function_count)
        return 0;
      target = f->resolved;
      f = &g->p->functions[target - 1];
    }
    unsigned found = 0;
    return f->body &&
           writable_returns(g, f->body, target, arguments, depth + 1, &found) &&
           found;
  }
  return 0;
}
static int memory_writable(Graph *g, NtFId id, const unsigned char *registers) {
  const NtFExpr *x = &EX(g->p, id);
  if (x->memory.symbol && x->resolved && x->resolved <= g->p->variable_count) {
    const NtFVariable *v = &g->p->variables[x->resolved - 1];
    return v->section && !strcmp(v->section, ".data");
  }
  int base = x->memory.base.family;
  if (base < 0 && x->memory.index.family >= 0 && x->memory.scale == 1)
    base = x->memory.index.family;
  return registers && base >= 0 && base < 16 ? registers[base] != 0 : 0;
}
static NtFId expression_pointer(Graph *g, NtFId id, const NtFId pointers[16],
                                unsigned depth) {
  if (!id || id > g->p->expression_count || depth > 128)
    return 0;
  const NtFExpr *x = &EX(g->p, id);
  if (x->kind == NTF_X_ADDRESS)
    return pointer_type(g->p, x->type);
  if (x->kind == NTF_X_REGISTER)
    return x->reg.bits == 64 && x->reg.family >= 0 && x->reg.family < 16
               ? pointers[x->reg.family]
               : 0;
  if (x->kind == NTF_X_NAME && x->resolved &&
      x->resolved <= g->p->variable_count) {
    const NtFVariable *v = &g->p->variables[x->resolved - 1];
    if (v->direction == NTF_OUT || v->direction == NTF_INOUT)
      return v->reg.family >= 0 && v->reg.family < 16 ? pointers[v->reg.family]
                                                      : 0;
    return pointer_type(g->p, v->type);
  }
  if (x->kind == NTF_X_CALL && x->resolved &&
      x->resolved <= g->p->function_count)
    return pointer_type(g->p, g->p->functions[x->resolved - 1].return_type);
  return 0;
}
static int memory_pointer(Graph *g, NtFId id, const NtFId pointers[16]) {
  const NtFExpr *x = &EX(g->p, id);
  NtFId annotation = pointer_type(g->p, x->type);
  if (!annotation)
    return fail(g, NTF_E_MEMORY, x->span,
                "memory address lacks a pointer type");
  const NtFType *want = &g->p->types[annotation - 1];
  NtFId element = 0;
  NtFSpace space = NTF_SPACE_NONE;
  if (x->memory.segment)
    return fail(g, NTF_E_MEMORY, x->span,
                "segment base provenance requires a registered frame contract");
  if (x->memory.symbol && x->resolved && x->resolved <= g->p->variable_count &&
      g->p->variables[x->resolved - 1].direction == NTF_DATA) {
    element = g->p->variables[x->resolved - 1].type;
    space = NTF_SPACE_USER;
  } else {
    int base = x->memory.base.family, index = x->memory.index.family;
    if (base < 0 && index >= 0 && x->memory.scale == 1) {
      base = index;
      index = -1;
    }
    if (base < 0 || base >= 16 || !pointer_type(g->p, pointers[base]))
      return fail(
          g, NTF_E_MEMORY, x->span,
          "memory annotation cannot manufacture register pointer provenance");
    if (index >= 0 && (index >= 16 || pointer_type(g->p, pointers[index])))
      return fail(g, NTF_E_MEMORY, x->span,
                  "memory index must be a scalar, not a second pointer");
    const NtFType *actual = &g->p->types[pointers[base] - 1];
    space = actual->space;
    element = actual->element;
  }
  if (element && element <= g->p->type_count &&
      g->p->types[element - 1].kind == NTF_T_ARRAY &&
      !same_type(g->p, element, want->element, 0))
    element = g->p->types[element - 1].element;
  if (space != want->space || !same_type(g->p, element, want->element, 0))
    return fail(
        g, NTF_E_MEMORY, x->span,
        "memory annotation differs from proven pointer space or element type");
  return 1;
}
static int pointer_expression(Graph *g, NtFId id, NtFId expected,
                              const NtFId pointers[16], unsigned depth) {
  if (!id)
    return 1;
  if (id > g->p->expression_count || depth > 128)
    return fail(g, NTF_E_FLOW, (NtFSpan){0}, "invalid pointer expression");
  const NtFExpr *x = &EX(g->p, id);
  NtFId actual = expression_pointer(g, id, pointers, 0);
  if (actual == UINT32_MAX)
    actual = 0;
  if (expected) {
    NtFId wanted = pointer_type(g->p, expected);
    if ((wanted && !same_type(g->p, wanted, actual, 0)) || (!wanted && actual))
      return fail(g, NTF_E_TYPE, x->span,
                  "value pointer provenance does not match its required type");
  }
  if (x->kind == NTF_X_NAME && x->resolved &&
      x->resolved <= g->p->variable_count) {
    const NtFVariable *v = &g->p->variables[x->resolved - 1];
    if ((v->direction == NTF_OUT || v->direction == NTF_INOUT) &&
        pointer_type(g->p, v->type) && !same_type(g->p, v->type, actual, 0))
      return fail(g, NTF_E_TYPE, x->span,
                  "live pointer binding has lost its declared provenance");
  }
  if (x->kind == NTF_X_REGISTER && actual && g->annotate)
    g->annotate->expressions[id - 1].type = actual;
  if (x->kind == NTF_X_MEMORY)
    return memory_pointer(g, id, pointers);
  if (x->kind == NTF_X_CALL && x->resolved &&
      x->resolved <= g->p->function_count) {
    const NtFFunction *callee = &g->p->functions[x->resolved - 1];
    NtFId arg = x->first_arg;
    for (uint32_t i = 0; i < callee->param_count; i++) {
      const NtFVariable *v = &g->p->variables[callee->first_param + i - 1];
      if (v->direction == NTF_OUT)
        continue;
      if (!arg || !pointer_expression(g, arg, v->type, pointers, depth + 1))
        return 0;
      if (pointer_type(g->p, v->type) &&
          (callee->effects.writes & (1u << g->p->types[v->type - 1].space)) &&
          !writable_value(g, arg, g->current_writable, g->function, NULL, 0))
        return fail(g, NTF_E_MEMORY, EX(g->p, arg).span,
                    "callee may write through a pointer without proven write "
                    "permission");
      arg = EX(g->p, arg).next;
    }
    return 1;
  }
  if (x->kind == NTF_X_BINARY || x->kind == NTF_X_UNARY) {
    NtFId left = expression_pointer(g, x->left, pointers, 0),
          right = expression_pointer(g, x->right, pointers, 0);
    int equality =
        x->kind == NTF_X_BINARY && (x->op == NTF_OP_EQ || x->op == NTF_OP_NE);
    if ((left || right) && (!equality || !same_type(g->p, left, right, 0)))
      return fail(g, NTF_E_TYPE, x->span,
                  "pointer arithmetic requires a typed address operation");
    return pointer_expression(g, x->left, left, pointers, depth + 1) &&
           pointer_expression(g, x->right, right, pointers, depth + 1);
  }
  return 1;
}
static void pointer_calls(Graph *g, NtFId id, NtFId pointers[16],
                          unsigned depth) {
  if (!id || id > g->p->expression_count || depth > 128)
    return;
  const NtFExpr *x = &EX(g->p, id);
  pointer_calls(g, x->left, pointers, depth + 1);
  pointer_calls(g, x->right, pointers, depth + 1);
  size_t steps = 0;
  for (NtFId a = x->first_arg; a && ++steps <= g->p->expression_count;
       a = EX(g->p, a).next)
    pointer_calls(g, a, pointers, depth + 1);
  if (x->kind == NTF_X_CALL && x->resolved &&
      x->resolved <= g->p->function_count) {
    const NtFFunction *f = &g->p->functions[x->resolved - 1];
    for (unsigned r = 0; r < 16; r++)
      if (f->clobbers & (UINT64_C(1) << r))
        pointers[r] = 0;
    pointers[0] = pointer_type(g->p, f->return_type);
    for (uint32_t i = 0; i < f->param_count; i++) {
      const NtFVariable *v = &g->p->variables[f->first_param + i - 1];
      if (v->direction == NTF_OUT || v->direction == NTF_INOUT) {
        NtFRegister r = parameter_register(g->p, f, i);
        if (r.family >= 0 && r.family < 16)
          pointers[r.family] = pointer_type(g->p, v->type);
      }
    }
  }
}
static int named(const char *name, const char *list) {
  size_t n = strlen(name);
  for (const char *s = list; *s;) {
    const char *end = strchr(s, '|');
    size_t length = end ? (size_t)(end - s) : strlen(s);
    if (n == length && !memcmp(s, name, n))
      return 1;
    if (!end)
      break;
    s = end + 1;
  }
  return 0;
}
static void permission_calls(Graph *g, NtFId id, const unsigned char *entry,
                             unsigned char *after, unsigned depth) {
  if (!id || id > g->p->expression_count || depth > 64)
    return;
  const NtFExpr *x = &EX(g->p, id);
  permission_calls(g, x->left, entry, after, depth + 1);
  permission_calls(g, x->right, entry, after, depth + 1);
  size_t steps = 0;
  for (NtFId a = x->first_arg; a && ++steps <= g->p->expression_count;
       a = EX(g->p, a).next)
    permission_calls(g, a, entry, after, depth + 1);
  if (x->kind == NTF_X_CALL && x->resolved &&
      x->resolved <= g->p->function_count) {
    const NtFFunction *f = &g->p->functions[x->resolved - 1];
    for (unsigned r = 0; r < 16; r++)
      if (f->clobbers & (UINT64_C(1) << r))
        after[r] = 0;
    after[0] =
        (unsigned char)writable_value(g, id, entry, g->function, NULL, 0);
    for (uint32_t i = 0; i < f->param_count; i++) {
      const NtFVariable *v = &g->p->variables[f->first_param + i - 1];
      if (v->direction == NTF_OUT || v->direction == NTF_INOUT) {
        NtFRegister r = parameter_register(g->p, f, i);
        if (r.family >= 0 && r.family < 16)
          after[r.family] = 0;
      }
    }
  }
}
static int permission_instruction(Graph *g, const NtFStmt *s,
                                  const unsigned char *before,
                                  unsigned char *after, const NtFId *types,
                                  int check) {
  if (branch(s))
    return 1;
  NtFId a = s->expression, b = a ? EX(g->p, a).next : 0;
  if (check)
    for (NtFId id = a; id; id = EX(g->p, id).next)
      if (EX(g->p, id).kind == NTF_X_MEMORY) {
        int writing =
            !strcmp(s->name, "xchg") || named(s->name, "sgdt|sidt") ||
            (id == a &&
             (!strncmp(s->name, "set", 3) ||
              named(s->name, "store|mov|add|adc|sub|sbb|and|or|xor|inc|dec|neg|"
                             "not|shl|shr|sar|rol|ror|bts|btr|btc|pop")));
        if (writing && !memory_writable(g, id, before))
          return fail(g, NTF_E_MEMORY, EX(g->p, id).span,
                      "store requires proven write permission; readonly or "
                      "unknown pointer");
      }
  if (!after)
    return 1;
  for (unsigned r = 0; r < 16; r++)
    if (!types[r])
      after[r] = 0;
  if (a && b && EX(g->p, a).kind == NTF_X_REGISTER &&
      EX(g->p, a).reg.family < 16 && EX(g->p, a).reg.bits == 64) {
    unsigned r = (unsigned)EX(g->p, a).reg.family;
    if (!strcmp(s->name, "lea"))
      after[r] = (unsigned char)memory_writable(g, b, before);
    else if (named(s->name, "mov|load|xchg")) {
      if (EX(g->p, b).kind == NTF_X_MEMORY) {
        const NtFExpr *memory = &EX(g->p, b);
        after[r] = 0;
        if (memory->memory.symbol && memory->resolved &&
            memory->resolved <= g->p->variable_count) {
          const NtFVariable *v = &g->p->variables[memory->resolved - 1];
          if (v->section && !strcmp(v->section, ".rdata"))
            after[r] = (unsigned char)writable_value(g, v->initializer, NULL,
                                                     g->function, NULL, 0);
        }
      } else
        after[r] =
            (unsigned char)writable_value(g, b, before, g->function, NULL, 0);
    }
  }
  if (!strcmp(s->name, "xchg") && a && b &&
      EX(g->p, b).kind == NTF_X_REGISTER && EX(g->p, b).reg.bits == 64)
    after[EX(g->p, b).reg.family] =
        (unsigned char)writable_value(g, a, before, g->function, NULL, 0);
  return 1;
}
static int pointer_instruction(Graph *g, const NtFStmt *s,
                               const NtFId before[16], NtFId *after,
                               int check) {
  if (branch(s))
    return 1;
  NtFId a = s->expression, b = a ? EX(g->p, a).next : 0;
  if (check)
    for (NtFId e = a; e; e = EX(g->p, e).next)
      if (EX(g->p, e).kind == NTF_X_MEMORY && !memory_pointer(g, e, before))
        return 0;
  int move = named(s->name, "mov|load|movzx|movsx|movsxd|lea");
  if (check && a && b && EX(g->p, a).kind == NTF_X_MEMORY &&
      named(s->name, "mov|store|xchg")) {
    NtFId element = g->p->types[EX(g->p, a).type - 1].element;
    if (!pointer_expression(g, b, element, before, 0))
      return 0;
  }
  if (check && a && b && !strcmp(s->name, "xchg") &&
      EX(g->p, b).kind == NTF_X_MEMORY) {
    NtFId element = g->p->types[EX(g->p, b).type - 1].element;
    if (!pointer_expression(g, a, element, before, 0))
      return 0;
  }
  if (check && a && b && named(s->name, "mov|xchg") &&
      EX(g->p, a).kind == NTF_X_REGISTER)
    if (!pointer_expression(g, b, 0, before, 0))
      return 0;
  if (!after)
    return 1;
  uint64_t writes = 0;
  if (a && EX(g->p, a).kind == NTF_X_REGISTER &&
      !named(s->name, "cmp|test|bt|store|push|out|mul|div|idiv|ltr|call|jmp"))
    writes |= UINT64_C(1) << EX(g->p, a).reg.family;
  if (!strcmp(s->name, "xchg") && b && EX(g->p, b).kind == NTF_X_REGISTER)
    writes |= UINT64_C(1) << EX(g->p, b).reg.family;
  if (named(s->name, "cpuid"))
    writes |= 15;
  if (named(s->name, "rdtsc|rdmsr|mul|div|idiv"))
    writes |= 5;
  if (named(s->name, "cbw|cwde|cdqe"))
    writes |= 1;
  if (named(s->name, "cwd|cdq|cqo"))
    writes |= 4;
  if (named(s->name, "syscall|call"))
    writes = UINT16_MAX;
  for (unsigned r = 0; r < 16; r++)
    if (writes & (UINT64_C(1) << r))
      after[r] = 0;
  if (a && b && EX(g->p, a).kind == NTF_X_REGISTER &&
      EX(g->p, a).reg.bits == 64 && EX(g->p, a).reg.family < 16) {
    NtFId value = 0;
    if (move && !strcmp(s->name, "lea"))
      value = pointer_type(g->p, EX(g->p, b).type);
    else if ((move || !strcmp(s->name, "xchg")) &&
             EX(g->p, b).kind == NTF_X_MEMORY)
      value = pointer_type(g->p, g->p->types[EX(g->p, b).type - 1].element);
    else if (!strcmp(s->name, "mov") || !strcmp(s->name, "xchg"))
      value = expression_pointer(g, b, before, 0);
    after[EX(g->p, a).reg.family] = value;
  }
  if (!strcmp(s->name, "xchg") && a && b &&
      EX(g->p, b).kind == NTF_X_REGISTER && EX(g->p, b).reg.bits == 64)
    after[EX(g->p, b).reg.family] = expression_pointer(g, a, before, 0);
  return 1;
}
static int valid_statement(Graph *g, NtFId id) {
  return id && id <= g->p->statement_count;
}
static int conditional_branch(const char *name) {
  static const char *names[] = {"je", "jne", "jb", "jbe", "ja", "jae",
                                "jl", "jle", "jg", "jge", "jo", "jno",
                                "js", "jns", "jp", "jnp"};
  if (!name)
    return 0;
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    if (!strcmp(name, names[i]))
      return 1;
  return 0;
}
static int branch(const NtFStmt *s) {
  return s->kind == NTF_S_INSTRUCTION && s->name &&
         (!strcmp(s->name, "jmp") || conditional_branch(s->name));
}
static int build_block(Graph *, NtFId, NtFId, unsigned);
static int build_statement(Graph *g, NtFId id, NtFId next, unsigned depth) {
  if (!valid_statement(g, id))
    return fail(g, NTF_E_FLOW, (NtFSpan){0}, "invalid CFG statement ID");
  const NtFStmt *s = &ST(g->p, id);
  Node *n = &g->nodes[id];
  if (n->built)
    return fail(g, NTF_E_FLOW, s->span,
                "AST statement is reused or cyclic outside a branch");
  if (s->kind == NTF_S_BLOCK)
    return build_block(g, id, next, depth + 1);
  n->built = 1;
  if (s->kind == NTF_S_RETURN)
    return 1;
  if (s->kind == NTF_S_IF) {
    if (!valid_statement(g, s->then_branch))
      return fail(g, NTF_E_FLOW, s->span, "if has no valid then block");
    const NtFExpr *condition =
        s->condition && s->condition <= g->p->expression_count
            ? &EX(g->p, s->condition)
            : NULL;
    int literal_boolean =
        condition && condition->kind == NTF_X_BOOL && condition->value <= 1;
    if (!literal_boolean || condition->value)
      n->edge[n->count++] = s->then_branch;
    if (!literal_boolean || !condition->value)
      n->edge[n->count++] = s->else_branch ? s->else_branch : next;
    if (!build_block(g, s->then_branch, next, depth + 1))
      return 0;
    if (s->else_branch && !build_block(g, s->else_branch, next, depth + 1))
      return 0;
    return 1;
  }
  if (branch(s)) {
    if (!valid_statement(g, s->variable))
      return fail(g, NTF_E_FLOW, s->span, "invalid branch target ID");
    n->edge[n->count++] = s->variable;
    if (conditional_branch(s->name))
      n->edge[n->count++] = next;
    return 1;
  }
  if (s->terminal)
    return 1;
  n->edge[n->count++] = next;
  return 1;
}
static int build_block(Graph *g, NtFId id, NtFId next, unsigned depth) {
  if (!valid_statement(g, id))
    return fail(g, NTF_E_FLOW, (NtFSpan){0}, "invalid CFG block ID");
  const NtFStmt *block = &ST(g->p, id);
  if (depth > 128)
    return fail(g, NTF_E_LIMIT, block->span, "CFG nesting limit exceeded");
  if (block->kind != NTF_S_BLOCK || g->nodes[id].built)
    return fail(g, NTF_E_FLOW, block->span, "invalid or repeated CFG block");
  g->nodes[id].built = 1;
  g->nodes[id].count = 1;
  g->nodes[id].edge[0] = block->first ? block->first : next;
  size_t steps = 0;
  for (NtFId child = block->first; child; child = ST(g->p, child).next) {
    if (!valid_statement(g, child) || ++steps > g->p->statement_count)
      return fail(g, NTF_E_FLOW, block->span,
                  "invalid lexical statement chain");
    NtFId following = ST(g->p, child).next ? ST(g->p, child).next : next;
    if (!build_statement(g, child, following, depth))
      return 0;
  }
  return 1;
}
static void enqueue(Graph *g, NtFId id) {
  if (g->nodes[id].queued)
    return;
  g->nodes[id].queued = 1;
  g->queue[g->tail] = id;
  g->tail = (g->tail + 1) % g->rows;
}
static NtFId dequeue(Graph *g) {
  NtFId id = g->queue[g->head];
  g->head = (g->head + 1) % g->rows;
  g->nodes[id].queued = 0;
  return id;
}
static int walk_expression(Graph *g, NtFId id, const uint64_t *state,
                           uint32_t *clobbers, unsigned depth, size_t *steps) {
  if (!id)
    return 1;
  if (id > g->p->expression_count || depth > 128 ||
      ++*steps > g->p->expression_count * 4 + 16)
    return fail(g, NTF_E_FLOW, (NtFSpan){0},
                "invalid or cyclic expression in CFG");
  const NtFExpr *x = &EX(g->p, id);
  NtFId variable = x->kind == NTF_X_NAME     ? x->resolved
                   : x->kind == NTF_X_MEMORY ? x->memory.provenance
                                             : 0;
  if (variable) {
    if (variable > g->p->variable_count)
      return fail(g, NTF_E_FLOW, x->span, "invalid resolved local variable");
    uint32_t bit = g->local_index[variable - 1];
    if (state && bit != UINT32_MAX &&
        !(state[bit / 64] & (UINT64_C(1) << (bit % 64))))
      return fail(g, NTF_E_FLOW, x->span,
                  "local variable is not initialized on every incoming path");
  }
  if (x->kind == NTF_X_CALL) {
    if (!x->resolved || x->resolved > g->p->function_count)
      return fail(g, NTF_E_FLOW, x->span, "unresolved call in CFG");
    if (g->p->functions[x->resolved - 1].clobbers & (UINT64_C(1) << 17))
      *clobbers = UINT32_MAX;
  }
  if (!walk_expression(g, x->left, state, clobbers, depth + 1, steps) ||
      !walk_expression(g, x->right, state, clobbers, depth + 1, steps))
    return 0;
  for (NtFId arg = x->first_arg; arg;) {
    if (!walk_expression(g, arg, state, clobbers, depth + 1, steps))
      return 0;
    arg = EX(g->p, arg).next;
  }
  return 1;
}
static int statement_expressions(Graph *g, NtFId id, const uint64_t *state,
                                 uint32_t *clobbers) {
  const NtFStmt *s = &ST(g->p, id);
  size_t steps = 0;
  if (branch(s))
    return 1; /* label operand is not a local value */
  if (s->kind == NTF_S_INSTRUCTION) {
    for (NtFId x = s->expression; x;) {
      if (!walk_expression(g, x, state, clobbers, 0, &steps))
        return 0;
      x = EX(g->p, x).next;
    }
    return 1;
  }
  return walk_expression(g, s->expression, state, clobbers, 0, &steps) &&
         walk_expression(g, s->condition, state, clobbers, 0, &steps);
}
static int verify_graph(const NtFProgram *p, NtFId function, NtFDiagnostic *d,
                        NtFProgram *annotate) {
  if (!d)
    return 0;
  memset(d, 0, sizeof(*d));
  Graph g = {0};
  g.p = p;
  g.permission_fuel = 1000000;
  g.annotate = annotate;
  g.function = function;
  g.diagnostic = d;
  if (!p || !function || function > p->function_count || !p->functions ||
      !p->statements)
    return fail(&g, NTF_E_FLOW, (NtFSpan){0},
                "invalid function supplied to CFG verifier");
  const NtFFunction *f = &p->functions[function - 1];
  if (f->imported)
    return 1;
  for (uint32_t i = 0; i < f->param_count; i++) {
    const NtFVariable *v = &p->variables[f->first_param + i - 1];
    if (v->direction != NTF_OUT && v->direction != NTF_INOUT)
      continue;
    if (v->reg.family <= 0 || v->reg.family >= 16 || v->reg.family == 4 ||
        v->reg.family == 5)
      return fail(&g, NTF_E_ABI, v->span,
                  "output binding uses a reserved register");
    NtFTypeKind kind = p->types[v->type - 1].kind;
    if ((kind < NTF_T_U8 || kind > NTF_T_I64) && kind != NTF_T_PTR)
      return fail(&g, NTF_E_UNSUPPORTED, v->span,
                  "bool output binding requires a canonical value proof");
    g.output_bindings = 1;
    g.required_outputs[v->reg.family] |= register_read_bits(v->reg);
  }
  if (p->statement_count > 262144 || p->variable_count > 262144)
    return fail(&g, NTF_E_LIMIT, f->span, "CFG size limit exceeded");
  g.rows = p->statement_count + 1;
  g.local_index =
      malloc((p->variable_count ? p->variable_count : 1) * sizeof(uint32_t));
  if (!g.local_index)
    return fail(&g, NTF_E_OOM, f->span, "CFG local-index allocation failed");
  for (size_t i = 0; i < p->variable_count; i++) {
    g.local_index[i] = UINT32_MAX;
    if (p->variables[i].function == function &&
        p->variables[i].direction == NTF_LOCAL)
      g.local_index[i] = (uint32_t)g.local_count++;
  }
  g.words = (g.local_count + 63) / 64;
  if (!g.words)
    g.words = 1;
  size_t overhead =
      (p->variable_count ? p->variable_count : 1) * sizeof(uint32_t) +
      g.words * sizeof(uint64_t);
  if (overhead >= CFG_BUDGET ||
      g.rows > (CFG_BUDGET - overhead) / (sizeof(Node) + sizeof(NtFId) +
                                          g.words * sizeof(uint64_t))) {
    fail(&g, NTF_E_LIMIT, f->span,
         "CFG dataflow exceeds 64MiB analysis budget");
    goto done;
  }
  g.nodes = calloc(g.rows, sizeof(Node));
  g.queue = malloc(g.rows * sizeof(NtFId));
  g.states = malloc(g.rows * g.words * sizeof(uint64_t));
  g.temporary = malloc(g.words * sizeof(uint64_t));
  if (!g.nodes || !g.queue || !g.states || !g.temporary) {
    fail(&g, NTF_E_OOM, f->span, "CFG dataflow allocation failed");
    goto done;
  }
  if (!build_block(&g, f->body, 0, 0))
    goto done;
  for (size_t id = 1; id < g.rows; id++)
    if (g.nodes[id].built)
      for (unsigned edge = 0; edge < g.nodes[id].count; edge++) {
        NtFId target = g.nodes[id].edge[edge];
        if (target && (target >= g.rows || !g.nodes[target].built)) {
          fail(&g, NTF_E_FLOW, ST(p, id).span,
               "branch leaves its source function");
          goto done;
        }
      }
  /* Reachability first: zero is the explicit function-fallthrough sink. */
  enqueue(&g, f->body);
  g.nodes[f->body].reachable = 1;
  while (g.head != g.tail) {
    NtFId id = dequeue(&g);
    for (unsigned i = 0; i < g.nodes[id].count; i++) {
      NtFId target = g.nodes[id].edge[i];
      if (!target) {
        fail(&g, NTF_E_FLOW, ST(p, id).span,
             "reachable path falls out of function");
        goto done;
      }
      if (!g.nodes[target].reachable) {
        g.nodes[target].reachable = 1;
        enqueue(&g, target);
      }
    }
  }
  /* Must analysis starts at TOP, except the entry which has no defined flags
   * and no initialized locals. Intersections decrease monotonically. */
  memset(g.states, 0xff, g.rows * g.words * sizeof(uint64_t));
  for (size_t i = 1; i < g.rows; i++) {
    g.nodes[i].flags = UINT32_MAX;
    for (unsigned r = 0; r < 16; r++) {
      g.nodes[i].initialized[r] = UINT64_MAX;
      g.nodes[i].pointers[r] = UINT32_MAX;
      g.nodes[i].writable[r] = 1;
    }
  }
  for (unsigned r = 0; r < 16; r++) {
    g.nodes[f->body].pointers[r] = 0;
    g.nodes[f->body].writable[r] = 0;
  }
  for (uint32_t i = 0; i < f->param_count; i++) {
    const NtFVariable *v = &p->variables[f->first_param + i - 1];
    NtFRegister binding = parameter_register(p, f, i);
    if (v->direction != NTF_OUT && binding.family >= 0 && binding.family < 16 &&
        binding.bits == 64) {
      g.nodes[f->body].pointers[binding.family] = pointer_type(p, v->type);
      g.nodes[f->body].writable[binding.family] =
          (unsigned char)(pointer_type(p, v->type) != 0);
    }
    if (v->direction == NTF_OUT)
      g.nodes[f->body].initialized[v->reg.family] &=
          ~register_read_bits(v->reg);
  }
  g.nodes[f->body].flags = 0;
  memset(g.states + f->body * g.words, 0, g.words * sizeof(uint64_t));
  for (size_t id = 1; id < g.rows; id++)
    if (g.nodes[id].reachable)
      enqueue(&g, (NtFId)id);
  while (g.head != g.tail) {
    NtFId id = dequeue(&g);
    const NtFStmt *s = &ST(p, id);
    uint32_t killed = 0;
    if (!statement_expressions(&g, id, NULL, &killed))
      goto done;
    uint32_t flags = g.nodes[id].flags & ~killed;
    uint64_t initialized[16];
    memcpy(initialized, g.nodes[id].initialized, sizeof(initialized));
    NtFId pointers[16];
    memcpy(pointers, g.nodes[id].pointers, sizeof(pointers));
    unsigned char writable[16];
    memcpy(writable, g.nodes[id].writable, sizeof(writable));
    if (s->kind == NTF_S_INSTRUCTION) {
      if (!pointer_instruction(&g, s, g.nodes[id].pointers, pointers, 0))
        goto done;
      if (!permission_instruction(&g, s, g.nodes[id].writable, writable,
                                  pointers, 0))
        goto done;
    } else {
      pointer_calls(&g, s->expression, pointers, 0);
      pointer_calls(&g, s->condition, pointers, 0);
      permission_calls(&g, s->expression, g.nodes[id].writable, writable, 0);
      permission_calls(&g, s->condition, g.nodes[id].writable, writable, 0);
    }
    if (g.output_bindings) {
      int ok =
          s->kind == NTF_S_INSTRUCTION
              ? outputs_instruction(&g, s, NULL, initialized)
              : outputs_expression(&g, s->expression, NULL, initialized, 0) &&
                    outputs_expression(&g, s->condition, NULL, initialized, 0);
      if (!ok) {
        fail(&g, NTF_E_FLOW, s->span, "cannot prove output register writes");
        goto done;
      }
    }
    if (s->kind == NTF_S_INSTRUCTION)
      flags = (flags & ~s->flags_undefined) | s->flags_written;
    memcpy(g.temporary, g.states + id * g.words, g.words * sizeof(uint64_t));
    if (s->kind == NTF_S_LET) {
      if (!s->variable || s->variable > p->variable_count) {
        fail(&g, NTF_E_FLOW, s->span, "let has invalid variable ID");
        goto done;
      }
      uint32_t bit = g.local_index[s->variable - 1];
      if (bit != UINT32_MAX)
        g.temporary[bit / 64] |= UINT64_C(1) << (bit % 64);
    }
    for (unsigned edge = 0; edge < g.nodes[id].count; edge++) {
      NtFId target = g.nodes[id].edge[edge];
      Node *to = &g.nodes[target];
      uint32_t incoming = to->flags & flags;
      int changed = incoming != to->flags;
      to->flags = incoming;
      for (unsigned r = 0; r < 16; r++) {
        NtFId value = to->pointers[r];
        if (value == UINT32_MAX)
          value = pointers[r];
        else if (pointers[r] != UINT32_MAX &&
                 !same_type(p, value, pointers[r], 0))
          value = 0;
        changed |= value != to->pointers[r];
        to->pointers[r] = value;
        unsigned char permission =
            (unsigned char)(to->writable[r] & writable[r]);
        changed |= permission != to->writable[r];
        to->writable[r] = permission;
      }
      if (g.output_bindings)
        for (unsigned r = 0; r < 16; r++) {
          uint64_t value = to->initialized[r] & initialized[r];
          changed |= value != to->initialized[r];
          to->initialized[r] = value;
        }
      for (size_t word = 0; word < g.words; word++) {
        uint64_t *value = &g.states[target * g.words + word];
        uint64_t incoming_word = *value & g.temporary[word];
        changed |= incoming_word != *value;
        *value = incoming_word;
      }
      if (changed)
        enqueue(&g, target);
    }
  }
  for (size_t id = 1; id < g.rows; id++)
    if (g.nodes[id].reachable) {
      const NtFStmt *s = &ST(p, id);
      g.current_writable = g.nodes[id].writable;
      if (s->kind == NTF_S_INSTRUCTION &&
          (s->flags_read & ~g.nodes[id].flags)) {
        fail(&g, NTF_E_CONTRACT, s->span,
             "instruction reads flags not defined on every incoming path");
        goto done;
      }
      uint32_t ignored = 0;
      if (!statement_expressions(&g, (NtFId)id, g.states + id * g.words,
                                 &ignored))
        goto done;
      if (s->kind == NTF_S_INSTRUCTION) {
        if (!pointer_instruction(&g, s, g.nodes[id].pointers, NULL, 1))
          goto done;
        if (!permission_instruction(&g, s, g.nodes[id].writable, NULL, NULL, 1))
          goto done;
      } else {
        NtFId expected = s->kind == NTF_S_RETURN ? f->return_type
                         : s->kind == NTF_S_LET && s->variable
                             ? p->variables[s->variable - 1].type
                             : 0;
        if (!pointer_expression(&g, s->expression, expected,
                                g.nodes[id].pointers, 0) ||
            !pointer_expression(&g, s->condition, 0, g.nodes[id].pointers, 0))
          goto done;
      }
      if (g.output_bindings) {
        int ok = s->kind == NTF_S_INSTRUCTION
                     ? outputs_instruction(&g, s, g.nodes[id].initialized, NULL)
                     : outputs_expression(&g, s->expression,
                                          g.nodes[id].initialized, NULL, 0) &&
                           outputs_expression(&g, s->condition,
                                              g.nodes[id].initialized, NULL, 0);
        if (!ok) {
          fail(&g, NTF_E_FLOW, s->span,
               "output register is read before complete assignment");
          goto done;
        }
        if (s->kind == NTF_S_RETURN)
          for (unsigned r = 0; r < 16; r++)
            if (g.required_outputs[r] & ~g.nodes[id].initialized[r]) {
              fail(&g, NTF_E_FLOW, s->span,
                   "output register is not assigned on every return path");
              goto done;
            }
        if (s->kind == NTF_S_RETURN)
          for (uint32_t i = 0; i < f->param_count; i++) {
            const NtFVariable *v = &p->variables[f->first_param + i - 1];
            if ((v->direction == NTF_OUT || v->direction == NTF_INOUT) &&
                pointer_type(p, v->type) &&
                !same_type(p, v->type, g.nodes[id].pointers[v->reg.family],
                           0)) {
              fail(&g, NTF_E_TYPE, s->span,
                   "output pointer does not retain its declared provenance");
              goto done;
            }
          }
      }
      if (s->kind == NTF_S_RETURN && f->return_type &&
          f->return_type <= p->type_count &&
          p->types[f->return_type - 1].kind == NTF_T_NEVER) {
        fail(&g, NTF_E_FLOW, s->span,
             "never function has a reachable normal return");
        goto done;
      }
    }
done:
  free(g.nodes);
  free(g.queue);
  free(g.local_index);
  free(g.states);
  free(g.temporary);
  return !g.failed;
}
int nt_conformance_verify(const NtFProgram *p, NtFId function,
                          NtFDiagnostic *d) {
  return verify_graph(p, function, d, NULL);
}
int nt_conformance_verify_typed(NtFProgram *p, NtFId function,
                                NtFDiagnostic *d) {
  return verify_graph(p, function, d, p);
}
