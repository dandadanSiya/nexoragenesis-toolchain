#include "frontend.h"
#include "nova.h"
#include "nova/run_host.h"
#include "nova/runtime_v2.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, failures, cases;
#define CHECK(c, m)                                                            \
  do {                                                                         \
    checks++;                                                                  \
    if (!(c)) {                                                                \
      failures++;                                                              \
      fprintf(stderr, "FAIL %s\n", m);                                        \
    }                                                                          \
  } while (0)
typedef struct {
  uint64_t *words;
  size_t word_count;
  uint64_t returned, runtime_error;
} Parsed;
static char *load(const char *path, size_t *length) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *data = size >= 0 ? (char *)malloc((size_t)size + 1) : NULL;
  if (!data || fread(data, 1, (size_t)size, f) != (size_t)size) {
    free(data);
    fclose(f);
    return NULL;
  }
  fclose(f);
  data[size] = 0;
  *length = (size_t)size;
  return data;
}
static Parsed parse(const NovaHostImage *image, const char *source,
                    size_t length) {
  Parsed result = {0};
  result.word_count = 10 + (length * 2 + 32) * 12;
  result.words = (uint64_t *)calloc(result.word_count, sizeof(uint64_t));
  NovaContextV2 context;
  if (!result.words || !nova_context_init_v2(&context)) {
    result.runtime_error = UINT64_MAX;
    return result;
  }
  uint64_t source_handle =
      nova_borrow_v2(&context, (void *)source, (uint64_t)length, 0);
  uint64_t output_handle = nova_borrow_v2(
      &context, result.words,
      (uint64_t)(result.word_count * sizeof(*result.words)), 1);
  uint64_t args[] = {source_handle, length, output_handle, result.word_count};
  result.returned = image->entry(&context, args, 4);
  result.runtime_error = context.error;
  CHECK(nova_context_destroy_v2(&context), "parser context cleanup");
  return result;
}
static int ast_well_formed(const Parsed *parsed, size_t source_length) {
  if (!parsed->words || !parsed->words[0] || parsed->words[9] == 0)
    return 0;
  uint64_t count = parsed->words[9];
  for (uint64_t id = 1; id <= count; id++) {
    const uint64_t *node = parsed->words + 10 + (id - 1) * 12;
    if (!node[0] || node[6] > node[7] || node[7] > source_length ||
        node[8] > count || node[9] > count || node[10] > count)
      return 0;
  }
  return parsed->words[10] == 1;
}
static uint64_t kind_count(const Parsed *parsed, uint64_t kind) {
  uint64_t count = 0;
  for (uint64_t id = 1; id <= parsed->words[9]; id++)
    if (parsed->words[10 + (id - 1) * 12] == kind)
      count++;
  return count;
}
static void expect_valid(const NovaHostImage *image, const char *name,
                         const char *source, uint64_t required_kind) {
  Parsed parsed = parse(image, source, strlen(source));
  NtFInput input = {name, source, strlen(source)};
  NtFProgram c;
  int c_ok = nt_frontend_compile(&input, 1, &c);
  int ok = !parsed.runtime_error && parsed.returned == 0 && parsed.words[0] &&
           ast_well_formed(&parsed, strlen(source)) && c_ok &&
           (!required_kind || kind_count(&parsed, required_kind));
  checks++;
  cases++;
  if (!ok) {
    failures++;
    fprintf(stderr,
            "FAIL valid %s nova=%llu runtime=%llu code=%llu reason=%llu "
            "at=%llu:%llu nodes=%llu c_ok=%d\n",
            name, (unsigned long long)parsed.returned,
            (unsigned long long)parsed.runtime_error,
            (unsigned long long)(parsed.words ? parsed.words[1] : 0),
            (unsigned long long)(parsed.words ? parsed.words[2] : 0),
            (unsigned long long)(parsed.words ? parsed.words[4] : 0),
            (unsigned long long)(parsed.words ? parsed.words[5] : 0),
            (unsigned long long)(parsed.words ? parsed.words[9] : 0), c_ok);
    for (size_t i = 0; i < c.diagnostic_count; i++)
      fprintf(stderr, " C E%u %u:%u %s\n", c.diagnostics[i].code,
              c.diagnostics[i].span.line, c.diagnostics[i].span.column,
              c.diagnostics[i].message);
  }
  nt_frontend_free(&c);
  free(parsed.words);
}
static void expect_parse_only(const NovaHostImage *image, const char *name,
                              const char *source, uint64_t required_kind) {
  Parsed parsed = parse(image, source, strlen(source));
  int ok = !parsed.runtime_error && parsed.returned == 0 && parsed.words[0] &&
           ast_well_formed(&parsed, strlen(source)) &&
           (!required_kind || kind_count(&parsed, required_kind));
  checks++;
  cases++;
  if (!ok) {
    failures++;
    fprintf(stderr,
            "FAIL parse-only %s return=%llu runtime=%llu E%llu reason=%llu "
            "at=%llu:%llu nodes=%llu\n",
            name, (unsigned long long)parsed.returned,
            (unsigned long long)parsed.runtime_error,
            (unsigned long long)(parsed.words ? parsed.words[1] : 0),
            (unsigned long long)(parsed.words ? parsed.words[2] : 0),
            (unsigned long long)(parsed.words ? parsed.words[4] : 0),
            (unsigned long long)(parsed.words ? parsed.words[5] : 0),
            (unsigned long long)(parsed.words ? parsed.words[9] : 0));
  }
  free(parsed.words);
}
static void expect_syntax_error(const NovaHostImage *image, const char *name,
                                const char *source) {
  Parsed parsed = parse(image, source, strlen(source));
  NtFInput input = {name, source, strlen(source)};
  NtFProgram c;
  int c_ok = nt_frontend_compile(&input, 1, &c);
  int c_syntax = !c_ok && c.diagnostic_count &&
                 (c.diagnostics[0].code == 100 || c.diagnostics[0].code == 200);
  int ok = !parsed.runtime_error && !parsed.words[0] && !parsed.words[9] &&
           parsed.words[1] == c.diagnostics[0].code && c_syntax &&
           parsed.words[3] == c.diagnostics[0].span.source &&
           parsed.words[4] == c.diagnostics[0].span.line &&
           parsed.words[5] == c.diagnostics[0].span.column &&
           parsed.words[6] == c.diagnostics[0].span.start &&
           parsed.words[7] == c.diagnostics[0].span.end;
  checks++;
  cases++;
  if (!ok)
    fprintf(stderr,
            "FAIL invalid %s nova=%llu E%llu %llu:%llu %llu-%llu "
            "C=%d E%u %u:%u %zu-%zu\n",
            name, (unsigned long long)parsed.returned,
            (unsigned long long)(parsed.words ? parsed.words[1] : 0),
            (unsigned long long)(parsed.words ? parsed.words[4] : 0),
            (unsigned long long)(parsed.words ? parsed.words[5] : 0),
            (unsigned long long)(parsed.words ? parsed.words[6] : 0),
            (unsigned long long)(parsed.words ? parsed.words[7] : 0), c_ok,
            c.diagnostic_count ? c.diagnostics[0].code : 0,
            c.diagnostic_count ? c.diagnostics[0].span.line : 0,
            c.diagnostic_count ? c.diagnostics[0].span.column : 0,
            c.diagnostic_count ? c.diagnostics[0].span.start : 0,
            c.diagnostic_count ? c.diagnostics[0].span.end : 0);
  if (!ok)
    failures++;
  nt_frontend_free(&c);
  free(parsed.words);
}
int main(void) {
  const char *paths[] = {"toolchain/ntasm-nova/tests/parser-entry.nova",
                         "toolchain/ntasm-nova/parser.nova",
                         "toolchain/ntasm-nova/ast.nova",
                         "toolchain/ntasm-nova/lexer.nova"};
  char *sources[4] = {0};
  NtFInput inputs[4];
  for (size_t i = 0; i < 4; i++) {
    size_t length = 0;
    sources[i] = load(paths[i], &length);
    inputs[i] = (NtFInput){paths[i], sources[i], length};
  }
  CHECK(sources[0] && sources[1] && sources[2] && sources[3],
        "parser Nova sources load");
  NtArtifact artifact;
  int compiled = sources[0] && sources[1] && sources[2] && sources[3] &&
                 nova_compile_many_v2(inputs, 4, &artifact);
  CHECK(compiled, "parser Nova modules compile");
  if (!compiled)
    fprintf(stderr, "Nova compile E%u %zu:%zu %s\n", artifact.error.code,
            artifact.error.line, artifact.error.column, artifact.error.message);
  NovaHostImage image = {0};
  CHECK(compiled && nova_host_map(&artifact, &image), "parser image maps");
  if (image.memory) {
    const char *basic =
        "module test\ntarget x86_64-nexora-none\nsection .text {\n"
        "fn main()->u64\neffects {}\nclobbers {} {\nreturn 42\n}\n}\n";
    expect_valid(&image, "basic", basic, 18);
    const char *control =
        "module control\ntarget x86_64-nexora-none\nsection .text {\n"
        "fn helper(in value:u64 @rcx)->u64\neffects {}\nclobbers {} {\n"
        "return value+1\n}\nfn main()->u64\neffects {}\nclobbers {} {\n"
        "let value:u64=(4+6)*4\nif value==40 {\nreturn helper(value)\n} "
        "else {\nreturn 0\n}\n}\n}\n";
    expect_valid(&image, "control_and_precedence", control, 30);
    const char *memory_source =
        "module memory\ntarget x86_64-nexora-none\nsection .rdata {\n"
        "data answer:u64=42\n}\nsection .text {\n"
        "fn main()->u64\neffects {reads_mem(user)}\nclobbers {rax} {\n"
        "load rax,[answer]:ptr<user,u64>\nreturn rax\n}\n}\n";
    expect_valid(&image, "data_and_memory", memory_source, 34);
    const char *macro_source =
        "module macros\ntarget x86_64-nexora-none\n"
        "comptime(fuel=64,call_depth=8,arena_bytes=1048576,"
        "ast_nodes=1024,expansion_depth=8,constant_bytes=64) {\nreturn 0;\n}\n"
        "macro finish(v:ast.expr)->ast.stmt {\n"
        "return quote(stmt) { return $v+1; };\n}\nsection .text {\n"
        "fn main()->u64\neffects {}\nclobbers {} {\n"
        "expand finish(41)\n}\n}\n";
    expect_valid(&image, "comptime_macro", macro_source, 13);
    const char *macro_declarations =
        "module macro_decls\ntarget x86_64-nexora-none\n"
        "macro make(T:ast.type,v:ast.expr)->ast.decl {\n"
        "return quote(decl) { export data answer:$T=$v; };\n}\n"
        "macro body(v:ast.expr)->ast.block {\nreturn quote(block) {\n"
        "let temp:u64=$v;\nreturn temp;\n};\n}\n"
        "section .rdata {\nexpand make(quote(type) { u64 },42)\n}\n"
        "section .text {\nfn main()->u64\neffects {reads_mem(user)}\n"
        "clobbers {rax} {\nload rax,[answer]:ptr<user,u64>\n"
        "expand body(rax)\n}\n}\n";
    expect_valid(&image, "macro_decl_type_block_quotes", macro_declarations,
                 43);
    const char *stub_source =
        "module interrupts\ntarget x86_64-nexora-none\n"
        "stub_table faults kind interrupt {\nstub 6 {\n"
        "handler advance_saved_rip_2\n"
        "entry_abi nx64-interrupt-same-cpl-v0\n"
        "exit_abi nx64-iretq-same-cpl-v0\nexit iretq\n"
        "frame x86_64.frame.no_error.same_cpl\nbindings {}\n"
        "hardware_error_code false\nsave {rax}\nclobbers {}\n"
        "stack_align 0\nswapgs never\ninterrupts preserve\n"
        "section .text\nvisibility export\nsymbol isr6\n}\n}\n"
        "section .text {\nfn main()->u64\neffects {}\nclobbers {} {\n"
        "return 0\n}\n}\n";
    expect_valid(&image, "structured_stub", stub_source, 15);
    const char *format_only =
        "module format\ntarget x86_64-nexora-none\n"
        "versions {\nntir 1.0\nmachine_ir 1.0\nsections 1.0\n"
        "symbols 1.0\nrelocations 1.0\nnxo 1.0\n}\n"
        "default_abi nx64-abi-v0\noutput pe-coff-v0\nfeatures {lm,syscall}\n";
    expect_parse_only(&image, "format_headers", format_only, 4);
    const char *import_only =
        "module app\ntarget x86_64-nexora-none\n"
        "import math.add as add: fn(in a:u64 @rcx,in b:u64 @rdx)->u64\n"
        "effects {}\nclobbers {}\nrequires {stack_aligned(16)}\n"
        "ensures {stack_aligned(16)}\n";
    expect_parse_only(&image, "typed_import", import_only, 9);
    expect_syntax_error(&image, "missing_module",
                        "target x86_64-nexora-none\n");
    expect_syntax_error(&image, "missing_return_expression",
                        "module test\ntarget x86_64-nexora-none\n"
                        "section .text {\nfn main()->u64\neffects {}\n"
                        "clobbers {} {\nreturn +\n}\n}\n");
    expect_syntax_error(&image, "missing_target",
                        "module test\nsection .text {\n}\n");
    expect_syntax_error(&image, "parameter_missing_colon",
                        "module test\ntarget x86_64-nexora-none\nsection .text {\n"
                        "fn f(in value u64)->u64\neffects {}\nclobbers {} {\n"
                        "return 0\n}\n}\n");
    expect_syntax_error(&image, "unterminated_block",
                        "module test\ntarget x86_64-nexora-none\nsection .text {\n"
                        "fn main()->u64\neffects {}\nclobbers {} {\nreturn 0\n");
    expect_syntax_error(&image, "pointer_missing_close",
                        "module test\ntarget x86_64-nexora-none\nsection .text {\n"
                        "fn f(in p:ptr<user,u64 @rcx)->u64\neffects {}\n"
                        "clobbers {} {\nreturn 0\n}\n}\n");
    const char *complex_address =
        "module address\ntarget x86_64-nexora-none\nsection .text {\n"
        "fn read(in base:u64 @rdi,in index:u64 @rsi)->u64\n"
        "effects {reads_mem(user)}\nclobbers {rax} {\n"
        "load rax,[rdi+rsi*8+16]:ptr<user,u64>\nreturn rax\n}\n}\n";
    expect_valid(&image, "complex_address", complex_address, 34);
    const char *integer_widths =
        "module widths\ntarget x86_64-nexora-none\nsection .text {\n"
        "fn widths(in a:u8 @dil,in b:u16 @si,in c:u32 @edx,"
        "in d:u64 @rcx)->u64\neffects {}\nclobbers {} {\n"
        "return d\n}\n}\n";
    expect_valid(&image, "integer_widths", integer_widths, 19);
    const char *typed_call =
        "module calls\ntarget x86_64-nexora-none\nsection .text {\n"
        "fn caller(in x:u32 @edi)->u64\neffects {}\nclobbers {} {\n"
        "call callee(x)\nreturn 0\n}\n"
        "fn callee(in y:u32 @edi)->u32\neffects {}\nclobbers {} {\n"
        "return y\n}\n}\n";
    expect_valid(&image, "typed_call", typed_call, 28);
    const char *nested_branch =
        "module branches\ntarget x86_64-nexora-none\nsection .text {\n"
        "fn choose(in x:u64 @rdi)->u64\neffects {}\nclobbers {} {\n"
        "if x>9 {\nreturn 42\n} else {\nif x==9 {\nreturn 9\n} "
        "else {\nreturn 0\n}\n}\n}\n}\n";
    expect_valid(&image, "nested_branch", nested_branch, 30);
    const char *declared_effects =
        "module effects\ntarget x86_64-nexora-none\nsection .text {\n"
        "fn mutate()->u64\neffects {reads_mem(user),writes_mem(user),"
        "changes_flags}\nclobbers {rax,rcx,r11} {\nreturn 42\n}\n}\n";
    expect_valid(&image, "declared_effects", declared_effects, 20);
    const char *contracts =
        "module contracts\ntarget x86_64-nexora-none\nsection .text {\n"
        "fn read(in p:ptr<user,u64> @rdi)->u64\n"
        "effects {reads_mem(user)}\nclobbers {rax}\n"
        "requires {non_null(p)}\nensures {} {\nreturn 0\n}\n}\n";
    expect_valid(&image, "contracts", contracts, 22);
    const char *versioned_features =
        "module formats\ntarget x86_64-nexora-none\nversions {\nntir 1.0\n"
        "machine_ir 1.0\nsections 1.0\nsymbols 1.0\n"
        "relocations 1.0\nnxo 1.0\n}\nfeatures {lm,syscall}\n";
    expect_parse_only(&image, "versioned_features", versioned_features, 8);
    expect_syntax_error(&image, "missing_memory_close",
                        "module bad\ntarget x86_64-nexora-none\nsection .text {\n"
                        "fn f()->u64\neffects {reads_mem(user)}\n"
                        "clobbers {rax} {\nload rax,[rax+8:ptr<user,u64>\n"
                        "return rax\n}\n}\n");
  }
  if (image.memory)
    nova_host_unmap(&image);
  if (compiled)
    nt_artifact_free(&artifact);
  for (size_t i = 0; i < 4; i++)
    free(sources[i]);
  printf("NTASM_NOVA_PARSER checks=%u failures=%u cases=%u\n", checks,
         failures, cases);
  return failures ? 1 : 0;
}
