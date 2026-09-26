#include "frontend.h"
#include "conformance.h"
#include "x64.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIMIT NTF_COMPTIME_AST_NODES_MAX
#define MAX_DIAGS 256u
#define MAX_DEPTH 128u
#define BIT(n) (UINT64_C(1) << (n))

enum {
  TK_EOF = 256,
  TK_NAME,
  TK_INT,
  TK_STRING,
  TK_NL,
  TK_ARROW,
  TK_EQ,
  TK_NE,
  TK_LE,
  TK_GE,
  TK_SHL,
  TK_SHR,
  TK_LAND,
  TK_LOR
};

typedef struct Allocation {
  struct Allocation *next;
} Allocation;
typedef struct {
  NtFStmtKind kind;
  NtFId expression, type;
  const char *introduced_name, *instruction_name;
  NtFSpan span;
  int meta_expand;
  int declaration, decl_exported, decl_data;
  const char *decl_name;
} MacroTemplate;
typedef struct {
  const char *name;
  const char *parameters[8];
  unsigned char parameter_kinds[8]; /* 1=ast.expr, 2=ast.type, 3=value */
  NtFId parameter_types[8];
  size_t parameter_count;
  const char *meta_names[32];
  NtFId meta_values[32];
  size_t meta_count;
  NtFStmtKind statement_kind;
  NtFId template_expression, template_type;
  const char *introduced_name;
  const char *instruction_name;
  MacroTemplate templates[512];
  size_t template_count;
  int result_block;
  int result_expr, result_type;
  int emit_list;
  int meta_if;
  NtFId meta_condition;
  size_t else_start;
  int meta_for;
  const char *loop_name;
  NtFId loop_count;
  uint64_t loop_value;
  int result_decl, decl_exported, decl_function;
  const char *decl_name;
  NtFId decl_type, decl_initializer;
  const char *decl_abi;
  const char *decl_param_names[64];
  NtFId decl_param_types[64], expanded_parameters[64];
  NtFRegister decl_param_regs[64];
  NtFDirection decl_param_directions[64];
  size_t decl_param_count;
  NtFEffects decl_effects;
  uint64_t decl_clobbers;
  NtFId expanded_variables[512];
  NtFId expanded_declarations[512];
  NtFSpan span, template_span;
  uint32_t ordinal;
} Macro;
typedef struct {
  Allocation *allocations;
  size_t module_cap, function_cap, variable_cap, type_cap, expression_cap,
      statement_cap, diagnostic_cap;
  Macro *macros;
  size_t macro_count, macro_capacity;
  uint64_t fuel_limit, call_depth_limit, arena_limit, node_limit,
      expansion_limit, constant_limit;
  uint64_t fuel_used, expansion_serial, expansion_depth, meta_call_depth,
      arena_used, arena_base, constant_used;
  int allow_unresolved_imports;
} Private;
typedef struct {
  int kind;
  const char *text;
  size_t length;
  uint64_t value;
  NtFSpan span;
} Token;
typedef struct {
  NtFProgram *p;
  const NtFInput *in;
  uint32_t source;
  size_t cursor;
  uint32_t line, column;
  Token token;
  NtFId module, function;
  unsigned depth;
  NtFId scopes[MAX_DEPTH];
  Macro *macro;
  int failed;
} Parser;

#define MO(p, id) ((p)->modules[(id) - 1])
#define FN(p, id) ((p)->functions[(id) - 1])
#define VA(p, id) ((p)->variables[(id) - 1])
#define TY(p, id) ((p)->types[(id) - 1])
#define EX(p, id) ((p)->expressions[(id) - 1])
#define ST(p, id) ((p)->statements[(id) - 1])

static int diag(NtFProgram *, unsigned, NtFSpan, const char *, ...);
static NtFId parse_expand_expression(Parser *p);
static NtFId parse_expand_type(Parser *p);
static void *owned(NtFProgram *p, size_t n) {
  Private *v = (Private *)p->_private;
  if (n > SIZE_MAX - sizeof(Allocation))
    return NULL;
  uint64_t used = v->arena_used - v->arena_base;
  if (n > v->arena_limit - used)
    return NULL;
  Allocation *a = (Allocation *)calloc(1, sizeof(*a) + n);
  if (!a)
    return NULL;
  a->next = v->allocations;
  v->allocations = a;
  v->arena_used += n;
  return a + 1;
}
static const char *copy_text(NtFProgram *p, const char *s, size_t n) {
  char *r = (char *)owned(p, n + 1);
  if (!r)
    return NULL;
  memcpy(r, s, n);
  r[n] = 0;
  return r;
}
static NtFId append(NtFProgram *p, void **items, size_t *count, size_t *cap,
                    size_t size, NtFSpan span) {
  if (*count >= LIMIT) {
    diag(p, NTF_E_LIMIT, span, "AST node limit exceeded");
    return 0;
  }
  if (*count == *cap) {
    size_t next = *cap ? *cap * 2 : 32;
    void *memory = realloc(*items, next * size);
    if (!memory) {
      diag(p, NTF_E_OOM, span, "allocation failed");
      return 0;
    }
    *items = memory;
    *cap = next;
  }
  memset((unsigned char *)*items + *count * size, 0, size);
  return (NtFId)++ * count;
}
static int node_allowed(NtFProgram *p, NtFSpan span) {
  Private *v = p->_private;
  uint64_t total = p->module_count + p->function_count + p->variable_count +
                   p->type_count + p->expression_count + p->statement_count +
                   v->macro_count;
  if (total >= v->node_limit) {
    diag(p, NTF_E_LIMIT, span, "configured AST node budget exhausted");
    return 0;
  }
  return 1;
}
static NtFId add_module(NtFProgram *p, NtFSpan s) {
  Private *v = p->_private;
  if (!node_allowed(p, s))
    return 0;
  return append(p, (void **)&p->modules, &p->module_count, &v->module_cap,
                sizeof(*p->modules), s);
}
static NtFId add_function(NtFProgram *p, NtFSpan s) {
  Private *v = p->_private;
  if (!node_allowed(p, s))
    return 0;
  return append(p, (void **)&p->functions, &p->function_count, &v->function_cap,
                sizeof(*p->functions), s);
}
static NtFId add_variable(NtFProgram *p, NtFSpan s) {
  Private *v = p->_private;
  if (!node_allowed(p, s))
    return 0;
  return append(p, (void **)&p->variables, &p->variable_count, &v->variable_cap,
                sizeof(*p->variables), s);
}
static NtFId add_type(NtFProgram *p, NtFSpan s) {
  Private *v = p->_private;
  if (!node_allowed(p, s))
    return 0;
  return append(p, (void **)&p->types, &p->type_count, &v->type_cap,
                sizeof(*p->types), s);
}
static NtFId add_expression(NtFProgram *p, NtFSpan s) {
  Private *v = p->_private;
  if (!node_allowed(p, s))
    return 0;
  return append(p, (void **)&p->expressions, &p->expression_count,
                &v->expression_cap, sizeof(*p->expressions), s);
}
static NtFId add_statement(NtFProgram *p, NtFSpan s) {
  Private *v = p->_private;
  if (!node_allowed(p, s))
    return 0;
  return append(p, (void **)&p->statements, &p->statement_count,
                &v->statement_cap, sizeof(*p->statements), s);
}
static Macro *add_macro(NtFProgram *p, NtFSpan span) {
  Private *v = p->_private;
  if (!node_allowed(p, span))
    return NULL;
  if (v->macro_count == v->macro_capacity) {
    size_t capacity = v->macro_capacity ? v->macro_capacity * 2 : 8;
    Macro *next = (Macro *)realloc(v->macros, capacity * sizeof(*next));
    if (!next) {
      diag(p, NTF_E_OOM, span, "macro allocation failed");
      return NULL;
    }
    v->macros = next;
    v->macro_capacity = capacity;
  }
  Macro *macro = &v->macros[v->macro_count++];
  memset(macro, 0, sizeof(*macro));
  macro->span = span;
  macro->ordinal = (uint32_t)v->macro_count;
  return macro;
}
static int diag(NtFProgram *p, unsigned code, NtFSpan span, const char *format,
                ...) {
  Private *v = p->_private;
  if (p->diagnostic_count >= MAX_DIAGS)
    return 0;
  NtFId id = append(p, (void **)&p->diagnostics, &p->diagnostic_count,
                    &v->diagnostic_cap, sizeof(*p->diagnostics), span);
  if (!id)
    return 0;
  NtFDiagnostic *d = &p->diagnostics[id - 1];
  d->code = code;
  d->span = span;
  va_list ap;
  va_start(ap, format);
  vsnprintf(d->message, sizeof(d->message), format, ap);
  va_end(ap);
  return 1;
}
static NtFSpan joined(NtFSpan a, NtFSpan b) {
  a.end = b.end;
  return a;
}
static int letter(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static int digit(unsigned char c) { return c >= '0' && c <= '9'; }
static int xdigit_(unsigned char c) {
  return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static unsigned xvalue(unsigned char c) {
  return digit(c)   ? (unsigned)(c - '0')
         : c >= 'a' ? (unsigned)(c - 'a' + 10)
                    : (unsigned)(c - 'A' + 10);
}
static void advance(Parser *p) {
  unsigned char c = (unsigned char)p->in->text[p->cursor++];
  if (c == '\n') {
    ++p->line;
    p->column = 1;
  } else
    ++p->column;
}
static void lex_fail(Parser *p, NtFSpan span, const char *message) {
  diag(p->p, NTF_E_LEX, span, "%s", message);
  p->failed = 1;
}
static void next(Parser *p) {
  const char *s = p->in->text;
  size_t n = p->in->length;
  while (p->cursor < n) {
    unsigned char c = (unsigned char)s[p->cursor];
    if (c == ' ' || c == '\t') {
      advance(p);
      continue;
    }
    if (c == '\r') {
      if (p->cursor + 1 >= n || s[p->cursor + 1] != '\n') {
        NtFSpan z = {p->source, p->line, p->column, p->cursor, p->cursor + 1};
        lex_fail(p, z, "isolated carriage return");
        return;
      }
      advance(p);
      continue;
    }
    if (c == '/' && p->cursor + 1 < n && s[p->cursor + 1] == '/') {
      while (p->cursor < n && s[p->cursor] != '\n')
        advance(p);
      continue;
    }
    break;
  }
  Token t = {TK_EOF,
             s + p->cursor,
             0,
             0,
             {p->source, p->line, p->column, p->cursor, p->cursor}};
  p->token = t;
  if (p->failed || p->cursor >= n)
    return;
  unsigned char c = (unsigned char)s[p->cursor];
  if (c == '\n') {
    t.kind = TK_NL;
    advance(p);
  } else if (letter(c)) {
    t.kind = TK_NAME;
    do {
      advance(p);
    } while (p->cursor < n && (letter((unsigned char)s[p->cursor]) ||
                               digit((unsigned char)s[p->cursor])));
  } else if (digit(c)) {
    t.kind = TK_INT;
    unsigned base = 10;
    int previous = 0;
    if (c == '0' && p->cursor + 1 < n &&
        (s[p->cursor + 1] == 'x' || s[p->cursor + 1] == 'X')) {
      base = 16;
      advance(p);
      advance(p);
      if (p->cursor >= n || !xdigit_((unsigned char)s[p->cursor])) {
        t.span.end = p->cursor;
        lex_fail(p, t.span, "hex literal requires digits");
        return;
      }
    }
    while (p->cursor < n) {
      unsigned char d = (unsigned char)s[p->cursor];
      if (d == '_') {
        if (!previous || p->cursor + 1 >= n ||
            !(base == 16 ? xdigit_((unsigned char)s[p->cursor + 1])
                         : digit((unsigned char)s[p->cursor + 1]))) {
          t.span.end = p->cursor + 1;
          lex_fail(p, t.span, "invalid underscore in integer");
          return;
        }
        previous = 0;
        advance(p);
        continue;
      }
      if (!(base == 16 ? xdigit_(d) : digit(d)))
        break;
      unsigned v = base == 16 ? xvalue(d) : (unsigned)(d - '0');
      if (t.value > (UINT64_MAX - v) / base) {
        t.span.end = p->cursor + 1;
        lex_fail(p, t.span, "integer exceeds u64");
        return;
      }
      t.value = t.value * base + v;
      previous = 1;
      advance(p);
    }
    if (p->cursor < n && letter((unsigned char)s[p->cursor])) {
      while (p->cursor < n && (letter((unsigned char)s[p->cursor]) ||
                               digit((unsigned char)s[p->cursor])))
        advance(p);
      t.span.end = p->cursor;
      lex_fail(p, t.span, "invalid integer suffix");
      return;
    }
  } else if (c == '"') {
    t.kind = TK_STRING;
    advance(p);
    t.text = s + p->cursor;
    while (p->cursor < n && s[p->cursor] != '"') {
      unsigned char d = (unsigned char)s[p->cursor];
      if (d == '\n' || d == '\r' || d == 0) {
        t.span.end = p->cursor + 1;
        lex_fail(p, t.span, "invalid string byte");
        return;
      }
      if (d == '\\') {
        advance(p);
        if (p->cursor >= n)
          break;
        unsigned char e = (unsigned char)s[p->cursor];
        if (e == 'x') {
          advance(p);
          if (p->cursor + 1 >= n || !xdigit_((unsigned char)s[p->cursor]) ||
              !xdigit_((unsigned char)s[p->cursor + 1])) {
            t.span.end = p->cursor;
            lex_fail(p, t.span, "invalid hex string escape");
            return;
          }
          advance(p);
          advance(p);
          continue;
        }
        if (!strchr("\\\"nrt0", e)) {
          t.span.end = p->cursor + 1;
          lex_fail(p, t.span, "unknown string escape");
          return;
        }
      }
      advance(p);
    }
    if (p->cursor >= n || s[p->cursor] != '"') {
      t.span.end = p->cursor;
      lex_fail(p, t.span, "unterminated string");
      return;
    }
    t.length = (size_t)((s + p->cursor) - t.text);
    advance(p);
  } else {
    advance(p);
    t.kind = c;
    if (p->cursor < n) {
      unsigned char d = (unsigned char)s[p->cursor];
      if (c == '-' && d == '>') {
        t.kind = TK_ARROW;
        advance(p);
      } else if (c == '=' && d == '=') {
        t.kind = TK_EQ;
        advance(p);
      } else if (c == '!' && d == '=') {
        t.kind = TK_NE;
        advance(p);
      } else if (c == '<' && d == '=') {
        t.kind = TK_LE;
        advance(p);
      } else if (c == '>' && d == '=') {
        t.kind = TK_GE;
        advance(p);
      } else if (c == '<' && d == '<') {
        t.kind = TK_SHL;
        advance(p);
      } else if (c == '>' && d == '>') {
        t.kind = TK_SHR;
        advance(p);
      } else if (c == '&' && d == '&') {
        t.kind = TK_LAND;
        advance(p);
      } else if (c == '|' && d == '|') {
        t.kind = TK_LOR;
        advance(p);
      }
    }
    if (c < 32 || c > 126) {
      t.span.end = p->cursor;
      lex_fail(p, t.span, "invalid source byte");
      return;
    }
  }
  t.span.end = p->cursor;
  if (!t.length)
    t.length = t.span.end - t.span.start;
  p->token = t;
}
static int is(Parser *p, const char *word) {
  return p->token.kind == TK_NAME && p->token.length == strlen(word) &&
         !memcmp(p->token.text, word, p->token.length);
}
static void lines(Parser *p) {
  while (!p->failed && p->token.kind == TK_NL)
    next(p);
}
static int syntax(Parser *p, const char *m) {
  diag(p->p, NTF_E_SYNTAX, p->token.span, "%s", m);
  p->failed = 1;
  return 0;
}
static int accept(Parser *p, int kind) {
  if (p->token.kind != kind)
    return 0;
  next(p);
  return !p->failed;
}
static int expect(Parser *p, int kind, const char *m) {
  if (p->failed)
    return 0;
  if (p->token.kind != kind)
    return syntax(p, m);
  next(p);
  return !p->failed;
}
static int accept_word(Parser *p, const char *word) {
  if (!is(p, word))
    return 0;
  next(p);
  return !p->failed;
}
static int expect_word(Parser *p, const char *word) {
  if (!is(p, word)) {
    char m[160];
    snprintf(m, sizeof(m), "expected '%s'", word);
    return syntax(p, m);
  }
  next(p);
  return !p->failed;
}
static const char *name(Parser *p, NtFSpan *span) {
  if (p->token.kind != TK_NAME) {
    syntax(p, "expected name");
    return NULL;
  }
  const char *r = copy_text(p->p, p->token.text, p->token.length);
  if (!r) {
    diag(p->p, NTF_E_OOM, p->token.span, "allocation failed");
    p->failed = 1;
    return NULL;
  }
  if (span)
    *span = p->token.span;
  next(p);
  return r;
}
static const char *compound(Parser *p, int separator, NtFSpan *span) {
  NtFSpan first, last;
  const char *parts[32];
  size_t lens[32], count = 0, total = 0;
  parts[count] = name(p, &first);
  if (!parts[count])
    return NULL;
  lens[count] = strlen(parts[count]);
  total = lens[count++];
  last = first;
  while (p->token.kind == separator) {
    next(p);
    if (count == 32 || p->token.kind != TK_NAME) {
      syntax(p, "invalid compound name");
      return NULL;
    }
    parts[count] = name(p, &last);
    lens[count] = strlen(parts[count]);
    total += 1 + lens[count++];
  }
  char *r = (char *)owned(p->p, total + 1);
  if (!r) {
    p->failed = 1;
    return NULL;
  }
  size_t at = 0;
  for (size_t i = 0; i < count; i++) {
    if (i)
      r[at++] = (char)separator;
    memcpy(r + at, parts[i], lens[i]);
    at += lens[i];
  }
  r[at] = 0;
  if (span)
    *span = joined(first, last);
  return r;
}
static int term(Parser *p) {
  if (accept(p, ';')) {
    lines(p);
    return 1;
  }
  if (accept(p, TK_NL)) {
    lines(p);
    return 1;
  }
  return syntax(p, "expected newline or semicolon");
}

static NtFId intern_type(NtFProgram *p, NtFType value, NtFSpan span) {
  for (size_t i = 0; i < p->type_count; i++) {
    NtFType *t = &p->types[i];
    if (t->kind == value.kind && t->space == value.space &&
        t->element == value.element && t->count == value.count &&
        t->bits == value.bits)
      return (NtFId)(i + 1);
  }
  NtFId id = add_type(p, span);
  if (id)
    TY(p, id) = value;
  return id;
}
static NtFTypeKind scalar_kind(const char *s) {
  static const char *names[] = {"",    "u8",  "u16", "u32",  "u64",  "i8",
                                "i16", "i32", "i64", "bool", "never"};
  for (unsigned i = 1; i < sizeof names / sizeof names[0]; i++)
    if (!strcmp(s, names[i]))
      return (NtFTypeKind)i;
  return NTF_T_INVALID;
}
static NtFSpace space_kind(const char *s) {
  static const char *names[] = {"",     "user",   "kernel",  "physical",
                                "mmio", "device", "firmware"};
  for (unsigned i = 1; i < sizeof names / sizeof names[0]; i++)
    if (!strcmp(s, names[i]))
      return (NtFSpace)i;
  return NTF_SPACE_NONE;
}
static uint32_t target_feature_bit(const char *s) {
  static const char *names[] = {"lm", "msr", "syscall", "sse2", "tsc"};
  for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++)
    if (!strcmp(s, names[i]))
      return UINT32_C(1) << i;
  return 0;
}
static unsigned type_bits(NtFTypeKind kind) {
  switch (kind) {
  case NTF_T_U8:
  case NTF_T_I8:
    return 8;
  case NTF_T_U16:
  case NTF_T_I16:
    return 16;
  case NTF_T_U32:
  case NTF_T_I32:
    return 32;
  case NTF_T_U64:
  case NTF_T_I64:
    return 64;
  case NTF_T_BOOL:
    return 1;
  default:
    return 0;
  }
}
static int expect_type_close(Parser *p) {
  if (p->token.kind == TK_SHR) {
    p->token.kind = '>';
    if (p->token.text)
      ++p->token.text;
    p->token.length = 1;
    ++p->token.span.start;
    ++p->token.span.column;
    return 1;
  }
  return expect(p, '>', "expected '>' after type");
}
static NtFId parse_type(Parser *p) {
  NtFSpan span = p->token.span;
  if (is(p, "expand"))
    return parse_expand_type(p);
  if (accept(p, '$')) {
    NtFSpan splice_span;
    const char *splice = name(p, &splice_span);
    if (!p->macro || !splice) {
      diag(p->p, NTF_E_TYPE, span, "type splice is only valid in macro quote");
      p->failed = 1;
      return 0;
    }
    for (size_t i = 0; i < p->macro->parameter_count; i++)
      if (!strcmp(splice, p->macro->parameters[i])) {
        if (p->macro->parameter_kinds[i] != 2) {
          diag(p->p, NTF_E_TYPE, splice_span,
               "ast.expr parameter cannot splice into a type");
          p->failed = 1;
          return 0;
        }
        return UINT32_C(0x80000000) | (NtFId)i;
      }
    diag(p->p, NTF_E_NAME, splice_span, "unknown macro type parameter '%s'",
         splice);
    p->failed = 1;
    return 0;
  }
  if (p->token.kind != TK_NAME) {
    syntax(p, "expected type");
    return 0;
  }
  char word[32];
  if (p->token.length >= sizeof word) {
    syntax(p, "unknown type");
    return 0;
  }
  memcpy(word, p->token.text, p->token.length);
  word[p->token.length] = 0;
  NtFTypeKind kind = scalar_kind(word);
  if (kind) {
    next(p);
    NtFType t = {kind, NTF_SPACE_NONE, 0, 0, type_bits(kind)};
    return intern_type(p->p, t, span);
  }
  if (!strcmp(word, "ptr")) {
    next(p);
    if (!expect(p, '<', "expected '<' after ptr"))
      return 0;
    const char *space_name = name(p, NULL);
    NtFSpace space = space_name ? space_kind(space_name) : NTF_SPACE_NONE;
    if (!space) {
      diag(p->p, NTF_E_TYPE, span, "unknown address space");
      p->failed = 1;
      return 0;
    }
    if (!expect(p, ',', "expected ',' in pointer type"))
      return 0;
    lines(p);
    NtFId element = parse_type(p);
    if (!element || !expect_type_close(p))
      return 0;
    NtFType t = {NTF_T_PTR, space, element, 0, 64};
    return intern_type(p->p, t, span);
  }
  if (!strcmp(word, "array")) {
    next(p);
    if (!expect(p, '<', "expected '<' after array"))
      return 0;
    NtFId element = parse_type(p);
    if (!element || !expect(p, ',', "expected ',' in array type"))
      return 0;
    lines(p);
    if (p->token.kind != TK_INT) {
      syntax(p, "expected array count");
      return 0;
    }
    uint64_t count = p->token.value;
    next(p);
    if (!count) {
      diag(p->p, NTF_E_TYPE, span, "array count must be positive");
      p->failed = 1;
      return 0;
    }
    if (!expect_type_close(p))
      return 0;
    NtFType t = {NTF_T_ARRAY, NTF_SPACE_NONE, element, count, 0};
    return intern_type(p->p, t, span);
  }
  diag(p->p, NTF_E_TYPE, span, "unknown type '%s'", word);
  p->failed = 1;
  return 0;
}

NtFRegister nt_frontend_register(const char *s) {
  NtFRegister none = {-1, 0, 0};
  if (!s)
    return none;
  static const char *r64[] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp",
                              "rsi", "rdi", "r8",  "r9",  "r10", "r11",
                              "r12", "r13", "r14", "r15"};
  static const char *r32[] = {"eax",  "ecx",  "edx",  "ebx", "esp",  "ebp",
                              "esi",  "edi",  "r8d",  "r9d", "r10d", "r11d",
                              "r12d", "r13d", "r14d", "r15d"};
  static const char *r16[] = {"ax",   "cx",   "dx",   "bx",  "sp",   "bp",
                              "si",   "di",   "r8w",  "r9w", "r10w", "r11w",
                              "r12w", "r13w", "r14w", "r15w"};
  static const char *r8[] = {"al",   "cl",   "dl",   "bl",  "spl",  "bpl",
                             "sil",  "dil",  "r8b",  "r9b", "r10b", "r11b",
                             "r12b", "r13b", "r14b", "r15b"};
  for (int i = 0; i < 16; i++) {
    if (!strcmp(s, r64[i]))
      return (NtFRegister){i, 64, 0};
    if (!strcmp(s, r32[i]))
      return (NtFRegister){i, 32, 0};
    if (!strcmp(s, r16[i]))
      return (NtFRegister){i, 16, 0};
    if (!strcmp(s, r8[i]))
      return (NtFRegister){i, 8, 0};
  }
  static const char *high[] = {"ah", "ch", "dh", "bh"};
  for (int i = 0; i < 4; i++)
    if (!strcmp(s, high[i]))
      return (NtFRegister){i, 8, 1};
  if (!strcmp(s, "rip"))
    return (NtFRegister){16, 64, 0};
  if (!strcmp(s, "rflags"))
    return (NtFRegister){17, 64, 0};
  if (!strcmp(s, "gsbase"))
    return (NtFRegister){18, 64, 0};
  if (!strcmp(s, "fsbase"))
    return (NtFRegister){19, 64, 0};
  return none;
}
const char *nt_frontend_type_name(NtFTypeKind kind) {
  static const char *names[] = {"invalid", "u8",  "u16",  "u32", "u64",
                                "i8",      "i16", "i32",  "i64", "bool",
                                "never",   "ptr", "array"};
  return (unsigned)kind < sizeof names / sizeof names[0] ? names[kind]
                                                         : "invalid";
}

