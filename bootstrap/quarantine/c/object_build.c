#include "codegen.h"
#include "object.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char *bytes;
  size_t size, capacity;
  int failed;
} Text;
static void add(Text *t, const char *format, ...) {
  if (t->failed)
    return;
  char chunk[1024];
  va_list args;
  va_start(args, format);
  int count = vsnprintf(chunk, sizeof(chunk), format, args);
  va_end(args);
  if (count < 0 || (size_t)count >= sizeof(chunk) ||
      t->size + (size_t)count + 1 > 65536) {
    t->failed = 1;
    return;
  }
  size_t need = t->size + (size_t)count + 1;
  if (need > t->capacity) {
    size_t capacity = t->capacity ? t->capacity : 256;
    while (capacity < need)
      capacity *= 2;
    char *p = realloc(t->bytes, capacity);
    if (!p) {
      t->failed = 1;
      return;
    }
    t->bytes = p;
    t->capacity = capacity;
  }
  memcpy(t->bytes + t->size, chunk, (size_t)count + 1);
  t->size += (size_t)count;
}
static void type_text(Text *t, const NtFProgram *p, NtFId id, unsigned depth) {
  if (!id || id > p->type_count || depth > 64) {
    t->failed = 1;
    return;
  }
  const NtFType *type = p->types + id - 1;
  if (type->kind == NTF_T_PTR) {
    add(t, "ptr<%u,", (unsigned)type->space);
    type_text(t, p, type->element, depth + 1);
    add(t, ">");
  } else if (type->kind == NTF_T_ARRAY) {
    add(t, "array<");
    type_text(t, p, type->element, depth + 1);
    add(t, ",%" PRIu64 ">", type->count);
  } else if (type->kind == NTF_T_INVALID)
    t->failed = 1;
  else
    add(t, "%s", nt_frontend_type_name(type->kind));
}
static void expression_text(Text *t, const NtFProgram *p, const NtFFunction *fn,
                            NtFId id, unsigned depth) {
  if (!id || id > p->expression_count || depth > 64) {
    t->failed = 1;
    return;
  }
  const NtFExpr *x = p->expressions + id - 1;
  if (x->kind == NTF_X_LITERAL || x->kind == NTF_X_BOOL ||
      (x->constant && (x->kind == NTF_X_UNARY || x->kind == NTF_X_BINARY ||
                       x->kind == NTF_X_NAME))) {
    add(t, "lit(");
    type_text(t, p, x->type, 0);
    add(t, ",%016" PRIx64 ",%d)", x->value, x->negative);
    return;
  }
  switch (x->kind) {
  case NTF_X_NAME:
    if (x->resolved >= fn->first_param &&
        x->resolved < fn->first_param + fn->param_count)
      add(t, "param(%u)", x->resolved - fn->first_param);
    else
      add(t, "name(%s)", x->name ? x->name : "");
    break;
  case NTF_X_REGISTER:
    add(t, "reg(%d,%u,%d)", x->reg.family, x->reg.bits, x->reg.high8);
    break;
  case NTF_X_UNARY:
  case NTF_X_BINARY:
    add(t, "op(%u,", (unsigned)x->op);
    expression_text(t, p, fn, x->left, depth + 1);
    if (x->right) {
      add(t, ",");
      expression_text(t, p, fn, x->right, depth + 1);
    }
    add(t, ")");
    break;
  case NTF_X_CALL:
  case NTF_X_PREDICATE: {
    add(t, "%s(%s", x->kind == NTF_X_CALL ? "call" : "pred",
        x->name ? x->name : "");
    NtFId arg = x->first_arg;
    size_t seen = 0;
    while (arg && !t->failed) {
      if (arg > p->expression_count || ++seen > p->expression_count) {
        t->failed = 1;
        break;
      }
      add(t, ",");
      expression_text(t, p, fn, arg, depth + 1);
      arg = p->expressions[arg - 1].next;
    }
    add(t, ")");
    break;
  }
  default:
    t->failed = 1;
    break;
  }
}
static int compare_text(const void *a, const void *b) {
  return strcmp(*(char *const *)a, *(char *const *)b);
}
static void predicates_text(Text *t, const NtFProgram *p, const NtFFunction *fn,
                            NtFId first) {
  char **items = NULL;
  size_t count = 0;
  for (NtFId id = first; id;) {
    if (id > p->expression_count || count >= p->expression_count) {
      t->failed = 1;
      break;
    }
    Text item = {0};
    expression_text(&item, p, fn, id, 0);
    if (item.failed || !item.bytes) {
      free(item.bytes);
      t->failed = 1;
      break;
    }
    char **next = realloc(items, (count + 1) * sizeof(*next));
    if (!next) {
      free(item.bytes);
      t->failed = 1;
      break;
    }
    items = next;
    items[count++] = item.bytes;
    id = p->expressions[id - 1].next;
  }
  if (count > 1)
    qsort(items, count, sizeof(*items), compare_text);
  add(t, "{");
  for (size_t j = 0; j < count; j++) {
    if (j && !strcmp(items[j], items[j - 1]))
      continue;
    if (j)
      add(t, ",");
    add(t, "%s", items[j]);
  }
  add(t, "}");
  for (size_t j = 0; j < count; j++)
    free(items[j]);
  free(items);
}
static char *contract_text(const NtFProgram *p, const NtCodeSymbol *s) {
  Text t = {0};
  if (s->function_id) {
    if (s->function_id > p->function_count)
      return NULL;
    const NtFFunction *fn = p->functions + s->function_id - 1;
    if (!fn->module || fn->module > p->module_count)
      return NULL;
    const NtFModule *module = p->modules + fn->module - 1;
    add(&t, "fn;target=%s;abi=%s;features=%u;section=%s;ret=", module->target,
        fn->abi ? fn->abi : "nx64-abi-v0", module->features,
        fn->section ? fn->section : ".text");
    type_text(&t, p, fn->return_type, 0);
    add(&t, ";params=(");
    for (uint32_t j = 0; j < fn->param_count; j++) {
      NtFId id = fn->first_param + j;
      if (!id || id > p->variable_count) {
        t.failed = 1;
        break;
      }
      const NtFVariable *v = p->variables + id - 1;
      if (j)
        add(&t, ",");
      add(&t, "%u:", (unsigned)v->direction);
      type_text(&t, p, v->type, 0);
      add(&t, "@%d/%u/%d", v->reg.family, v->reg.bits, v->reg.high8);
    }
    add(&t, ");effects=%u/%u/%u;clobbers=%016" PRIx64 ";requires=",
        fn->effects.flags, fn->effects.reads, fn->effects.writes, fn->clobbers);
    predicates_text(&t, p, fn, fn->requires);
    add(&t, ";ensures=");
    predicates_text(&t, p, fn, fn->ensures);
  } else {
    if (!s->variable_id || s->variable_id > p->variable_count)
      return NULL;
    add(&t, "data;type=");
    type_text(&t, p, p->variables[s->variable_id - 1].type, 0);
    add(&t, ";section=%u", s->section);
  }
  if (t.failed) {
    free(t.bytes);
    return NULL;
  }
  return t.bytes;
}
typedef struct {
  const NtCodeSymbol *source;
  NtObjectSymbol symbol;
} SymbolOrder;
static int symbol_compare(const void *a, const void *b) {
  return strcmp(((const SymbolOrder *)a)->symbol.name,
                ((const SymbolOrder *)b)->symbol.name);
}
static int relocation_compare(const void *a, const void *b) {
  const NtObjectReloc *x = a, *y = b;
  if (x->section != y->section)
    return x->section < y->section ? -1 : 1;
  return x->offset < y->offset ? -1 : x->offset > y->offset ? 1 : 0;
}
static int failure(NtDiagnostic *e, const char *message) {
  memset(e, 0, sizeof(*e));
  e->code = 800;
  snprintf(e->message, sizeof(e->message), "%s", message);
  return 0;
}
int nt_object_compile(const NtFInput *inputs, size_t count, NtObject *o,
                      NtDiagnostic *e) {
  if (!o || !e)
    return 0;
  memset(e, 0, sizeof(*e));
  if (o->_owned || o->symbols || o->relocations)
    return failure(e, "object output must be empty");
  NtFProgram p = {0};
  NtCodeImage image = {0};
  NtCodeDiagnostic cd = {0};
  SymbolOrder *order = NULL;
  uint32_t *functions = NULL, *variables = NULL;
  int ok = 0;
  if (!nt_frontend_compile_object(inputs, count, &p)) {
    if (p.diagnostic_count) {
      NtFDiagnostic *d = p.diagnostics;
      e->code = d->code;
      e->source_index = d->span.source;
      e->offset = d->span.start;
      e->line = d->span.line;
      e->column = d->span.column;
      snprintf(e->message, sizeof(e->message), "%s", d->message);
    } else
      failure(e, "frontend failed");
    goto finish;
  }
  if (!p.module_count) {
    failure(e, "object has no module");
    goto finish;
  }
  uint32_t target = !strcmp(p.modules[0].target, "x86_64-nexora-uefi")   ? 1
                    : !strcmp(p.modules[0].target, "x86_64-nexora-none") ? 2
                                                                         : 0;
  if (!target) {
    failure(e, "unknown object target");
    goto finish;
  }
  for (size_t j = 1; j < p.module_count; j++)
    if (strcmp(p.modules[0].target, p.modules[j].target)) {
      failure(e, "mixed object targets");
      goto finish;
    }
  if (!nt_codegen_x64_object(&p, &image, &cd)) {
    e->code = cd.code;
    e->source_index = cd.span.source;
    e->offset = cd.span.start;
    e->line = cd.span.line;
    e->column = cd.span.column;
    snprintf(e->message, sizeof(e->message), "%s", cd.message);
    goto finish;
  }
  if (image.symbol_count > 65536 || image.relocation_count > 1048576) {
    failure(e, "object metadata limits");
    goto finish;
  }
  order = calloc(image.symbol_count ? image.symbol_count : 1, sizeof(*order));
  functions = calloc(p.function_count + 1, sizeof(*functions));
  variables = calloc(p.variable_count + 1, sizeof(*variables));
  if (!order || !functions || !variables) {
    failure(e, "object metadata allocation failed");
    goto finish;
  }
  for (size_t j = 0; j < image.symbol_count; j++) {
    const NtCodeSymbol *s = image.symbols + j;
    order[j].source = s;
    size_t mn = strlen(s->module), sn = strlen(s->name);
    if (mn + sn + 1 > 512) {
      failure(e, "object symbol name too long");
      goto finish;
    }
    order[j].symbol.name = malloc(mn + sn + 2);
    if (!order[j].symbol.name) {
      failure(e, "symbol allocation failed");
      goto finish;
    }
    snprintf(order[j].symbol.name, mn + sn + 2, "%s.%s", s->module, s->name);
    order[j].symbol.contract = contract_text(&p, s);
    if (!order[j].symbol.contract) {
      failure(e, "unsupported or oversized typed symbol contract");
      goto finish;
    }
    NtObjectSymbol *d = &order[j].symbol;
    d->section = s->section;
    d->offset = s->offset;
    d->size = s->size;
    if (s->function_id) {
      const NtFFunction *f = p.functions + s->function_id - 1;
      d->flags = NT_OBJECT_FUNCTION | (f->exported ? NT_OBJECT_EXPORT : 0) |
                 (f->imported && !f->resolved ? NT_OBJECT_IMPORT : 0);
    } else {
      const NtFVariable *v = p.variables + s->variable_id - 1;
      d->flags = NT_OBJECT_VARIABLE | (v->exported ? NT_OBJECT_EXPORT : 0);
    }
  }
  qsort(order, image.symbol_count, sizeof(*order), symbol_compare);
  o->_owned = 1;
  o->target = target;
  o->symbol_count = image.symbol_count;
  o->relocation_count = image.relocation_count;
  o->symbols =
      calloc(o->symbol_count ? o->symbol_count : 1, sizeof(*o->symbols));
  o->relocations = calloc(o->relocation_count ? o->relocation_count : 1,
                          sizeof(*o->relocations));
  if (!o->symbols || !o->relocations) {
    failure(e, "object table allocation failed");
    goto finish;
  }
  for (size_t j = 0; j < o->symbol_count; j++) {
    o->symbols[j] = order[j].symbol;
    order[j].symbol.name = order[j].symbol.contract = NULL;
    const NtCodeSymbol *s = order[j].source;
    if (s->function_id) {
      functions[s->function_id] = (uint32_t)j + 1;
      if (image.entry != SIZE_MAX && s->section == NT_CODE_SECTION_TEXT &&
          p.functions[s->function_id - 1].exported &&
          s->offset == image.entry &&
          !strcmp(p.functions[s->function_id - 1].name, "main"))
        o->entry_symbol = (uint32_t)j + 1;
    }
    if (s->variable_id)
      variables[s->variable_id] = (uint32_t)j + 1;
  }
  if (image.entry != SIZE_MAX && !o->entry_symbol) {
    failure(e, "codegen entry lacks a symbol");
    goto finish;
  }
  for (size_t j = 0; j < o->relocation_count; j++) {
    const NtCodeReloc *r = image.relocations + j;
    NtObjectReloc *d = o->relocations + j;
    if ((!r->target_function) == (!r->target_variable) ||
        r->target_function > p.function_count ||
        r->target_variable > p.variable_count) {
      failure(e, "invalid relocation identity");
      goto finish;
    }
    d->target = r->target_function ? functions[r->target_function]
                                   : variables[r->target_variable];
    if (!d->target) {
      failure(e, "relocation target has no symbol");
      goto finish;
    }
    d->section = r->source_section;
    d->kind = r->kind;
    d->offset = r->offset;
    d->addend = r->addend;
  }
  if (o->relocation_count > 1)
    qsort(o->relocations, o->relocation_count, sizeof(*o->relocations),
          relocation_compare);
  o->sections[0] = (NtBuffer){image.bytes, image.size, image.capacity};
  image.bytes = NULL;
  image.size = image.capacity = 0;
  o->sections[1] =
      (NtBuffer){image.rdata.bytes, image.rdata.size, image.rdata.capacity};
  memset(&image.rdata, 0, sizeof(image.rdata));
  o->sections[2] =
      (NtBuffer){image.data.bytes, image.data.size, image.data.capacity};
  memset(&image.data, 0, sizeof(image.data));
  ok = 1;
finish:
  if (order)
    for (size_t j = 0; j < image.symbol_count; j++) {
      free(order[j].symbol.name);
      free(order[j].symbol.contract);
    }
  free(order);
  free(functions);
  free(variables);
  nt_code_image_free(&image);
  nt_frontend_free(&p);
  if (!ok)
    nt_object_free(o);
  return ok;
}
