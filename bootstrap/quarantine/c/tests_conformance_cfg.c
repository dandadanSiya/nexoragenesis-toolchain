#include "conformance.h"
#include "x64.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, failures;
#define CHECK(x)                                                               \
  do {                                                                         \
    checks++;                                                                  \
    if (!(x)) {                                                                \
      failures++;                                                              \
      fprintf(stderr, "FAIL CFG line%d %s\n", __LINE__, #x);                   \
    }                                                                          \
  } while (0)
typedef struct {
  NtFProgram p;
  NtFFunction fn;
  NtFType types[2];
  NtFStmt s[16];
  NtFExpr x[8];
  NtFVariable v[4];
} Fixture;
static void init(Fixture *f) {
  memset(f, 0, sizeof(*f));
  f->p.functions = &f->fn;
  f->p.function_count = 1;
  f->p.types = f->types;
  f->p.type_count = 2;
  f->types[0].kind = NTF_T_U64;
  f->types[0].bits = 64;
  f->types[1].kind = NTF_T_NEVER;
  f->fn.return_type = 1;
  f->fn.body = 1;
  f->p.statements = f->s;
  f->p.statement_count = 16;
  f->p.expressions = f->x;
  f->p.expression_count = 8;
  f->p.variables = f->v;
  f->p.variable_count = 4;
  f->s[0].kind = NTF_S_BLOCK;
  f->s[0].first = 2;
  for (unsigned i = 0; i < 16; i++) {
    f->s[i].span.source = 1;
    f->s[i].span.line = i + 1;
    f->s[i].span.column = 1;
  }
}
static void run(Fixture *f, unsigned expected) {
  NtFDiagnostic d = {0};
  int ok = nt_conformance_verify(&f->p, 1, &d);
  CHECK(expected ? (!ok && d.code == expected) : (ok && d.code == 0));
  if (expected && !ok)
    CHECK(d.span.source == 1);
}
int main(void) {
  Fixture f;
  init(&f);
  f.s[1].kind = NTF_S_RETURN;
  run(&f, 0);
  init(&f);
  f.s[1].kind = NTF_S_LABEL;
  run(&f, NTF_E_FLOW);
  init(&f);
  f.fn.return_type = 2;
  f.s[1].kind = NTF_S_LABEL;
  f.s[1].next = 3;
  f.s[2].kind = NTF_S_INSTRUCTION;
  f.s[2].name = "jmp";
  f.s[2].variable = 2;
  run(&f, 0);
  /* A real backedge retains flags established before the loop. */
  init(&f);
  f.fn.return_type = 2;
  f.s[1].kind = NTF_S_INSTRUCTION;
  f.s[1].name = "cmp";
  f.s[1].flags_written = NT_X64_FLAGS_ARITH;
  f.s[1].next = 3;
  f.s[2].kind = NTF_S_LABEL;
  f.s[2].next = 4;
  f.s[3].kind = NTF_S_INSTRUCTION;
  f.s[3].name = "je";
  f.s[3].flags_read = NT_X64_FLAG_ZF;
  f.s[3].variable = 3;
  f.s[3].next = 5;
  f.s[4].kind = NTF_S_INSTRUCTION;
  f.s[4].name = "jmp";
  f.s[4].variable = 3;
  run(&f, 0);
  /* Bypassed CMP must not establish a fact. */
  init(&f);
  f.s[1].kind = NTF_S_INSTRUCTION;
  f.s[1].name = "jmp";
  f.s[1].variable = 4;
  f.s[1].next = 3;
  f.s[2].kind = NTF_S_INSTRUCTION;
  f.s[2].name = "cmp";
  f.s[2].flags_written = NT_X64_FLAGS_ARITH;
  f.s[2].next = 4;
  f.s[3].kind = NTF_S_LABEL;
  f.s[3].next = 5;
  f.s[4].kind = NTF_S_INSTRUCTION;
  f.s[4].name = "je";
  f.s[4].flags_read = NT_X64_FLAG_ZF;
  f.s[4].variable = 6;
  f.s[4].next = 6;
  f.s[5].kind = NTF_S_RETURN;
  run(&f, NTF_E_CONTRACT);
  /* Both IF arms define ZF, so the merge may read it. */
  init(&f);
  f.s[1].kind = NTF_S_IF;
  f.s[1].then_branch = 3;
  f.s[1].else_branch = 5;
  f.s[1].next = 7;
  f.s[2].kind = NTF_S_BLOCK;
  f.s[2].first = 4;
  f.s[3].kind = NTF_S_INSTRUCTION;
  f.s[3].name = "cmp";
  f.s[3].flags_written = NT_X64_FLAG_ZF;
  f.s[4].kind = NTF_S_BLOCK;
  f.s[4].first = 6;
  f.s[5].kind = NTF_S_INSTRUCTION;
  f.s[5].name = "cmp";
  f.s[5].flags_written = NT_X64_FLAG_ZF;
  f.s[6].kind = NTF_S_INSTRUCTION;
  f.s[6].name = "je";
  f.s[6].flags_read = NT_X64_FLAG_ZF;
  f.s[6].variable = 8;
  f.s[6].next = 8;
  f.s[7].kind = NTF_S_RETURN;
  run(&f, 0);
  f.s[5].flags_written = 0;
  run(&f, NTF_E_CONTRACT);
  /* A local declaration skipped by jump stays uninitialized. */
  init(&f);
  f.v[0].direction = NTF_LOCAL;
  f.v[0].function = 1;
  f.v[0].name = "value";
  f.s[1].kind = NTF_S_INSTRUCTION;
  f.s[1].name = "jmp";
  f.s[1].variable = 4;
  f.s[1].next = 3;
  f.s[2].kind = NTF_S_LET;
  f.s[2].variable = 1;
  f.s[2].next = 4;
  f.s[3].kind = NTF_S_LABEL;
  f.s[3].next = 5;
  f.s[4].kind = NTF_S_RETURN;
  f.s[4].expression = 1;
  f.x[0].kind = NTF_X_NAME;
  f.x[0].resolved = 1;
  f.x[0].span = f.s[4].span;
  run(&f, NTF_E_FLOW);
  f.s[1].name = "nop";
  f.s[1].variable = 0;
  run(&f, 0);
  /* Malformed cross-function/out-of-array target is rejected, never followed.
   */
  f.s[1].name = "jmp";
  f.s[1].variable = 99;
  run(&f, NTF_E_FLOW);
  /* Both branches initialize a mutable IR local before the merged read. */
  init(&f);
  f.v[0].direction = NTF_LOCAL;
  f.v[0].function = 1;
  f.s[1].kind = NTF_S_IF;
  f.s[1].then_branch = 3;
  f.s[1].else_branch = 5;
  f.s[1].next = 7;
  f.s[2].kind = NTF_S_BLOCK;
  f.s[2].first = 4;
  f.s[3].kind = NTF_S_LET;
  f.s[3].variable = 1;
  f.s[4].kind = NTF_S_BLOCK;
  f.s[4].first = 6;
  f.s[5].kind = NTF_S_LET;
  f.s[5].variable = 1;
  f.s[6].kind = NTF_S_RETURN;
  f.s[6].expression = 1;
  f.x[0].kind = NTF_X_NAME;
  f.x[0].resolved = 1;
  f.x[0].span = f.s[6].span;
  run(&f, 0);
  f.s[5].kind = NTF_S_LABEL;
  run(&f, NTF_E_FLOW);
  /* Real callee flag clobbers kill the preceding comparison; pure calls do not.
   */
  init(&f);
  NtFFunction functions[2] = {f.fn, {0}};
  f.p.functions = functions;
  f.p.function_count = 2;
  functions[1].clobbers = UINT64_C(1) << 17;
  f.s[1].kind = NTF_S_INSTRUCTION;
  f.s[1].name = "cmp";
  f.s[1].flags_written = NT_X64_FLAG_ZF;
  f.s[1].next = 3;
  f.s[2].kind = NTF_S_CALL;
  f.s[2].expression = 1;
  f.s[2].next = 4;
  f.x[0].kind = NTF_X_CALL;
  f.x[0].resolved = 2;
  f.s[3].kind = NTF_S_INSTRUCTION;
  f.s[3].name = "je";
  f.s[3].flags_read = NT_X64_FLAG_ZF;
  f.s[3].variable = 5;
  f.s[3].next = 5;
  f.s[4].kind = NTF_S_RETURN;
  run(&f, NTF_E_CONTRACT);
  functions[1].clobbers = 0;
  run(&f, 0);
  /* Long lexical chains do not recurse once per statement. */
  init(&f);
  size_t count = 10000;
  NtFStmt *chain = calloc(count, sizeof(*chain));
  CHECK(chain != NULL);
  if (chain) {
    f.p.statements = chain;
    f.p.statement_count = count;
    chain[0].kind = NTF_S_BLOCK;
    chain[0].first = 2;
    for (size_t i = 1; i < count - 1; i++) {
      chain[i].kind = NTF_S_LABEL;
      chain[i].next = (NtFId)(i + 2);
    }
    chain[count - 1].kind = NTF_S_RETURN;
    run(&f, 0);
    free(chain);
  }
  /* A literal condition has one feasible successor, with no fabricated
   * fallthrough. */
  init(&f);
  f.s[1].kind = NTF_S_IF;
  f.s[1].condition = 1;
  f.s[1].then_branch = 3;
  f.x[0].kind = NTF_X_BOOL;
  f.x[0].value = 1;
  f.s[2].kind = NTF_S_BLOCK;
  f.s[2].first = 4;
  f.s[3].kind = NTF_S_RETURN;
  run(&f, 0);
  init(&f);
  f.fn.first_param = 1;
  f.fn.param_count = 1;
  f.v[0].function = 1;
  f.v[0].type = 1;
  f.v[0].direction = NTF_OUT;
  f.v[0].reg = (NtFRegister){1, 64, 0};
  f.s[1].kind = NTF_S_RETURN;
  run(&f, NTF_E_FLOW);
  f.s[1].kind = NTF_S_INSTRUCTION;
  f.s[1].name = "mov";
  f.s[1].expression = 1;
  f.s[1].next = 3;
  f.x[0].kind = NTF_X_REGISTER;
  f.x[0].reg = (NtFRegister){1, 64, 0};
  f.x[0].next = 2;
  f.x[1].kind = NTF_X_LITERAL;
  f.x[1].value = 42;
  f.s[2].kind = NTF_S_RETURN;
  run(&f, 0);
  f.x[0].reg.bits = 8;
  run(&f, NTF_E_FLOW);
  f.x[0].reg.bits = 64;
  f.s[1].name = "add";
  run(&f, NTF_E_FLOW);
  f.s[1].name = "xor";
  f.x[1].kind = NTF_X_REGISTER;
  f.x[1].reg = (NtFRegister){1, 64, 0};
  run(&f, 0);
  f.v[0].direction = NTF_INOUT;
  f.s[1].name = "add";
  f.x[1].kind = NTF_X_LITERAL;
  f.x[1].value = 1;
  run(&f, 0);
  printf("CFG_CONFORMANCE_TESTS checks=%u failures=%u\n", checks, failures);
  return failures ? 1 : 0;
}