static NtFId find_variable(Parser *p, const char *s) {
  for (size_t i = p->p->variable_count; i > 0; i--) {
    NtFVariable *v = &p->p->variables[i - 1];
    if (strcmp(v->name, s))
      continue;
    if (!v->function && v->module == p->module)
      return (NtFId)i;
    if (p->function && v->function == p->function) {
      if (!v->scope)
        return (NtFId)i;
      for (unsigned depth = p->depth; depth > 0; depth--)
        if (p->scopes[depth - 1] == v->scope)
          return (NtFId)i;
    }
  }
  return 0;
}
static NtFId parse_expression(Parser *, unsigned);
static NtFId parse_operand(Parser *p);
static NtFId primary(Parser *p) {
  if (is(p, "expand"))
    return parse_expand_expression(p);
  NtFSpan span = p->token.span;
  NtFId id = add_expression(p->p, span);
  if (!id) {
    p->failed = 1;
    return 0;
  }
  NtFExpr *x = &EX(p->p, id);
  x->span = span;
  x->reg.family = -1;
  if (p->token.kind == TK_INT) {
    x->kind = NTF_X_LITERAL;
    x->value = p->token.value;
    x->constant = 1;
    next(p);
    return id;
  }
  if (p->token.kind == TK_STRING) {
    Private *private_data = p->p->_private;
    if (p->token.length >
        private_data->constant_limit - private_data->constant_used) {
      diag(p->p, NTF_E_LIMIT, p->token.span,
           "comptime constant byte budget exceeded");
      p->failed = 1;
      return 0;
    }
    private_data->constant_used += p->token.length;
    x->kind = NTF_X_STRING;
    x->name = copy_text(p->p, p->token.text, p->token.length);
    if (!x->name) {
      diag(p->p, NTF_E_LIMIT, p->token.span, "comptime arena budget exhausted");
      p->failed = 1;
      return 0;
    }
    x->string_length = p->token.length;
    x->constant = 1;
    next(p);
    return id;
  }
  if (accept(p, '$')) {
    NtFSpan splice_span;
    const char *splice = name(p, &splice_span);
    if (!p->macro || !splice) {
      diag(p->p, NTF_E_UNSUPPORTED, span,
           "splice is only valid inside a macro quote");
      p->failed = 1;
      return 0;
    }
    size_t parameter = p->macro->parameter_count;
    for (size_t i = 0; i < p->macro->parameter_count; i++)
      if (!strcmp(splice, p->macro->parameters[i])) {
        parameter = i;
        break;
      }
    if (parameter == p->macro->parameter_count && p->macro->loop_name &&
        !strcmp(splice, p->macro->loop_name)) {
      x = &EX(p->p, id);
      x->kind = NTF_X_NAME;
      x->name = splice;
      x->resolved = UINT32_C(0x20000000);
      x->origin = span;
      return id;
    }
    if (parameter == p->macro->parameter_count) {
      for (size_t i = 0; i < p->macro->meta_count; i++)
        if (!strcmp(splice, p->macro->meta_names[i])) {
          x = &EX(p->p, id);
          x->kind = NTF_X_NAME;
          x->name = splice;
          x->resolved = UINT32_C(0x30000000) | (NtFId)i;
          x->origin = span;
          return id;
        }
    }
    if (parameter < p->macro->parameter_count &&
        p->macro->parameter_kinds[parameter] == 2) {
      diag(p->p, NTF_E_TYPE, splice_span,
           "ast.type parameter cannot splice into an expression");
      p->failed = 1;
      return 0;
    }
    if (parameter == p->macro->parameter_count) {
      diag(p->p, NTF_E_NAME, splice_span, "unknown macro parameter '%s'",
           splice);
      p->failed = 1;
      return 0;
    }
    x = &EX(p->p, id);
    x->kind = NTF_X_NAME;
    x->name = splice;
    x->resolved = UINT32_C(0x80000000) | (NtFId)parameter;
    x->origin = span;
    return id;
  }
  if (accept(p, '(')) {
    NtFId inside = parse_expression(p, 0);
    if (!inside || !expect(p, ')', "expected ')'"))
      return 0;
    return inside;
  }
  if (is(p, "intrinsic")) {
    next(p);
    NtFSpan intrinsic_span;
    const char *intrinsic_name = compound(p, '.', &intrinsic_span);
    if (!intrinsic_name || strcmp(intrinsic_name, "address_of")) {
      diag(p->p, NTF_E_UNSUPPORTED, intrinsic_span,
           "unknown expression intrinsic '%s'",
           intrinsic_name ? intrinsic_name : "");
      p->failed = 1;
      return 0;
    }
    if (!expect(p, '(', "expected '(' after address_of"))
      return 0;
    NtFSpan symbol_span;
    const char *symbol = name(p, &symbol_span);
    NtFId variable = symbol ? find_variable(p, symbol) : 0;
    if (!variable || VA(p->p, variable).direction != NTF_DATA) {
      diag(p->p, NTF_E_NAME, symbol_span,
           "address_of requires a static data symbol");
      p->failed = 1;
      return 0;
    }
    int64_t addend = 0;
    if (p->token.kind == '+' || p->token.kind == '-') {
      int negative = p->token.kind == '-';
      next(p);
      uint64_t limit = negative ? UINT64_C(2147483648) : (uint64_t)INT32_MAX;
      if (p->token.kind != TK_INT || p->token.value > limit) {
        diag(p->p, NTF_E_LITERAL, p->token.span,
             "address_of addend is outside signed32 range");
        p->failed = 1;
        return 0;
      }
      addend = negative ? -(int64_t)p->token.value : (int64_t)p->token.value;
      next(p);
    }
    if (!expect(p, ')', "expected ')' after address_of"))
      return 0;
    NtFType pointer = {NTF_T_PTR, NTF_SPACE_USER, VA(p->p, variable).type, 0,
                       64};
    NtFId pointer_type = intern_type(p->p, pointer, span);
    if (!pointer_type) {
      p->failed = 1;
      return 0;
    }
    x = &EX(p->p, id);
    x->kind = NTF_X_ADDRESS;
    x->span = joined(span, p->token.span);
    x->type = pointer_type;
    x->resolved = variable;
    x->name = symbol;
    x->memory.symbol = VA(p->p, variable).name;
    x->memory.provenance = variable;
    x->memory.displacement = addend;
    x->constant = 1;
    x->reg.family = -1;
    return id;
  }
  if (p->token.kind != TK_NAME) {
    syntax(p, "expected expression");
    return 0;
  }
  const char *s = compound(p, '.', &span);
  x = &EX(p->p, id);
  x->span = span;
  x->name = s;
  if (p->macro && p->macro->decl_function) {
    for (size_t i = 0; i < p->macro->decl_param_count; i++)
      if (!strcmp(s, p->macro->decl_param_names[i])) {
        x->kind = NTF_X_NAME;
        x->resolved = UINT32_C(0x50000000) | (NtFId)i;
        x->origin = span;
        return id;
      }
  }
  if (p->macro && p->macro->result_decl && p->macro->emit_list) {
    for (size_t i = 0; i + 1 < p->macro->template_count; i++)
      if (p->macro->templates[i].decl_name &&
          !strcmp(s, p->macro->templates[i].decl_name)) {
        x->kind = NTF_X_NAME;
        x->resolved = UINT32_C(0x10000000) | (NtFId)i;
        x->origin = span;
        return id;
      }
  }
  if (p->macro && (p->macro->result_block || p->macro->decl_function)) {
    for (size_t i = 0; i + 1 < p->macro->template_count; i++)
      if (p->macro->templates[i].introduced_name &&
          !strcmp(s, p->macro->templates[i].introduced_name)) {
        x->kind = NTF_X_NAME;
        x->resolved = UINT32_C(0x40000000) | (NtFId)i;
        x->origin = span;
        return id;
      }
  }
  if (!strcmp(s, "true") || !strcmp(s, "false")) {
    x->kind = NTF_X_BOOL;
    x->value = !strcmp(s, "true");
    x->constant = 1;
    return id;
  }
  NtFRegister reg = nt_frontend_register(s);
  if (reg.family >= 0) {
    x->kind = NTF_X_REGISTER;
    x->reg = reg;
    return id;
  }
  if (accept(p, '(')) {
    x->kind = NTF_X_CALL;
    NtFId first = 0, last = 0;
    lines(p);
    while (p->token.kind != ')' && !p->failed) {
      NtFId arg = parse_expression(p, 0);
      if (!arg)
        return 0;
      if (!first)
        first = arg;
      else
        EX(p->p, last).next = arg;
      last = arg;
      lines(p);
      if (!accept(p, ','))
        break;
      lines(p);
    }
    x = &EX(p->p, id);
    x->first_arg = first;
    if (!expect(p, ')', "expected ')' after arguments"))
      return 0;
    return id;
  }
  x->kind = NTF_X_NAME;
  x->resolved = find_variable(p, s);
  return id;
}
static NtFId parse_memory(Parser *p) {
  NtFSpan span = p->token.span;
  if (!expect(p, '[', "expected '['"))
    return 0;
  NtFId id = add_expression(p->p, span);
  if (!id) {
    p->failed = 1;
    return 0;
  }
  NtFExpr *x = &EX(p->p, id);
  x->kind = NTF_X_MEMORY;
  x->span = span;
  x->reg.family = -1;
  x->memory.base.family = -1;
  x->memory.index.family = -1;
  x->memory.scale = 1;
  if (is(p, "fs") || is(p, "gs")) {
    int segment = is(p, "fs") ? 1 : 2;
    next(p);
    if (!expect(p, ':', "expected ':' after memory segment"))
      return 0;
    EX(p->p, id).memory.segment = (unsigned)segment;
  }
  int sign = 1;
  int have_term = 0;
  for (;;) {
    NtFSpan address_span = p->token.span;
    if (p->token.kind == TK_NAME) {
      const char *address = name(p, &address_span);
      NtFRegister reg = nt_frontend_register(address);
      x = &EX(p->p, id);
      if (reg.family >= 0 && reg.family < 16) {
        if (sign < 0 || reg.bits != 64) {
          diag(p->p, NTF_E_MEMORY, address_span,
               "address registers must be positive 64-bit terms");
          p->failed = 1;
          return 0;
        }
        unsigned scale = 1;
        int scaled = accept(p, '*');
        if (scaled) {
          if (p->token.kind != TK_INT ||
              (p->token.value != 1 && p->token.value != 2 &&
               p->token.value != 4 && p->token.value != 8)) {
            diag(p->p, NTF_E_MEMORY, p->token.span,
                 "memory scale must be 1, 2, 4 or 8");
            p->failed = 1;
            return 0;
          }
          scale = (unsigned)p->token.value;
          next(p);
        }
        x = &EX(p->p, id);
        if (scaled) {
          if (reg.family == 4 || x->memory.index.family >= 0 ||
              x->memory.symbol) {
            diag(p->p, NTF_E_MEMORY, address_span,
                 "invalid or duplicate memory index");
            p->failed = 1;
            return 0;
          }
          x->memory.index = reg;
          x->memory.scale = scale;
        } else if (x->memory.base.family < 0 && !x->memory.symbol) {
          x->memory.base = reg;
        } else if (x->memory.index.family < 0 && !x->memory.symbol &&
                   reg.family != 4) {
          x->memory.index = reg;
          x->memory.scale = 1;
        } else {
          diag(p->p, NTF_E_MEMORY, address_span, "too many address registers");
          p->failed = 1;
          return 0;
        }
      } else {
        if (sign < 0 || x->memory.symbol || x->memory.base.family >= 0 ||
            x->memory.index.family >= 0) {
          diag(p->p, NTF_E_MEMORY, address_span,
               "symbol address cannot be combined with registers");
          p->failed = 1;
          return 0;
        }
        x->memory.symbol = address;
        x->resolved = find_variable(p, address);
      }
    } else if (p->token.kind == TK_INT) {
      uint64_t limit = sign < 0 ? UINT64_C(2147483648) : INT32_MAX;
      if (p->token.value > limit) {
        diag(p->p, NTF_E_MEMORY, p->token.span,
             "memory displacement is outside disp32");
        p->failed = 1;
        return 0;
      }
      int64_t amount = (int64_t)p->token.value * sign;
      int64_t total = EX(p->p, id).memory.displacement + amount;
      if (total < INT32_MIN || total > INT32_MAX) {
        diag(p->p, NTF_E_MEMORY, p->token.span,
             "combined memory displacement is outside disp32");
        p->failed = 1;
        return 0;
      }
      EX(p->p, id).memory.displacement = total;
      next(p);
    } else {
      syntax(p, "expected memory address term");
      return 0;
    }
    have_term = 1;
    if (p->token.kind != '+' && p->token.kind != '-')
      break;
    sign = p->token.kind == '-' ? -1 : 1;
    next(p);
  }
  if (!have_term)
    return syntax(p, "empty memory address"), (NtFId)0;
  if (!expect(p, ']', "expected ']' after address") ||
      !expect(p, ':', "expected pointer annotation after memory"))
    return 0;
  NtFId pointer = parse_type(p);
  if (!pointer || TY(p->p, pointer).kind != NTF_T_PTR) {
    if (!p->failed) {
      diag(p->p, NTF_E_MEMORY, span,
           "memory annotation must be a pointer type");
      p->failed = 1;
    }
    return 0;
  }
  x = &EX(p->p, id);
  x->type = pointer;
  x->span.end = p->token.span.start;
  return id;
}
static NtFId parse_operand(Parser *p) {
  return p->token.kind == '[' ? parse_memory(p) : parse_expression(p, 0);
}
static NtFOp unary_op(int kind) {
  if (kind == '-')
    return NTF_OP_NEG;
  if (kind == '~' || kind == '!')
    return NTF_OP_NOT;
  if (kind == '+')
    return NTF_OP_POS;
  return NTF_OP_NONE;
}
static NtFId unary(Parser *p) {
  NtFOp op = unary_op(p->token.kind);
  if (!op)
    return primary(p);
  NtFSpan span = p->token.span;
  next(p);
  NtFId operand = unary(p);
  if (!operand)
    return 0;
  NtFId id = add_expression(p->p, span);
  if (!id) {
    p->failed = 1;
    return 0;
  }
  NtFExpr *x = &EX(p->p, id);
  x->kind = NTF_X_UNARY;
  x->span = joined(span, EX(p->p, operand).span);
  x->left = operand;
  x->op = op;
  return id;
}
static int binary_op(int kind, NtFOp *op, unsigned *precedence) {
  switch (kind) {
  case TK_LOR:
    *op = NTF_OP_LOGICAL_OR;
    *precedence = 1;
    return 1;
  case TK_LAND:
    *op = NTF_OP_LOGICAL_AND;
    *precedence = 2;
    return 1;
  case '*':
    *op = NTF_OP_MUL;
    *precedence = 21;
    return 1;
  case '/':
    *op = NTF_OP_DIV;
    *precedence = 21;
    return 1;
  case '%':
    *op = NTF_OP_MOD;
    *precedence = 21;
    return 1;
  case '+':
    *op = NTF_OP_ADD;
    *precedence = 18;
    return 1;
  case '-':
    *op = NTF_OP_SUB;
    *precedence = 18;
    return 1;
  case TK_SHL:
    *op = NTF_OP_SHL;
    *precedence = 15;
    return 1;
  case TK_SHR:
    *op = NTF_OP_SHR;
    *precedence = 15;
    return 1;
  case '&':
    *op = NTF_OP_AND;
    *precedence = 12;
    return 1;
  case '^':
    *op = NTF_OP_XOR;
    *precedence = 9;
    return 1;
  case '|':
    *op = NTF_OP_OR;
    *precedence = 6;
    return 1;
  case TK_EQ:
    *op = NTF_OP_EQ;
    *precedence = 3;
    return 1;
  case TK_NE:
    *op = NTF_OP_NE;
    *precedence = 3;
    return 1;
  case '<':
    *op = NTF_OP_LT;
    *precedence = 3;
    return 1;
  case TK_LE:
    *op = NTF_OP_LE;
    *precedence = 3;
    return 1;
  case '>':
    *op = NTF_OP_GT;
    *precedence = 3;
    return 1;
  case TK_GE:
    *op = NTF_OP_GE;
    *precedence = 3;
    return 1;
  default:
    return 0;
  }
}
static NtFId parse_expression(Parser *p, unsigned minimum) {
  NtFId left = unary(p);
  while (left && !p->failed) {
    NtFOp op;
    unsigned precedence;
    if (!binary_op(p->token.kind, &op, &precedence) || precedence < minimum)
      break;
    NtFSpan operator_span = p->token.span;
    next(p);
    NtFId right = parse_expression(p, precedence + 1);
    if (!right)
      return 0;
    NtFId id = add_expression(p->p, operator_span);
    if (!id) {
      p->failed = 1;
      return 0;
    }
    NtFExpr *x = &EX(p->p, id);
    x->kind = NTF_X_BINARY;
    x->span = joined(EX(p->p, left).span, EX(p->p, right).span);
    x->left = left;
    x->right = right;
    x->op = op;
    left = id;
  }
  return left;
}

static int type_equal(NtFProgram *p, NtFId a, NtFId b) {
  if (a == b)
    return 1;
  if (!a || !b)
    return 0;
  NtFType *x = &TY(p, a), *y = &TY(p, b);
  return x->kind == y->kind && x->space == y->space && x->count == y->count &&
         x->bits == y->bits && type_equal(p, x->element, y->element);
}
static int integer_kind(NtFTypeKind k) {
  return k >= NTF_T_U8 && k <= NTF_T_I64;
}
static int literal_fits(NtFType *t, uint64_t value, int negative) {
  unsigned bits = t->bits;
  int signed_type = t->kind >= NTF_T_I8 && t->kind <= NTF_T_I64;
  if (!integer_kind(t->kind))
    return 0;
  if (negative) {
    if (!signed_type)
      return 0;
    return bits == 64 ? value <= UINT64_C(0x8000000000000000)
                      : value <= (UINT64_C(1) << (bits - 1));
  }
  if (bits == 64)
    return signed_type ? value <= INT64_MAX : 1;
  return value <= (signed_type ? ((UINT64_C(1) << (bits - 1)) - 1)
                               : ((UINT64_C(1) << bits) - 1));
}
static size_t decoded_string_length(const NtFExpr *x) {
  size_t decoded = 0;
  for (size_t i = 0; i < x->string_length; i++) {
    if (x->name[i] == '\\') {
      ++i;
      if (x->name[i] == 'x')
        i += 2;
    }
    ++decoded;
  }
  return decoded;
}
static NtFId find_function(NtFProgram *p, NtFId module, const char *s) {
  for (size_t i = 0; i < p->function_count; i++)
    if (p->functions[i].module == module && !strcmp(p->functions[i].name, s))
      return (NtFId)(i + 1);
  for (size_t i = 0; i < p->function_count; i++)
    if (p->functions[i].exported && !strcmp(p->functions[i].name, s))
      return (NtFId)(i + 1);
  return 0;
}
static int constant_value(NtFProgram *, NtFId, uint64_t *, int *);
static int same_fact(NtFProgram *p, NtFId list, const char *name, NtFId subject,
                     uint64_t value) {
  for (NtFId id = list; id; id = EX(p, id).next) {
    NtFExpr *predicate = &EX(p, id);
    NtFId predicate_subject =
        predicate->first_arg ? EX(p, predicate->first_arg).resolved : 0;
    if (!strcmp(predicate->name, name) && predicate_subject == subject &&
        predicate->value == value)
      return 1;
  }
  return 0;
}
static NtFId call_argument_for_parameter(NtFProgram *p, const NtFExpr *call,
                                         const NtFFunction *callee,
                                         NtFId parameter) {
  if (parameter < callee->first_param ||
      parameter >= callee->first_param + callee->param_count)
    return 0;
  if (VA(p, parameter).direction == NTF_OUT)
    return 0;
  uint32_t index = 0;
  for (NtFId id = callee->first_param; id < parameter; id++)
    if (VA(p, id).direction != NTF_OUT)
      ++index;
  NtFId argument = call->first_arg;
  while (index-- && argument)
    argument = EX(p, argument).next;
  return argument;
}
static int call_predicate_proved(NtFProgram *p, const NtFFunction *caller,
                                 const NtFFunction *callee, const NtFExpr *call,
                                 NtFId predicate_id) {
  NtFExpr *predicate = &EX(p, predicate_id);
  if (!strcmp(predicate->name, "feature"))
    return !((uint32_t)predicate->value &
             ~p->modules[caller->module - 1].features);
  if (!strcmp(predicate->name, "stack_aligned")) {
    uint64_t alignment;
    int negative;
    return constant_value(p, predicate->first_arg, &alignment, &negative) &&
           !negative && alignment && alignment <= 16 && (16 % alignment) == 0;
  }
  if (!strcmp(predicate->name, "non_null") ||
      !strcmp(predicate->name, "canonical")) {
    NtFExpr *subject = &EX(p, predicate->first_arg);
    NtFId argument =
        call_argument_for_parameter(p, call, callee, subject->resolved);
    if (!argument)
      return 0;
    NtFExpr *actual = &EX(p, argument);
    uint64_t value;
    int negative;
    if (constant_value(p, argument, &value, &negative)) {
      if (!strcmp(predicate->name, "non_null"))
        return value != 0;
      uint64_t bits = negative ? UINT64_C(0) - value : value;
      uint64_t top = bits >> 48;
      return top == 0 || top == UINT64_C(0xffff);
    }
    return actual->kind == NTF_X_NAME && actual->resolved &&
           same_fact(p, caller->requires, predicate->name, actual->resolved, 0);
  }
  return 0;
}
static int constant_value(NtFProgram *, NtFId, uint64_t *, int *);
static NtFId infer(NtFProgram *, NtFId, NtFId, NtFId);
static int constant_value(NtFProgram *p, NtFId id, uint64_t *value,
                          int *negative) {
  NtFExpr *x = &EX(p, id);
  if (x->kind == NTF_X_LITERAL) {
    *value = x->value;
    *negative = x->negative;
    return 1;
  }
  if (x->kind == NTF_X_BOOL) {
    *value = x->value;
    *negative = 0;
    return 1;
  }
  if (x->kind == NTF_X_STRING) {
    *value = x->string_length;
    *negative = 0;
    return 1;
  }
  if (x->kind == NTF_X_NAME && x->resolved) {
    NtFVariable *v = &VA(p, x->resolved);
    return v->direction == NTF_CONST && v->initializer
               ? constant_value(p, v->initializer, value, negative)
               : 0;
  }
  if (x->kind == NTF_X_UNARY) {
    uint64_t operand;
    int neg;
    if (!constant_value(p, x->left, &operand, &neg))
      return 0;
    if (x->op == NTF_OP_POS) {
      *value = operand;
      *negative = neg;
      return 1;
    }
    if (x->op == NTF_OP_NEG) {
      if (neg)
        return 0;
      *value = operand;
      *negative = operand != 0;
      return 1;
    }
    if (x->op == NTF_OP_NOT) {
      *value = ~operand;
      *negative = 0;
      return 1;
    }
    return 0;
  }
  if (x->kind != NTF_X_BINARY)
    return 0;
  uint64_t left, right;
  int ln, rn;
  if (!constant_value(p, x->left, &left, &ln) ||
      !constant_value(p, x->right, &right, &rn) || ln || rn)
    return 0;
  *negative = 0;
  switch (x->op) {
  case NTF_OP_ADD:
    if (UINT64_MAX - left < right)
      return 0;
    *value = left + right;
    return 1;
  case NTF_OP_SUB:
    if (left < right) {
      *value = right - left;
      *negative = *value != 0;
    } else
      *value = left - right;
    return 1;
  case NTF_OP_MUL:
    if (right && left > UINT64_MAX / right)
      return 0;
    *value = left * right;
    return 1;
  case NTF_OP_DIV:
    if (!right)
      return 0;
    *value = left / right;
    return 1;
  case NTF_OP_MOD:
    if (!right)
      return 0;
    *value = left % right;
    return 1;
  case NTF_OP_AND:
    *value = left & right;
    return 1;
  case NTF_OP_OR:
    *value = left | right;
    return 1;
  case NTF_OP_XOR:
    *value = left ^ right;
    return 1;
  case NTF_OP_SHL:
    if (right >= 64)
      return 0;
    *value = left << right;
    return 1;
  case NTF_OP_SHR:
    if (right >= 64)
      return 0;
    *value = left >> right;
    return 1;
  case NTF_OP_EQ:
    *value = left == right;
    return 1;
  case NTF_OP_NE:
    *value = left != right;
    return 1;
  case NTF_OP_LT:
    *value = left < right;
    return 1;
  case NTF_OP_LE:
    *value = left <= right;
    return 1;
  case NTF_OP_GT:
    *value = left > right;
    return 1;
  case NTF_OP_GE:
    *value = left >= right;
    return 1;
  default:
    return 0;
  }
}
static int symbolic_address(NtFProgram *p, NtFId id, NtFId *target,
                            int64_t *addend, unsigned depth) {
  if (!id || id > p->expression_count || depth > 32)
    return 0;
  NtFExpr *expression = &EX(p, id);
  if (expression->kind == NTF_X_ADDRESS) {
    *target = expression->resolved;
    *addend = expression->memory.displacement;
    return *target && *target <= p->variable_count;
  }
  if (expression->kind == NTF_X_NAME && expression->resolved &&
      expression->resolved <= p->variable_count) {
    NtFVariable *variable = &VA(p, expression->resolved);
    if (variable->direction == NTF_CONST)
      return symbolic_address(p, variable->initializer, target, addend,
                              depth + 1);
  }
  return 0;
}
static NtFId infer(NtFProgram *p, NtFId id, NtFId expected, NtFId owner) {
  NtFExpr *x = &EX(p, id);
  switch (x->kind) {
  case NTF_X_LITERAL: {
    if (expected && !integer_kind(TY(p, expected).kind)) {
      diag(p, NTF_E_TYPE, x->span, "integer literal cannot initialize %s",
           nt_frontend_type_name(TY(p, expected).kind));
      return 0;
    }
    if (!expected) {
      NtFType t = {NTF_T_U64, NTF_SPACE_NONE, 0, 0, 64};
      expected = intern_type(p, t, x->span);
    }
    if (!literal_fits(&TY(p, expected), x->value, x->negative)) {
      diag(p, NTF_E_LITERAL, x->span, "literal is outside %s range",
           nt_frontend_type_name(TY(p, expected).kind));
      return 0;
    }
    x->type = expected;
    return expected;
  }
  case NTF_X_BOOL: {
    NtFType t = {NTF_T_BOOL, NTF_SPACE_NONE, 0, 0, 1};
    x->type = intern_type(p, t, x->span);
    if (expected && !type_equal(p, x->type, expected)) {
      diag(p, NTF_E_TYPE, x->span, "bool type mismatch");
      return 0;
    }
    return x->type;
  }
  case NTF_X_NAME:
    if (!x->resolved) {
      diag(p, NTF_E_NAME, x->span, "unknown name '%s'", x->name);
      return 0;
    }
    x->type = VA(p, x->resolved).type;
    if (expected && !type_equal(p, x->type, expected)) {
      diag(p, NTF_E_TYPE, x->span, "type mismatch for '%s'", x->name);
      return 0;
    }
    return x->type;
  case NTF_X_REGISTER: {
    NtFTypeKind k = x->reg.bits == 8    ? NTF_T_U8
                    : x->reg.bits == 16 ? NTF_T_U16
                    : x->reg.bits == 32 ? NTF_T_U32
                                        : NTF_T_U64;
    NtFType t = {k, NTF_SPACE_NONE, 0, 0, x->reg.bits};
    x->type = intern_type(p, t, x->span);
    if (expected && TY(p, expected).bits != x->reg.bits) {
      diag(p, NTF_E_TYPE, x->span, "register width mismatch");
      return 0;
    }
    return x->type;
  }
  case NTF_X_CALL: {
    NtFId callee = find_function(p, FN(p, owner).module, x->name);
    if (!callee) {
      diag(p, NTF_E_NAME, x->span, "unknown function '%s'", x->name);
      return 0;
    }
    x->resolved = callee;
    NtFFunction *target = &FN(p, callee);
    NtFFunction *caller = &FN(p, owner);
    for (NtFId predicate = target->requires; predicate;
         predicate = EX(p, predicate).next) {
      if (!call_predicate_proved(p, caller, target, x, predicate)) {
        diag(p, NTF_E_CONTRACT, x->span,
             "call '%s' does not satisfy required predicate '%s'", target->name,
             EX(p, predicate).name);
        return 0;
      }
    }
    caller->inferred_effects.flags |= target->effects.flags;
    caller->inferred_effects.reads |= target->effects.reads;
    caller->inferred_effects.writes |= target->effects.writes;
    caller->inferred_clobbers |= target->clobbers;
    caller->write_footprint |= target->write_footprint;
    uint64_t outputs = nt_conformance_output_registers(p, callee);
    caller->inferred_clobbers |= outputs;
    caller->write_footprint |= outputs;
    NtFId arg = x->first_arg;
    uint32_t expected_arguments = 0;
    for (uint32_t i = 0; i < target->param_count; i++) {
      NtFVariable *parameter = &VA(p, target->first_param + i);
      if (parameter->direction == NTF_OUT)
        continue;
      ++expected_arguments;
      if (!arg) {
        diag(p, NTF_E_ABI, x->span, "call '%s' expects %u arguments",
             target->name, expected_arguments);
        return 0;
      }
      if (!infer(p, arg, parameter->type, owner))
        return 0;
      arg = EX(p, arg).next;
    }
    if (arg) {
      diag(p, NTF_E_ABI, x->span, "call '%s' has too many arguments",
           target->name);
      return 0;
    }
    x->type = target->return_type;
    if (expected && !type_equal(p, x->type, expected)) {
      diag(p, NTF_E_TYPE, x->span, "call return type mismatch");
      return 0;
    }
    return x->type;
  }
  case NTF_X_UNARY: {
    NtFExpr *operand = &EX(p, x->left);
    if (x->op == NTF_OP_NEG && operand->kind == NTF_X_LITERAL) {
      if (!expected || !integer_kind(TY(p, expected).kind)) {
        NtFType signed64 = {NTF_T_I64, NTF_SPACE_NONE, 0, 0, 64};
        expected = intern_type(p, signed64, x->span);
      }
      if (!literal_fits(&TY(p, expected), operand->value, 1)) {
        diag(p, NTF_E_LITERAL, x->span, "negative literal is outside %s range",
             nt_frontend_type_name(TY(p, expected).kind));
        return 0;
      }
      operand->type = expected;
      x->type = expected;
      x->constant = 1;
      x->negative = operand->value != 0;
      x->value = operand->value;
      return x->type;
    }
  case NTF_X_STRING: {
    if (!expected || TY(p, expected).kind != NTF_T_ARRAY) {
      diag(p, NTF_E_TYPE, x->span, "string initializer requires an array type");
      return 0;
    }
    NtFType *array = &TY(p, expected);
    if (TY(p, array->element).kind != NTF_T_U8 ||
        array->count != decoded_string_length(x)) {
      diag(p, NTF_E_TYPE, x->span,
           "string byte length does not match array<u8,N>");
      return 0;
    }
    x->type = expected;
    return expected;
  }
  case NTF_X_MEMORY: {
    NtFType *pointer = &TY(p, x->type);
    if (pointer->kind != NTF_T_PTR) {
      diag(p, NTF_E_MEMORY, x->span, "memory operand lacks pointer type");
      return 0;
    }
    if (x->memory.symbol) {
      if (!x->resolved) {
        diag(p, NTF_E_NAME, x->span, "unknown data symbol '%s'",
             x->memory.symbol);
        return 0;
      }
      NtFVariable *symbol = &VA(p, x->resolved);
      if (symbol->direction != NTF_DATA) {
        diag(p, NTF_E_MEMORY, x->span, "memory symbol '%s' is not static data",
             x->memory.symbol);
        return 0;
      }
      if (!type_equal(p, pointer->element, symbol->type)) {
        diag(p, NTF_E_TYPE, x->span,
             "pointer annotation does not match data symbol type");
        return 0;
      }
      x->memory.provenance = x->resolved;
    }
    return x->type;
  }
  case NTF_X_ADDRESS:
    if (!x->resolved || x->resolved > p->variable_count ||
        VA(p, x->resolved).direction != NTF_DATA) {
      diag(p, NTF_E_NAME, x->span, "address_of target is invalid");
      return 0;
    }
    if (expected && !type_equal(p, x->type, expected)) {
      diag(p, NTF_E_TYPE, x->span, "address_of pointer type mismatch");
      return 0;
    }
    return x->type;
    x->type = infer(p, x->left, expected, owner);
    return x->type;
  }
  case NTF_X_BINARY: {
    if (x->op == NTF_OP_LOGICAL_AND || x->op == NTF_OP_LOGICAL_OR) {
      NtFType boolean = {NTF_T_BOOL, NTF_SPACE_NONE, 0, 0, 1};
      NtFId bool_type = intern_type(p, boolean, x->span);
      NtFId left = infer(p, x->left, bool_type, owner);
      NtFId right = infer(p, x->right, bool_type, owner);
      if (!left || !right)
        return 0;
      if (expected && !type_equal(p, expected, bool_type)) {
        diag(p, NTF_E_TYPE, x->span, "logical operators require bool operands");
        return 0;
      }
      x->type = bool_type;
      return x->type;
    }
    int comparison = x->op >= NTF_OP_EQ && x->op <= NTF_OP_GE;
    NtFId left = infer(p, x->left, comparison ? 0 : expected, owner);
    NtFId right = infer(p, x->right, left, owner);
    if (!left || !right)
      return 0;
    if (comparison) {
      NtFType t = {NTF_T_BOOL, NTF_SPACE_NONE, 0, 0, 1};
      x->type = intern_type(p, t, x->span);
    } else
      x->type = left;
    uint64_t value;
    int negative;
    if (constant_value(p, id, &value, &negative)) {
      x->constant = 1;
      x->value = value;
      x->negative = negative;
      if (expected && !literal_fits(&TY(p, expected), value, negative)) {
        diag(p, NTF_E_LITERAL, x->span,
             "constant expression outside result range");
        return 0;
      }
    } else if ((x->op == NTF_OP_DIV || x->op == NTF_OP_MOD) &&
               EX(p, x->right).constant && EX(p, x->right).value == 0) {
      diag(p, NTF_E_LITERAL, x->span, "division by zero");
      return 0;
    }
    return x->type;
  }
  default:
    diag(p, NTF_E_UNSUPPORTED, x->span, "expression form is not implemented");
    return 0;
  }
}

