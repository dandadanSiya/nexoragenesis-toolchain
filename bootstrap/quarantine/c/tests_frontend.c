#include "frontend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned passed, failed;
static void check(const char *name, const char *source, unsigned error) {
  NtFInput in = {name, source, strlen(source)};
  NtFProgram p;
  int ok = nt_frontend_compile(&in, 1, &p);
  int found = 0;
  int unresolved_has_no_body = 1;
  for (size_t i = 0; i < p.diagnostic_count; i++)
    if (p.diagnostics[i].code == error)
      found = 1;
  for (size_t i = 0; i < p.function_count; i++)
    if (p.functions[i].imported && !p.functions[i].resolved &&
        p.functions[i].body)
      unresolved_has_no_body = 0;
  if (error ? (!ok && !p.verified && found)
            : (ok && p.verified && !p.diagnostic_count &&
               unresolved_has_no_body))
    passed++;
  else {
    failed++;
    fprintf(stderr, "FAIL %s expected %u got ok=%d\n", name, error, ok);
    for (size_t i = 0; i < p.diagnostic_count; i++)
      fprintf(stderr, " %u %u:%u %s\n", p.diagnostics[i].code,
              p.diagnostics[i].span.line, p.diagnostics[i].span.column,
              p.diagnostics[i].message);
  }
  nt_frontend_free(&p);
}
static void check_many(const char *name, const NtFInput *inputs, size_t count,
                       unsigned error) {
  NtFProgram p;
  int ok = nt_frontend_compile(inputs, count, &p);
  int found = 0;
  for (size_t i = 0; i < p.diagnostic_count; i++)
    if (p.diagnostics[i].code == error)
      found = 1;
  if (error ? (!ok && !p.verified && found)
            : (ok && p.verified && !p.diagnostic_count))
    passed++;
  else {
    failed++;
    fprintf(stderr, "FAIL %s expected %u got ok=%d\n", name, error, ok);
    for (size_t i = 0; i < p.diagnostic_count; i++)
      fprintf(stderr, " %u %u:%u %s\n", p.diagnostics[i].code,
              p.diagnostics[i].span.line, p.diagnostics[i].span.column,
              p.diagnostics[i].message);
  }
  nt_frontend_free(&p);
}
static void check_many_object(const char *name, const NtFInput *inputs,
                              size_t count, unsigned error) {
  NtFProgram p;
  int ok = nt_frontend_compile_object(inputs, count, &p);
  int found = 0;
  int unresolved_has_no_body = 1;
  for (size_t i = 0; i < p.diagnostic_count; i++)
    if (p.diagnostics[i].code == error)
      found = 1;
  for (size_t i = 0; i < p.function_count; i++)
    if (p.functions[i].imported && !p.functions[i].resolved &&
        p.functions[i].body)
      unresolved_has_no_body = 0;
  if (error ? (!ok && !p.verified && found)
            : (ok && p.verified && !p.diagnostic_count &&
               unresolved_has_no_body))
    passed++;
  else {
    failed++;
    fprintf(stderr, "FAIL %s object expected %u got ok=%d\n", name, error,
            ok);
  }
  nt_frontend_free(&p);
}
static void check_macro_provenance(const char *source) {
  NtFInput input = {"macro-provenance", source, strlen(source)};
  NtFProgram p;
  int ok = nt_frontend_compile(&input, 1, &p);
  int statement_provenance = 0, expression_provenance = 0;
  if (ok)
    for (size_t i = 0; i < p.statement_count; i++)
      if (p.statements[i].origin.source && p.statements[i].invocation.source)
        statement_provenance = 1;
  if (ok)
    for (size_t i = 0; i < p.expression_count; i++)
      if (p.expressions[i].origin.source && p.expressions[i].invocation.source)
        expression_provenance = 1;
  if (ok && statement_provenance && expression_provenance)
    passed++;
  else {
    failed++;
    fprintf(stderr, "FAIL macro_provenance ok=%d statement=%d expression=%d\n",
            ok, statement_provenance, expression_provenance);
  }
  nt_frontend_free(&p);
}
static void check_decl_emit_limit(size_t rows, unsigned error) {
  const char *prefix = "module decl_limit\ntarget x86_64-nexora-none\n"
                       "macro bulk()->list<ast.decl> {\n";
  const char *row = "emit quote(decl) { const item:u64=1; };\n";
  const char *suffix = "}\nsection .rdata {\nexpand bulk()\n}\n"
                       "section .text {\nfn main()->u64\n"
                       "effects {}\nclobbers {} {\nreturn 0\n}\n}\n";
  size_t length = strlen(prefix) + rows * strlen(row) + strlen(suffix);
  char *source = (char *)malloc(length + 1);
  if (!source) {
    failed++;
    return;
  }
  size_t at = 0;
  memcpy(source + at, prefix, strlen(prefix));
  at += strlen(prefix);
  for (size_t i = 0; i < rows; i++) {
    memcpy(source + at, row, strlen(row));
    at += strlen(row);
  }
  memcpy(source + at, suffix, strlen(suffix) + 1);
  check(rows == 512 ? "decl_emit_exact_512" : "decl_emit_plus_one_513", source,
        error);
  free(source);
}
static void check_meta_let_limit(size_t bindings, unsigned error) {
  const char *prefix =
      "module meta_let_limit\ntarget x86_64-nexora-none\n"
      "macro computed(v:ast.expr)->ast.stmt {\n";
  const char *suffix =
      "return quote(stmt) { return $m31; };\n}\n"
      "section .text {\nexport fn main()->u64\neffects {}\nclobbers {} {\n"
      "expand computed(10)\n}\n}\n";
  size_t capacity = strlen(prefix) + strlen(suffix) + bindings * 40 + 1;
  char *source = (char *)malloc(capacity);
  if (!source) {
    failed++;
    return;
  }
  size_t used = (size_t)snprintf(source, capacity, "%s", prefix);
  for (size_t i = 0; i < bindings; i++) {
    if (i)
      used += (size_t)snprintf(source + used, capacity - used,
                              "let m%zu=$m%zu+1;\n", i, i - 1);
    else
      used += (size_t)snprintf(source + used, capacity - used,
                              "let m%zu=$v+1;\n", i);
  }
  (void)snprintf(source + used, capacity - used, "%s", suffix);
  check(bindings == 32 ? "meta_let_exact_32" : "meta_let_plus_one_33",
        source, error);
  free(source);
}
static void check_multi_source_recovery(void) {
  const char *a = "module a\ntarget bad-one\n";
  const char *b = "module b\ntarget bad-two\n";
  NtFInput inputs[] = {{"a", a, strlen(a)}, {"b", b, strlen(b)}};
  NtFProgram p;
  int ok = nt_frontend_compile(inputs, 2, &p);
  int ordered = !ok && p.diagnostic_count == 2 &&
                p.diagnostics[0].span.source == 1 &&
                p.diagnostics[1].span.source == 2 &&
                p.diagnostics[0].code == NTF_E_TARGET &&
                p.diagnostics[1].code == NTF_E_TARGET;
  if (ordered)
    passed++;
  else {
    failed++;
    fprintf(stderr, "FAIL multi_source_recovery ok=%d diagnostics=%zu\n", ok,
            p.diagnostic_count);
  }
  nt_frontend_free(&p);
}
static void check_intra_source_recovery(const char *name, const char *source,
                                        size_t minimum) {
  NtFInput input = {name, source, strlen(source)};
  NtFProgram p;
  int ok = nt_frontend_compile(&input, 1, &p);
  size_t syntax_count = 0;
  size_t previous = SIZE_MAX;
  int distinct = 1;
  for (size_t i = 0; i < p.diagnostic_count; i++)
    if (p.diagnostics[i].code == NTF_E_SYNTAX) {
      syntax_count++;
      if (previous == p.diagnostics[i].span.start)
        distinct = 0;
      previous = p.diagnostics[i].span.start;
    }
  if (!ok && !p.verified && syntax_count >= minimum && distinct)
    passed++;
  else {
    failed++;
    fprintf(stderr, "FAIL %s ok=%d syntax=%zu total=%zu\n", name, ok,
            syntax_count, p.diagnostic_count);
  }
  nt_frontend_free(&p);
}
#define HEAD "module test\ntarget x86_64-nexora-none\nsection .text {\n"
#define FN "fn main() -> u64\neffects {}\nclobbers {} {\n"
#define END "}\n}\n"
int main(void) {
  check("return_literal", HEAD FN "return 42\n" END, 0);
  check("unknown_target", "module a\ntarget x86_64-fake-none\n", NTF_E_TARGET);
  check("known_features",
        "module features\ntarget x86_64-nexora-none\n"
        "features { tsc, sse2 }\nsection .text {\n" FN "return 0\n" END,
        0);
  check("unknown_feature",
        "module features\ntarget x86_64-nexora-none\n"
        "features { avx512 }\n",
        NTF_E_TARGET);
  check("encoder_catalogue_safe_forms",
        HEAD "fn main()->u64\n"
             "effects {}\nclobbers {rax,rcx} {\n"
             "mov cl,1\nmovzx rax,cl\nbswap rax\nreturn rax\n" END,
        0);
  check("encoder_width_mismatch",
        HEAD "fn main()->u64\n"
             "effects {changes_flags}\nclobbers {rax,rflags} {\n"
             "add eax,rcx\nreturn rax\n" END,
        NTF_E_TYPE);
  check("encoder_arity_mismatch",
        HEAD "fn main()->u64\n"
             "effects {changes_flags}\nclobbers {rax,rflags} {\n"
             "add rax\nreturn rax\n" END,
        NTF_E_TYPE);
  check("encoder_high8_rex",
        HEAD "fn main()->u64\n"
             "effects {}\nclobbers {rax} {\n"
             "mov ah,r8b\nreturn rax\n" END,
        NTF_E_REGISTER);
  check("mov_variable_value",
        HEAD "fn main()->u64\n"
             "effects {}\nclobbers {rax} {\n"
             "let value:u64=42\nmov rax,value\nreturn rax\n" END,
        0);
  check("encoder_feature_missing",
        HEAD "fn main()->u64\n"
             "effects {privileged,reads_clock}\nclobbers {rax,rdx} {\n"
             "rdtsc\nreturn rax\n" END,
        NTF_E_TARGET);
  check("encoder_feature_present",
        "module tsc\ntarget x86_64-nexora-none\nfeatures { tsc }\n"
        "section .text {\nfn main()->u64\n"
        "effects {privileged,reads_clock}\nclobbers {rax,rdx} {\n"
        "rdtsc\nreturn rax\n}\n}\n",
        0);
  check("raw_stack_write_fail_closed",
        HEAD "fn main()->u64\n"
             "effects {writes_mem(user)}\nclobbers {rsp} {\n"
             "push rax\nreturn 0\n" END,
        NTF_E_CONTRACT);
  check("raw_frame_register_fail_closed",
        HEAD "fn main()->u64\n"
             "effects {}\nclobbers {rbp} {\n"
             "mov rbp,rax\nreturn 0\n" END,
        NTF_E_CONTRACT);
  check("missing_return", HEAD FN "let x:u64 = 2\n" END, NTF_E_FLOW);
  check("return_range",
        HEAD "fn f()->u8\neffects {}\nclobbers {} {\nreturn 256\n" END,
        NTF_E_LITERAL);
  check("signed_min",
        HEAD "fn f()->i64\neffects {}\nclobbers {} {\nreturn "
             "-9223372036854775808\n" END,
        0);
  check("unsigned_negative", HEAD FN "return -1\n" END, NTF_E_LITERAL);
  check("local_scope", HEAD FN "let x:u64=4\nreturn x\n" END, 0);
  check("local_duplicate", HEAD FN "let x:u64=4\nlet x:u64=5\nreturn x\n" END,
        NTF_E_DUPLICATE);
  check("forward_call",
        HEAD FN "return helper(4)\n}\nfn helper(in x:u64 @rcx)->u64\neffects "
                "{}\nclobbers {} {\nreturn x\n}\n}\n",
        0);
  check("call_arity",
        HEAD FN "return helper()\n}\nfn helper(in x:u64 @rcx)->u64\neffects "
                "{}\nclobbers {} {\nreturn x\n}\n}\n",
        NTF_E_ABI);
  check("param_alias",
        HEAD "fn f(in x:u64 @rax,in y:u32 @eax)->u64\neffects {}\nclobbers {} "
             "{\nreturn x\n" END,
        NTF_E_REGISTER);
  check("param_width",
        HEAD
        "fn f(in x:u64 @eax)->u64\neffects {}\nclobbers {} {\nreturn x\n" END,
        NTF_E_REGISTER);
  check("param_stack_register_rejected",
        HEAD "fn f(in x:u64 @rsp)->u64\n"
             "effects {}\nclobbers {} {\nreturn x\n" END,
        NTF_E_ABI);
  check("fifth_param_requires_register",
        HEAD "fn f(in a:u64 @rcx,in b:u64 @rdx,in c:u64 @r8,"
             "in d:u64 @r9,in e:u64)->u64\n"
             "effects {}\nclobbers {} {\nreturn a\n" END,
        NTF_E_ABI);
  check("efi_fifth_param_stack",
        HEAD "fn f(in a:u64,in b:u64,in c:u64,in d:u64,in e:u64)->u64\n"
             "abi efi-x64-v0\n"
             "effects {}\nclobbers {} {\nreturn e\n" END,
        0);
  check("efi_wrong_fixed_register",
        HEAD "fn f(in a:u64 @rax)->u64\nabi efi-x64-v0\n"
             "effects {}\nclobbers {} {\nreturn a\n" END,
        NTF_E_ABI);
  check("efi_stack_explicit_register",
        HEAD "fn f(in a:u64,in b:u64,in c:u64,in d:u64,"
             "in e:u64 @r10)->u64\nabi efi-x64-v0\n"
             "effects {}\nclobbers {} {\nreturn e\n" END,
        NTF_E_ABI);
  check("efi_high8_parameter_rejected",
        HEAD "fn f(in a:u8 @ch)->u8\nabi efi-x64-v0\n"
             "effects {}\nclobbers {} {\nreturn a\n" END,
        NTF_E_ABI);
  check("efi_nonvolatile_clobber_rejected",
        HEAD "fn f()->u64\nabi efi-x64-v0\n"
             "effects {}\nclobbers {rbx} {\nreturn 0\n" END,
        NTF_E_ABI);
  check("efi_array_parameter_rejected",
        HEAD "fn f(in values:array<u64,2>)->u64\nabi efi-x64-v0\n"
             "effects {}\nclobbers {} {\nreturn 0\n" END,
        NTF_E_ABI);
  check("efi_array_return_rejected",
        HEAD "fn f()->array<u64,2>\nabi efi-x64-v0\n"
             "effects {}\nclobbers {} {\nreturn 0\n" END,
        NTF_E_ABI);
  check("out_parameter_omits_call_argument",
        HEAD "fn caller()->u64\neffects {}\nclobbers {rcx} {\n"
             "return set()\n}\n"
             "fn set(out x:u64 @rcx)->u64\neffects {}\nclobbers {} {\n"
             "mov rcx,42\nreturn 0\n" END,
        0);
  check("out_parameter_rejects_call_argument",
        HEAD "fn caller()->u64\neffects {}\nclobbers {rcx} {\n"
             "return set(1)\n}\n"
             "fn set(out x:u64 @rcx)->u64\neffects {}\nclobbers {} {\n"
             "mov rcx,42\nreturn 0\n" END,
        NTF_E_ABI);
  check("out_parameter_requires_explicit_register",
        HEAD "fn f(out x:u64)->u64\neffects {}\nclobbers {} {\n"
             "mov rcx,42\nreturn 0\n" END,
        NTF_E_ABI);
  check("out_parameter_is_not_a_clobber",
        HEAD "fn f(out x:u64 @rcx)->u64\neffects {}\nclobbers {rcx} {\n"
             "mov rcx,42\nreturn 0\n" END,
        NTF_E_ABI);
  check("out_parameter_read_before_write",
        HEAD "fn f(out x:u64 @rcx)->u64\neffects {}\nclobbers {} {\n"
             "return x\n" END,
        NTF_E_FLOW);
  check("out_parameter_partial_write",
        HEAD "fn f(out x:u64 @rcx)->u64\neffects {}\nclobbers {} {\n"
             "mov cx,42\nreturn 0\n" END,
        NTF_E_FLOW);
  check("inout_parameter_consumes_argument",
        HEAD "fn caller()->u64\neffects {changes_flags}\n"
             "clobbers {rcx,rflags} {\nreturn inc(41)\n}\n"
             "fn inc(inout x:u64 @rcx)->u64\n"
             "effects {changes_flags}\nclobbers {rflags} {\n"
             "add rcx,1\nreturn 0\n" END,
        0);
  check("out_pointer_provenance",
        "module out_pointer\ntarget x86_64-nexora-none\n"
        "section .data {\ndata answer:u64=42\n}\nsection .text {\n"
        "fn main()->u64\neffects {reads_mem(user)}\nclobbers {rcx,rax} {\n"
        "call make()\nload rax,[rcx]:ptr<user,u64>\nreturn rax\n}\n"
        "fn make(out p:ptr<user,u64> @rcx)->u64\neffects {}\nclobbers {} {\n"
        "lea rcx,[answer]:ptr<user,u64>\nreturn 0\n}\n}\n",
        0);
  check("out_pointer_copied_from_typed_parameter",
        HEAD "fn copy(in source:ptr<user,u64> @rdx,"
             "out target:ptr<user,u64> @rcx)->u64\n"
             "effects {}\nclobbers {} {\nmov rcx,rdx\nreturn 0\n" END,
        0);
  check("out_pointer_rejects_integer_write",
        HEAD "fn make(out p:ptr<user,u64> @rcx)->u64\n"
             "effects {}\nclobbers {} {\nmov rcx,0\nreturn 0\n" END,
        NTF_E_TYPE);
  check("out_pointer_rejects_32bit_alias_write",
        HEAD "fn make(out p:ptr<user,u64> @rcx)->u64\n"
             "effects {}\nclobbers {} {\nmov ecx,1\nreturn 0\n" END,
        NTF_E_TYPE);
  check("inout_pointer_invalidation_rejected",
        HEAD "fn use(inout p:ptr<user,u64> @rcx)->u64\n"
             "effects {}\nclobbers {} {\nmov rcx,0\nreturn 0\n" END,
        NTF_E_TYPE);
  check("out_bool_fail_closed",
        HEAD "fn f(out value:bool @cl)->u64\n"
             "effects {}\nclobbers {} {\nmov cl,1\nreturn 0\n" END,
        NTF_E_UNSUPPORTED);
  check("pointer_return",
        HEAD "fn f(in p:ptr<kernel,u64> @rcx)->ptr<user,u64>\neffects "
             "{}\nclobbers {} {\nreturn p\n" END,
        NTF_E_TYPE);
  check("nested_pointer_adjacent_closers",
        HEAD "fn f(in p:ptr<user,ptr<user,u64>> @rcx)->u64\n"
             "effects {}\nclobbers {} {\nreturn 0\n" END,
        0);
  check("unknown_name", HEAD FN "return missing\n" END, NTF_E_NAME);
  check("constant_math",
        "module test\ntarget x86_64-nexora-none\nconst K:u64=(5+3)*4\nsection "
        ".text {\n" FN "return K\n" END,
        0);
  check("divide_zero", HEAD FN "return 42/0\n" END, NTF_E_LITERAL);
  check("unknown_contract",
        HEAD "fn f()->u64\neffects {}\nclobbers {}\nrequires { magic() } "
             "{\nreturn 0\n" END,
        NTF_E_CONTRACT);
  check("missing_effect",
        HEAD "fn f(in x:u64 @rcx)->u64\neffects {}\nclobbers {rcx,rflags} "
             "{\nadd rcx,1\nreturn rcx\n" END,
        NTF_E_EFFECT);
  check("missing_clobber",
        HEAD "fn f(in x:u64 @rcx)->u64\neffects {changes_flags}\nclobbers "
             "{rflags} {\nadd rcx,1\nreturn rcx\n" END,
        NTF_E_CLOBBER);
  check("typed_add",
        HEAD "fn f(in x:u64 @rcx)->u64\neffects {changes_flags}\nclobbers "
             "{rcx,rflags} {\nadd rcx,1\nreturn rcx\n" END,
        0);
  check("unimplemented_macro",
        "module a\ntarget x86_64-nexora-none\nmacro a() -> u64 {}\n",
        NTF_E_UNSUPPORTED);
  check("bool_register_parameter",
        HEAD "fn f(in enabled:bool @cl)->bool\n"
             "effects {}\nclobbers {} {\nreturn enabled\n" END,
        0);
  const char *app =
      "module app\ntarget x86_64-nexora-none\nimport math.add as add: fn(in "
      "a:u64 @rcx,in b:u64 @rdx)->u64\neffects {}\nclobbers {}\nsection .text "
      "{\n" FN "return add(20,22)\n" END;
  const char *math =
      "module math\ntarget x86_64-nexora-none\nsection .text {\nexport fn "
      "add(in a:u64 @rcx,in b:u64 @rdx)->u64\neffects {}\nclobbers {} "
      "{\nreturn a+b\n}\n}\n";
  NtFInput imports[] = {{"app", app, strlen(app)},
                        {"math", math, strlen(math)}};
  check_many("cross_module_import", imports, 2, 0);
  const char *missing =
      "module app\ntarget x86_64-nexora-none\nimport missing.add as add: "
      "fn()->u64\neffects {}\nclobbers {}\n";
  NtFInput bad_import[] = {{"bad", missing, strlen(missing)}};
  check_many("unresolved_import", bad_import, 1, NTF_E_IMPORT);
  const char *external_ok =
      "module app\ntarget x86_64-nexora-none\n"
      "import runtime.add as add: fn(in a:u64 @rcx,in b:u64 @rdx)->u64\n"
      "effects {}\nclobbers {}\nsection .text {\n" FN
      "return add(20,22)\n" END;
  NtFInput external_object[] = {
      {"external", external_ok, strlen(external_ok)}};
  check_many_object("object_unresolved_typed_import", external_object, 1, 0);
  const char *external_bad_call =
      "module app\ntarget x86_64-nexora-none\n"
      "import runtime.take as take: fn(in p:ptr<user,u64> @rcx)->u64\n"
      "effects {}\nclobbers {}\nsection .text {\n" FN
      "return take(1)\n" END;
  NtFInput external_bad_object[] = {
      {"external-bad", external_bad_call, strlen(external_bad_call)}};
  check_many_object("object_import_call_type_checked", external_bad_object, 1,
                    NTF_E_TYPE);
  const char *stack_import =
      "module app\ntarget x86_64-nexora-none\n"
      "import math.f as f: fn()->u64\neffects {}\nclobbers {}\n"
      "requires { stack_aligned(16) }\n";
  const char *stack_export =
      "module math\ntarget x86_64-nexora-none\nsection .text {\n"
      "export fn f()->u64\neffects {}\nclobbers {}\n"
      "requires { stack_aligned(32) } {\nreturn 0\n}\n}\n";
  NtFInput stack_contract_mismatch[] = {
      {"app", stack_import, strlen(stack_import)},
      {"math", stack_export, strlen(stack_export)}};
  check_many("import_stack_predicate_value_mismatch", stack_contract_mismatch,
             2, NTF_E_IMPORT);
  const char *subject_import =
      "module app\ntarget x86_64-nexora-none\n"
      "import math.f as f: fn(in p:ptr<user,u64> @rcx,"
      "in q:ptr<user,u64> @rdx)->u64\neffects {}\nclobbers {}\n"
      "requires { non_null(p) }\n";
  const char *subject_export =
      "module math\ntarget x86_64-nexora-none\nsection .text {\n"
      "export fn f(in p:ptr<user,u64> @rcx,"
      "in q:ptr<user,u64> @rdx)->u64\neffects {}\nclobbers {}\n"
      "requires { non_null(q) } {\nreturn 0\n}\n}\n";
  NtFInput subject_contract_mismatch[] = {
      {"app", subject_import, strlen(subject_import)},
      {"math", subject_export, strlen(subject_export)}};
  check_many("import_predicate_subject_mismatch", subject_contract_mismatch, 2,
             NTF_E_IMPORT);
  check("static_data_load",
        "module data_test\n"
        "target x86_64-nexora-none\n"
        "section .rdata {\n"
        "data answer:u64=42\n"
        "}\n"
        "section .text {\n"
        "fn main()->u64\n"
        "effects {reads_mem(user)}\n"
        "clobbers {rax} {\n"
        "load rax,[answer]:ptr<user,u64>\n"
        "return rax\n"
        "}\n}\n",
        0);
  check("static_data_width_error",
        "module data_test\n"
        "target x86_64-nexora-none\n"
        "section .rdata {\n"
        "data answer:u64=42\n"
        "}\n"
        "section .text {\n"
        "fn main()->u64\n"
        "effects {reads_mem(user)}\n"
        "clobbers {rax} {\n"
        "load eax,[answer]:ptr<user,u64>\n"
        "return rax\n"
        "}\n}\n",
        NTF_E_TYPE);
  check("store_immediate",
        "module store_imm\ntarget x86_64-nexora-none\n"
        "section .data {\ndata answer:u64=0\n}\n"
        "section .text {\nfn main()->u64\n"
        "effects {writes_mem(user)}\nclobbers {} {\n"
        "store [answer]:ptr<user,u64>,42\nreturn 0\n}\n}\n",
        0);
  check("memory_arithmetic_effects",
        "module mem_add\ntarget x86_64-nexora-none\n"
        "section .data {\ndata answer:u64=41\n}\n"
        "section .text {\nfn main()->u64\n"
        "effects {reads_mem(user),writes_mem(user),changes_flags}\n"
        "clobbers {rax,rflags} {\n"
        "add [answer]:ptr<user,u64>,1\n"
        "load rax,[answer]:ptr<user,u64>\nreturn rax\n}\n}\n",
        0);
  check("memory_arithmetic_missing_effect",
        "module mem_add_bad\ntarget x86_64-nexora-none\n"
        "section .data {\ndata answer:u64=41\n}\n"
        "section .text {\nfn main()->u64\n"
        "effects {changes_flags}\nclobbers {rflags} {\n"
        "add [answer]:ptr<user,u64>,1\nreturn 0\n}\n}\n",
        NTF_E_EFFECT);
  check("known_contract_requires_proof",
        HEAD "fn f(in p:ptr<user,u64> @rcx)->u64\n"
             "effects {}\nclobbers {}\n"
             "requires { non_null(p) } {\nreturn 0\n" END,
        0);
  check("unknown_function_abi",
        HEAD "fn f()->u64\nabi invented-abi\n"
             "effects {}\nclobbers {} {\nreturn 0\n" END,
        NTF_E_ABI);
  check("nested_scope_shadow",
        HEAD FN "let x:u64=1\n"
                "if true {\nlet x:u64=2\nreturn x\n"
                "} else {\nreturn x\n}\n" END,
        0);
  check("nested_scope_does_not_leak",
        HEAD FN "if true {\nlet hidden:u64=2\n}\n"
                "return hidden\n" END,
        NTF_E_NAME);
  check("string_array_data",
        "module strings\n"
        "target x86_64-nexora-none\n"
        "section .rdata {\n"
        "data message:array<u8,4> =\"A\\nB\\0\"\n"
        "}\n"
        "section .text {\n" FN "return 0\n" END,
        0);
  check("string_array_length",
        "module strings\n"
        "target x86_64-nexora-none\n"
        "section .rdata {\n"
        "data message:array<u8,3> =\"four\"\n"
        "}\n"
        "section .text {\n" FN "return 0\n" END,
        NTF_E_TYPE);
  check("sib_memory",
        HEAD "fn f(in base:ptr<user,u64> @r13,in index:u64 @r12)->u64\n"
             "effects {reads_mem(user)}\nclobbers {rax} {\n"
             "load rax,[r13+r12*8-16]:ptr<user,u64>\n"
             "return rax\n" END,
        0);
  check("pointer_provenance_killed_by_integer_write",
        HEAD "fn f(in p:ptr<user,u64> @rcx)->u64\n"
             "effects {reads_mem(user)}\nclobbers {rcx,rax} {\n"
             "mov rcx,0\nload rax,[rcx]:ptr<user,u64>\nreturn rax\n" END,
        NTF_E_MEMORY);
  check("integer_register_cannot_return_as_pointer",
        HEAD "fn f()->ptr<user,u64>\neffects {}\nclobbers {rcx} {\n"
             "mov rcx,1\nreturn rcx\n" END,
        NTF_E_TYPE);
  check("callee_clobber_kills_pointer_provenance",
        HEAD "fn f(in p:ptr<user,u64> @rcx)->u64\n"
             "effects {reads_mem(user)}\nclobbers {rcx,rax} {\n"
             "call kill()\nload rax,[rcx]:ptr<user,u64>\nreturn rax\n}\n"
             "fn kill()->u64\neffects {}\nclobbers {rcx} {\n"
             "mov rcx,0\nreturn 0\n" END,
        NTF_E_MEMORY);
  check("rsp_memory_index",
        HEAD "fn f()->u64\n"
             "effects {reads_mem(user)}\nclobbers {rax} {\n"
             "load rax,[r13+rsp*2]:ptr<user,u64>\n"
             "return rax\n" END,
        NTF_E_MEMORY);
  check("never_fallthrough",
        HEAD "fn stop()->never\n"
             "effects {no_return}\nclobbers {} {\n"
             "let x:u64=1\n" END,
        NTF_E_FLOW);
  check("feature_contract_proved",
        HEAD FN "return helper()\n}\n"
                "fn helper()->u64\n"
                "effects {}\nclobbers {}\n"
                "requires { feature(lm) } {\nreturn 7\n}\n}\n",
        0);
  check("declared_syscall_feature_contract_proved",
        "module feature_syscall\ntarget x86_64-nexora-none\n"
        "features {syscall}\n"
        "section .text {\nfn f()->u64\neffects {}\nclobbers {}\n"
        "requires { feature(syscall) } {\nreturn 7\n}\n}\n",
        0);
  check("registered_feature_contract_absent",
        "module feature_absent\ntarget x86_64-nexora-none\n"
        "section .text {\nfn f()->u64\neffects {}\nclobbers {}\n"
        "requires { feature(msr) } {\nreturn 0\n}\n}\n",
        NTF_E_CONTRACT);
  check("feature_contract_unknown",
        HEAD "fn f()->u64\n"
             "effects {}\nclobbers {}\n"
             "requires { feature(avx) } {\nreturn 0\n" END,
        NTF_E_CONTRACT);
  check("nonnull_call_proved",
        HEAD "fn caller(in q:ptr<user,u64> @rcx)->u64\n"
             "effects {}\nclobbers {}\n"
             "requires { non_null(q) } {\nreturn consume(q)\n}\n"
             "fn consume(in p:ptr<user,u64> @rcx)->u64\n"
             "effects {}\nclobbers {}\n"
             "requires { non_null(p) } {\nreturn 1\n}\n}\n",
        0);
  check("nonnull_call_unproved",
        HEAD "fn caller(in q:ptr<user,u64> @rcx)->u64\n"
             "effects {}\nclobbers {} {\nreturn consume(q)\n}\n"
             "fn consume(in p:ptr<user,u64> @rcx)->u64\n"
             "effects {}\nclobbers {}\n"
             "requires { non_null(p) } {\nreturn 1\n}\n}\n",
        NTF_E_CONTRACT);
  check("canonical_call_proved",
        HEAD "fn caller(in q:ptr<user,u64> @rcx)->u64\n"
             "effects {}\nclobbers {}\n"
             "requires { canonical(q) } {\nreturn consume(q)\n}\n"
             "fn consume(in p:ptr<user,u64> @rcx)->u64\n"
             "effects {}\nclobbers {}\n"
             "requires { canonical(p) } {\nreturn 1\n}\n}\n",
        0);
  check("stack_alignment_proved",
        HEAD FN "return helper()\n}\n"
                "fn helper()->u64\n"
                "effects {}\nclobbers {}\n"
                "requires { stack_aligned(16) } {\nreturn 1\n}\n}\n",
        0);
  check("stack_alignment_unproved",
        HEAD FN "return helper()\n}\n"
                "fn helper()->u64\n"
                "effects {}\nclobbers {}\n"
                "requires { stack_aligned(32) } {\nreturn 1\n}\n}\n",
        NTF_E_CONTRACT);
  check("ensures_must_be_proved",
        HEAD "fn f(in p:ptr<user,u64> @rcx)->u64\n"
             "effects {}\nclobbers {}\n"
             "ensures { non_null(p) } {\nreturn 1\n" END,
        NTF_E_CONTRACT);
  check("integer_is_not_pointer",
        HEAD FN "return consume(1)\n}\n"
                "fn consume(in p:ptr<user,u64> @rcx)->u64\n"
                "effects {}\nclobbers {} {\nreturn 1\n}\n}\n",
        NTF_E_TYPE);
  check("typed_address_of",
        "module addresses\ntarget x86_64-nexora-none\n"
        "section .data {\ndata target:u64=42\n}\n"
        "section .rdata {\n"
        "data pointer:ptr<user,u64> =intrinsic address_of(target)\n}\n"
        "section .text {\n" FN "return 0\n" END,
        0);
  check("address_of_unknown",
        "module addresses\ntarget x86_64-nexora-none\n"
        "section .rdata {\n"
        "data pointer:ptr<user,u64> =intrinsic address_of(missing)\n}\n"
        "section .text {\n" FN "return 0\n" END,
        NTF_E_NAME);
  check("address_of_space_mismatch",
        "module addresses\ntarget x86_64-nexora-none\n"
        "section .data {\ndata target:u64=42\n}\n"
        "section .rdata {\n"
        "data pointer:ptr<kernel,u64> =intrinsic address_of(target)\n}\n"
        "section .text {\n" FN "return 0\n" END,
        NTF_E_TYPE);
  check("address_of_return_mismatch",
        "module addresses\ntarget x86_64-nexora-none\n"
        "section .data {\ndata target:u64=42\n}\n"
        "section .text {\nfn main()->ptr<kernel,u64>\n"
        "effects {}\nclobbers {} {\n"
        "return intrinsic address_of(target)\n}\n}\n",
        NTF_E_TYPE);
  check("symbolic_address_alias",
        "module aliases\ntarget x86_64-nexora-none\n"
        "section .data {\ndata target:u64=42\n}\n"
        "const P:ptr<user,u64> =intrinsic address_of(target)\n"
        "section .rdata {\ndata pointer:ptr<user,u64> =P\n}\n"
        "section .text {\n" FN "return 0\n" END,
        0);
  check("address_addend_i32_min",
        "module addend\ntarget x86_64-nexora-none\n"
        "section .data {\ndata target:u64=42\n}\n"
        "const P:ptr<user,u64> =intrinsic address_of(target-2147483648)\n"
        "section .text {\n" FN "return 0\n" END,
        0);
  check("address_addend_positive_overflow",
        "module addend\ntarget x86_64-nexora-none\n"
        "section .data {\ndata target:u64=42\n}\n"
        "const P:ptr<user,u64> =intrinsic address_of(target+2147483648)\n",
        NTF_E_LITERAL);
  check("comptime_exact_limits",
        "module comptime_ok\n"
        "target x86_64-nexora-none\n"
        "comptime(fuel=1048576,call_depth=256,arena_bytes=16777216,"
        "ast_nodes=1048576,expansion_depth=128,constant_bytes=16777216) {\n"
        "return (5+3)*4;\n}\n"
        "section .text {\n" FN "return 32\n" END,
        0);
  check("comptime_fuel_limit_plus_one",
        "module comptime_bad\n"
        "target x86_64-nexora-none\n"
        "comptime(fuel=1048577,call_depth=256,arena_bytes=16777216,"
        "ast_nodes=1048576,expansion_depth=128,constant_bytes=16777216) {\n"
        "return 1;\n}\n",
        NTF_E_LIMIT);
  check("comptime_call_depth_plus_one",
        "module budget\ntarget x86_64-nexora-none\n"
        "comptime(fuel=1048576,call_depth=257,arena_bytes=16777216,"
        "ast_nodes=1048576,expansion_depth=128,constant_bytes=16777216) {\n"
        "return 1;\n}\n",
        NTF_E_LIMIT);
  check("comptime_arena_plus_one",
        "module budget\ntarget x86_64-nexora-none\n"
        "comptime(fuel=1048576,call_depth=256,arena_bytes=16777217,"
        "ast_nodes=1048576,expansion_depth=128,constant_bytes=16777216) {\n"
        "return 1;\n}\n",
        NTF_E_LIMIT);
  check("comptime_nodes_plus_one",
        "module budget\ntarget x86_64-nexora-none\n"
        "comptime(fuel=1048576,call_depth=256,arena_bytes=16777216,"
        "ast_nodes=1048577,expansion_depth=128,constant_bytes=16777216) {\n"
        "return 1;\n}\n",
        NTF_E_LIMIT);
  check("comptime_expansion_depth_plus_one",
        "module budget\ntarget x86_64-nexora-none\n"
        "comptime(fuel=1048576,call_depth=256,arena_bytes=16777216,"
        "ast_nodes=1048576,expansion_depth=129,constant_bytes=16777216) {\n"
        "return 1;\n}\n",
        NTF_E_LIMIT);
  check("comptime_constant_bytes_plus_one",
        "module budget\ntarget x86_64-nexora-none\n"
        "comptime(fuel=1048576,call_depth=256,arena_bytes=16777216,"
        "ast_nodes=1048576,expansion_depth=128,constant_bytes=16777217) {\n"
        "return 1;\n}\n",
        NTF_E_LIMIT);
  check("macro_fuel_exact",
        "module fuel_ok\ntarget x86_64-nexora-none\n"
        "comptime(fuel=5,call_depth=32,arena_bytes=262144,"
        "ast_nodes=65536,expansion_depth=1,constant_bytes=65536) {\n"
        "return 0;\n}\n"
        "macro bind(v:ast.expr)->ast.stmt {\n"
        "return quote(stmt) { let temp:u64=$v; };\n}\n"
        "section .text {\n" FN "expand bind(1)\nexpand bind(2)\nreturn 0\n" END,
        0);
  check("macro_fuel_plus_one",
        "module fuel_bad\ntarget x86_64-nexora-none\n"
        "comptime(fuel=4,call_depth=32,arena_bytes=262144,"
        "ast_nodes=65536,expansion_depth=1,constant_bytes=65536) {\n"
        "return 0;\n}\n"
        "macro bind(v:ast.expr)->ast.stmt {\n"
        "return quote(stmt) { let temp:u64=$v; };\n}\n"
        "section .text {\n" FN "expand bind(1)\nexpand bind(2)\nreturn 0\n" END,
        NTF_E_LIMIT);
  check("constant_bytes_exact",
        "module bytes_ok\ntarget x86_64-nexora-none\n"
        "comptime(fuel=8,call_depth=4,arena_bytes=262144,"
        "ast_nodes=128,expansion_depth=4,constant_bytes=1) {\n"
        "return 0;\n}\n"
        "section .rdata {\ndata b:array<u8,1> =\"A\"\n}\n"
        "section .text {\n" FN "return 0\n" END,
        0);
  check("constant_bytes_plus_one_consumed",
        "module bytes_bad\ntarget x86_64-nexora-none\n"
        "comptime(fuel=8,call_depth=4,arena_bytes=262144,"
        "ast_nodes=128,expansion_depth=4,constant_bytes=1) {\n"
        "return 0;\n}\n"
        "section .rdata {\ndata b:array<u8,2> =\"AB\"\n}\n"
        "section .text {\n" FN "return 0\n" END,
        NTF_E_LIMIT);
  check("ast_nodes_consumed_exact",
        "module nodes_ok\ntarget x86_64-nexora-none\n"
        "comptime(fuel=8,call_depth=4,arena_bytes=262144,"
        "ast_nodes=7,expansion_depth=4,constant_bytes=8) {\n"
        "return 0;\n}\n"
        "section .text {\n" FN "return 0\n" END,
        0);
  check("ast_nodes_consumed_plus_one",
        "module nodes_bad\ntarget x86_64-nexora-none\n"
        "comptime(fuel=8,call_depth=4,arena_bytes=262144,"
        "ast_nodes=6,expansion_depth=4,constant_bytes=8) {\n"
        "return 0;\n}\n"
        "section .text {\n" FN "return 0\n" END,
        NTF_E_LIMIT);
  check("macro_expansion_depth_zero",
        "module depth_bad\ntarget x86_64-nexora-none\n"
        "comptime(fuel=8,call_depth=4,arena_bytes=262144,"
        "ast_nodes=128,expansion_depth=0,constant_bytes=8) {\n"
        "return 0;\n}\n"
        "macro bind(v:ast.expr)->ast.stmt {\n"
        "return quote(stmt) { let temp:u64=$v; };\n}\n"
        "section .text {\n" FN "expand bind(1)\nreturn 0\n" END,
        NTF_E_LIMIT);
  check("comptime_arena_consumed_exact",
        "module arena_ok\ntarget x86_64-nexora-none\n"
        "comptime(fuel=4,call_depth=2,arena_bytes=5,"
        "ast_nodes=64,expansion_depth=2,constant_bytes=4) {\n"
        "return \"ABCD\";\n}\n",
        0);
  check("comptime_arena_consumed_plus_one",
        "module arena_bad\ntarget x86_64-nexora-none\n"
        "comptime(fuel=4,call_depth=2,arena_bytes=4,"
        "ast_nodes=64,expansion_depth=2,constant_bytes=4) {\n"
        "return \"ABCD\";\n}\n",
        NTF_E_LIMIT);
  const char *macro_return = "module macro_return\n"
                             "target x86_64-nexora-none\n"
                             "macro ret(v:ast.expr)->ast.stmt {\n"
                             "return quote(stmt) { return $v; };\n}\n"
                             "section .text {\n"
                             "fn main()->u64\neffects {}\nclobbers {} {\n"
                             "expand ret(42)\n}\n}\n";
  check("macro_ast_return", macro_return, 0);
  check_macro_provenance(macro_return);
  check("macro_arity",
        "module macro_bad\n"
        "target x86_64-nexora-none\n"
        "macro ret(v:ast.expr)->ast.stmt {\n"
        "return quote(stmt) { return $v; };\n}\n"
        "section .text {\n"
        "fn main()->u64\neffects {}\nclobbers {} {\n"
        "expand ret()\n}\n}\n",
        NTF_E_CONTRACT);
  check("macro_ast_instruction",
        "module macro_instruction\n"
        "target x86_64-nexora-none\n"
        "macro increment(reg:ast.expr)->ast.stmt {\n"
        "return quote(stmt) { add $reg,1; };\n}\n"
        "section .text {\n"
        "fn main()->u64\n"
        "effects {changes_flags}\nclobbers {rax,rflags} {\n"
        "mov rax,41\nexpand increment(rax)\nreturn rax\n}\n}\n",
        0);
  check("macro_ast_block",
        "module macro_block\n"
        "target x86_64-nexora-none\n"
        "macro finish(v:ast.expr)->ast.block {\n"
        "return quote(block) {\nlet temp:u64=$v;\nreturn temp+1;\n};\n}\n"
        "section .text {\n" FN "expand finish(41)\n}\n}\n",
        0);
  check("raw_label_cfg",
        HEAD "fn main()->u64\n"
             "effects {changes_flags}\nclobbers {rax,rflags} {\n"
             "mov rax,40\nadd rax,2\ncmp rax,42\nje done\n"
             "return 0\ndone:\nreturn rax\n" END,
        0);
  check("raw_missing_label", HEAD FN "jmp missing\n" END, NTF_E_NAME);
  check("raw_branch_flags_unproved",
        HEAD FN "je done\nreturn 0\ndone:\nreturn 1\n" END, NTF_E_CONTRACT);
  check("raw_label_resets_flag_proof",
        HEAD "fn main()->u64\n"
             "effects {changes_flags}\nclobbers {rflags} {\n"
             "jmp joined\ncmp rax,0\njoined:\nje done\n"
             "return 0\ndone:\nreturn 1\n" END,
        NTF_E_CONTRACT);
  check("structured_return_stub",
        "module stubs\n"
        "target x86_64-nexora-none\n"
        "stub_table routes kind syscall {\n"
        "stub 0 {\nhandler dispatch\nentry_abi nx64-abi-v0\n"
        "exit_abi nx64-abi-v0\nexit return\nframe syscall.frame\n"
        "bindings {}\nhardware_error_code false\nsave {}\nclobbers {}\n"
        "stack_align 16\nswapgs never\ninterrupts preserve\n"
        "section .text\nvisibility export\nsymbol main\n}\n}\n"
        "section .text {\n"
        "fn dispatch()->u64\neffects {}\nclobbers {} {\nreturn 42\n}\n}\n",
        0);
  check("structured_unsafe_exit_rejected",
        "module stubs\ntarget x86_64-nexora-none\n"
        "stub_table routes kind syscall {\nstub 0 {\n"
        "handler dispatch\nentry_abi nx64-abi-v0\n"
        "exit_abi nx64-abi-v0\nexit sysretq\n",
        NTF_E_UNSUPPORTED);
  check("structured_ud2_interrupt",
        "module interrupts\ntarget x86_64-nexora-none\n"
        "stub_table faults kind interrupt {\nstub 6 {\n"
        "handler advance_saved_rip_2\n"
        "entry_abi nx64-interrupt-same-cpl-v0\n"
        "exit_abi nx64-iretq-same-cpl-v0\nexit iretq\n"
        "frame x86_64.frame.no_error.same_cpl\nbindings {}\n"
        "hardware_error_code false\nsave {rax}\nclobbers {}\n"
        "stack_align 0\nswapgs never\ninterrupts preserve\n"
        "section .text\nvisibility export\nsymbol isr6\n}\n}\n"
        "section .text {\n" FN "return 0\n" END,
        0);
  check("structured_ud2_wrong_vector",
        "module interrupts\ntarget x86_64-nexora-none\n"
        "stub_table faults kind interrupt {\nstub 7 {\n",
        NTF_E_CONTRACT);
  check("structured_gp_error_interrupt",
        "module interrupts\ntarget x86_64-nexora-none\n"
        "stub_table faults kind interrupt {\nstub 13 {\n"
        "handler advance_saved_rip_2\n"
        "entry_abi nx64-interrupt-same-cpl-error-v0\n"
        "exit_abi nx64-iretq-same-cpl-error-v0\nexit iretq\n"
        "frame x86_64.frame.error.same_cpl\nbindings {}\n"
        "hardware_error_code true\nsave {rax}\nclobbers {}\n"
        "stack_align 0\nswapgs never\ninterrupts preserve\n"
        "section .text\nvisibility export\nsymbol isr13\n}\n}\n"
        "section .text {\n" FN "return 0\n" END,
        0);
  check("structured_gp_requires_error_code",
        "module interrupts\ntarget x86_64-nexora-none\n"
        "stub_table faults kind interrupt {\nstub 13 {\n"
        "handler advance_saved_rip_2\n"
        "entry_abi nx64-interrupt-same-cpl-error-v0\n"
        "exit_abi nx64-iretq-same-cpl-error-v0\nexit iretq\n"
        "frame x86_64.frame.error.same_cpl\nbindings {}\n"
        "hardware_error_code false\n",
        NTF_E_CONTRACT);
  check("structured_interrupt_table_multiple_vectors",
        "module interrupts\ntarget x86_64-nexora-none\n"
        "stub_table faults kind interrupt {\n"
        "stub 6 {\nhandler advance_saved_rip_2\n"
        "entry_abi nx64-interrupt-same-cpl-v0\n"
        "exit_abi nx64-iretq-same-cpl-v0\nexit iretq\n"
        "frame x86_64.frame.no_error.same_cpl\nbindings {}\n"
        "hardware_error_code false\nsave {rax}\nclobbers {}\n"
        "stack_align 0\nswapgs never\ninterrupts preserve\n"
        "section .text\nvisibility export\nsymbol isr6\n}\n"
        "stub 13 {\nhandler advance_saved_rip_2\n"
        "entry_abi nx64-interrupt-same-cpl-error-v0\n"
        "exit_abi nx64-iretq-same-cpl-error-v0\nexit iretq\n"
        "frame x86_64.frame.error.same_cpl\nbindings {}\n"
        "hardware_error_code true\nsave {rax}\nclobbers {}\n"
        "stack_align 0\nswapgs never\ninterrupts preserve\n"
        "section .text\nvisibility export\nsymbol isr13\n}\n}\n",
        0);
  check("structured_interrupt_duplicate_vector",
        "module interrupts\ntarget x86_64-nexora-none\n"
        "stub_table faults kind interrupt {\n"
        "stub 6 {\nhandler advance_saved_rip_2\n"
        "entry_abi nx64-interrupt-same-cpl-v0\n"
        "exit_abi nx64-iretq-same-cpl-v0\nexit iretq\n"
        "frame x86_64.frame.no_error.same_cpl\nbindings {}\n"
        "hardware_error_code false\nsave {rax}\nclobbers {}\n"
        "stack_align 0\nswapgs never\ninterrupts preserve\n"
        "section .text\nvisibility local\nsymbol first\n}\n"
        "stub 6 {\n",
        NTF_E_DUPLICATE);
  check("nested_macro_call",
        "module nested_macro\ntarget x86_64-nexora-none\n"
        "macro inner(v:ast.expr)->ast.stmt {\n"
        "return quote(stmt) { return $v+1; };\n}\n"
        "macro outer(v:ast.expr)->ast.block {\n"
        "return quote(block) { expand inner($v); };\n}\n"
        "section .text {\n" FN "expand outer(41)\n}\n}\n",
        0);
  check("macro_expression_primary_nested",
        "module macro_expr\ntarget x86_64-nexora-none\n"
        "macro plus(v:ast.expr)->ast.expr {\n"
        "return quote(expr) { $v+1 };\n}\n"
        "section .text {\n" FN
        "return expand plus(expand plus(40))\n}\n}\n",
        0);
  check("macro_expression_call_depth_exact",
        "module macro_depth_ok\ntarget x86_64-nexora-none\n"
        "comptime(fuel=64,call_depth=2,arena_bytes=1048576,"
        "ast_nodes=1024,expansion_depth=8,constant_bytes=64) {\n"
        "return 0;\n}\nmacro plus(v:ast.expr)->ast.expr {\n"
        "return quote(expr) { $v+1 };\n}\nsection .text {\n" FN
        "return expand plus(expand plus(40))\n}\n}\n",
        0);
  check("macro_expression_call_depth_plus_one",
        "module macro_depth_bad\ntarget x86_64-nexora-none\n"
        "comptime(fuel=64,call_depth=1,arena_bytes=1048576,"
        "ast_nodes=1024,expansion_depth=8,constant_bytes=64) {\n"
        "return 0;\n}\nmacro plus(v:ast.expr)->ast.expr {\n"
        "return quote(expr) { $v+1 };\n}\nsection .text {\n" FN
        "return expand plus(expand plus(40))\n}\n}\n",
        NTF_E_LIMIT);
  check("macro_typed_comptime_value",
        "module macro_value\ntarget x86_64-nexora-none\n"
        "macro plus(v:u64)->ast.expr {\n"
        "return quote(expr) { $v+1 };\n}\n"
        "section .text {\n" FN "return expand plus(41)\n}\n}\n",
        0);
  check("macro_typed_comptime_rejects_runtime_value",
        "module macro_value_bad\ntarget x86_64-nexora-none\n"
        "macro plus(v:u64)->ast.expr {\n"
        "return quote(expr) { $v+1 };\n}\n"
        "section .text {\nfn main(in value:u64 @rcx)->u64\n"
        "effects {}\nclobbers {} {\nreturn expand plus(value)\n}\n}\n",
        NTF_E_CONTRACT);
  check("macro_type_form",
        "module macro_type\ntarget x86_64-nexora-none\n"
        "macro word()->ast.type {\nreturn quote(type) { u64 };\n}\n"
        "section .text {\nfn main()->expand word()\n"
        "effects {}\nclobbers {} {\nlet value:expand word()=42\n"
        "return value\n}\n}\n",
        0);
  check("macro_expression_rejects_type_result",
        "module macro_expr_bad\ntarget x86_64-nexora-none\n"
        "macro word()->ast.type {\nreturn quote(type) { u64 };\n}\n"
        "section .text {\n" FN "return expand word()\n}\n}\n",
        NTF_E_TYPE);
  check("macro_type_rejects_expression_result",
        "module macro_type_bad\ntarget x86_64-nexora-none\n"
        "macro value()->ast.expr {\nreturn quote(expr) { 42 };\n}\n"
        "section .text {\nfn main()->expand value()\n"
        "effects {}\nclobbers {} {\nreturn 0\n}\n}\n",
        NTF_E_TYPE);
  check("macro_list_emit",
        "module macro_emit\ntarget x86_64-nexora-none\n"
        "macro pair(v:ast.expr)->list<ast.stmt> {\n"
        "emit quote(stmt) { let temp:u64=$v; };\n"
        "emit quote(stmt) { return temp+1; };\n}\n"
        "section .text {\n" FN "expand pair(41)\n}\n}\n",
        0);
  check("macro_meta_if",
        "module macro_if\ntarget x86_64-nexora-none\n"
        "macro choose(flag:ast.expr)->list<ast.stmt> {\n"
        "if $flag {\nemit quote(stmt) { return 42; };\n"
        "} else {\nemit quote(stmt) { return 0; };\n}\n}\n"
        "section .text {\n" FN "expand choose(true)\n}\n}\n",
        0);
  check("macro_meta_for",
        "module macro_for\ntarget x86_64-nexora-none\n"
        "macro rows(n:ast.expr)->list<ast.stmt> {\n"
        "for i in $n {\nemit quote(stmt) { add rax,$i; };\n}\n}\n"
        "section .text {\n"
        "fn main()->u64\neffects {changes_flags}\nclobbers {rax,rflags} {\n"
        "mov rax,0\nexpand rows(4)\nreturn rax\n}\n}\n",
        0);
  check("macro_meta_let_chain",
        "module macro_let\ntarget x86_64-nexora-none\n"
        "macro computed(v:ast.expr)->ast.stmt {\n"
        "let first=$v+1;\nlet second=$first+1;\n"
        "return quote(stmt) { return $second; };\n}\n"
        "section .text {\n" FN "expand computed(40)\n}\n}\n",
        0);
  check("macro_meta_let_duplicate",
        "module macro_let_bad\ntarget x86_64-nexora-none\n"
        "macro computed(v:ast.expr)->ast.stmt {\n"
        "let value=$v;\nlet value=$v+1;\n"
        "return quote(stmt) { return $value; };\n}\n"
        "section .text {\n" FN "expand computed(40)\n}\n}\n",
        NTF_E_DUPLICATE);
  check_meta_let_limit(32, 0);
  check_meta_let_limit(33, NTF_E_LIMIT);
  check("macro_meta_if_without_else_true",
        "module macro_if_true\ntarget x86_64-nexora-none\n"
        "macro maybe(flag:ast.expr)->list<ast.stmt> {\n"
        "if $flag {\nemit quote(stmt) { add rax,2; };\n}\n}\n"
        "section .text {\nfn main()->u64\n"
        "effects {changes_flags}\nclobbers {rax,rflags} {\n"
        "mov rax,40\nexpand maybe(true)\nreturn rax\n}\n}\n",
        0);
  check("macro_meta_if_without_else_false",
        "module macro_if_false\ntarget x86_64-nexora-none\n"
        "macro maybe(flag:ast.expr)->list<ast.stmt> {\n"
        "if $flag {\nemit quote(stmt) { return 0; };\n}\n}\n"
        "section .text {\n" FN
        "expand maybe(false)\nreturn 42\n}\n}\n",
        0);
  check("macro_type_decl",
        "module macro_decl\ntarget x86_64-nexora-none\n"
        "macro make(T:ast.type,v:ast.expr)->ast.decl {\n"
        "return quote(decl) { export data answer:$T=$v; };\n}\n"
        "section .rdata {\n"
        "expand make(quote(type) { u64 },42)\n}\n"
        "section .text {\n"
        "fn main()->u64\neffects {reads_mem(user)}\nclobbers {rax} {\n"
        "load rax,[answer]:ptr<user,u64>\nreturn rax\n}\n}\n",
        0);
  check("macro_type_argument_required",
        "module macro_decl_bad\ntarget x86_64-nexora-none\n"
        "macro make(T:ast.type,v:ast.expr)->ast.decl {\n"
        "return quote(decl) { export data answer:$T=$v; };\n}\n"
        "section .rdata {\nexpand make(42,42)\n}\n",
        NTF_E_TYPE);
  check("macro_decl_cross_reference",
        "module macro_decls\ntarget x86_64-nexora-none\n"
        "macro make(T:ast.type,v:ast.expr)->list<ast.decl> {\n"
        "emit quote(decl) { const base:$T=$v; };\n"
        "emit quote(decl) { export data answer:$T=base+1; };\n}\n"
        "section .rdata {\n"
        "expand make(quote(type) { u64 },41)\n}\n"
        "section .text {\n"
        "fn main()->u64\neffects {reads_mem(user)}\nclobbers {rax} {\n"
        "load rax,[answer]:ptr<user,u64>\nreturn rax\n}\n}\n",
        0);
  check_decl_emit_limit(512, 0);
  check_decl_emit_limit(513, NTF_E_LIMIT);
  check_multi_source_recovery();
  check_intra_source_recovery("instruction_recovery",
                              HEAD FN "mov rax,\nadd rax,\nreturn 0\n" END, 2);
  check_intra_source_recovery("function_body_recovery",
                              HEAD
                              "fn first()->u64\neffects {}\nclobbers {} {\n"
                              "let a:u64=;\nreturn 0\n}\n"
                              "fn second()->u64\neffects {}\nclobbers {} {\n"
                              "let b:u64=;\nreturn 0\n}\n}\n",
                              2);
  check_intra_source_recovery("recovery_ignores_string_braces",
                              HEAD
                              "fn first()->u64\neffects {}\nclobbers {} {\n"
                              "let a:u64=\"} fn fake\"+;\nreturn 0\n}\n"
                              "fn second()->u64\neffects {}\nclobbers {} {\n"
                              "let b:u64=;\nreturn 0\n}\n}\n",
                              2);
  check("macro_function_decl",
        "module macro_fn\ntarget x86_64-nexora-none\n"
        "macro make(v:ast.expr)->ast.decl {\n"
        "return quote(decl) {\n"
        " export fn helper()->u64\n"
        " effects {}\nclobbers {} {\nreturn $v+1\n}\n"
        "};\n}\n"
        "section .text {\n"
        "expand make(41)\n" FN "return helper()\n" END,
        0);
  check("macro_function_parameters_and_type_splices",
        "module macro_fn_params\ntarget x86_64-nexora-none\n"
        "macro word()->ast.type {\nreturn quote(type) { u64 };\n}\n"
        "macro finish(v:ast.expr)->ast.stmt {\n"
        "return quote(stmt) { return $v; };\n}\n"
        "macro make(T:ast.type)->ast.decl {\n"
        "return quote(decl) {\n"
        " export fn add(in x:$T @rcx,in y:$T @rdx)->$T\n"
        " effects {}\nclobbers {} {\nlet sum:$T=x+y\n"
        "expand finish(sum)\n}\n"
        "};\n}\n"
        "section .text {\nexpand make(expand word())\n"
        "fn main()->u64\neffects {}\nclobbers {} {\nreturn add(20,22)\n}\n}\n",
        0);
  check("macro_function_parameter_width_mismatch",
        "module macro_fn_bad_width\ntarget x86_64-nexora-none\n"
        "macro make(T:ast.type)->ast.decl {\n"
        "return quote(decl) {\n"
        " export fn bad(in x:$T @ecx)->$T\n"
        " effects {}\nclobbers {} {\nreturn x\n}\n"
        "};\n}\n"
        "section .text {\nexpand make(quote(type) { u64 })\n}\n",
        NTF_E_REGISTER);
  check("macro_decl_meta_if",
        "module decl_if\ntarget x86_64-nexora-none\n"
        "macro choose(flag:ast.expr)->list<ast.decl> {\n"
        "if $flag {\nemit quote(decl) { export data answer:u64=42; };\n"
        "} else {\nemit quote(decl) { export data answer:u64=0; };\n}\n}\n"
        "section .rdata {\nexpand choose(true)\n}\n"
        "section .text {\nfn main()->u64\n"
        "effects {reads_mem(user)}\nclobbers {rax} {\n"
        "load rax,[answer]:ptr<user,u64>\nreturn rax\n}\n}\n",
        0);
  check("recursive_macro_call_depth",
        "module recursive_macro\ntarget x86_64-nexora-none\n"
        "comptime(fuel=64,call_depth=2,arena_bytes=1048576,"
        "ast_nodes=1024,expansion_depth=8,constant_bytes=64) {\n"
        "return 0;\n}\n"
        "macro recurse(v:ast.expr)->ast.block {\n"
        "return quote(block) { expand recurse($v); };\n}\n"
        "section .text {\n" FN "expand recurse(1)\n}\n}\n",
        NTF_E_LIMIT);
  const char *duplicate_a =
      "module duplicate\ntarget x86_64-nexora-none\nsection .text {\n" FN
      "return 1\n" END;
  const char *duplicate_b =
      "module duplicate\ntarget x86_64-nexora-none\nsection .text {\nfn "
      "other()->u64\neffects {}\nclobbers {} {\nreturn 2\n}\n}\n";
  NtFInput duplicate_modules[] = {{"a", duplicate_a, strlen(duplicate_a)},
                                  {"b", duplicate_b, strlen(duplicate_b)}};
  check_many("duplicate_module", duplicate_modules, 2, NTF_E_DUPLICATE);
  const char *import_none =
      "module app\ntarget x86_64-nexora-none\nimport math.add as add: "
      "fn()->u64\neffects {}\nclobbers {}\nsection .text {\n" FN
      "return add()\n" END;
  const char *export_uefi =
      "module math\ntarget x86_64-nexora-uefi\nsection .text {\nexport fn "
      "add()->u64\neffects {}\nclobbers {} {\nreturn 1\n}\n}\n";
  NtFInput target_mismatch[] = {{"app", import_none, strlen(import_none)},
                                {"math", export_uefi, strlen(export_uefi)}};
  check_many("import_target_mismatch", target_mismatch, 2, NTF_E_IMPORT);
  check_many_object("object_resolved_import_mismatch", target_mismatch, 2,
                    NTF_E_IMPORT);
  printf("NTASM_FRONTEND_TESTS passed=%u failed=%u\n", passed, failed);
  return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