static int parse_effects(Parser *p, NtFFunction *f) {
  if (!expect_word(p, "effects") || !expect(p, '{', "expected effects set"))
    return 0;
  lines(p);
  while (p->token.kind != '}' && !p->failed) {
    NtFSpan span;
    const char *s = name(p, &span);
    uint32_t flag = 0, reads = 0, writes = 0;
    if (!strcmp(s, "privileged"))
      flag = NTF_F_PRIVILEGED;
    else if (!strcmp(s, "changes_flags"))
      flag = NTF_F_CHANGES_FLAGS;
    else if (!strcmp(s, "mmio"))
      flag = NTF_F_MMIO;
    else if (!strcmp(s, "io_port"))
      flag = NTF_F_IO_PORT;
    else if (!strcmp(s, "msr"))
      flag = NTF_F_MSR;
    else if (!strcmp(s, "control"))
      flag = NTF_F_CONTROL;
    else if (!strcmp(s, "interrupt_state"))
      flag = NTF_F_INTERRUPT_STATE;
    else if (!strcmp(s, "no_return"))
      flag = NTF_F_NO_RETURN;
    else if (!strcmp(s, "memory_ordering"))
      flag = NTF_F_MEMORY_ORDERING;
    else if (!strcmp(s, "reads_clock"))
      flag = NTF_F_READS_CLOCK;
    else if (!strcmp(s, "suspends"))
      flag = NTF_F_SUSPENDS;
    else if (!strcmp(s, "reads_mem"))
      reads = 126;
    else if (!strcmp(s, "writes_mem"))
      writes = 126;
    else {
      diag(p->p, NTF_E_EFFECT, span, "unknown effect '%s'", s);
      p->failed = 1;
      return 0;
    }
    if ((reads || writes) && accept(p, '(')) {
      const char *space_name = name(p, NULL);
      NtFSpace space = space_kind(space_name);
      if (!space || !expect(p, ')', "expected ')' after memory effect"))
        return 0;
      reads = reads ? 1u << space : 0;
      writes = writes ? 1u << space : 0;
    }
    f->effects.flags |= flag;
    f->effects.reads |= reads;
    f->effects.writes |= writes;
    lines(p);
    if (!accept(p, ','))
      break;
    lines(p);
  }
  return expect(p, '}', "expected '}' after effects");
}
static int parse_clobbers(Parser *p, NtFFunction *f) {
  if (!expect_word(p, "clobbers") || !expect(p, '{', "expected clobber set"))
    return 0;
  lines(p);
  while (p->token.kind != '}' && !p->failed) {
    NtFSpan span;
    const char *s = name(p, &span);
    NtFRegister reg = nt_frontend_register(s);
    if (reg.family < 0 || reg.family >= 64) {
      diag(p->p, NTF_E_REGISTER, span, "unknown clobber '%s'", s);
      p->failed = 1;
      return 0;
    }
    uint64_t bit = BIT((unsigned)reg.family);
    if (f->clobbers & bit) {
      diag(p->p, NTF_E_DUPLICATE, span, "duplicate clobber '%s'", s);
      p->failed = 1;
      return 0;
    }
    f->clobbers |= bit;
    lines(p);
    if (!accept(p, ','))
      break;
    lines(p);
  }
  return expect(p, '}', "expected '}' after clobbers");
}
static int parse_predicates(Parser *p, NtFId *first) {
  if (!expect(p, '{', "expected predicate set"))
    return 0;
  lines(p);
  NtFId last = 0;
  while (p->token.kind != '}' && !p->failed) {
    NtFSpan span;
    const char *s = compound(p, '.', &span);
    static const char *known[] = {"feature",
                                  "non_null",
                                  "canonical",
                                  "stack_in",
                                  "stack_aligned",
                                  "msr_initialized",
                                  "interrupts_enabled",
                                  "interrupts_disabled",
                                  "mapping_present",
                                  "valid_user_return"};
    int found = 0;
    for (size_t i = 0; i < sizeof known / sizeof known[0]; i++)
      if (!strcmp(s, known[i]))
        found = 1;
    if (!found) {
      diag(p->p, NTF_E_CONTRACT, span, "unknown predicate '%s'", s);
      p->failed = 1;
      return 0;
    }
    if (strcmp(s, "feature") && strcmp(s, "non_null") &&
        strcmp(s, "canonical") && strcmp(s, "stack_aligned")) {
      diag(p->p, NTF_E_UNSUPPORTED, span,
           "predicate '%s' is recognized but proof checking is not implemented",
           s);
      p->failed = 1;
      return 0;
    }
    if (!expect(p, '(', "expected '(' after predicate"))
      return 0;
    NtFId argument = 0;
    uint64_t predicate_value = 0;
    if (!strcmp(s, "feature")) {
      NtFSpan feature_span;
      const char *feature = compound(p, '.', &feature_span);
      if (!feature || !expect(p, ')', "expected ')' after feature"))
        return 0;
      predicate_value = target_feature_bit(feature);
      if (!predicate_value) {
        diag(p->p, NTF_E_CONTRACT, feature_span,
             "unknown or unavailable feature '%s'", feature);
        p->failed = 1;
        return 0;
      }
    } else {
      argument = parse_expression(p, 0);
      if (!argument || !expect(p, ')', "expected ')' after predicate argument"))
        return 0;
    }
    NtFId predicate = add_expression(p->p, span);
    if (!predicate) {
      p->failed = 1;
      return 0;
    }
    NtFExpr *x = &EX(p->p, predicate);
    x->kind = NTF_X_PREDICATE;
    x->span = span;
    x->name = s;
    x->value = predicate_value;
    x->first_arg = argument;
    x->constant = 1;
    x->reg.family = -1;
    if (!*first)
      *first = predicate;
    else
      EX(p->p, last).next = predicate;
    last = predicate;
    lines(p);
    if (!accept(p, ','))
      break;
    lines(p);
  }
  return expect(p, '}', "expected '}' after predicates");
}
static Macro *find_macro(Parser *p, const char *name) {
  Private *v = p->p->_private;
  for (size_t i = 0; i < v->macro_count; i++)
    if (!strcmp(v->macros[i].name, name))
      return &v->macros[i];
  return NULL;
}
static int validate_macro_arguments(Parser *p, const Macro *macro,
                                    const NtFId *arguments, size_t count,
                                    NtFSpan span) {
  if (count != macro->parameter_count)
    return 0;
  for (size_t i = 0; i < count; i++) {
    if (macro->parameter_kinds[i] != 3)
      continue;
    uint64_t value;
    int negative;
    if (!constant_value(p->p, arguments[i], &value, &negative)) {
      diag(p->p, NTF_E_CONTRACT, span,
           "typed comptime macro argument is not constant");
      p->failed = 1;
      return 0;
    }
    NtFType *type = &TY(p->p, macro->parameter_types[i]);
    if ((type->kind == NTF_T_BOOL && (negative || value > 1)) ||
        (type->kind != NTF_T_BOOL && !literal_fits(type, value, negative))) {
      diag(p->p, NTF_E_LITERAL, span,
           "typed comptime macro argument is outside its declared type");
      p->failed = 1;
      return 0;
    }
  }
  return 1;
}
static int consume_fuel(Parser *p, NtFSpan span) {
  Private *v = p->p->_private;
  if (v->fuel_used >= v->fuel_limit) {
    diag(p->p, NTF_E_LIMIT, span, "comptime fuel exhausted");
    p->failed = 1;
    return 0;
  }
  ++v->fuel_used;
  return 1;
}
static NtFId clone_macro_type(Parser *p, NtFId type_id,
                              const NtFId *arguments, size_t argument_count,
                              NtFSpan invocation);
static NtFId clone_macro_expression(Parser *p, NtFId source,
                                    const NtFId *arguments,
                                    size_t argument_count, NtFSpan invocation) {
  NtFExpr original = EX(p->p, source);
  if ((original.resolved & UINT32_C(0xf0000000)) == UINT32_C(0x10000000)) {
    size_t index = (size_t)(original.resolved & UINT32_C(0x0fffffff));
    if (!p->macro || index >= p->macro->template_count ||
        !p->macro->expanded_declarations[index]) {
      diag(p->p, NTF_E_CONTRACT, invocation,
           "macro declaration reference is unresolved");
      p->failed = 1;
      return 0;
    }
    NtFId id = add_expression(p->p, invocation);
    if (!id) {
      p->failed = 1;
      return 0;
    }
    NtFExpr copy = original;
    copy.span = invocation;
    copy.origin = original.span;
    copy.invocation = invocation;
    copy.resolved = p->macro->expanded_declarations[index];
    copy.name = VA(p->p, copy.resolved).name;
    copy.next = 0;
    EX(p->p, id) = copy;
    return id;
  }
  if (original.resolved == UINT32_C(0x20000000)) {
    if (!p->macro || !p->macro->meta_for) {
      diag(p->p, NTF_E_CONTRACT, invocation,
           "meta-for value used outside expansion");
      p->failed = 1;
      return 0;
    }
    if (!consume_fuel(p, invocation))
      return 0;
    NtFId id = add_expression(p->p, invocation);
    if (!id) {
      p->failed = 1;
      return 0;
    }
    NtFExpr value = {0};
    value.kind = NTF_X_LITERAL;
    value.span = invocation;
    value.origin = original.span;
    value.invocation = invocation;
    value.value = p->macro->loop_value;
    value.constant = 1;
    value.reg.family = -1;
    EX(p->p, id) = value;
    return id;
  }
  if ((original.resolved & UINT32_C(0xf0000000)) == UINT32_C(0x30000000)) {
    size_t index = (size_t)(original.resolved & UINT32_C(0x0fffffff));
    if (!p->macro || index >= p->macro->meta_count) {
      diag(p->p, NTF_E_CONTRACT, invocation,
           "macro meta-let reference is unresolved");
      p->failed = 1;
      return 0;
    }
    if (!consume_fuel(p, invocation))
      return 0;
    return clone_macro_expression(p, p->macro->meta_values[index], arguments,
                                  argument_count, invocation);
  }
  if ((original.resolved & UINT32_C(0xf0000000)) == UINT32_C(0x40000000)) {
    size_t index = (size_t)(original.resolved & UINT32_C(0x0fffffff));
    if (!p->macro || index >= p->macro->template_count ||
        !p->macro->expanded_variables[index]) {
      diag(p->p, NTF_E_CONTRACT, invocation,
           "macro hygienic reference is unresolved");
      p->failed = 1;
      return 0;
    }
    NtFId id = add_expression(p->p, invocation);
    if (!id) {
      p->failed = 1;
      return 0;
    }
    NtFExpr copy = original;
    copy.span = invocation;
    copy.origin = original.span;
    copy.invocation = invocation;
    copy.resolved = p->macro->expanded_variables[index];
    copy.name = VA(p->p, copy.resolved).name;
    copy.next = 0;
    EX(p->p, id) = copy;
    return id;
  }
  if ((original.resolved & UINT32_C(0xf0000000)) == UINT32_C(0x50000000)) {
    size_t index = (size_t)(original.resolved & UINT32_C(0x0fffffff));
    if (!p->macro || index >= p->macro->decl_param_count ||
        !p->macro->expanded_parameters[index]) {
      diag(p->p, NTF_E_CONTRACT, invocation,
           "generated function parameter reference is unresolved");
      p->failed = 1;
      return 0;
    }
    NtFId id = add_expression(p->p, invocation);
    if (!id) {
      p->failed = 1;
      return 0;
    }
    NtFExpr copy = original;
    copy.span = invocation;
    copy.origin = original.span;
    copy.invocation = invocation;
    copy.resolved = p->macro->expanded_parameters[index];
    copy.name = VA(p->p, copy.resolved).name;
    copy.next = 0;
    EX(p->p, id) = copy;
    return id;
  }
  if ((original.resolved & UINT32_C(0x80000000)) != 0) {
    size_t index = (size_t)(original.resolved & UINT32_C(0x7fffffff));
    if (index >= argument_count) {
      diag(p->p, NTF_E_CONTRACT, invocation, "macro splice index is invalid");
      p->failed = 1;
      return 0;
    }
    return clone_macro_expression(p, arguments[index], NULL, 0, invocation);
  }
  if (!consume_fuel(p, invocation))
    return 0;
  NtFId id = add_expression(p->p, invocation);
  if (!id) {
    p->failed = 1;
    return 0;
  }
  NtFExpr copy = original;
  copy.origin = original.span;
  copy.invocation = invocation;
  copy.span = invocation;
  copy.left = copy.right = copy.first_arg = copy.next = 0;
  EX(p->p, id) = copy;
  if (original.left) {
    NtFId child = clone_macro_expression(p, original.left, arguments,
                                         argument_count, invocation);
    if (!child)
      return 0;
    EX(p->p, id).left = child;
  }
  if (original.right) {
    NtFId child = clone_macro_expression(p, original.right, arguments,
                                         argument_count, invocation);
    if (!child)
      return 0;
    EX(p->p, id).right = child;
  }
  NtFId last = 0;
  for (NtFId arg = original.first_arg; arg; arg = EX(p->p, arg).next) {
    NtFId child =
        clone_macro_expression(p, arg, arguments, argument_count, invocation);
    if (!child)
      return 0;
    if (!EX(p->p, id).first_arg)
      EX(p->p, id).first_arg = child;
    else
      EX(p->p, last).next = child;
    last = child;
  }
  return id;
}
static NtFId expand_macro_statement(Parser *p, NtFSpan invocation, Macro *macro,
                                    const NtFId *arguments,
                                    size_t argument_count) {
  if (!validate_macro_arguments(p, macro, arguments, argument_count,
                                invocation))
    return 0;
  Private *v = p->p->_private;
  if (v->expansion_depth >= v->expansion_limit) {
    diag(p->p, NTF_E_LIMIT, invocation, "macro expansion budget exhausted");
    p->failed = 1;
    return 0;
  }
  ++v->expansion_depth;
  ++v->expansion_serial;
  if (!consume_fuel(p, invocation))
    return 0;
  if (macro->result_block) {
    NtFId block = add_statement(p->p, invocation);
    if (!block) {
      p->failed = 1;
      return 0;
    }
    ST(p->p, block).kind = NTF_S_BLOCK;
    ST(p->p, block).span = invocation;
    ST(p->p, block).origin = macro->template_span;
    ST(p->p, block).invocation = invocation;
    --v->expansion_depth;
    if (p->depth >= MAX_DEPTH) {
      diag(p->p, NTF_E_LIMIT, invocation, "macro block nesting limit exceeded");
      p->failed = 1;
      return 0;
    }
    p->scopes[p->depth++] = block;
    p->macro = macro;
    memset(macro->expanded_variables, 0, sizeof(macro->expanded_variables));
    size_t template_begin = 0, template_end = macro->template_count;
    if (macro->meta_if) {
      NtFId condition = clone_macro_expression(
          p, macro->meta_condition, arguments, argument_count, invocation);
      uint64_t value;
      int negative;
      if (!condition || !constant_value(p->p, condition, &value, &negative)) {
        diag(p->p, NTF_E_CONTRACT, invocation,
             "meta-if condition is not a deterministic constant");
        p->failed = 1;
        p->macro = NULL;
        --p->depth;
        return 0;
      }
      if (value && !negative)
        template_end = macro->else_start;
      else
        template_begin = macro->else_start;
    }
    uint64_t repeat_count = 1;
    if (macro->meta_for) {
      NtFId count_expression = clone_macro_expression(
          p, macro->loop_count, arguments, argument_count, invocation);
      int negative;
      if (!count_expression ||
          !constant_value(p->p, count_expression, &repeat_count, &negative) ||
          negative || repeat_count > 512 ||
          repeat_count * (template_end - template_begin) > 512) {
        diag(p->p, NTF_E_LIMIT, invocation,
             "meta-for iteration output exceeds 512 statements");
        p->failed = 1;
        p->macro = NULL;
        --p->depth;
        return 0;
      }
    }
    NtFId first = 0, last = 0;
    for (uint64_t repeat = 0; repeat < repeat_count; repeat++) {
      if (macro->meta_for) {
        macro->loop_value = repeat;
        memset(macro->expanded_variables, 0, sizeof(macro->expanded_variables));
      }
      for (size_t i = template_begin; i < template_end; i++) {
        MacroTemplate *item = &macro->templates[i];
        NtFId child = 0;
        if (item->meta_expand) {
          Macro *nested = find_macro(p, item->instruction_name);
          if (!nested) {
            diag(p->p, NTF_E_NAME, item->span, "unknown nested macro '%s'",
                 item->instruction_name);
            p->failed = 1;
            p->macro = NULL;
            --p->depth;
            return 0;
          }
          NtFId nested_arguments[8];
          size_t nested_count = 0;
          for (NtFId arg = item->expression; arg; arg = EX(p->p, arg).next) {
            if (nested_count == 8) {
              diag(p->p, NTF_E_LIMIT, item->span,
                   "nested macro argument limit exceeded");
              p->failed = 1;
              p->macro = NULL;
              --p->depth;
              return 0;
            }
            nested_arguments[nested_count] = clone_macro_expression(
                p, arg, arguments, argument_count, invocation);
            if (!nested_arguments[nested_count++]) {
              p->macro = NULL;
              --p->depth;
              return 0;
            }
          }
          if (nested_count != nested->parameter_count) {
            diag(p->p, NTF_E_CONTRACT, item->span,
                 "nested macro '%s' expects %zu arguments", nested->name,
                 nested->parameter_count);
            p->failed = 1;
            p->macro = NULL;
            --p->depth;
            return 0;
          }
          if (v->meta_call_depth >= v->call_depth_limit) {
            diag(p->p, NTF_E_LIMIT, item->span, "macro call depth exhausted");
            p->failed = 1;
            p->macro = NULL;
            --p->depth;
            return 0;
          }
          ++v->meta_call_depth;
          child = expand_macro_statement(p, invocation, nested,
                                         nested_arguments, nested_count);
          --v->meta_call_depth;
          p->macro = macro;
        } else {
          Macro single = *macro;
          single.result_block = 0;
          single.statement_kind = item->kind;
          single.template_expression = item->expression;
          single.template_type = item->type;
          single.introduced_name = item->introduced_name;
          single.instruction_name = item->instruction_name;
          single.template_span = item->span;
          child = expand_macro_statement(p, invocation, &single, arguments,
                                         argument_count);
        }
        if (!child) {
          p->macro = NULL;
          --p->depth;
          return 0;
        }
        if (item->kind == NTF_S_LET)
          macro->expanded_variables[i] = ST(p->p, child).variable;
        if (!first)
          first = child;
        else
          ST(p->p, last).next = child;
        last = child;
      }
    }
    p->macro = NULL;
    --p->depth;
    ST(p->p, block).first = first;
    ST(p->p, block).terminal = last ? ST(p->p, last).terminal : 0;
    return block;
  }
  NtFId expression = 0, last_expression = 0;
  Macro *previous_macro = p->macro;
  p->macro = macro;
  for (NtFId template_id = macro->template_expression; template_id;
       template_id = EX(p->p, template_id).next) {
    NtFId cloned = clone_macro_expression(p, template_id, arguments,
                                          argument_count, invocation);
    if (!cloned) {
      p->macro = previous_macro;
      return 0;
    }
    if (!expression)
      expression = cloned;
    else
      EX(p->p, last_expression).next = cloned;
    last_expression = cloned;
  }
  NtFId statement_type = macro->template_type;
  if (macro->statement_kind == NTF_S_LET) {
    statement_type = clone_macro_type(p, macro->template_type, arguments,
                                      argument_count, invocation);
    if (!statement_type) {
      p->macro = previous_macro;
      return 0;
    }
  }
  NtFId statement = add_statement(p->p, invocation);
  if (!statement) {
    p->failed = 1;
    p->macro = previous_macro;
    return 0;
  }
  NtFStmt *s = &ST(p->p, statement);
  s->kind = macro->statement_kind;
  s->span = invocation;
  s->origin = macro->template_span;
  s->invocation = invocation;
  s->expression = expression;
  s->name = macro->instruction_name;
  s->terminal = macro->statement_kind == NTF_S_RETURN;
  if (macro->statement_kind == NTF_S_LET) {
    char generated[160];
    snprintf(generated, sizeof(generated), "__ntm$%u$e%" PRIu64 "$%s",
             macro->ordinal, v->expansion_serial, macro->introduced_name);
    const char *fresh = copy_text(p->p, generated, strlen(generated));
    if (!fresh) {
      diag(p->p, NTF_E_LIMIT, invocation,
           "macro hygiene name exceeds arena budget");
      p->failed = 1;
      p->macro = previous_macro;
      return 0;
    }
    NtFId variable = add_variable(p->p, invocation);
    if (!variable) {
      p->failed = 1;
      p->macro = previous_macro;
      return 0;
    }
    NtFVariable *local = &VA(p->p, variable);
    local->name = fresh;
    local->span = invocation;
    local->type = statement_type;
    local->function = p->function;
    local->module = p->module;
    local->initializer = expression;
    local->direction = NTF_LOCAL;
    local->reg.family = -1;
    local->scope = p->depth ? p->scopes[p->depth - 1] : 0;
    s = &ST(p->p, statement);
    s->variable = variable;
  }
  p->macro = previous_macro;
  --v->expansion_depth;
  return statement;
}
static int duplicate_local(NtFProgram *p, NtFId function, const char *s,
                           NtFId scope) {
  for (size_t i = 0; i < p->variable_count; i++)
    if (p->variables[i].function == function &&
        p->variables[i].scope == scope && !strcmp(p->variables[i].name, s))
      return 1;
  return 0;
}
static NtFId parse_block(Parser *);
static NtFId parse_statement(Parser *p) {
  NtFSpan span = p->token.span;
  if (accept_word(p, "let")) {
    NtFSpan name_span;
    const char *s = name(p, &name_span);
    if (!s)
      return 0;
    NtFId current_scope = p->depth ? p->scopes[p->depth - 1] : 0;
    if (duplicate_local(p->p, p->function, s, current_scope)) {
      diag(p->p, NTF_E_DUPLICATE, name_span, "duplicate local '%s'", s);
      p->failed = 1;
      return 0;
    }
    if (!expect(p, ':', "expected ':' after local"))
      return 0;
    NtFId type = parse_type(p);
    if (!type || !expect(p, '=', "expected '=' after local type"))
      return 0;
    NtFId initializer = parse_expression(p, 0);
    if (!initializer || !term(p))
      return 0;
    NtFId variable = add_variable(p->p, name_span);
    if (!variable) {
      p->failed = 1;
      return 0;
    }
    NtFVariable *v = &VA(p->p, variable);
    v->name = s;
    v->span = name_span;
    v->type = type;
    v->function = p->function;
    v->module = p->module;
    v->initializer = initializer;
    v->direction = NTF_LOCAL;
    v->reg.family = -1;
    v->scope = current_scope;
    NtFId id = add_statement(p->p, span);
    if (!id) {
      p->failed = 1;
      return 0;
    }
    NtFStmt value = {NTF_S_LET, span,        0,        0,    0, 0,
                     0,         initializer, variable, NULL, 0, 0,
                     {0},       {0},         0,        0,    0};
    ST(p->p, id) = value;
    return id;
  }
  if (accept_word(p, "return")) {
    NtFId expression = 0;
    if (p->token.kind != ';' && p->token.kind != TK_NL && p->token.kind != '}')
      expression = parse_expression(p, 0);
    if (!term(p))
      return 0;
    NtFId id = add_statement(p->p, span);
    if (!id) {
      p->failed = 1;
      return 0;
    }
    NtFStmt value = {NTF_S_RETURN, span, 0, 0,   0,   0, 0, expression, 0,
                     NULL,         0,    1, {0}, {0}, 0, 0, 0};
    ST(p->p, id) = value;
    return id;
  }
  if (accept_word(p, "call")) {
    NtFSpan name_span;
    const char *s = compound(p, '.', &name_span);
    if (!s)
      return 0;
    NtFId expression = add_expression(p->p, name_span);
    if (!expression) {
      p->failed = 1;
      return 0;
    }
    NtFExpr *call = &EX(p->p, expression);
    call->kind = NTF_X_CALL;
    call->span = name_span;
    call->name = s;
    call->reg.family = -1;
    int explicit_args = 0;
    if (accept(p, '(')) {
      explicit_args = 1;
      NtFId first = 0, last = 0;
      lines(p);
      while (p->token.kind != ')' && !p->failed) {
        NtFId arg = parse_expression(p, 0);
        if (!arg)
          return 0;
        if (!first)
          first = arg;
        else
          EX(p->p, last).next = arg;
        last = arg;
        lines(p);
        if (!accept(p, ','))
          break;
        lines(p);
      }
      call = &EX(p->p, expression);
      call->first_arg = first;
      if (!expect(p, ')', "expected ')' after call"))
        return 0;
    }
    if (!term(p))
      return 0;
    NtFId id = add_statement(p->p, span);
    if (!id) {
      p->failed = 1;
      return 0;
    }
    NtFStmt value = {NTF_S_CALL,    span, 0,   0,   0, 0, 0, expression, 0, s,
                     explicit_args, 0,    {0}, {0}, 0, 0, 0};
    ST(p->p, id) = value;
    return id;
  }
  if (accept_word(p, "if")) {
    NtFId condition = parse_expression(p, 0);
    if (!condition)
      return 0;
    lines(p);
    NtFId then_branch = parse_block(p);
    if (!then_branch)
      return 0;
    lines(p);
    NtFId else_branch = 0;
    if (accept_word(p, "else")) {
      lines(p);
      else_branch = parse_block(p);
      if (!else_branch)
        return 0;
    }
    NtFId id = add_statement(p->p, span);
    if (!id) {
      p->failed = 1;
      return 0;
    }
    NtFStmt value = {NTF_S_IF,    span, 0, 0,    condition, then_branch,
                     else_branch, 0,    0, NULL, 0,         0,
                     {0},         {0},  0, 0,    0};
    ST(p->p, id) = value;
    return id;
  }
  if (accept_word(p, "expand")) {
    NtFSpan macro_span;
    const char *macro_name = name(p, &macro_span);
    Macro *macro = macro_name ? find_macro(p, macro_name) : NULL;
    if (!macro) {
      if (!p->failed) {
        diag(p->p, NTF_E_NAME, macro_span, "unknown macro '%s'", macro_name);
        p->failed = 1;
      }
      return 0;
    }
    if (!expect(p, '(', "expected '(' after macro name"))
      return 0;
    NtFId arguments[8];
    size_t argument_count = 0;
    lines(p);
    while (p->token.kind != ')' && !p->failed) {
      if (argument_count == 8) {
        diag(p->p, NTF_E_LIMIT, p->token.span, "macro argument limit exceeded");
        p->failed = 1;
        return 0;
      }
      arguments[argument_count] = parse_expression(p, 0);
      if (!arguments[argument_count++])
        return 0;
      lines(p);
      if (!accept(p, ','))
        break;
      lines(p);
    }
    if (!expect(p, ')', "expected ')' after macro arguments") || !term(p))
      return 0;
    if (argument_count != macro->parameter_count) {
      diag(p->p, NTF_E_CONTRACT, span, "macro '%s' expects %zu arguments",
           macro->name, macro->parameter_count);
      p->failed = 1;
      return 0;
    }
    return expand_macro_statement(p, span, macro, arguments, argument_count);
  }
  if (is(p, "macro") || is(p, "comptime")) {
    diag(p->p, NTF_E_UNSUPPORTED, span,
         "metaprogramming is not implemented by this bootstrap slice");
    p->failed = 1;
    return 0;
  }
  if (p->token.kind != TK_NAME) {
    syntax(p, "expected statement");
    return 0;
  }
  const char *mnemonic = name(p, NULL);
  if (accept(p, ':')) {
    if (!term(p))
      return 0;
    NtFId id = add_statement(p->p, span);
    if (!id) {
      p->failed = 1;
      return 0;
    }
    ST(p->p, id).kind = NTF_S_LABEL;
    ST(p->p, id).span = span;
    ST(p->p, id).name = mnemonic;
    return id;
  }
  NtFId first = 0, last = 0;
  while (p->token.kind != ';' && p->token.kind != TK_NL &&
         p->token.kind != '}' && !p->failed) {
    NtFId operand = parse_operand(p);
    if (!operand)
      return 0;
    if (!first)
      first = operand;
    else
      EX(p->p, last).next = operand;
    last = operand;
    if (!accept(p, ','))
      break;
    if (p->token.kind == ';' || p->token.kind == TK_NL || p->token.kind == '}')
      return syntax(p, "expected operand after comma"), (NtFId)0;
  }
  if (!term(p))
    return 0;
  NtFId id = add_statement(p->p, span);
  if (!id) {
    p->failed = 1;
    return 0;
  }
  NtFStmt value = {NTF_S_INSTRUCTION, span, 0, 0,   0,   0, 0, first, 0,
                   mnemonic,          0,    0, {0}, {0}, 0, 0, 0};
  ST(p->p, id) = value;
  return id;
}
static NtFId parse_block(Parser *p) {
  if (p->depth >= MAX_DEPTH) {
    diag(p->p, NTF_E_LIMIT, p->token.span, "block nesting limit exceeded");
    p->failed = 1;
    return 0;
  }
  NtFSpan span = p->token.span;
  if (!expect(p, '{', "expected '{'"))
    return 0;
  lines(p);
  NtFId block = add_statement(p->p, span);
  if (!block) {
    p->failed = 1;
    return 0;
  }
  ST(p->p, block).kind = NTF_S_BLOCK;
  ST(p->p, block).span = span;
  p->scopes[p->depth++] = block;
  NtFId first = 0, last = 0;
  while (p->token.kind != '}' && p->token.kind != TK_EOF && !p->failed) {
    size_t diagnostics_before = p->p->diagnostic_count;
    NtFId statement = parse_statement(p);
    if (!statement) {
      if (p->p->diagnostic_count == diagnostics_before)
        return 0;
      unsigned code = p->p->diagnostics[p->p->diagnostic_count - 1].code;
      if (code == NTF_E_OOM || code == NTF_E_LIMIT)
        return 0;
      p->failed = 0;
      unsigned nesting = 0;
      size_t start = p->cursor;
      while (p->token.kind != TK_EOF) {
        if (p->token.kind == '{') {
          ++nesting;
          next(p);
          continue;
        }
        if (p->token.kind == '}') {
          if (!nesting)
            break;
          --nesting;
          next(p);
          continue;
        }
        if ((p->token.kind == TK_NL || p->token.kind == ';') && !nesting) {
          next(p);
          lines(p);
          break;
        }
        next(p);
      }
      if (p->cursor == start && p->token.kind != TK_EOF && p->token.kind != '}')
        next(p);
      continue;
    }
    if (!first)
      first = statement;
    else
      ST(p->p, last).next = statement;
    last = statement;
    lines(p);
  }
  if (!expect(p, '}', "expected '}' after block"))
    return 0;
  ST(p->p, block).first = first;
  --p->depth;
  return block;
}
static int parse_parameters(Parser *p, NtFFunction *function,
                            NtFId function_id) {
  if (!expect(p, '(', "expected parameters"))
    return 0;
  lines(p);
  uint64_t families = 0;
  while (p->token.kind != ')' && !p->failed) {
    if (function->param_count >= 64) {
      diag(p->p, NTF_E_ABI, p->token.span,
           "function parameter hard limit is 64");
      p->failed = 1;
      return 0;
    }
    NtFDirection direction;
    if (accept_word(p, "in"))
      direction = NTF_IN;
    else if (accept_word(p, "out"))
      direction = NTF_OUT;
    else if (accept_word(p, "inout"))
      direction = NTF_INOUT;
    else
      return syntax(p, "expected parameter direction");
    NtFSpan name_span;
    const char *s = name(p, &name_span);
    if (!s)
      return 0;
    if (duplicate_local(p->p, function_id, s, 0)) {
      diag(p->p, NTF_E_DUPLICATE, name_span, "duplicate parameter '%s'", s);
      p->failed = 1;
      return 0;
    }
    if (!expect(p, ':', "expected ':' after parameter"))
      return 0;
    NtFId type = parse_type(p);
    if (!type)
      return 0;
    NtFRegister reg = {-1, 0, 0};
    if (accept(p, '@')) {
      const char *reg_name = name(p, &name_span);
      reg = nt_frontend_register(reg_name);
      if (reg.family < 0 || reg.family > 15) {
        diag(p->p, NTF_E_REGISTER, name_span, "invalid parameter register '%s'",
             reg_name);
        p->failed = 1;
        return 0;
      }
      if (reg.family == 4 || reg.family == 5) {
        diag(p->p, NTF_E_ABI, name_span,
             "rsp/rbp cannot bind an nx64 parameter");
        p->failed = 1;
        return 0;
      }
      if (TY(p->p, type).bits != reg.bits &&
          !(TY(p->p, type).kind == NTF_T_BOOL && reg.bits == 8)) {
        diag(p->p, NTF_E_REGISTER, name_span,
             "parameter/register width mismatch");
        p->failed = 1;
        return 0;
      }
      if (families & BIT((unsigned)reg.family)) {
        diag(p->p, NTF_E_REGISTER, name_span, "parameter registers alias");
        p->failed = 1;
        return 0;
      }
      families |= BIT((unsigned)reg.family);
    }
    NtFId variable = add_variable(p->p, name_span);
    if (!variable) {
      p->failed = 1;
      return 0;
    }
    NtFVariable *v = &VA(p->p, variable);
    v->name = s;
    v->span = name_span;
    v->type = type;
    v->function = function_id;
    v->module = p->module;
    v->direction = direction;
    v->reg = reg;
    if (!function->first_param)
      function->first_param = variable;
    ++function->param_count;
    lines(p);
    if (!accept(p, ','))
      break;
    lines(p);
  }
  return expect(p, ')', "expected ')' after parameters");
}
static int abi_scalar_or_pointer(const NtFProgram *p, NtFId type) {
  NtFTypeKind kind = TY(p, type).kind;
  return kind == NTF_T_U8 || kind == NTF_T_U16 || kind == NTF_T_U32 ||
         kind == NTF_T_U64 || kind == NTF_T_I8 || kind == NTF_T_I16 ||
         kind == NTF_T_I32 || kind == NTF_T_I64 || kind == NTF_T_BOOL ||
         kind == NTF_T_PTR;
}
static int abi_integer_output(const NtFProgram *p, NtFId type) {
  NtFTypeKind kind = TY(p, type).kind;
  return kind >= NTF_T_U8 && kind <= NTF_T_I64;
}
static int validate_parameter_abi(Parser *p, NtFFunction *function) {
  static const unsigned fixed[] = {1, 2, 8, 9};
  uint64_t families = 0;
  if (!strcmp(function->abi, "nx64-abi-v0")) {
    uint64_t output_families = 0;
    if (function->param_count > 14) {
      diag(p->p, NTF_E_ABI, function->span,
           "nx64-abi-v0 supports at most 14 register parameters");
      p->failed = 1;
      return 0;
    }
    for (uint32_t i = 0; i < function->param_count; i++) {
      NtFVariable *parameter = &VA(p->p, function->first_param + i);
      int family = parameter->reg.family;
      if ((parameter->direction == NTF_OUT ||
           parameter->direction == NTF_INOUT) &&
          family < 0) {
        diag(p->p, NTF_E_ABI, parameter->span,
             "nx64 out/inout requires an explicit @register binding");
        p->failed = 1;
        return 0;
      }
      if (family < 0) {
        if (i >= 4) {
          diag(p->p, NTF_E_ABI, parameter->span,
               "nx64 parameter five and later require explicit @GPR");
          p->failed = 1;
          return 0;
        }
        family = (int)fixed[i];
      }
      if ((parameter->direction == NTF_OUT ||
           parameter->direction == NTF_INOUT) &&
          !abi_integer_output(p->p, parameter->type) &&
          TY(p->p, parameter->type).kind != NTF_T_PTR) {
        diag(p->p, NTF_E_UNSUPPORTED, parameter->span,
             "nx64 out/inout currently requires an integer or pointer");
        p->failed = 1;
        return 0;
      }
      if ((parameter->direction == NTF_OUT ||
           parameter->direction == NTF_INOUT) &&
          (family == 0 || family == 4 || family == 5)) {
        diag(p->p, NTF_E_ABI, parameter->span,
             "nx64 output cannot bind rax, rsp or rbp");
        p->failed = 1;
        return 0;
      }
      if (parameter->direction == NTF_OUT ||
          parameter->direction == NTF_INOUT)
        output_families |= BIT((unsigned)family);
      if (families & BIT((unsigned)family)) {
        diag(p->p, NTF_E_REGISTER, parameter->span,
             "parameter bindings alias under nx64 ABI");
        p->failed = 1;
        return 0;
      }
      families |= BIT((unsigned)family);
    }
    if (function->clobbers & output_families) {
      diag(p->p, NTF_E_ABI, function->span,
           "nx64 output registers must not be declared as clobbers");
      p->failed = 1;
      return 0;
    }
    return 1;
  }
  if (!strcmp(function->abi, "efi-x64-v0")) {
    const uint64_t nonvolatile =
        BIT(3) | BIT(5) | BIT(6) | BIT(7) | BIT(12) | BIT(13) | BIT(14) |
        BIT(15);
    if (!abi_scalar_or_pointer(p->p, function->return_type)) {
      diag(p->p, NTF_E_ABI, function->span,
           "EFI return type must be a scalar or pointer");
      p->failed = 1;
      return 0;
    }
    if (function->clobbers & nonvolatile) {
      diag(p->p, NTF_E_ABI, function->span,
           "EFI function cannot declare a nonvolatile register clobber");
      p->failed = 1;
      return 0;
    }
    for (uint32_t i = 0; i < function->param_count; i++) {
      NtFVariable *parameter = &VA(p->p, function->first_param + i);
      if (parameter->direction != NTF_IN) {
        diag(p->p, NTF_E_UNSUPPORTED, parameter->span,
             "EFI out/inout parameters are not implemented");
        p->failed = 1;
        return 0;
      }
      if (!abi_scalar_or_pointer(p->p, parameter->type)) {
        diag(p->p, NTF_E_ABI, parameter->span,
             "EFI parameter type must be a scalar or pointer");
        p->failed = 1;
        return 0;
      }
      if (parameter->reg.high8) {
        diag(p->p, NTF_E_ABI, parameter->span,
             "EFI parameters cannot use high-byte register aliases");
        p->failed = 1;
        return 0;
      }
      if (i < 4 && parameter->reg.family >= 0 &&
          parameter->reg.family != (int)fixed[i]) {
        diag(p->p, NTF_E_ABI, parameter->span,
             "EFI register parameter does not match RCX/RDX/R8/R9");
        p->failed = 1;
        return 0;
      }
      if (i >= 4 && parameter->reg.family >= 0) {
        diag(p->p, NTF_E_ABI, parameter->span,
             "EFI parameter five and later must use stack binding");
        p->failed = 1;
        return 0;
      }
    }
    return 1;
  }
  diag(p->p, NTF_E_ABI, function->span, "unsupported ABI '%s'", function->abi);
  p->failed = 1;
  return 0;
}
static int duplicate_function(NtFProgram *p, NtFId module, const char *s) {
  for (size_t i = 0; i < p->function_count; i++)
    if (p->functions[i].module == module && !strcmp(p->functions[i].name, s))
      return 1;
  return 0;
}
static int parse_import(Parser *p) {
  NtFSpan span = p->token.span;
  if (!expect_word(p, "import"))
    return 0;
  const char *qualified = compound(p, '.', &span);
  if (!qualified || !expect_word(p, "as"))
    return 0;
  NtFSpan name_span;
  const char *alias = name(p, &name_span);
  if (!alias || !expect(p, ':', "expected ':' after import alias") ||
      !expect_word(p, "fn"))
    return 0;
  if (duplicate_function(p->p, p->module, alias)) {
    diag(p->p, NTF_E_DUPLICATE, name_span, "duplicate function '%s'", alias);
    p->failed = 1;
    return 0;
  }
  NtFId id = add_function(p->p, span);
  if (!id) {
    p->failed = 1;
    return 0;
  }
  NtFFunction *f = &FN(p->p, id);
  f->name = alias;
  f->import_name = qualified;
  f->imported = 1;
  f->module = p->module;
  f->span = span;
  f->abi = MO(p->p, p->module).default_abi;
  p->function = id;
  if (!parse_parameters(p, f, id) ||
      !expect(p, TK_ARROW, "expected '->' after import parameters"))
    return 0;
  f = &FN(p->p, id);
  f->return_type = parse_type(p);
  if (!f->return_type || !term(p))
    return 0;
  if (accept_word(p, "abi")) {
    f = &FN(p->p, id);
    f->abi = compound(p, '-', NULL);
    if (!f->abi || !term(p))
      return 0;
  }
  f = &FN(p->p, id);
  if (!validate_parameter_abi(p, f))
    return 0;
  f = &FN(p->p, id);
  if (!parse_effects(p, f))
    return 0;
  if (p->token.kind == ';' || p->token.kind == TK_NL)
    term(p);
  else
    lines(p);
  f = &FN(p->p, id);
  if (!parse_clobbers(p, f))
    return 0;
  if (!validate_parameter_abi(p, f))
    return 0;
  if (p->token.kind == ';' || p->token.kind == TK_NL)
    term(p);
  else
    lines(p);
  f = &FN(p->p, id);
  if (accept_word(p, "requires")) {
    if (!parse_predicates(p, &f->requires))
      return 0;
    if (p->token.kind == ';' || p->token.kind == TK_NL)
      term(p);
  }
  f = &FN(p->p, id);
  if (accept_word(p, "ensures")) {
    if (!parse_predicates(p, &f->ensures))
      return 0;
    if (p->token.kind == ';' || p->token.kind == TK_NL)
      term(p);
  }
  p->function = 0;
  return 1;
}
static int parse_function(Parser *p, const char *section, int exported) {
  NtFSpan span = p->token.span;
  if (!expect_word(p, "fn"))
    return 0;
  NtFSpan name_span;
  const char *s = name(p, &name_span);
  if (!s)
    return 0;
  if (duplicate_function(p->p, p->module, s)) {
    diag(p->p, NTF_E_DUPLICATE, name_span, "duplicate function '%s'", s);
    p->failed = 1;
    return 0;
  }
  NtFId id = add_function(p->p, span);
  if (!id) {
    p->failed = 1;
    return 0;
  }
  NtFFunction *f = &FN(p->p, id);
  f->name = s;
  f->section = section;
  f->span = span;
  f->module = p->module;
  f->exported = exported;
  f->abi = MO(p->p, p->module).default_abi;
  p->function = id;
  if (!parse_parameters(p, f, id) ||
      !expect(p, TK_ARROW, "expected '->' after parameters"))
    return 0;
  f = &FN(p->p, id);
  f->return_type = parse_type(p);
  if (!f->return_type)
    return 0;
  if (p->token.kind == ';' || p->token.kind == TK_NL)
    term(p);
  else
    lines(p);
  if (accept_word(p, "abi")) {
    f = &FN(p->p, id);
    f->abi = compound(p, '-', NULL);
    if (!f->abi || !term(p))
      return 0;
  }
  f = &FN(p->p, id);
  if (!validate_parameter_abi(p, f))
    return 0;
  f = &FN(p->p, id);
  if (!parse_effects(p, f))
    return 0;
  if (p->token.kind == ';' || p->token.kind == TK_NL)
    term(p);
  else
    lines(p);
  f = &FN(p->p, id);
  if (!parse_clobbers(p, f))
    return 0;
  if (!validate_parameter_abi(p, f))
    return 0;
  if (p->token.kind == ';' || p->token.kind == TK_NL)
    term(p);
  else
    lines(p);
  f = &FN(p->p, id);
  if (accept_word(p, "requires")) {
    if (!parse_predicates(p, &f->requires))
      return 0;
    if (p->token.kind == ';' || p->token.kind == TK_NL)
      term(p);
    else
      lines(p);
  }
  f = &FN(p->p, id);
  if (accept_word(p, "ensures")) {
    if (!parse_predicates(p, &f->ensures))
      return 0;
    if (p->token.kind == ';' || p->token.kind == TK_NL)
      term(p);
    else
      lines(p);
  }
  f = &FN(p->p, id);
  f->body = parse_block(p);
  p->function = 0;
  return f->body != 0;
}
static int parse_constant(Parser *p) {
  NtFSpan span = p->token.span;
  if (!expect_word(p, "const"))
    return 0;
  NtFSpan name_span;
  const char *s = name(p, &name_span);
  if (!s)
    return 0;
  for (size_t i = 0; i < p->p->variable_count; i++) {
    NtFVariable *v = &p->p->variables[i];
    if (!v->function && v->module == p->module && !strcmp(v->name, s)) {
      diag(p->p, NTF_E_DUPLICATE, name_span, "duplicate constant '%s'", s);
      p->failed = 1;
      return 0;
    }
  }
  if (!expect(p, ':', "expected ':' after constant"))
    return 0;
  NtFId type = parse_type(p);
  if (!type || !expect(p, '=', "expected '=' after constant type"))
    return 0;
  NtFId initializer = parse_expression(p, 0);
  if (!initializer || !term(p))
    return 0;
  NtFId variable = add_variable(p->p, span);
  if (!variable) {
    p->failed = 1;
    return 0;
  }
  NtFVariable *v = &VA(p->p, variable);
  v->name = s;
  v->span = span;
  v->type = type;
  v->module = p->module;
  v->direction = NTF_CONST;
  v->initializer = initializer;
  v->reg.family = -1;
  return 1;
}
static int parse_data(Parser *p, const char *section, int exported) {
  NtFSpan span = p->token.span;
  if (!expect_word(p, "data"))
    return 0;
  NtFSpan name_span;
  const char *s = name(p, &name_span);
  if (!s)
    return 0;
  for (size_t i = 0; i < p->p->variable_count; i++) {
    NtFVariable *v = &p->p->variables[i];
    if (!v->function && v->module == p->module && !strcmp(v->name, s)) {
      diag(p->p, NTF_E_DUPLICATE, name_span, "duplicate data '%s'", s);
      p->failed = 1;
      return 0;
    }
  }
  if (!expect(p, ':', "expected ':' after data name"))
    return 0;
  NtFId type = parse_type(p);
  if (!type || !expect(p, '=', "expected '=' after data type"))
    return 0;
  NtFId initializer = parse_expression(p, 0);
  if (!initializer || !term(p))
    return 0;
  NtFId variable = add_variable(p->p, span);
  if (!variable) {
    p->failed = 1;
    return 0;
  }
  NtFVariable *v = &VA(p->p, variable);
  v->name = s;
  v->span = span;
  v->type = type;
  v->module = p->module;
  v->direction = NTF_DATA;
  v->initializer = initializer;
  v->reg.family = -1;
  v->exported = exported;
  v->section = section;
  return 1;
}
static int parse_emitted_quote(Parser *p, Macro *macro) {
  if (macro->template_count == 512) {
    diag(p->p, NTF_E_LIMIT, p->token.span, "emitted statement limit exceeded");
    p->failed = 1;
    return 0;
  }
  if (!expect_word(p, "emit") || !expect_word(p, "quote") ||
      !expect(p, '(', "expected '(' after quote") ||
      !expect_word(p, macro->result_decl ? "decl" : "stmt") ||
      !expect(p, ')', "expected ')' after quote kind") ||
      !expect(p, '{', "expected emitted quote body"))
    return 0;
  lines(p);
  MacroTemplate *item = &macro->templates[macro->template_count++];
  memset(item, 0, sizeof(*item));
  item->span = p->token.span;
  if (macro->result_decl) {
    item->declaration = 1;
    item->decl_exported = accept_word(p, "export");
    if (accept_word(p, "data"))
      item->decl_data = 1;
    else if (!accept_word(p, "const"))
      return syntax(p, "emitted declaration must be const or data");
    item->decl_name = name(p, NULL);
    if (!item->decl_name || !expect(p, ':', "expected ':' in declaration"))
      return 0;
    item->type = parse_type(p);
    if (!item->type || !expect(p, '=', "expected '=' in declaration"))
      return 0;
    item->expression = parse_expression(p, 0);
  } else if (accept_word(p, "return")) {
    item->kind = NTF_S_RETURN;
    item->expression = parse_expression(p, 0);
  } else if (accept_word(p, "let")) {
    item->kind = NTF_S_LET;
    item->introduced_name = name(p, NULL);
    if (!item->introduced_name ||
        !expect(p, ':', "expected ':' in emitted let"))
      return 0;
    item->type = parse_type(p);
    if (!item->type || !expect(p, '=', "expected '=' in emitted let"))
      return 0;
    item->expression = parse_expression(p, 0);
  } else if (p->token.kind == TK_NAME) {
    item->kind = NTF_S_INSTRUCTION;
    item->instruction_name = name(p, NULL);
    NtFId first = 0, last = 0;
    while (p->token.kind != ';' && p->token.kind != TK_NL &&
           p->token.kind != '}' && !p->failed) {
      NtFId operand = parse_expression(p, 0);
      if (!operand)
        return 0;
      if (!first)
        first = operand;
      else
        EX(p->p, last).next = operand;
      last = operand;
      if (!accept(p, ','))
        break;
    }
    item->expression = first;
  } else
    return syntax(p, "unsupported emitted statement");
  return term(p) && expect(p, '}', "expected emitted quote end") && term(p);
}
static int parse_macro(Parser *p) {
  NtFSpan span = p->token.span;
  if (!expect_word(p, "macro"))
    return 0;
  NtFSpan name_span;
  const char *macro_name = name(p, &name_span);
  if (!macro_name)
    return 0;
  if (find_macro(p, macro_name)) {
    diag(p->p, NTF_E_DUPLICATE, name_span, "duplicate macro '%s'", macro_name);
    p->failed = 1;
    return 0;
  }
  Macro *macro = add_macro(p->p, span);
  if (!macro) {
    p->failed = 1;
    return 0;
  }
  macro->name = macro_name;
  if (!expect(p, '(', "expected macro parameters"))
    return 0;
  lines(p);
  while (p->token.kind != ')' && !p->failed) {
    if (macro->parameter_count == 8) {
      diag(p->p, NTF_E_LIMIT, p->token.span, "macro parameter limit exceeded");
      p->failed = 1;
      return 0;
    }
    const char *parameter = name(p, NULL);
    if (!parameter || !expect(p, ':', "expected ':' after macro parameter"))
      return 0;
    unsigned char parameter_kind = 0;
    NtFId parameter_type = 0;
    if (accept_word(p, "ast")) {
      if (!expect(p, '.', "expected '.' in macro AST parameter"))
        return 0;
      if (is(p, "expr"))
        parameter_kind = 1;
      else if (is(p, "type"))
        parameter_kind = 2;
      else {
        diag(p->p, NTF_E_UNSUPPORTED, p->token.span,
             "only ast.expr and ast.type parameters are implemented");
        p->failed = 1;
        return 0;
      }
      next(p);
    } else {
      parameter_type = parse_type(p);
      if (!parameter_type)
        return 0;
      NtFTypeKind kind = TY(p->p, parameter_type).kind;
      if ((kind < NTF_T_U8 || kind > NTF_T_I64) && kind != NTF_T_BOOL) {
        diag(p->p, NTF_E_UNSUPPORTED, span,
             "comptime value parameters support integer and bool types");
        p->failed = 1;
        return 0;
      }
      parameter_kind = 3;
    }
    for (size_t i = 0; i < macro->parameter_count; i++)
      if (!strcmp(macro->parameters[i], parameter)) {
        diag(p->p, NTF_E_DUPLICATE, span, "duplicate macro parameter '%s'",
             parameter);
        p->failed = 1;
        return 0;
      }
    macro->parameters[macro->parameter_count] = parameter;
    macro->parameter_kinds[macro->parameter_count] = parameter_kind;
    macro->parameter_types[macro->parameter_count++] = parameter_type;
    lines(p);
    if (!accept(p, ','))
      break;
    lines(p);
  }
  if (!expect(p, ')', "expected ')' after macro parameters") ||
      !expect(p, TK_ARROW, "expected '->' after macro parameters"))
    return 0;
  if (is(p, "list")) {
    next(p);
    if (!expect(p, '<', "expected '<' after list") || !expect_word(p, "ast") ||
        !expect(p, '.', "expected '.' in list result"))
      return 0;
    if (is(p, "stmt"))
      macro->result_block = 1;
    else if (is(p, "decl"))
      macro->result_decl = 1;
    else {
      diag(p->p, NTF_E_UNSUPPORTED, p->token.span,
           "only stmt/decl AST lists are implemented");
      p->failed = 1;
      return 0;
    }
    next(p);
    if (!expect(p, '>', "expected '>' after list result"))
      return 0;
    macro->emit_list = 1;
  } else {
    if (!is(p, "ast")) {
      diag(p->p, NTF_E_UNSUPPORTED, p->token.span,
           "only AST macro results are implemented");
      p->failed = 1;
      return 0;
    }
    if (!expect_word(p, "ast") ||
        !expect(p, '.', "expected '.' in macro result"))
      return 0;
    if (is(p, "stmt"))
      next(p);
    else if (is(p, "expr")) {
      macro->result_expr = 1;
      next(p);
    } else if (is(p, "type")) {
      macro->result_type = 1;
      next(p);
    }
    else if (is(p, "block")) {
      macro->result_block = 1;
      next(p);
    } else if (is(p, "decl")) {
      macro->result_decl = 1;
      next(p);
    } else {
      diag(p->p, NTF_E_UNSUPPORTED, p->token.span,
           "only ast.expr/type/stmt/block/decl and supported lists are "
           "implemented");
      p->failed = 1;
      return 0;
    }
  }
  if (!expect(p, '{', "expected macro body"))
    return 0;
  lines(p);
  p->macro = macro;
  while (is(p, "let") && !p->failed) {
    if (macro->meta_count == sizeof macro->meta_names /
                                 sizeof macro->meta_names[0]) {
      diag(p->p, NTF_E_LIMIT, p->token.span,
           "macro meta-let binding limit exceeded");
      p->failed = 1;
      p->macro = NULL;
      return 0;
    }
    next(p);
    NtFSpan binding_span;
    const char *binding = name(p, &binding_span);
    if (!binding) {
      p->macro = NULL;
      return 0;
    }
    for (size_t i = 0; i < macro->parameter_count; i++)
      if (!strcmp(binding, macro->parameters[i])) {
        diag(p->p, NTF_E_DUPLICATE, binding_span,
             "duplicate macro binding '%s'", binding);
        p->failed = 1;
        p->macro = NULL;
        return 0;
      }
    for (size_t i = 0; i < macro->meta_count; i++)
      if (!strcmp(binding, macro->meta_names[i])) {
        diag(p->p, NTF_E_DUPLICATE, binding_span,
             "duplicate macro binding '%s'", binding);
        p->failed = 1;
        p->macro = NULL;
        return 0;
      }
    if (!expect(p, '=', "expected '=' after macro binding")) {
      p->macro = NULL;
      return 0;
    }
    NtFId value = parse_expression(p, 0);
    if (!value || !term(p)) {
      p->macro = NULL;
      return 0;
    }
    macro->meta_names[macro->meta_count] = binding;
    macro->meta_values[macro->meta_count++] = value;
    lines(p);
  }
  p->macro = NULL;
  if (macro->emit_list) {
    p->macro = macro;
    if (accept_word(p, "if")) {
      macro->meta_if = 1;
      macro->meta_condition = parse_expression(p, 0);
      if (!macro->meta_condition ||
          !expect(p, '{', "expected meta-if true block")) {
        p->macro = NULL;
        return 0;
      }
      lines(p);
      while (p->token.kind != '}' && !p->failed)
        if (!parse_emitted_quote(p, macro)) {
          p->macro = NULL;
          return 0;
        }
      if (!expect(p, '}', "expected meta-if true end")) {
        p->macro = NULL;
        return 0;
      }
      lines(p);
      macro->else_start = macro->template_count;
      if (accept_word(p, "else")) {
        if (!expect(p, '{', "expected meta-if else block")) {
          p->macro = NULL;
          return 0;
        }
        lines(p);
        while (p->token.kind != '}' && !p->failed)
          if (!parse_emitted_quote(p, macro)) {
            p->macro = NULL;
            return 0;
          }
        if (!expect(p, '}', "expected meta-if else end")) {
          p->macro = NULL;
          return 0;
        }
        lines(p);
      }
    } else if (accept_word(p, "for")) {
      macro->meta_for = 1;
      macro->loop_name = name(p, NULL);
      if (!macro->loop_name || !expect_word(p, "in")) {
        p->macro = NULL;
        return 0;
      }
      macro->loop_count = parse_expression(p, 0);
      if (!macro->loop_count || !expect(p, '{', "expected meta-for body")) {
        p->macro = NULL;
        return 0;
      }
      lines(p);
      while (p->token.kind != '}' && !p->failed)
        if (!parse_emitted_quote(p, macro)) {
          p->macro = NULL;
          return 0;
        }
      if (!expect(p, '}', "expected meta-for end")) {
        p->macro = NULL;
        return 0;
      }
      lines(p);
    } else {
      while (p->token.kind != '}' && !p->failed)
        if (!parse_emitted_quote(p, macro)) {
          p->macro = NULL;
          return 0;
        }
    }
    p->macro = NULL;
    return expect(p, '}', "expected macro end");
  }
  if (!expect_word(p, "return") || !expect_word(p, "quote") ||
      !expect(p, '(', "expected '(' after quote") ||
      !expect_word(p, macro->result_decl    ? "decl"
                      : macro->result_expr  ? "expr"
                      : macro->result_type  ? "type"
                      : macro->result_block ? "block"
                                            : "stmt") ||
      !expect(p, ')', "expected ')' after quote kind") ||
      !expect(p, '{', "expected quote body"))
    return 0;
  lines(p);
  macro->template_span = p->token.span;
  p->macro = macro;
  if (macro->result_expr) {
    macro->template_expression = parse_expression(p, 0);
    p->macro = NULL;
    if (!macro->template_expression) return 0;
    lines(p);
    return expect(p, '}', "expected expression quote end") && term(p) &&
           expect(p, '}', "expected macro end");
  } else if (macro->result_type) {
    macro->template_type = parse_type(p);
    p->macro = NULL;
    if (!macro->template_type) return 0;
    lines(p);
    return expect(p, '}', "expected type quote end") && term(p) &&
           expect(p, '}', "expected macro end");
  } else if (macro->result_decl) {
    macro->decl_exported = accept_word(p, "export");
    if (accept_word(p, "fn")) {
      macro->decl_function = 1;
      macro->decl_name = name(p, NULL);
      if (!macro->decl_name || !expect(p, '(', "expected function parameters")) {
        p->macro = NULL;
        return 0;
      }
      lines(p);
      while (p->token.kind != ')' && !p->failed) {
        if (macro->decl_param_count == 64) {
          diag(p->p, NTF_E_LIMIT, p->token.span,
               "generated function parameter limit exceeded");
          p->failed = 1;
          p->macro = NULL;
          return 0;
        }
        NtFDirection direction;
        if (accept_word(p, "in"))
          direction = NTF_IN;
        else if (accept_word(p, "out"))
          direction = NTF_OUT;
        else if (accept_word(p, "inout"))
          direction = NTF_INOUT;
        else {
          p->macro = NULL;
          return syntax(p, "expected generated parameter direction");
        }
        NtFSpan parameter_span;
        const char *parameter = name(p, &parameter_span);
        if (!parameter || !expect(p, ':', "expected ':' after parameter")) {
          p->macro = NULL;
          return 0;
        }
        for (size_t i = 0; i < macro->decl_param_count; i++)
          if (!strcmp(parameter, macro->decl_param_names[i])) {
            diag(p->p, NTF_E_DUPLICATE, parameter_span,
                 "duplicate generated parameter '%s'", parameter);
            p->failed = 1;
            p->macro = NULL;
            return 0;
          }
        NtFId parameter_type = parse_type(p);
        if (!parameter_type) {
          p->macro = NULL;
          return 0;
        }
        NtFRegister parameter_reg = {-1, 0, 0};
        if (accept(p, '@')) {
          const char *register_name = name(p, &parameter_span);
          parameter_reg = nt_frontend_register(register_name);
          if (parameter_reg.family < 0 || parameter_reg.family > 15 ||
              parameter_reg.family == 4 || parameter_reg.family == 5) {
            diag(p->p, NTF_E_REGISTER, parameter_span,
                 "invalid generated parameter register '%s'", register_name);
            p->failed = 1;
            p->macro = NULL;
            return 0;
          }
        }
        size_t index = macro->decl_param_count++;
        macro->decl_param_names[index] = parameter;
        macro->decl_param_types[index] = parameter_type;
        macro->decl_param_regs[index] = parameter_reg;
        macro->decl_param_directions[index] = direction;
        lines(p);
        if (!accept(p, ','))
          break;
        lines(p);
      }
      if (!expect(p, ')', "expected ')' after generated parameters") ||
          !expect(p, TK_ARROW, "expected generated function return type")) {
        p->macro = NULL;
        return 0;
      }
      macro->decl_type = parse_type(p);
      if (!macro->decl_type || !term(p)) {
        p->macro = NULL;
        return 0;
      }
      if (accept_word(p, "abi")) {
        macro->decl_abi = compound(p, '-', NULL);
        if (!macro->decl_abi || !term(p)) {
          p->macro = NULL;
          return 0;
        }
      }
      NtFFunction contract = {0};
      if (!parse_effects(p, &contract)) {
        p->macro = NULL;
        return 0;
      }
      if (p->token.kind == ';' || p->token.kind == TK_NL)
        term(p);
      else
        lines(p);
      if (!parse_clobbers(p, &contract)) {
        p->macro = NULL;
        return 0;
      }
      macro->decl_effects = contract.effects;
      macro->decl_clobbers = contract.clobbers;
      lines(p);
      if (!expect(p, '{', "expected generated function body")) {
        p->macro = NULL;
        return 0;
      }
      lines(p);
      while (p->token.kind != '}' && !p->failed) {
        if (macro->template_count == 512) {
          diag(p->p, NTF_E_LIMIT, p->token.span,
               "generated function statement limit exceeded");
          p->failed = 1;
          p->macro = NULL;
          return 0;
        }
        MacroTemplate *item = &macro->templates[macro->template_count++];
        memset(item, 0, sizeof(*item));
        item->span = p->token.span;
        if (accept_word(p, "expand")) {
          item->meta_expand = 1;
          item->instruction_name = name(p, NULL);
          if (!item->instruction_name ||
              !expect(p, '(', "expected nested macro arguments")) {
            p->macro = NULL;
            return 0;
          }
          NtFId first = 0, last = 0;
          lines(p);
          while (p->token.kind != ')' && !p->failed) {
            NtFId argument = parse_expression(p, 0);
            if (!argument) {
              p->macro = NULL;
              return 0;
            }
            if (!first)
              first = argument;
            else
              EX(p->p, last).next = argument;
            last = argument;
            lines(p);
            if (!accept(p, ','))
              break;
            lines(p);
          }
          if (!expect(p, ')', "expected ')' after nested macro arguments")) {
            p->macro = NULL;
            return 0;
          }
          item->expression = first;
        } else if (accept_word(p, "return")) {
          item->kind = NTF_S_RETURN;
          if (p->token.kind != ';' && p->token.kind != TK_NL &&
              p->token.kind != '}')
            item->expression = parse_expression(p, 0);
        } else if (accept_word(p, "let")) {
          item->kind = NTF_S_LET;
          item->introduced_name = name(p, NULL);
          if (!item->introduced_name ||
              !expect(p, ':', "expected ':' in generated let")) {
            p->macro = NULL;
            return 0;
          }
          item->type = parse_type(p);
          if (!item->type || !expect(p, '=', "expected '=' in generated let")) {
            p->macro = NULL;
            return 0;
          }
          item->expression = parse_expression(p, 0);
        } else if (p->token.kind == TK_NAME) {
          item->kind = NTF_S_INSTRUCTION;
          item->instruction_name = name(p, NULL);
          NtFId first = 0, last = 0;
          while (p->token.kind != ';' && p->token.kind != TK_NL &&
                 p->token.kind != '}' && !p->failed) {
            NtFId operand = parse_operand(p);
            if (!operand) {
              p->macro = NULL;
              return 0;
            }
            if (!first)
              first = operand;
            else
              EX(p->p, last).next = operand;
            last = operand;
            if (!accept(p, ','))
              break;
          }
          item->expression = first;
        } else {
          p->macro = NULL;
          return syntax(p, "unsupported generated function statement");
        }
        if (!term(p)) {
          p->macro = NULL;
          return 0;
        }
      }
      if (!expect(p, '}', "expected generated function body end")) {
        p->macro = NULL;
        return 0;
      }
      lines(p);
      p->macro = NULL;
      if (!expect(p, '}', "expected quote end") || !term(p) ||
          !expect(p, '}', "expected macro end"))
        return 0;
      return 1;
    }
    if (!expect_word(p, "data")) {
      p->macro = NULL;
      diag(p->p, NTF_E_UNSUPPORTED, p->token.span,
           "only data/function ast.decl quotes are implemented");
      p->failed = 1;
      return 0;
    }
    macro->decl_name = name(p, NULL);
    if (!macro->decl_name ||
        !expect(p, ':', "expected ':' in data declaration")) {
      p->macro = NULL;
      return 0;
    }
    macro->decl_type = parse_type(p);
    if (!macro->decl_type ||
        !expect(p, '=', "expected '=' in data declaration")) {
      p->macro = NULL;
      return 0;
    }
    macro->decl_initializer = parse_expression(p, 0);
    p->macro = NULL;
    if (!macro->decl_initializer || !term(p) ||
        !expect(p, '}', "expected quote end") || !term(p) ||
        !expect(p, '}', "expected macro end"))
      return 0;
    return 1;
  } else if (macro->result_block) {
    while (p->token.kind != '}' && !p->failed) {
      if (macro->template_count == 512) {
        diag(p->p, NTF_E_LIMIT, p->token.span,
             "quoted block statement limit exceeded");
        p->failed = 1;
        p->macro = NULL;
        return 0;
      }
      MacroTemplate *item = &macro->templates[macro->template_count++];
      memset(item, 0, sizeof(*item));
      item->span = p->token.span;
      if (accept_word(p, "expand")) {
        item->meta_expand = 1;
        item->instruction_name = name(p, NULL);
        if (!item->instruction_name ||
            !expect(p, '(', "expected nested macro arguments")) {
          p->macro = NULL;
          return 0;
        }
        NtFId first = 0, last = 0;
        lines(p);
        while (p->token.kind != ')' && !p->failed) {
          NtFId argument = parse_expression(p, 0);
          if (!argument) {
            p->macro = NULL;
            return 0;
          }
          if (!first)
            first = argument;
          else
            EX(p->p, last).next = argument;
          last = argument;
          lines(p);
          if (!accept(p, ','))
            break;
          lines(p);
        }
        if (!expect(p, ')', "expected ')' after nested macro arguments")) {
          p->macro = NULL;
          return 0;
        }
        item->expression = first;
      } else if (accept_word(p, "return")) {
        item->kind = NTF_S_RETURN;
        item->expression = parse_expression(p, 0);
      } else if (accept_word(p, "let")) {
        item->kind = NTF_S_LET;
        item->introduced_name = name(p, NULL);
        if (!item->introduced_name ||
            !expect(p, ':', "expected ':' in quoted let")) {
          p->macro = NULL;
          return 0;
        }
        item->type = parse_type(p);
        if (!item->type || !expect(p, '=', "expected '=' in quoted let")) {
          p->macro = NULL;
          return 0;
        }
        item->expression = parse_expression(p, 0);
      } else if (p->token.kind == TK_NAME) {
        item->kind = NTF_S_INSTRUCTION;
        item->instruction_name = name(p, NULL);
        NtFId first = 0, last = 0;
        while (p->token.kind != ';' && p->token.kind != TK_NL &&
               p->token.kind != '}' && !p->failed) {
          NtFId operand = parse_expression(p, 0);
          if (!operand) {
            p->macro = NULL;
            return 0;
          }
          if (!first)
            first = operand;
          else
            EX(p->p, last).next = operand;
          last = operand;
          if (!accept(p, ','))
            break;
        }
        item->expression = first;
      } else {
        p->macro = NULL;
        return syntax(p, "unsupported quoted block statement");
      }
      if (!item->expression && item->kind != NTF_S_INSTRUCTION &&
          !item->meta_expand) {
        p->macro = NULL;
        return 0;
      }
      if (!term(p)) {
        p->macro = NULL;
        return 0;
      }
    }
    p->macro = NULL;
    if (!expect(p, '}', "expected quote end") || !term(p) ||
        !expect(p, '}', "expected macro end"))
      return 0;
    return 1;
  } else if (accept_word(p, "return")) {
    macro->statement_kind = NTF_S_RETURN;
    macro->template_expression = parse_expression(p, 0);
    if (!macro->template_expression) {
      p->macro = NULL;
      return 0;
    }
  } else if (accept_word(p, "let")) {
    macro->statement_kind = NTF_S_LET;
    macro->introduced_name = name(p, NULL);
    if (!macro->introduced_name ||
        !expect(p, ':', "expected ':' in quoted let")) {
      p->macro = NULL;
      return 0;
    }
    macro->template_type = parse_type(p);
    if (!macro->template_type ||
        !expect(p, '=', "expected '=' in quoted let")) {
      p->macro = NULL;
      return 0;
    }
    macro->template_expression = parse_expression(p, 0);
    if (!macro->template_expression) {
      p->macro = NULL;
      return 0;
    }
  } else if (p->token.kind == TK_NAME) {
    macro->statement_kind = NTF_S_INSTRUCTION;
    macro->instruction_name = name(p, NULL);
    NtFId first = 0, last = 0;
    while (p->token.kind != ';' && p->token.kind != TK_NL &&
           p->token.kind != '}' && !p->failed) {
      NtFId operand = parse_expression(p, 0);
      if (!operand) {
        p->macro = NULL;
        return 0;
      }
      if (!first)
        first = operand;
      else
        EX(p->p, last).next = operand;
      last = operand;
      if (!accept(p, ','))
        break;
    }
    macro->template_expression = first;
  } else {
    p->macro = NULL;
    return syntax(p,
                  "quoted macro statement must be return, let or instruction");
  }
  p->macro = NULL;
  if (!term(p) || !expect(p, '}', "expected quote end") || !term(p) ||
      !expect(p, '}', "expected macro end"))
    return 0;
  return 1;
}
static int parse_comptime(Parser *p) {
  NtFSpan span = p->token.span;
  if (!expect_word(p, "comptime") ||
      !expect(p, '(', "expected comptime limits"))
    return 0;
  uint64_t values[6] = {0};
  unsigned seen = 0;
  lines(p);
  static const char *names[] = {
      "fuel",      "call_depth",      "arena_bytes",
      "ast_nodes", "expansion_depth", "constant_bytes"};
  static const uint64_t maxima[] = {NTF_COMPTIME_FUEL_MAX,
                                    NTF_COMPTIME_CALL_DEPTH_MAX,
                                    NTF_COMPTIME_ARENA_BYTES_MAX,
                                    NTF_COMPTIME_AST_NODES_MAX,
                                    NTF_COMPTIME_EXPANSION_DEPTH_MAX,
                                    NTF_COMPTIME_CONSTANT_BYTES_MAX};
  while (p->token.kind != ')' && !p->failed) {
    NtFSpan limit_span;
    const char *limit_name = name(p, &limit_span);
    int index = -1;
    for (int i = 0; i < 6; i++)
      if (limit_name && !strcmp(limit_name, names[i]))
        index = i;
    if (index < 0 || !expect(p, '=', "expected '=' after comptime limit"))
      return 0;
    if (seen & (1u << index)) {
      diag(p->p, NTF_E_DUPLICATE, limit_span, "duplicate comptime limit '%s'",
           limit_name);
      p->failed = 1;
      return 0;
    }
    if (p->token.kind != TK_INT)
      return syntax(p, "expected comptime limit value");
    values[index] = p->token.value;
    seen |= 1u << index;
    if (values[index] > maxima[index]) {
      diag(p->p, NTF_E_LIMIT, p->token.span,
           "comptime limit '%s' exceeds frozen maximum", limit_name);
      p->failed = 1;
      return 0;
    }
    next(p);
    lines(p);
    if (!accept(p, ','))
      break;
    lines(p);
  }
  if (seen != 63)
    return syntax(p, "all six comptime limits are required");
  if (!expect(p, ')', "expected ')' after comptime limits"))
    return 0;
  Private *v = p->p->_private;
  if (values[3] < p->p->expression_count + p->p->statement_count +
                      p->p->variable_count ||
      values[5] < v->constant_used) {
    diag(p->p, NTF_E_LIMIT, span,
         "comptime budget is below already consumed state");
    p->failed = 1;
    return 0;
  }
  v->fuel_limit = values[0];
  v->call_depth_limit = values[1];
  v->arena_base = v->arena_used;
  v->arena_limit = values[2];
  v->node_limit = values[3];
  v->expansion_limit = values[4];
  v->constant_limit = values[5];
  if (!expect(p, '{', "expected comptime body"))
    return 0;
  lines(p);
  if (!expect_word(p, "return"))
    return 0;
  size_t before = p->p->expression_count;
  NtFId expression = parse_expression(p, 0);
  if (!expression || !term(p) || !expect(p, '}', "expected comptime body end"))
    return 0;
  size_t cost = p->p->expression_count - before;
  if (cost > v->fuel_limit - v->fuel_used) {
    diag(p->p, NTF_E_LIMIT, span, "comptime evaluation exceeds fuel");
    p->failed = 1;
    return 0;
  }
  v->fuel_used += cost;
  uint64_t result;
  int negative;
  if (!constant_value(p->p, expression, &result, &negative)) {
    diag(p->p, NTF_E_CONTRACT, span,
         "comptime expression is not deterministic constant");
    p->failed = 1;
    return 0;
  }
  (void)result;
  (void)negative;
  return 1;
}
static int parse_empty_named_set(Parser *p, const char *field) {
  return expect_word(p, field) && expect(p, '{', "expected empty set") &&
         expect(p, '}', "only empty set is supported") && term(p);
}
static NtFId stub_register_expr(Parser *p, NtFSpan span, const char *name) {
  NtFId id = add_expression(p->p, span);
  if (!id)
    return 0;
  EX(p->p, id).kind = NTF_X_REGISTER;
  EX(p->p, id).span = span;
  EX(p->p, id).reg = nt_frontend_register(name);
  return id;
}
static NtFId stub_memory_expr(Parser *p, NtFSpan span, NtFId pointer_type,
                              int64_t displacement) {
  NtFId id = add_expression(p->p, span);
  if (!id)
    return 0;
  NtFExpr *x = &EX(p->p, id);
  x->kind = NTF_X_MEMORY;
  x->span = span;
  x->type = pointer_type;
  x->reg.family = -1;
  x->memory.base = nt_frontend_register("rsp");
  x->memory.index.family = -1;
  x->memory.scale = 1;
  x->memory.displacement = displacement;
  return id;
}
static NtFId stub_instruction(Parser *p, NtFSpan span, const char *name,
                              NtFId first, NtFId second, int terminal) {
  if (first && second)
    EX(p->p, first).next = second;
  NtFId id = add_statement(p->p, span);
  if (!id)
    return 0;
  NtFStmt *s = &ST(p->p, id);
  s->kind = NTF_S_INSTRUCTION;
  s->span = span;
  s->name = name;
  s->expression = first;
  s->terminal = terminal;
  return id;
}
static int build_resume_interrupt_stub(Parser *p, NtFSpan span,
                                       const char *symbol, int exported,
                                       uint32_t vector, int hardware_error) {
  if (duplicate_function(p->p, p->module, symbol)) {
    diag(p->p, NTF_E_DUPLICATE, span, "duplicate interrupt symbol '%s'",
         symbol);
    p->failed = 1;
    return 0;
  }
  NtFType u64 = {NTF_T_U64, NTF_SPACE_NONE, 0, 0, 64};
  NtFId u64_type = intern_type(p->p, u64, span);
  NtFType pointer = {NTF_T_PTR, NTF_SPACE_KERNEL, u64_type, 0, 64};
  NtFId pointer_type = intern_type(p->p, pointer, span);
  NtFType never = {NTF_T_NEVER, NTF_SPACE_NONE, 0, 0, 0};
  NtFId never_type = intern_type(p->p, never, span);
  NtFId function = add_function(p->p, span);
  NtFId operands[10] = {0};
  operands[0] = stub_register_expr(p, span, "rax");
  operands[1] = stub_register_expr(p, span, "rax");
  operands[2] = stub_memory_expr(p, span, pointer_type,
                                 hardware_error ? 16 : 8);
  operands[3] = stub_register_expr(p, span, "rax");
  operands[4] = add_expression(p->p, span);
  if (operands[4]) {
    EX(p->p, operands[4]).kind = NTF_X_LITERAL;
    EX(p->p, operands[4]).span = span;
    EX(p->p, operands[4]).type = u64_type;
    EX(p->p, operands[4]).value = 2;
    EX(p->p, operands[4]).constant = 1;
    EX(p->p, operands[4]).reg.family = -1;
  }
  operands[5] = stub_memory_expr(p, span, pointer_type,
                                 hardware_error ? 16 : 8);
  operands[6] = stub_register_expr(p, span, "rax");
  operands[7] = stub_register_expr(p, span, "rax");
  if (hardware_error) {
    operands[8] = stub_register_expr(p, span, "rsp");
    operands[9] = add_expression(p->p, span);
    if (operands[9]) {
      EX(p->p, operands[9]).kind = NTF_X_LITERAL;
      EX(p->p, operands[9]).span = span;
      EX(p->p, operands[9]).type = u64_type;
      EX(p->p, operands[9]).value = 8;
      EX(p->p, operands[9]).constant = 1;
      EX(p->p, operands[9]).reg.family = -1;
    }
  }
  size_t operand_count = hardware_error ? 10 : 8;
  for (size_t i = 0; i < operand_count; i++)
    if (!operands[i] || !function || !u64_type || !pointer_type ||
        !never_type) {
      p->failed = 1;
      return 0;
    }
  NtFId statements[7] = {0};
  statements[0] = stub_instruction(p, span, "push", operands[0], 0, 0);
  statements[1] =
      stub_instruction(p, span, "load", operands[1], operands[2], 0);
  statements[2] = stub_instruction(p, span, "add", operands[3], operands[4], 0);
  statements[3] =
      stub_instruction(p, span, "store", operands[5], operands[6], 0);
  statements[4] = stub_instruction(p, span, "pop", operands[7], 0, 0);
  size_t statement_count = 6;
  if (hardware_error) {
    statements[5] =
        stub_instruction(p, span, "add", operands[8], operands[9], 0);
    statements[6] = stub_instruction(p, span, "iretq", 0, 0, 1);
    statement_count = 7;
  } else
    statements[5] = stub_instruction(p, span, "iretq", 0, 0, 1);
  for (size_t i = 0; i < statement_count; i++)
    if (!statements[i]) {
      p->failed = 1;
      return 0;
    }
  for (size_t i = 0; i + 1 < statement_count; i++)
    ST(p->p, statements[i]).next = statements[i + 1];
  NtFId block = add_statement(p->p, span);
  if (!block) {
    p->failed = 1;
    return 0;
  }
  ST(p->p, block).kind = NTF_S_BLOCK;
  ST(p->p, block).span = span;
  ST(p->p, block).first = statements[0];
  ST(p->p, block).terminal = 1;
  NtFFunction *fn = &FN(p->p, function);
  fn->name = symbol;
  fn->abi = hardware_error ? "nx64-interrupt-same-cpl-error-v0"
                           : "nx64-interrupt-same-cpl-v0";
  fn->section = ".text";
  fn->span = span;
  fn->module = p->module;
  fn->return_type = never_type;
  fn->body = block;
  fn->effects.flags =
      NTF_F_PRIVILEGED | NTF_F_CHANGES_FLAGS | NTF_F_CONTROL | NTF_F_NO_RETURN;
  fn->effects.reads = 1u << NTF_SPACE_KERNEL;
  fn->effects.writes = 1u << NTF_SPACE_KERNEL;
  fn->write_footprint = BIT(0) | BIT(4) | BIT(17);
  fn->exported = exported;
  fn->generated_stub = hardware_error ? 2u : 1u;
  fn->stub_vector = vector;
  return 1;
}
static int parse_interrupt_stub_entry(Parser *p, NtFSpan table_span,
                                      uint8_t seen[256]) {
  if (!expect_word(p, "stub") || p->token.kind != TK_INT)
    return syntax(p, "expected interrupt vector");
  uint64_t vector = p->token.value;
  next(p);
  if (vector != 6 && vector != 13) {
    diag(p->p, NTF_E_CONTRACT, table_span,
         "witness profiles support only #UD vector 6 or #GP vector 13");
    p->failed = 1;
    return 0;
  }
  if (seen[vector]) {
    diag(p->p, NTF_E_DUPLICATE, table_span,
         "duplicate interrupt vector %" PRIu64, vector);
    p->failed = 1;
    return 0;
  }
  seen[vector] = 1;
  if (!expect(p, '{', "expected interrupt stub body"))
    return 0;
  lines(p);
  if (!expect_word(p, "handler"))
    return 0;
  const char *handler = name(p, NULL);
  if (!handler || !term(p))
    return 0;
  if (strcmp(handler, "advance_saved_rip_2")) {
    diag(p->p, NTF_E_CONTRACT, table_span,
         "interrupt witness handler policy must advance_saved_rip_2");
    p->failed = 1;
    return 0;
  }
  if (!expect_word(p, "entry_abi"))
    return 0;
  const char *entry = compound(p, '-', NULL);
  if (!entry || !term(p))
    return 0;
  if (!expect_word(p, "exit_abi"))
    return 0;
  const char *exit_abi = compound(p, '-', NULL);
  if (!exit_abi || !term(p))
    return 0;
  const char *required_entry = vector == 13
                                   ? "nx64-interrupt-same-cpl-error-v0"
                                   : "nx64-interrupt-same-cpl-v0";
  const char *required_exit = vector == 13
                                  ? "nx64-iretq-same-cpl-error-v0"
                                  : "nx64-iretq-same-cpl-v0";
  if (strcmp(entry, required_entry) || strcmp(exit_abi, required_exit)) {
    diag(p->p, NTF_E_ABI, table_span, "interrupt witness ABI mismatch");
    p->failed = 1;
    return 0;
  }
  if (!expect_word(p, "exit"))
    return 0;
  const char *exit = name(p, NULL);
  if (!exit || !term(p))
    return 0;
  if (strcmp(exit, "iretq")) {
    diag(p->p, NTF_E_CONTRACT, table_span,
         "interrupt witness exit must be iretq");
    p->failed = 1;
    return 0;
  }
  if (!expect_word(p, "frame"))
    return 0;
  const char *frame = compound(p, '.', NULL);
  if (!frame || !term(p))
    return 0;
  const char *required_frame = vector == 13
                                   ? "x86_64.frame.error.same_cpl"
                                   : "x86_64.frame.no_error.same_cpl";
  if (strcmp(frame, required_frame)) {
    diag(p->p, NTF_E_CONTRACT, table_span, "interrupt frame mismatch");
    p->failed = 1;
    return 0;
  }
  if (!parse_empty_named_set(p, "bindings") ||
      !expect_word(p, "hardware_error_code"))
    return 0;
  const char *error = name(p, NULL);
  if (!error || !term(p))
    return 0;
  int hardware_error = !strcmp(error, "true");
  if ((vector == 6 && hardware_error) ||
      (vector == 13 && strcmp(error, "true"))) {
    diag(p->p, NTF_E_CONTRACT, table_span,
         vector == 13 ? "#GP must declare a hardware error code"
                      : "#UD must not declare a hardware error code");
    p->failed = 1;
    return 0;
  }
  if (!expect_word(p, "save") || !expect(p, '{', "expected save set") ||
      !expect_word(p, "rax") ||
      !expect(p, '}', "witness save set must be {rax}") || !term(p) ||
      !parse_empty_named_set(p, "clobbers"))
    return 0;
  if (!expect_word(p, "stack_align") || p->token.kind != TK_INT)
    return syntax(p, "expected raw stack alignment");
  uint64_t alignment = p->token.value;
  next(p);
  if (!term(p))
    return 0;
  if (alignment != 0) {
    diag(p->p, NTF_E_ABI, table_span,
         "raw interrupt witness requires stack_align 0");
    p->failed = 1;
    return 0;
  }
  if (!expect_word(p, "swapgs"))
    return 0;
  const char *swapgs = name(p, NULL);
  if (!swapgs || !term(p))
    return 0;
  if (strcmp(swapgs, "never")) {
    diag(p->p, NTF_E_CONTRACT, table_span,
         "same-CPL witness requires swapgs never");
    p->failed = 1;
    return 0;
  }
  if (!expect_word(p, "interrupts"))
    return 0;
  const char *interrupts = name(p, NULL);
  if (!interrupts || !term(p))
    return 0;
  if (strcmp(interrupts, "preserve")) {
    diag(p->p, NTF_E_CONTRACT, table_span,
         "interrupt witness must preserve interrupt policy");
    p->failed = 1;
    return 0;
  }
  if (!expect_word(p, "section") || !expect(p, '.', "expected .text") ||
      !expect_word(p, "text") || !term(p) || !expect_word(p, "visibility"))
    return 0;
  const char *visibility = name(p, NULL);
  if (!visibility || !term(p))
    return 0;
  if (strcmp(visibility, "local") && strcmp(visibility, "export")) {
    diag(p->p, NTF_E_CONTRACT, table_span, "invalid interrupt visibility");
    p->failed = 1;
    return 0;
  }
  if (!expect_word(p, "symbol"))
    return 0;
  NtFSpan symbol_span;
  const char *symbol = name(p, &symbol_span);
  if (!symbol || !term(p) || !expect(p, '}', "expected interrupt stub end")) {
    return 0;
  }
  lines(p);
  return build_resume_interrupt_stub(p, symbol_span, symbol,
                                     !strcmp(visibility, "export"),
                                     (uint32_t)vector, hardware_error);
}
static int parse_interrupt_stub_table(Parser *p, NtFSpan table_span) {
  if (!expect(p, '{', "expected interrupt table body"))
    return 0;
  lines(p);
  uint8_t seen[256] = {0};
  size_t count = 0;
  while (p->token.kind != '}' && !p->failed) {
    if (!parse_interrupt_stub_entry(p, table_span, seen))
      return 0;
    ++count;
    lines(p);
  }
  if (!count)
    return syntax(p, "interrupt table requires at least one stub");
  return expect(p, '}', "expected interrupt table end");
}
/* interrupt_table NAME { handler FN  vectors FIRST LAST }
 * Declares generated entry stubs for vectors FIRST..LAST (at most 127). Each
 * stub pushes a zero error code when the CPU does not, then its vector, and
 * jumps to a common routine that saves the 15 general registers, calls
 * FN(frame:u64 @rcx) -> u64 and resumes (restore, drop vector/error, iretq)
 * on the frame FN returns. Returning another frame switches task. */
static int parse_interrupt_table(Parser *p) {
  NtFSpan span = p->token.span;
  if (!expect_word(p, "interrupt_table"))
    return 0;
  NtFSpan symbol_span;
  const char *symbol = name(p, &symbol_span);
  if (!symbol || !expect(p, '{', "expected interrupt table body"))
    return 0;
  lines(p);
  if (!expect_word(p, "handler"))
    return 0;
  const char *handler = name(p, NULL);
  if (!handler || !term(p))
    return 0;
  if (!expect_word(p, "vectors") || p->token.kind != TK_INT)
    return syntax(p, "expected first interrupt vector");
  uint64_t first = p->token.value;
  next(p);
  if (p->token.kind != TK_INT)
    return syntax(p, "expected last interrupt vector");
  uint64_t last = p->token.value;
  next(p);
  if (!term(p) || !expect(p, '}', "expected interrupt table end"))
    return 0;
  if (first > last || last > 127) {
    diag(p->p, NTF_E_CONTRACT, span,
         "interrupt table vectors must be an ascending range within 0..127");
    p->failed = 1;
    return 0;
  }
  NtFType never = {NTF_T_NEVER, NTF_SPACE_NONE, 0, 0, 0};
  NtFId never_type = intern_type(p->p, never, span);
  NtFId function = add_function(p->p, span);
  if (!function || !never_type) {
    p->failed = 1;
    return 0;
  }
  NtFFunction *fn = &FN(p->p, function);
  fn->name = symbol;
  fn->abi = "nx64-interrupt-dispatch-v0";
  fn->section = ".text";
  fn->span = symbol_span;
  fn->module = p->module;
  fn->return_type = never_type;
  fn->effects.flags =
      NTF_F_PRIVILEGED | NTF_F_CHANGES_FLAGS | NTF_F_CONTROL | NTF_F_NO_RETURN;
  fn->effects.reads = 1u << NTF_SPACE_KERNEL;
  fn->effects.writes = 1u << NTF_SPACE_KERNEL;
  fn->generated_stub = 3;
  fn->stub_vector = (uint32_t)(first | (last << 8));
  fn->import_name = handler;
  return 1;
}
static int parse_stub_table(Parser *p) {
  NtFSpan table_span = p->token.span;
  if (!expect_word(p, "stub_table"))
    return 0;
  const char *table_name = name(p, NULL);
  (void)table_name;
  if (!expect_word(p, "kind"))
    return 0;
  const char *kind = name(p, NULL);
  if (kind && !strcmp(kind, "interrupt"))
    return parse_interrupt_stub_table(p, table_span);
  if (!kind || strcmp(kind, "syscall")) {
    diag(p->p, NTF_E_UNSUPPORTED, table_span,
         "only safe-return syscall stub tables are implemented");
    p->failed = 1;
    return 0;
  }
  if (!expect(p, '{', "expected stub table body"))
    return 0;
  lines(p);
  uint64_t numbers[512];
  size_t number_count = 0;
  while (p->token.kind != '}' && !p->failed) {
    NtFSpan stub_span = p->token.span;
    if (!expect_word(p, "stub") || p->token.kind != TK_INT)
      return syntax(p, "expected numeric stub");
    uint64_t number = p->token.value;
    next(p);
    for (size_t i = 0; i < number_count; i++)
      if (numbers[i] == number) {
        diag(p->p, NTF_E_DUPLICATE, stub_span, "duplicate stub number");
        p->failed = 1;
        return 0;
      }
    if (number_count == 512) {
      diag(p->p, NTF_E_LIMIT, stub_span, "stub table entry limit exceeded");
      p->failed = 1;
      return 0;
    }
    numbers[number_count++] = number;
    if (!expect(p, '{', "expected stub body"))
      return 0;
    lines(p);
    if (!expect_word(p, "handler"))
      return 0;
    const char *handler = compound(p, '.', NULL);
    if (!handler || !term(p))
      return 0;
    if (!expect_word(p, "entry_abi"))
      return 0;
    const char *entry_abi = compound(p, '-', NULL);
    if (!entry_abi || !term(p))
      return 0;
    if (!expect_word(p, "exit_abi"))
      return 0;
    const char *exit_abi = compound(p, '-', NULL);
    if (!exit_abi || !term(p))
      return 0;
    if (strcmp(entry_abi, "nx64-abi-v0") || strcmp(exit_abi, "nx64-abi-v0")) {
      diag(p->p, NTF_E_ABI, stub_span, "unsupported structured stub ABI");
      p->failed = 1;
      return 0;
    }
    if (!expect_word(p, "exit"))
      return 0;
    const char *exit_policy = name(p, NULL);
    if (!exit_policy || !term(p))
      return 0;
    if (strcmp(exit_policy, "return")) {
      diag(p->p, NTF_E_UNSUPPORTED, stub_span,
           "only safe return structured stubs are implemented");
      p->failed = 1;
      return 0;
    }
    if (!expect_word(p, "frame"))
      return 0;
    const char *frame = compound(p, '.', NULL);
    if (!frame || !term(p))
      return 0;
    if (strcmp(frame, "syscall.frame")) {
      diag(p->p, NTF_E_CONTRACT, stub_span, "unknown structured stub frame");
      p->failed = 1;
      return 0;
    }
    if (!parse_empty_named_set(p, "bindings") ||
        !expect_word(p, "hardware_error_code"))
      return 0;
    const char *hardware_error = name(p, NULL);
    if (!hardware_error || !term(p))
      return 0;
    if (strcmp(hardware_error, "false")) {
      diag(p->p, NTF_E_CONTRACT, stub_span,
           "safe return stub cannot consume hardware error code");
      p->failed = 1;
      return 0;
    }
    if (!parse_empty_named_set(p, "save") ||
        !parse_empty_named_set(p, "clobbers"))
      return 0;
    if (!expect_word(p, "stack_align") || p->token.kind != TK_INT)
      return syntax(p, "expected stack alignment");
    uint64_t stack_align = p->token.value;
    next(p);
    if (!term(p))
      return 0;
    if (stack_align != 16) {
      diag(p->p, NTF_E_ABI, stub_span,
           "structured stub requires stack_align 16");
      p->failed = 1;
      return 0;
    }
    if (!expect_word(p, "swapgs"))
      return 0;
    const char *swapgs = name(p, NULL);
    if (!swapgs || !term(p))
      return 0;
    if (strcmp(swapgs, "never")) {
      diag(p->p, NTF_E_CONTRACT, stub_span,
           "safe return stub requires swapgs never");
      p->failed = 1;
      return 0;
    }
    if (!expect_word(p, "interrupts"))
      return 0;
    const char *interrupts = name(p, NULL);
    if (!interrupts || !term(p))
      return 0;
    if (strcmp(interrupts, "preserve")) {
      diag(p->p, NTF_E_CONTRACT, stub_span,
           "safe return stub requires interrupt preservation");
      p->failed = 1;
      return 0;
    }
    if (!expect_word(p, "section") || !expect(p, '.', "expected stub section"))
      return 0;
    const char *section_part = name(p, NULL);
    if (!section_part || !term(p))
      return 0;
    if (strcmp(section_part, "text")) {
      diag(p->p, NTF_E_CONTRACT, stub_span,
           "structured executable stub must use .text");
      p->failed = 1;
      return 0;
    }
    if (!expect_word(p, "visibility"))
      return 0;
    const char *visibility = name(p, NULL);
    if (!visibility || !term(p))
      return 0;
    if (strcmp(visibility, "local") && strcmp(visibility, "export")) {
      diag(p->p, NTF_E_CONTRACT, stub_span, "invalid stub visibility");
      p->failed = 1;
      return 0;
    }
    if (!expect_word(p, "symbol"))
      return 0;
    NtFSpan symbol_span;
    const char *symbol = name(p, &symbol_span);
    if (!symbol || !term(p) || !expect(p, '}', "expected stub end"))
      return 0;
    if (duplicate_function(p->p, p->module, symbol)) {
      diag(p->p, NTF_E_DUPLICATE, symbol_span, "duplicate stub symbol '%s'",
           symbol);
      p->failed = 1;
      return 0;
    }
    NtFId function = add_function(p->p, stub_span);
    NtFType u64 = {NTF_T_U64, NTF_SPACE_NONE, 0, 0, 64};
    NtFId return_type = intern_type(p->p, u64, stub_span);
    NtFId call = add_expression(p->p, stub_span);
    NtFId statement = add_statement(p->p, stub_span);
    NtFId block = add_statement(p->p, stub_span);
    if (!function || !return_type || !call || !statement || !block) {
      p->failed = 1;
      return 0;
    }
    EX(p->p, call).kind = NTF_X_CALL;
    EX(p->p, call).span = stub_span;
    EX(p->p, call).name = handler;
    EX(p->p, call).reg.family = -1;
    ST(p->p, statement).kind = NTF_S_RETURN;
    ST(p->p, statement).span = stub_span;
    ST(p->p, statement).expression = call;
    ST(p->p, statement).terminal = 1;
    ST(p->p, block).kind = NTF_S_BLOCK;
    ST(p->p, block).span = stub_span;
    ST(p->p, block).first = statement;
    ST(p->p, block).terminal = 1;
    NtFFunction *fn = &FN(p->p, function);
    fn->name = symbol;
    fn->abi = entry_abi;
    fn->section = ".text";
    fn->span = stub_span;
    fn->module = p->module;
    fn->return_type = return_type;
    fn->body = block;
    fn->exported = !strcmp(visibility, "export");
    lines(p);
  }
  return expect(p, '}', "expected stub table end");
}
static NtFId clone_macro_type(Parser *p, NtFId type_id, const NtFId *arguments,
                              size_t argument_count, NtFSpan invocation) {
  if (type_id & UINT32_C(0x80000000)) {
    size_t index = (size_t)(type_id & UINT32_C(0x7fffffff));
    if (index >= argument_count) {
      diag(p->p, NTF_E_CONTRACT, invocation, "macro type splice is invalid");
      p->failed = 1;
      return 0;
    }
    return arguments[index];
  }
  NtFType type = TY(p->p, type_id);
  if (type.element) {
    type.element = clone_macro_type(p, type.element, arguments, argument_count,
                                    invocation);
    if (!type.element)
      return 0;
  }
  return intern_type(p->p, type, invocation);
}
static int parse_value_macro_arguments(Parser *p, Macro *macro,
                                       NtFId arguments[8], size_t *count) {
  if (!expect(p, '(', "expected macro arguments"))
    return 0;
  *count = 0;
  lines(p);
  while (*count < macro->parameter_count && !p->failed) {
    if (macro->parameter_kinds[*count] == 2) {
      if (is(p, "quote")) {
        next(p);
        if (!expect(p, '(', "expected '(' after quote") ||
            !expect_word(p, "type") ||
            !expect(p, ')', "expected ')' after type quote") ||
            !expect(p, '{', "expected type quote body"))
          return 0;
        lines(p);
        arguments[*count] = parse_type(p);
        lines(p);
        if (!arguments[*count] || !expect(p, '}', "expected type quote end"))
          return 0;
      } else if (is(p, "expand"))
        arguments[*count] = parse_type(p);
      else {
        diag(p->p, NTF_E_TYPE, p->token.span,
             "ast.type argument requires quote(type) or type expansion");
        p->failed = 1;
        return 0;
      }
    } else
      arguments[*count] = parse_expression(p, 0);
    if (!arguments[*count])
      return 0;
    ++*count;
    lines(p);
    if (*count < macro->parameter_count) {
      if (!expect(p, ',', "expected next macro argument"))
        return 0;
      lines(p);
    }
  }
  if (!expect(p, ')', "expected ')' after macro arguments"))
    return 0;
  if (*count != macro->parameter_count) {
    diag(p->p, NTF_E_CONTRACT, p->token.span,
         "macro argument count mismatch");
    p->failed = 1;
    return 0;
  }
  return 1;
}
static NtFId expand_macro_value(Parser *p, Macro *macro,
                                const NtFId *arguments, size_t count,
                                NtFSpan invocation, int want_type) {
  if (!validate_macro_arguments(p, macro, arguments, count, invocation))
    return 0;
  Private *v = p->p->_private;
  if (v->expansion_depth >= v->expansion_limit ||
      !consume_fuel(p, invocation)) {
    if (!p->failed) {
      diag(p->p, NTF_E_LIMIT, invocation, "macro expansion budget exhausted");
      p->failed = 1;
    }
    return 0;
  }
  ++v->expansion_depth;
  ++v->expansion_serial;
  Macro *previous = p->macro;
  p->macro = macro;
  NtFId result = want_type
                     ? clone_macro_type(p, macro->template_type, arguments,
                                        count, invocation)
                     : clone_macro_expression(p, macro->template_expression,
                                              arguments, count, invocation);
  p->macro = previous;
  --v->expansion_depth;
  return result;
}
static NtFId parse_expand_value(Parser *p, int want_type) {
  NtFSpan invocation = p->token.span;
  if (!expect_word(p, "expand"))
    return 0;
  const char *macro_name = name(p, NULL);
  Macro *macro = macro_name ? find_macro(p, macro_name) : NULL;
  if (!macro) {
    diag(p->p, NTF_E_NAME, invocation, "unknown macro '%s'",
         macro_name ? macro_name : "");
    p->failed = 1;
    return 0;
  }
  if ((want_type && !macro->result_type) ||
      (!want_type && !macro->result_expr)) {
    diag(p->p, NTF_E_TYPE, invocation,
         want_type ? "type expansion requires an ast.type macro"
                   : "expression expansion requires an ast.expr macro");
    p->failed = 1;
    return 0;
  }
  Private *v = p->p->_private;
  if (v->meta_call_depth >= v->call_depth_limit) {
    diag(p->p, NTF_E_LIMIT, invocation, "macro call depth exhausted");
    p->failed = 1;
    return 0;
  }
  ++v->meta_call_depth;
  NtFId arguments[8] = {0};
  size_t count = 0;
  if (!parse_value_macro_arguments(p, macro, arguments, &count)) {
    --v->meta_call_depth;
    return 0;
  }
  NtFId result =
      expand_macro_value(p, macro, arguments, count, invocation, want_type);
  --v->meta_call_depth;
  return result;
}
static NtFId parse_expand_expression(Parser *p) {
  return parse_expand_value(p, 0);
}
static NtFId parse_expand_type(Parser *p) { return parse_expand_value(p, 1); }
static int expand_macro_declaration(Parser *p, Macro *macro,
                                    const char *section, NtFSpan invocation) {
  if (!macro->result_decl) {
    diag(p->p, NTF_E_TYPE, invocation,
         "section expansion requires an ast.decl macro");
    p->failed = 1;
    return 0;
  }
  if (!expect(p, '(', "expected macro arguments"))
    return 0;
  NtFId arguments[8];
  size_t count = 0;
  lines(p);
  while (count < macro->parameter_count && !p->failed) {
    if (macro->parameter_kinds[count] == 2) {
      if (is(p, "quote")) {
        next(p);
        if (!expect(p, '(', "expected '(' after quote") ||
            !expect_word(p, "type") ||
            !expect(p, ')', "expected ')' after type quote") ||
            !expect(p, '{', "expected type quote body"))
          return 0;
        lines(p);
        arguments[count] = parse_type(p);
        lines(p);
        if (!arguments[count] || !expect(p, '}', "expected type quote end"))
          return 0;
      } else if (is(p, "expand"))
        arguments[count] = parse_type(p);
      else {
        diag(p->p, NTF_E_TYPE, p->token.span,
             "ast.type argument requires quote(type) or type expansion");
        p->failed = 1;
        return 0;
      }
    } else
      arguments[count] = parse_expression(p, 0);
    if (!arguments[count])
      return 0;
    ++count;
    lines(p);
    if (count < macro->parameter_count) {
      if (!expect(p, ',', "expected next macro argument"))
        return 0;
      lines(p);
    }
  }
  if (!expect(p, ')', "expected ')' after macro arguments") || !term(p))
    return 0;
  if (count != macro->parameter_count) {
    diag(p->p, NTF_E_CONTRACT, invocation, "macro argument count mismatch");
    p->failed = 1;
    return 0;
  }
  if (!validate_macro_arguments(p, macro, arguments, count, invocation))
    return 0;
  Private *v = p->p->_private;
  if (v->expansion_depth >= v->expansion_limit ||
      !consume_fuel(p, invocation)) {
    if (!p->failed) {
      diag(p->p, NTF_E_LIMIT, invocation,
           "declaration expansion budget exhausted");
      p->failed = 1;
    }
    return 0;
  }
  ++v->expansion_depth;
  ++v->expansion_serial;
  if (macro->emit_list) {
    p->macro = macro;
    size_t template_begin = 0, template_end = macro->template_count;
    if (macro->meta_if) {
      NtFId condition = clone_macro_expression(p, macro->meta_condition,
                                               arguments, count, invocation);
      uint64_t value;
      int negative;
      if (!condition || !constant_value(p->p, condition, &value, &negative)) {
        diag(p->p, NTF_E_CONTRACT, invocation,
             "declaration meta-if is not constant");
        p->failed = 1;
        p->macro = NULL;
        --v->expansion_depth;
        return 0;
      }
      if (value && !negative)
        template_end = macro->else_start;
      else
        template_begin = macro->else_start;
    }
    uint64_t repeat_count = 1;
    if (macro->meta_for) {
      NtFId count_expression = clone_macro_expression(
          p, macro->loop_count, arguments, count, invocation);
      int negative;
      if (!count_expression ||
          !constant_value(p->p, count_expression, &repeat_count, &negative) ||
          negative || repeat_count > 512 ||
          repeat_count * (template_end - template_begin) > 512) {
        diag(p->p, NTF_E_LIMIT, invocation,
             "declaration meta-for output exceeds 512");
        p->failed = 1;
        p->macro = NULL;
        --v->expansion_depth;
        return 0;
      }
    }
    for (uint64_t repeat = 0; repeat < repeat_count; repeat++) {
      macro->loop_value = repeat;
      memset(macro->expanded_declarations, 0,
             sizeof(macro->expanded_declarations));
      for (size_t item_index = template_begin; item_index < template_end;
           item_index++) {
        MacroTemplate *item = &macro->templates[item_index];
        NtFId type =
            clone_macro_type(p, item->type, arguments, count, invocation);
        NtFId initializer = clone_macro_expression(
            p, item->expression, arguments, count, invocation);
        if (!type || !initializer) {
          p->macro = NULL;
          --v->expansion_depth;
          return 0;
        }
        const char *decl_name = item->decl_name;
        if (!item->decl_exported) {
          char generated[160];
          snprintf(generated, sizeof(generated),
                   "__ntd$%u$e%" PRIu64 "$r%" PRIu64 "$t%zu$%s", macro->ordinal,
                   v->expansion_serial, repeat, item_index, item->decl_name);
          decl_name = copy_text(p->p, generated, strlen(generated));
          if (!decl_name) {
            diag(p->p, NTF_E_LIMIT, invocation,
                 "declaration hygiene exceeds arena");
            p->failed = 1;
            p->macro = NULL;
            --v->expansion_depth;
            return 0;
          }
        }
        for (size_t i = 0; i < p->p->variable_count; i++)
          if (!p->p->variables[i].function &&
              p->p->variables[i].module == p->module &&
              !strcmp(p->p->variables[i].name, decl_name)) {
            diag(p->p, NTF_E_DUPLICATE, invocation,
                 "duplicate generated declaration '%s'", decl_name);
            p->failed = 1;
            p->macro = NULL;
            --v->expansion_depth;
            return 0;
          }
        NtFId variable = add_variable(p->p, invocation);
        if (!variable) {
          p->failed = 1;
          p->macro = NULL;
          --v->expansion_depth;
          return 0;
        }
        NtFVariable *decl = &VA(p->p, variable);
        decl->name = decl_name;
        decl->span = invocation;
        decl->type = type;
        decl->module = p->module;
        decl->initializer = initializer;
        decl->direction = item->decl_data ? NTF_DATA : NTF_CONST;
        decl->reg.family = -1;
        decl->exported = item->decl_exported;
        decl->section = item->decl_data ? section : NULL;
        macro->expanded_declarations[item_index] = variable;
      }
    }
    p->macro = NULL;
    --v->expansion_depth;
    return 1;
  }
  Macro *previous_macro = p->macro;
  p->macro = macro;
  NtFId type =
      clone_macro_type(p, macro->decl_type, arguments, count, invocation);
  NtFId initializer = 0;
  if (!macro->decl_function)
    initializer = clone_macro_expression(p, macro->decl_initializer, arguments,
                                         count, invocation);
  p->macro = previous_macro;
  --v->expansion_depth;
  if (!type || (!macro->decl_function && !initializer))
    return 0;
  const char *decl_name = macro->decl_name;
  if (!macro->decl_exported) {
    char generated[160];
    snprintf(generated, sizeof(generated), "__ntd$%u$e%" PRIu64 "$%s",
             macro->ordinal, v->expansion_serial, macro->decl_name);
    decl_name = copy_text(p->p, generated, strlen(generated));
    if (!decl_name) {
      diag(p->p, NTF_E_LIMIT, invocation, "declaration hygiene exceeds arena");
      p->failed = 1;
      return 0;
    }
  }
  if (macro->decl_function) {
    if (strcmp(section, ".text")) {
      diag(p->p, NTF_E_CONTRACT, invocation,
           "generated function declaration requires .text");
      p->failed = 1;
      return 0;
    }
    if (duplicate_function(p->p, p->module, decl_name)) {
      diag(p->p, NTF_E_DUPLICATE, invocation,
           "duplicate generated function '%s'", decl_name);
      p->failed = 1;
      return 0;
    }
    NtFId function = add_function(p->p, invocation);
    if (!function) {
      p->failed = 1;
      return 0;
    }
    NtFFunction *fn = &FN(p->p, function);
    fn->name = decl_name;
    fn->abi = macro->decl_abi ? macro->decl_abi
                              : MO(p->p, p->module).default_abi;
    fn->section = section;
    fn->span = invocation;
    fn->module = p->module;
    fn->return_type = type;
    fn->exported = macro->decl_exported;
    fn->effects = macro->decl_effects;
    fn->clobbers = macro->decl_clobbers;
    memset(macro->expanded_parameters, 0, sizeof(macro->expanded_parameters));
    p->macro = macro;
    for (size_t i = 0; i < macro->decl_param_count; i++) {
      NtFId parameter_type = clone_macro_type(
          p, macro->decl_param_types[i], arguments, count, invocation);
      if (!parameter_type) {
        p->macro = previous_macro;
        return 0;
      }
      NtFRegister reg = macro->decl_param_regs[i];
      if (reg.family >= 0 && TY(p->p, parameter_type).bits != reg.bits &&
          !(TY(p->p, parameter_type).kind == NTF_T_BOOL && reg.bits == 8)) {
        diag(p->p, NTF_E_REGISTER, invocation,
             "generated parameter/register width mismatch");
        p->failed = 1;
        p->macro = previous_macro;
        return 0;
      }
      char generated[160];
      snprintf(generated, sizeof(generated),
               "__ntp$%u$e%" PRIu64 "$p%zu$%s", macro->ordinal,
               v->expansion_serial, i, macro->decl_param_names[i]);
      const char *parameter_name =
          copy_text(p->p, generated, strlen(generated));
      NtFId parameter = add_variable(p->p, invocation);
      if (!parameter_name || !parameter) {
        diag(p->p, NTF_E_LIMIT, invocation,
             "generated parameter exceeds arena or node budget");
        p->failed = 1;
        p->macro = previous_macro;
        return 0;
      }
      NtFVariable *variable = &VA(p->p, parameter);
      variable->name = parameter_name;
      variable->span = invocation;
      variable->type = parameter_type;
      variable->function = function;
      variable->module = p->module;
      variable->direction = macro->decl_param_directions[i];
      variable->reg = reg;
      if (!fn->first_param)
        fn->first_param = parameter;
      ++fn->param_count;
      macro->expanded_parameters[i] = parameter;
    }
    if (!validate_parameter_abi(p, fn)) {
      p->macro = previous_macro;
      return 0;
    }
    p->macro = previous_macro;
    Macro body = *macro;
    body.result_block = 1;
    body.result_decl = 0;
    body.emit_list = 0;
    body.meta_if = 0;
    body.meta_for = 0;
    NtFId previous_function = p->function;
    p->function = function;
    NtFId block =
        expand_macro_statement(p, invocation, &body, arguments, count);
    p->function = previous_function;
    p->macro = previous_macro;
    if (!block)
      return 0;
    fn->body = block;
    return 1;
  }
  for (size_t i = 0; i < p->p->variable_count; i++)
    if (!p->p->variables[i].function &&
        p->p->variables[i].module == p->module &&
        !strcmp(p->p->variables[i].name, decl_name)) {
      diag(p->p, NTF_E_DUPLICATE, invocation,
           "duplicate generated declaration '%s'", decl_name);
      p->failed = 1;
      return 0;
    }
  NtFId variable = add_variable(p->p, invocation);
  if (!variable) {
    p->failed = 1;
    return 0;
  }
  NtFVariable *data = &VA(p->p, variable);
  data->name = decl_name;
  data->span = invocation;
  data->type = type;
  data->module = p->module;
  data->initializer = initializer;
  data->direction = NTF_DATA;
  data->reg.family = -1;
  data->exported = macro->decl_exported;
  data->section = section;
  return 1;
}
static int parse_section(Parser *p) {
  if (!expect_word(p, "section") || !expect(p, '.', "expected section name"))
    return 0;
  const char *part = name(p, NULL);
  if (!part)
    return 0;
  size_t n = strlen(part);
  char *section = (char *)owned(p->p, n + 2);
  if (!section) {
    p->failed = 1;
    return 0;
  }
  section[0] = '.';
  memcpy(section + 1, part, n + 1);
  if (!expect(p, '{', "expected section block"))
    return 0;
  lines(p);
  while (p->token.kind != '}' && p->token.kind != TK_EOF && !p->failed) {
    if (accept_word(p, "expand")) {
      NtFSpan invocation;
      const char *macro_name = name(p, &invocation);
      Macro *macro = macro_name ? find_macro(p, macro_name) : NULL;
      if (!macro) {
        diag(p->p, NTF_E_NAME, invocation, "unknown macro '%s'",
             macro_name ? macro_name : "");
        p->failed = 1;
        return 0;
      }
      if (!expand_macro_declaration(p, macro, section, invocation))
        return 0;
      lines(p);
      continue;
    }
    int exported = accept_word(p, "export");
    if (is(p, "fn")) {
      if (!parse_function(p, section, exported))
        return 0;
    } else if (is(p, "data")) {
      if (!parse_data(p, section, exported))
        return 0;
    } else
      return syntax(p, "expected function in section");
    lines(p);
  }
  return expect(p, '}', "expected section end");
}
static int parse_source(Parser *p) {
  next(p);
  lines(p);
  NtFSpan span = p->token.span;
  if (!expect_word(p, "module"))
    return 0;
  const char *module_name = compound(p, '.', &span);
  if (!module_name || !term(p) || !expect_word(p, "target"))
    return 0;
  NtFSpan target_span;
  const char *target = compound(p, '-', &target_span);
  if (!target || !term(p))
    return 0;
  for (size_t i = 0; i < p->p->module_count; i++)
    if (!strcmp(p->p->modules[i].name, module_name)) {
      diag(p->p, NTF_E_DUPLICATE, span, "duplicate module '%s'", module_name);
      p->failed = 1;
      return 0;
    }
  NtFId module = add_module(p->p, span);
  if (!module) {
    p->failed = 1;
    return 0;
  }
  p->module = module;
  NtFModule *m = &MO(p->p, module);
  m->name = module_name;
  m->target = target;
  m->span = span;
  m->features = 1;
  m->default_abi = "nx64-abi-v0";
  if (strcmp(target, "x86_64-nexora-none") &&
      strcmp(target, "x86_64-nexora-uefi")) {
    diag(p->p, NTF_E_TARGET, target_span, "unsupported target '%s'", target);
    p->failed = 1;
    return 0;
  }
  while (p->token.kind != TK_EOF && !p->failed) {
    lines(p);
    if (p->token.kind == TK_EOF)
      break;
    if (is(p, "default_abi")) {
      next(p);
      m = &MO(p->p, module);
      m->default_abi = compound(p, '-', NULL);
      if (!m->default_abi || !term(p))
        return 0;
    } else if (is(p, "features")) {
      next(p);
      if (!expect(p, '{', "expected feature set"))
        return 0;
      lines(p);
      m = &MO(p->p, module);
      while (p->token.kind != '}' && !p->failed) {
        NtFSpan feature_span;
        const char *feature = compound(p, '.', &feature_span);
        uint32_t bit = target_feature_bit(feature);
        if (!bit) {
          diag(p->p, NTF_E_TARGET, feature_span, "unknown target feature '%s'",
               feature);
          p->failed = 1;
          return 0;
        }
        if (m->features & bit && bit != 1) {
          diag(p->p, NTF_E_DUPLICATE, feature_span,
               "duplicate target feature '%s'", feature);
          p->failed = 1;
          return 0;
        }
        m->features |= bit;
        lines(p);
        if (!accept(p, ','))
          break;
        lines(p);
      }
      if (!expect(p, '}', "expected feature set end"))
        return 0;
      if (p->token.kind == ';' || p->token.kind == TK_NL)
        term(p);
    } else if (is(p, "import")) {
      if (!parse_import(p))
        return 0;
    } else if (is(p, "const")) {
      if (!parse_constant(p))
        return 0;
    } else if (is(p, "comptime")) {
      if (!parse_comptime(p))
        return 0;
    } else if (is(p, "macro")) {
      if (!parse_macro(p))
        return 0;
    } else if (is(p, "section")) {
      if (!parse_section(p))
        return 0;
    } else if (is(p, "stub_table")) {
      if (!parse_stub_table(p))
        return 0;
    } else if (is(p, "interrupt_table")) {
      if (!parse_interrupt_table(p))
        return 0;
    } else
      return syntax(p, "unsupported top-level declaration");
    lines(p);
  }
  return !p->failed;
}

typedef struct {
  const char **names;
  NtFId *ids;
  size_t count, capacity;
} LabelTable;
static int branch_name(const char *name) {
  static const char *names[] = {"jmp", "je", "jne", "jb", "jbe", "ja",
                                "jae", "jl", "jle", "jg", "jge", "jo",
                                "jno", "js", "jns", "jp", "jnp"};
  for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
    if (!strcmp(name, names[i]))
      return 1;
  return 0;
}
static uint32_t branch_flags(const char *name) {
  if (!strcmp(name, "jmp"))
    return 0;
  if (!strcmp(name, "je") || !strcmp(name, "jne"))
    return NT_X64_FLAG_ZF;
  if (!strcmp(name, "jb") || !strcmp(name, "jae"))
    return NT_X64_FLAG_CF;
  if (!strcmp(name, "jbe") || !strcmp(name, "ja"))
    return NT_X64_FLAG_CF | NT_X64_FLAG_ZF;
  if (!strcmp(name, "jl") || !strcmp(name, "jge"))
    return NT_X64_FLAG_SF | NT_X64_FLAG_OF;
  if (!strcmp(name, "jle") || !strcmp(name, "jg"))
    return NT_X64_FLAG_ZF | NT_X64_FLAG_SF | NT_X64_FLAG_OF;
  if (!strcmp(name, "jo") || !strcmp(name, "jno"))
    return NT_X64_FLAG_OF;
  if (!strcmp(name, "js") || !strcmp(name, "jns"))
    return NT_X64_FLAG_SF;
  return NT_X64_FLAG_PF;
}
static int collect_labels(NtFProgram *p, NtFId block, LabelTable *table) {
  for (NtFId id = ST(p, block).first; id; id = ST(p, id).next) {
    NtFStmt *s = &ST(p, id);
    if (s->kind == NTF_S_LABEL) {
      for (size_t i = 0; i < table->count; i++)
        if (!strcmp(table->names[i], s->name)) {
          diag(p, NTF_E_DUPLICATE, s->span, "duplicate label '%s'", s->name);
          return 0;
        }
      if (table->count == table->capacity) {
        size_t capacity = table->capacity ? table->capacity * 2 : 16;
        const char **names =
            (const char **)realloc(table->names, capacity * sizeof(*names));
        if (!names) {
          diag(p, NTF_E_OOM, s->span, "label table allocation failed");
          return 0;
        }
        table->names = names;
        NtFId *ids = (NtFId *)realloc(table->ids, capacity * sizeof(*ids));
        if (!ids) {
          diag(p, NTF_E_OOM, s->span, "label table allocation failed");
          return 0;
        }
        table->ids = ids;
        table->capacity = capacity;
      }
      table->names[table->count] = s->name;
      table->ids[table->count++] = id;
    }
    if (s->kind == NTF_S_BLOCK && !collect_labels(p, id, table))
      return 0;
    if (s->then_branch && !collect_labels(p, s->then_branch, table))
      return 0;
    if (s->else_branch && !collect_labels(p, s->else_branch, table))
      return 0;
  }
  return 1;
}
static int resolve_jumps(NtFProgram *p, NtFId block, const LabelTable *table) {
  for (NtFId id = ST(p, block).first; id; id = ST(p, id).next) {
    NtFStmt *s = &ST(p, id);
    if (s->kind == NTF_S_INSTRUCTION && branch_name(s->name)) {
      NtFId operand = s->expression;
      if (!operand || EX(p, operand).next ||
          EX(p, operand).kind != NTF_X_NAME) {
        diag(p, NTF_E_FLOW, s->span, "branch requires one local label");
        return 0;
      }
      NtFId target = 0;
      for (size_t i = 0; i < table->count; i++)
        if (!strcmp(table->names[i], EX(p, operand).name))
          target = table->ids[i];
      if (!target) {
        diag(p, NTF_E_NAME, EX(p, operand).span, "unknown label '%s'",
             EX(p, operand).name);
        return 0;
      }
      s->variable = target;
    }
    if (s->kind == NTF_S_BLOCK && !resolve_jumps(p, id, table))
      return 0;
    if (s->then_branch && !resolve_jumps(p, s->then_branch, table))
      return 0;
    if (s->else_branch && !resolve_jumps(p, s->else_branch, table))
      return 0;
  }
  return 1;
}
static int resolve_function_labels(NtFProgram *p, NtFId function) {
  LabelTable table = {0};
  int ok = collect_labels(p, FN(p, function).body, &table);
  if (ok)
    ok = resolve_jumps(p, FN(p, function).body, &table);
  free(table.names);
  free(table.ids);
  return ok;
}
static int verify_statement(NtFProgram *, NtFId, NtFId);
static int frontend_x64_operand(NtFProgram *p, NtFId id, const char *mnemonic,
                                NtX64Operand *out) {
  NtFExpr *expression = &EX(p, id);
  memset(out, 0, sizeof(*out));
  if (expression->kind == NTF_X_REGISTER) {
    out->kind = NT_X64_REG;
    out->width = expression->reg.bits;
    out->reg = (unsigned)expression->reg.family;
    out->high8 = (unsigned)expression->reg.high8;
    return 1;
  }
  if (expression->kind == NTF_X_MEMORY) {
    out->kind = NT_X64_MEM;
    NtFType *pointer = &TY(p, expression->type);
    NtFType *element = &TY(p, pointer->element);
    out->width = element->bits;
    if ((!strcmp(mnemonic, "lgdt") || !strcmp(mnemonic, "lidt") ||
         !strcmp(mnemonic, "sgdt") || !strcmp(mnemonic, "sidt")) &&
        element->kind == NTF_T_ARRAY && element->count == 10 &&
        element->element && TY(p, element->element).kind == NTF_T_U8)
      out->width = 80;
    if (!strcmp(mnemonic, "invlpg"))
      out->width = 0;
    out->base = expression->memory.symbol ? -2 : expression->memory.base.family;
    out->index = expression->memory.index.family;
    out->scale = expression->memory.scale;
    out->disp = expression->memory.displacement;
    out->segment = expression->memory.segment == 1   ? NT_X64_FS
                   : expression->memory.segment == 2 ? NT_X64_GS
                                                     : NT_X64_SEG_NONE;
    return 1;
  }
  if (expression->kind == NTF_X_NAME && expression->name &&
      expression->name[0] == 'c' && expression->name[1] == 'r') {
    char *end = NULL;
    unsigned long number = strtoul(expression->name + 2, &end, 10);
    if (end && !*end) {
      out->kind = NT_X64_CR;
      out->width = 64;
      out->reg = (unsigned)number;
      return 1;
    }
  }
  uint64_t value;
  int negative;
  if (constant_value(p, id, &value, &negative)) {
    out->kind = NT_X64_IMM;
    out->imm = negative ? UINT64_C(0) - value : value;
    out->is_signed = (unsigned)negative;
    return 1;
  }
  return 0;
}
static int verify_block(NtFProgram *p, NtFId block_id, NtFId owner) {
  NtFStmt *block = &ST(p, block_id);
  NtFId last = 0;
  for (NtFId id = block->first; id; id = ST(p, id).next) {
    if (!verify_statement(p, id, owner))
      return 0;
    last = id;
  }
  ST(p, block_id).terminal = last ? ST(p, last).terminal : 0;
  return 1;
}
static int verify_statement(NtFProgram *p, NtFId id, NtFId owner) {
  NtFStmt *s = &ST(p, id);
  NtFFunction *f = &FN(p, owner);
  if (s->kind == NTF_S_BLOCK) {
    if (!verify_block(p, id, owner))
      return 0;
    return 1;
  }
  if (s->kind == NTF_S_LABEL)
    return 1;
  if (s->kind == NTF_S_LET) {
    NtFVariable *v = &VA(p, s->variable);
    return infer(p, s->expression, v->type, owner) != 0;
  }
  if (s->kind == NTF_S_RETURN) {
    if (TY(p, f->return_type).kind == NTF_T_NEVER) {
      diag(p, NTF_E_FLOW, s->span, "never function cannot return");
      return 0;
    }
    if (!s->expression) {
      diag(p, NTF_E_TYPE, s->span, "return value required");
      return 0;
    }
    return infer(p, s->expression, f->return_type, owner) != 0;
  }
  if (s->kind == NTF_S_CALL)
    return infer(p, s->expression, 0, owner) != 0;
  if (s->kind == NTF_S_IF) {
    NtFType boolean = {NTF_T_BOOL, NTF_SPACE_NONE, 0, 0, 1};
    NtFId bool_id = intern_type(p, boolean, s->span);
    if (!infer(p, s->condition, bool_id, owner) ||
        !verify_block(p, s->then_branch, owner) ||
        (s->else_branch && !verify_block(p, s->else_branch, owner)))
      return 0;
    s = &ST(p, id);
    s->terminal = s->else_branch && ST(p, s->then_branch).terminal &&
                  ST(p, s->else_branch).terminal;
    return 1;
  }
  if (s->kind == NTF_S_INSTRUCTION) {
    uint32_t flags = 0;
    uint64_t writes = 0;
    if (branch_name(s->name)) {
      if (!s->variable) {
        diag(p, NTF_E_FLOW, s->span, "unresolved local branch");
        return 0;
      }
      s->flags_read = branch_flags(s->name);
      return 1;
    } else if (!strcmp(s->name, "adopt")) {
      /* adopt r64,[address]:ptr<space,T> computes the address like lea and
       * gives the register that declared pointer type. It is the only way to
       * turn an integer address into a pointer, so it is a privileged act. */
      NtFId first = s->expression;
      NtFId second = first ? EX(p, first).next : 0;
      if (!first || !second || EX(p, second).next ||
          EX(p, first).kind != NTF_X_REGISTER || EX(p, first).reg.bits != 64 ||
          EX(p, first).reg.family < 0 || EX(p, first).reg.family > 15 ||
          EX(p, first).reg.family == 4 || EX(p, first).reg.family == 5 ||
          EX(p, second).kind != NTF_X_MEMORY || EX(p, second).memory.segment) {
        diag(p, NTF_E_MEMORY, s->span,
             "adopt requires a 64-bit GPR and a typed memory address");
        return 0;
      }
      if (!infer(p, second, 0, owner))
        return 0;
      f = &FN(p, owner);
      f->inferred_effects.flags |= NTF_F_PRIVILEGED;
      writes |= BIT((unsigned)EX(p, first).reg.family);
    } else if (!strcmp(s->name, "fn_address")) {
      /* fn_address r64,name loads the address of a local function, e.g.
       * an interrupt table or a callback given to foreign code. */
      NtFId first = s->expression;
      NtFId second = first ? EX(p, first).next : 0;
      if (!first || !second || EX(p, second).next ||
          EX(p, first).kind != NTF_X_REGISTER || EX(p, first).reg.bits != 64 ||
          EX(p, first).reg.family < 0 || EX(p, first).reg.family > 15 ||
          EX(p, first).reg.family == 4 || EX(p, first).reg.family == 5 ||
          EX(p, second).kind != NTF_X_NAME || !EX(p, second).name) {
        diag(p, NTF_E_TYPE, s->span,
             "fn_address requires a 64-bit GPR and a function name");
        return 0;
      }
      NtFId target = find_function(p, f->module, EX(p, second).name);
      if (!target || FN(p, target).imported) {
        diag(p, NTF_E_NAME, EX(p, second).span, "unknown local function '%s'",
             EX(p, second).name);
        return 0;
      }
      NtFType u64 = {NTF_T_U64, NTF_SPACE_NONE, 0, 0, 64};
      NtFId u64_type = intern_type(p, u64, s->span);
      NtFExpr *x = &EX(p, second);
      x->kind = NTF_X_LITERAL;
      x->value = target;
      x->type = u64_type;
      x->resolved = 0;
      x->constant = 1;
      f = &FN(p, owner);
      f->write_footprint |= BIT((unsigned)EX(p, first).reg.family);
      f->inferred_clobbers |= BIT((unsigned)EX(p, first).reg.family);
      return 1;
    } else if (!strcmp(s->name, "call_efi")) {
      /* call_efi target,arg5,... calls a UEFI function pointer; the first
       * four arguments are already in rcx,rdx,r8,r9 and the listed 64-bit
       * registers become stack arguments five onwards. */
      NtFId target = s->expression;
      unsigned count = 0;
      for (NtFId id = target; id; id = EX(p, id).next, count++)
        if (EX(p, id).kind != NTF_X_REGISTER || EX(p, id).reg.bits != 64 ||
            EX(p, id).reg.family < 0 || EX(p, id).reg.family > 15 ||
            EX(p, id).reg.family == 4 || EX(p, id).reg.family == 5) {
          diag(p, NTF_E_ABI, s->span,
               "call_efi operands must be 64-bit general registers");
          return 0;
        }
      if (!count || count > 9) {
        diag(p, NTF_E_ABI, s->span,
             "call_efi takes a target and at most eight stack arguments");
        return 0;
      }
      f = &FN(p, owner);
      f->inferred_effects.flags |= NTF_F_CHANGES_FLAGS;
      /* Volatile registers of the UEFI calling convention. */
      uint64_t volatile_set = BIT(0) | BIT(1) | BIT(2) | BIT(8) | BIT(9) |
                              BIT(10) | BIT(11) | BIT(17);
      f->write_footprint |= volatile_set;
      f->inferred_clobbers |= volatile_set;
      s->flags_written = NT_X64_FLAGS_ARITH;
      return 1;
    } else if (!strcmp(s->name, "load") || !strcmp(s->name, "store")) {
      NtFId first = s->expression;
      NtFId second = first ? EX(p, first).next : 0;
      if (!first || !second || EX(p, second).next) {
        diag(p, NTF_E_MEMORY, s->span, "%s requires exactly two operands",
             s->name);
        return 0;
      }
      int loading = !strcmp(s->name, "load");
      NtFId memory_id = loading ? second : first;
      NtFId value_id = loading ? first : second;
      NtFExpr *memory = &EX(p, memory_id);
      NtFExpr *value = &EX(p, value_id);
      if (memory->kind != NTF_X_MEMORY ||
          (loading && value->kind != NTF_X_REGISTER)) {
        diag(p, NTF_E_MEMORY, s->span, "invalid %s operand form", s->name);
        return 0;
      }
      if (!infer(p, (NtFId)(memory - p->expressions + 1), 0, owner))
        return 0;
      NtFType *pointer = &TY(p, memory->type);
      NtFType *element = &TY(p, pointer->element);
      if (value->kind == NTF_X_REGISTER) {
        if (element->bits != value->reg.bits) {
          diag(p, NTF_E_TYPE, s->span,
               "%s register width does not match pointed element", s->name);
          return 0;
        }
      } else if (!loading && !infer(p, value_id, pointer->element, owner))
        return 0;
      if (!strcmp(s->name, "store") && memory->resolved &&
          !strcmp(VA(p, memory->resolved).section, ".rdata")) {
        diag(p, NTF_E_MEMORY, s->span, "cannot store through read-only data");
        return 0;
      }
      f = &FN(p, owner);
      if (!strcmp(s->name, "load")) {
        f->inferred_effects.reads |= 1u << pointer->space;
        writes |= BIT((unsigned)value->reg.family);
      } else {
        f->inferred_effects.writes |= 1u << pointer->space;
      }
    } else if (!strcmp(s->name, "mov") && s->expression &&
               EX(p, s->expression).kind == NTF_X_REGISTER &&
               EX(p, s->expression).next &&
               EX(p, EX(p, s->expression).next).kind == NTF_X_NAME &&
               EX(p, EX(p, s->expression).next).resolved &&
               !EX(p, EX(p, s->expression).next).next) {
      NtFId source = EX(p, s->expression).next;
      NtFId source_type = infer(p, source, 0, owner);
      if (!source_type ||
          TY(p, source_type).bits != EX(p, s->expression).reg.bits) {
        diag(p, NTF_E_TYPE, s->span,
             "mov variable width does not match destination");
        return 0;
      }
      writes |= BIT((unsigned)EX(p, s->expression).reg.family);
    } else {
      NtX64Operand operands[3];
      size_t operand_count = 0;
      for (NtFId id = s->expression; id; id = EX(p, id).next) {
        if (operand_count == 3) {
          diag(p, NTF_E_ABI, s->span, "too many machine operands");
          return 0;
        }
        if (EX(p, id).kind == NTF_X_MEMORY && !infer(p, id, 0, owner))
          return 0;
        if (!frontend_x64_operand(p, id, s->name, &operands[operand_count])) {
          diag(p, NTF_E_TYPE, EX(p, id).span,
               "operand cannot lower to x86-64 form");
          return 0;
        }
        ++operand_count;
      }
      NtX64Effects facts;
      NtX64Context context = {p->modules[f->module - 1].features, 0};
      const char *machine_name =
          !strcmp(s->name, "call_indirect") ? "call" : s->name;
      NtX64Error error =
          nt_x64_effects(machine_name, operands, operand_count, context, &facts);
      if (error) {
        unsigned code = error == NT_X64_FEATURE     ? NTF_E_TARGET
                        : error == NT_X64_PRIVILEGE ? NTF_E_CONTRACT
                        : error == NT_X64_REGISTER  ? NTF_E_REGISTER
                        : error == NT_X64_HIGH8_REX ? NTF_E_REGISTER
                        : error == NT_X64_RANGE     ? NTF_E_LITERAL
                                                    : NTF_E_TYPE;
        diag(p, code, s->span, "%s: %s", s->name, nt_x64_error_string(error));
        return 0;
      }
      /* A returning indirect call balances its implicit stack write with the
       * callee RET, exactly like the typed call path. Other raw stack effects
       * still require the unavailable general stack proof. */
      if (strcmp(s->name, "call_indirect") &&
          (facts.stack_read || facts.stack_write ||
           (facts.registers_written & (BIT(4) | BIT(5))))) {
        diag(p, NTF_E_CONTRACT, s->span,
             "%s requires unavailable stack/frame balance proof", s->name);
        return 0;
      }
      if (facts.may_trap || facts.destination_requires_nonzero) {
        diag(p, NTF_E_CONTRACT, s->span,
             "%s requires an unavailable runtime safety proof", s->name);
        return 0;
      }
      f = &FN(p, owner);
      uint64_t effective_writes = facts.registers_written;
      if (!strcmp(s->name, "call_indirect"))
        effective_writes &= ~BIT(4); /* balanced CALL/RET stack motion */
      f->inferred_effects.flags |= facts.effects;
      f->write_footprint |= effective_writes;
      f->inferred_clobbers |= effective_writes;
      s->flags_read = facts.flags_read;
      s->flags_written = facts.flags_written;
      s->flags_undefined = facts.flags_undefined;
      for (NtFId id = s->expression; id; id = EX(p, id).next)
        if (EX(p, id).kind == NTF_X_MEMORY) {
          NtFType *pointer = &TY(p, EX(p, id).type);
          if (facts.memory_read)
            f->inferred_effects.reads |= 1u << pointer->space;
          if (facts.memory_write)
            f->inferred_effects.writes |= 1u << pointer->space;
        }
      return 1;
    }
    f = &FN(p, owner);
    if (flags & NTF_F_CHANGES_FLAGS)
      s->flags_written = NT_X64_FLAGS_ARITH;
    f->inferred_effects.flags |= flags;
    f->write_footprint |= writes;
    f->inferred_clobbers |= writes;
    if (strcmp(s->name, "load") && strcmp(s->name, "store"))
      for (NtFId x = s->expression; x; x = EX(p, x).next)
        if (!infer(p, x, 0, owner))
          return 0;
  }
  return 1;
}
static int verify_function_predicates(NtFProgram *p, NtFId function_id) {
  NtFFunction *function = &FN(p, function_id);
  for (int phase = 0; phase < 2; phase++) {
    NtFId list = phase ? function->ensures : function->requires;
    for (NtFId id = list; id; id = EX(p, id).next) {
      NtFExpr *predicate = &EX(p, id);
      int proved = 1;
      if (!strcmp(predicate->name, "non_null") ||
          !strcmp(predicate->name, "canonical")) {
        NtFId type = infer(p, predicate->first_arg, 0, function_id);
        if (!type || TY(p, type).kind != NTF_T_PTR) {
          diag(p, NTF_E_CONTRACT, predicate->span,
               "predicate '%s' requires a pointer", predicate->name);
          return 0;
        }
        if (phase) {
          NtFId subject = EX(p, predicate->first_arg).resolved;
          proved = subject && same_fact(p, function->requires, predicate->name,
                                        subject, 0);
        }
      } else if (!strcmp(predicate->name, "stack_aligned")) {
        uint64_t alignment;
        int negative;
        proved =
            constant_value(p, predicate->first_arg, &alignment, &negative) &&
            !negative && alignment && !(alignment & (alignment - 1)) &&
            alignment <= 4096;
        if (phase)
          proved = proved && alignment <= 16 && (16 % alignment) == 0;
      } else if (!strcmp(predicate->name, "feature")) {
        proved = !((uint32_t)predicate->value &
                   ~p->modules[function->module - 1].features);
      } else
        proved = 0;
      if (!proved) {
        diag(p, NTF_E_CONTRACT, predicate->span,
             phase ? "ensures predicate '%s' is not proved"
                   : "requires predicate '%s' is invalid",
             predicate->name);
        return 0;
      }
    }
  }
  return 1;
}
static int predicate_parameter_index(NtFProgram *p, const NtFFunction *function,
                                     NtFId expression, uint32_t *index) {
  if (!expression || expression > p->expression_count)
    return 0;
  NtFId variable = EX(p, expression).resolved;
  if (!variable || variable < function->first_param ||
      variable >= function->first_param + function->param_count)
    return 0;
  *index = (uint32_t)(variable - function->first_param);
  return 1;
}
static int predicate_contract_equal(NtFProgram *p,
                                    const NtFFunction *left_function,
                                    NtFId left_id,
                                    const NtFFunction *right_function,
                                    NtFId right_id) {
  if (!left_id || !right_id)
    return 0;
  NtFExpr *left = &EX(p, left_id);
  NtFExpr *right = &EX(p, right_id);
  if (strcmp(left->name, right->name) || left->value != right->value)
    return 0;
  if (!left->first_arg || !right->first_arg)
    return left->first_arg == right->first_arg;
  if (!strcmp(left->name, "non_null") || !strcmp(left->name, "canonical")) {
    uint32_t left_index, right_index;
    return predicate_parameter_index(p, left_function, left->first_arg,
                                     &left_index) &&
           predicate_parameter_index(p, right_function, right->first_arg,
                                     &right_index) &&
           left_index == right_index;
  }
  if (!strcmp(left->name, "stack_aligned")) {
    uint64_t left_value, right_value;
    int left_negative, right_negative;
    return constant_value(p, left->first_arg, &left_value, &left_negative) &&
           constant_value(p, right->first_arg, &right_value, &right_negative) &&
           left_value == right_value && left_negative == right_negative;
  }
  return 0;
}
static int verify_import_predicate_shapes(NtFProgram *p, NtFId function_id) {
  NtFFunction *function = &FN(p, function_id);
  NtFId lists[2] = {function->requires, function->ensures};
  for (unsigned phase = 0; phase < 2; phase++)
    for (NtFId id = lists[phase]; id; id = EX(p, id).next) {
      NtFExpr *predicate = &EX(p, id);
      if (!strcmp(predicate->name, "feature"))
        continue;
      if (!strcmp(predicate->name, "non_null") ||
          !strcmp(predicate->name, "canonical")) {
        NtFId type = infer(p, predicate->first_arg, 0, function_id);
        if (type && TY(p, type).kind == NTF_T_PTR)
          continue;
      } else if (!strcmp(predicate->name, "stack_aligned")) {
        uint64_t alignment;
        int negative;
        if (constant_value(p, predicate->first_arg, &alignment, &negative) &&
            !negative && alignment && !(alignment & (alignment - 1)) &&
            alignment <= 4096)
          continue;
      }
      diag(p, NTF_E_CONTRACT, predicate->span,
           "import predicate '%s' has invalid operands", predicate->name);
      return 0;
    }
  return 1;
}
static int verify_program(NtFProgram *p) {
  for (size_t i = 0; i < p->function_count; i++) {
    NtFFunction *import = &p->functions[i];
    if (!import->imported)
      continue;
    if (!verify_import_predicate_shapes(p, (NtFId)(i + 1)))
      return 0;
    const char *dot = strrchr(import->import_name, '.');
    NtFId target = 0;
    if (dot) {
      size_t module_length = (size_t)(dot - import->import_name);
      for (size_t j = 0; j < p->function_count; j++) {
        NtFFunction *candidate = &p->functions[j];
        NtFModule *module = &MO(p, candidate->module);
        if (!candidate->imported && candidate->exported &&
            strlen(module->name) == module_length &&
            !memcmp(module->name, import->import_name, module_length) &&
            !strcmp(candidate->name, dot + 1)) {
          target = (NtFId)(j + 1);
          break;
        }
      }
    }
    if (!target) {
      if (((Private *)p->_private)->allow_unresolved_imports)
        continue;
      diag(p, NTF_E_IMPORT, import->span, "unresolved import '%s'",
           import->import_name);
      return 0;
    }
    NtFFunction *actual = &FN(p, target);
    if (strcmp(MO(p, import->module).target, MO(p, actual->module).target)) {
      diag(p, NTF_E_IMPORT, import->span, "import target mismatch for '%s'",
           import->import_name);
      return 0;
    }
    int compatible = import->param_count == actual->param_count &&
                     type_equal(p, import->return_type, actual->return_type) &&
                     import->effects.flags == actual->effects.flags &&
                     import->effects.reads == actual->effects.reads &&
                     import->effects.writes == actual->effects.writes &&
                     import->clobbers == actual->clobbers &&
                     !strcmp(import->abi, actual->abi);
    NtFId import_predicate = import->requires;
    NtFId actual_predicate = actual->requires;
    while (compatible && (import_predicate || actual_predicate)) {
      compatible =
          import_predicate && actual_predicate &&
          predicate_contract_equal(p, import, import_predicate, actual,
                                   actual_predicate);
      if (import_predicate)
        import_predicate = EX(p, import_predicate).next;
      if (actual_predicate)
        actual_predicate = EX(p, actual_predicate).next;
    }
    import_predicate = import->ensures;
    actual_predicate = actual->ensures;
    while (compatible && (import_predicate || actual_predicate)) {
      compatible =
          import_predicate && actual_predicate &&
          predicate_contract_equal(p, import, import_predicate, actual,
                                   actual_predicate);
      if (import_predicate)
        import_predicate = EX(p, import_predicate).next;
      if (actual_predicate)
        actual_predicate = EX(p, actual_predicate).next;
    }
    for (uint32_t k = 0; compatible && k < import->param_count; k++) {
      NtFVariable *a = &VA(p, import->first_param + k);
      NtFVariable *b = &VA(p, actual->first_param + k);
      compatible = a->direction == b->direction &&
                   type_equal(p, a->type, b->type) &&
                   a->reg.family == b->reg.family && a->reg.bits == b->reg.bits;
    }
    if (!compatible) {
      diag(p, NTF_E_IMPORT, import->span, "import contract mismatch for '%s'",
           import->import_name);
      return 0;
    }
    import->resolved = target;
  }
  for (size_t i = 0; i < p->variable_count; i++) {
    NtFVariable *v = &p->variables[i];
    if (v->direction == NTF_CONST || v->direction == NTF_DATA) {
      NtFId owner = 0;
      for (size_t j = 0; j < p->function_count; j++)
        if (p->functions[j].module == v->module) {
          owner = (NtFId)(j + 1);
          break;
        }
      if (!owner) {
        diag(p, NTF_E_TYPE, v->span, "constant module has no function context");
        return 0;
      }
      if (!infer(p, v->initializer, v->type, owner))
        return 0;
      uint64_t value;
      int negative;
      if (EX(p, v->initializer).kind == NTF_X_STRING)
        continue;
      if (TY(p, v->type).kind == NTF_T_PTR) {
        NtFId target;
        int64_t addend;
        if (symbolic_address(p, v->initializer, &target, &addend, 0))
          continue;
        diag(p, NTF_E_LITERAL, v->span,
             "pointer initializer is not a symbolic address");
        return 0;
      }
      if (!constant_value(p, v->initializer, &value, &negative)) {
        diag(p, NTF_E_LITERAL, v->span, "constant is not evaluable");
        return 0;
      }
    }
  }
  for (size_t i = 0; i < p->function_count; i++) {
    NtFFunction *f = &p->functions[i];
    if (f->imported)
      continue;
    if (f->generated_stub == 3) {
      NtFId handler = find_function(p, f->module, f->import_name);
      const NtFFunction *h = handler ? &FN(p, handler) : NULL;
      if (!h || h->generated_stub || h->imported || h->param_count != 1 ||
          TY(p, h->return_type).kind != NTF_T_U64 ||
          TY(p, VA(p, h->first_param).type).kind != NTF_T_U64 ||
          VA(p, h->first_param).reg.family != 1) {
        diag(p, NTF_E_CONTRACT, f->span,
             "interrupt table handler must be fn(in frame:u64 @rcx) -> u64");
        return 0;
      }
      f->resolved = handler;
      continue;
    }
    if (f->generated_stub) {
      int ud2 = f->generated_stub == 1 && f->stub_vector == 6 &&
                !strcmp(f->abi, "nx64-interrupt-same-cpl-v0");
      int gp = f->generated_stub == 2 && f->stub_vector == 13 &&
               !strcmp(f->abi, "nx64-interrupt-same-cpl-error-v0");
      if ((!ud2 && !gp) || f->param_count ||
          TY(p, f->return_type).kind != NTF_T_NEVER) {
        diag(p, NTF_E_CONTRACT, f->span,
             "generated interrupt stub descriptor is invalid");
        return 0;
      }
      continue;
    }
    if (!resolve_function_labels(p, (NtFId)(i + 1)))
      return 0;
    if (!verify_function_predicates(p, (NtFId)(i + 1)))
      return 0;
    if (!verify_block(p, f->body, (NtFId)(i + 1)))
      return 0;
    NtFDiagnostic cfg = {0};
    if (!nt_conformance_verify_typed(p, (NtFId)(i + 1), &cfg)) {
      diag(p, cfg.code, cfg.span, "%s", cfg.message);
      return 0;
    }
    if ((f->inferred_effects.flags & ~f->effects.flags) ||
        (f->inferred_effects.reads & ~f->effects.reads) ||
        (f->inferred_effects.writes & ~f->effects.writes)) {
      diag(p, NTF_E_EFFECT, f->span, "function '%s' omits an inferred effect",
           f->name);
      return 0;
    }
    uint64_t output_registers =
        nt_conformance_output_registers(p, (NtFId)(i + 1));
    if (f->inferred_clobbers & ~(f->clobbers | output_registers)) {
      diag(p, NTF_E_CLOBBER, f->span, "function '%s' omits an inferred clobber",
           f->name);
      return 0;
    }
  }
  return p->diagnostic_count == 0;
}
static int frontend_compile_mode(const NtFInput *inputs, size_t count,
                                 NtFProgram *out, int object_mode) {
  if (!out)
    return 0;
  memset(out, 0, sizeof(*out));
  Private *v = (Private *)calloc(1, sizeof(*v));
  if (!v)
    return 0;
  v->fuel_limit = NTF_COMPTIME_FUEL_MAX;
  v->call_depth_limit = NTF_COMPTIME_CALL_DEPTH_MAX;
  v->arena_limit = NTF_COMPTIME_ARENA_BYTES_MAX;
  v->node_limit = NTF_COMPTIME_AST_NODES_MAX;
  v->expansion_limit = NTF_COMPTIME_EXPANSION_DEPTH_MAX;
  v->constant_limit = NTF_COMPTIME_CONSTANT_BYTES_MAX;
  v->allow_unresolved_imports = object_mode;
  out->_private = v;
  if (!inputs || !count) {
    NtFSpan span = {0};
    diag(out, NTF_E_LEX, span, "no input modules");
    return 0;
  }
  for (size_t i = 0; i < count; i++) {
    if (!inputs[i].text || inputs[i].length > 16u * 1024u * 1024u) {
      NtFSpan span = {(uint32_t)(i + 1), 1, 1, 0, 0};
      diag(out, NTF_E_LIMIT, span, "source missing or exceeds 16 MiB");
      continue;
    }
    Parser parser;
    memset(&parser, 0, sizeof(parser));
    parser.p = out;
    parser.in = &inputs[i];
    parser.source = (uint32_t)(i + 1);
    parser.line = 1;
    parser.column = 1;
    (void)parse_source(&parser);
  }
  if (!out->diagnostic_count && verify_program(out))
    out->verified = 1;
  return out->verified;
}
int nt_frontend_compile(const NtFInput *inputs, size_t count, NtFProgram *out) {
  return frontend_compile_mode(inputs, count, out, 0);
}
int nt_frontend_compile_object(const NtFInput *inputs, size_t count,
                               NtFProgram *out) {
  return frontend_compile_mode(inputs, count, out, 1);
}
void nt_frontend_free(NtFProgram *p) {
  if (!p)
    return;
  Private *v = (Private *)p->_private;
  free(p->modules);
  free(p->functions);
  free(p->variables);
  free(p->types);
  free(p->expressions);
  free(p->statements);
  free(p->diagnostics);
  if (v) {
    free(v->macros);
    Allocation *a = v->allocations;
    while (a) {
      Allocation *next = a->next;
      free(a);
      a = next;
    }
    free(v);
  }
  memset(p, 0, sizeof(*p));
}
