#define _GNU_SOURCE
#include "codegen.h"
#include "frontend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

static unsigned checks, failures;
#define CHECK(c, m)                                                            \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(c)) {                                                                \
      ++failures;                                                              \
      fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, m);               \
    }                                                                          \
  } while (0)

static uint64_t execute(const NtCodeImage *image) {
  uint64_t result = 0;
  size_t rdata_offset = (image->size + 4095u) & ~(size_t)4095u;
  size_t data_offset =
      (rdata_offset + 8u + image->rdata.size + 4095u) & ~(size_t)4095u;
  size_t mapping_size =
      image->rdata.size || image->data.size ? data_offset + 4096u : image->size;
#ifdef _WIN32
  void *memory = VirtualAlloc(NULL, mapping_size, MEM_COMMIT | MEM_RESERVE,
                              PAGE_READWRITE);
  DWORD prior = 0;
  CHECK(memory != NULL, "VirtualAlloc");
  if (!memory)
    return 0;
  memcpy(memory, image->bytes, image->size);
  if (image->rdata.size)
    memcpy((uint8_t *)memory + rdata_offset + 8, image->rdata.bytes,
           image->rdata.size);
  if (image->data.size)
    memcpy((uint8_t *)memory + data_offset, image->data.bytes,
           image->data.size);
  if (image->rdata.size || image->data.size) {
    CHECK(VirtualProtect(memory, rdata_offset, PAGE_EXECUTE_READ, &prior),
          "seal text RX");
    CHECK(VirtualProtect((uint8_t *)memory + rdata_offset,
                         data_offset - rdata_offset, PAGE_READONLY, &prior),
          "seal rdata R");
  } else
    CHECK(VirtualProtect(memory, mapping_size, PAGE_EXECUTE_READ, &prior),
          "seal RX");
  FlushInstructionCache(GetCurrentProcess(), memory, image->size);
  result = ((uint64_t (*)(void))((uint8_t *)memory + image->entry))();
  VirtualFree(memory, 0, MEM_RELEASE);
#else
  void *memory = mmap(NULL, mapping_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(memory != MAP_FAILED, "mmap");
  if (memory == MAP_FAILED)
    return 0;
  memcpy(memory, image->bytes, image->size);
  if (image->rdata.size)
    memcpy((uint8_t *)memory + rdata_offset + 8, image->rdata.bytes,
           image->rdata.size);
  if (image->data.size)
    memcpy((uint8_t *)memory + data_offset, image->data.bytes,
           image->data.size);
  if (image->rdata.size || image->data.size) {
    CHECK(mprotect(memory, rdata_offset, PROT_READ | PROT_EXEC) == 0,
          "seal text RX");
    CHECK(mprotect((uint8_t *)memory + rdata_offset, data_offset - rdata_offset,
                   PROT_READ) == 0,
          "seal rdata R");
  } else
    CHECK(mprotect(memory, mapping_size, PROT_READ | PROT_EXEC) == 0,
          "seal RX");
  result = ((uint64_t(__attribute__((ms_abi)) *)(void))((uint8_t *)memory +
                                                        image->entry))();
  munmap(memory, mapping_size);
#endif
  return result;
}
static uint64_t execute_preserves_flags(const NtCodeImage *image) {
  /* Independent caller: establish flags after stack setup, save them in r10,
   * call the generated entry, then return 1 iff all saved flag bits match. */
  static const uint8_t prefix[] = {
      0x48, 0x83, 0xec, 0x28,   /* sub rsp,40 */
      0x31, 0xc0,               /* xor eax,eax: known flags */
      0x9c, 0x41, 0x5a,         /* pushfq; pop r10 */
      0xe8, 0,    0,    0,    0 /* call rel32 */
  };
  static const uint8_t suffix[] = {0x9c, 0x41, 0x5b,       /* pushfq; pop r11 */
                                   0x48, 0x83, 0xc4, 0x28, /* add rsp,40 */
                                   0x4d, 0x31, 0xda,       /* xor r10,r11 */
                                   0x0f, 0x94, 0xc0,       /* sete al */
                                   0x0f, 0xb6, 0xc0,       /* movzx eax,al */
                                   0xc3};
  NtCodeImage wrapper = {0};
  wrapper.size = sizeof(prefix) + sizeof(suffix) + image->size;
  wrapper.bytes = (uint8_t *)calloc(wrapper.size, 1);
  CHECK(wrapper.bytes != NULL, "flags wrapper allocation");
  if (!wrapper.bytes)
    return 0;
  memcpy(wrapper.bytes, prefix, sizeof(prefix));
  size_t generated = sizeof(prefix) + sizeof(suffix);
  int32_t displacement = (int32_t)(generated - (9 + 5));
  for (unsigned i = 0; i < 4; i++)
    wrapper.bytes[10 + i] = (uint8_t)((uint32_t)displacement >> (8 * i));
  memcpy(wrapper.bytes + sizeof(prefix), suffix, sizeof(suffix));
  memcpy(wrapper.bytes + generated, image->bytes, image->size);
  uint64_t result = execute(&wrapper);
  free(wrapper.bytes);
  return result;
}
static uint64_t compile_and_run(const char *name, const char *source,
                                NtCodeImage *keep) {
  NtFInput input = {name, source, strlen(source)};
  NtFProgram program;
  NtCodeImage image = {0};
  NtCodeDiagnostic diagnostic = {0};
  int parsed = nt_frontend_compile(&input, 1, &program);
  CHECK(parsed, "frontend accepted codegen program");
  int generated = parsed && nt_codegen_x64(&program, &image, &diagnostic);
  CHECK(generated, "verified IR lowered to x64");
  if (!generated)
    fprintf(stderr, " codegen E%u %s\n", diagnostic.code, diagnostic.message);
  uint64_t result = generated ? execute(&image) : 0;
  nt_frontend_free(&program);
  if (keep)
    *keep = image;
  else
    nt_code_image_free(&image);
  return result;
}
#define HEAD                                                                   \
  "module test\n"                                                              \
  "target x86_64-nexora-none\n"                                                \
  "section .text {\n"
#define TAIL "}\n"

static void literal_golden(void) {
  const char *source = HEAD "export fn main()->u64\n"
                            "effects {}\nclobbers {} {\nreturn 42\n}\n" TAIL;
  const uint8_t expected[] = {0x48, 0xb8, 0x2a, 0, 0, 0, 0, 0, 0, 0, 0xc3};
  NtCodeImage image = {0};
  uint64_t result = compile_and_run("literal", source, &image);
  CHECK(result == 42, "literal program executes");
  CHECK(image.size == sizeof expected &&
            !memcmp(image.bytes, expected, sizeof expected),
        "independent leaf-function byte golden");
  CHECK(execute_preserves_flags(&image) == 1,
        "pure generated function preserves caller RFLAGS");
  nt_code_image_free(&image);
}
static void calls_arithmetic_and_forward(void) {
  const char *source = HEAD
      "export fn main()->u64\n"
      "effects {}\nclobbers {} {\nreturn combine(5,7)\n}\n"
      "fn combine(in a:u64 @rcx,in b:u64 @rdx)->u64\n"
      "effects {}\nclobbers {} {\nlet twice:u64=b*2\nreturn a+twice\n}\n" TAIL;
  CHECK(compile_and_run("calls", source, NULL) == 19,
        "forward call and arithmetic execute");
}
static void parameter_entry_snapshot(void) {
  const char *source = HEAD "export fn main()->u64\n"
                            "effects {changes_flags}\nclobbers {rcx,rflags} "
                            "{\nreturn snapshot(41)\n}\n"
                            "fn snapshot(in original:u64 @rcx)->u64\n"
                            "effects {changes_flags}\nclobbers {rcx,rflags} {\n"
                            "add rcx,1\nreturn original\n}\n" TAIL;
  CHECK(compile_and_run("snapshot", source, NULL) == 41,
        "named parameter denotes immutable entry-value snapshot");
}
static void branches_and_values(void) {
  const char *source0 = HEAD "export fn main()->u64\n"
                             "effects {}\nclobbers {} {\nreturn choose(0)\n}\n"
                             "fn choose(in x:u64 @rcx)->u64\n"
                             "effects {}\nclobbers {} {\nif x==0 {\nreturn "
                             "11\n} else {\nreturn x+1\n}\n}\n" TAIL;
  const char *source4 = HEAD "export fn main()->u64\n"
                             "effects {}\nclobbers {} {\nreturn choose(4)\n}\n"
                             "fn choose(in x:u64 @rcx)->u64\n"
                             "effects {}\nclobbers {} {\nif x==0 {\nreturn "
                             "11\n} else {\nreturn x+1\n}\n}\n" TAIL;
  CHECK(compile_and_run("branch0", source0, NULL) == 11,
        "true branch executes");
  CHECK(compile_and_run("branch4", source4, NULL) == 5,
        "false branch executes");
}
static void scalar_widths_and_division(void) {
  const char *narrow =
      HEAD "export fn main()->u8\n"
           "effects {}\nclobbers {} {\nlet x:u8=250\nreturn x+5\n}\n" TAIL;
  const char *signed_value =
      HEAD "export fn main()->i8\n"
           "effects {}\nclobbers {} {\nreturn identity(-1)\n}\n"
           "fn identity(in x:i8 @cl)->i8\n"
           "effects {}\nclobbers {} {\nreturn x\n}\n" TAIL;
  const char *division = HEAD
      "export fn main()->u64\n"
      "effects {}\nclobbers {} {\nreturn quotient(85,2)+remainder(85,2)\n}\n"
      "fn quotient(in x:u64 @rcx,in y:u64 @rdx)->u64\n"
      "effects {}\nclobbers {} {\nreturn x/y\n}\n"
      "fn remainder(in x:u64 @rcx,in y:u64 @rdx)->u64\n"
      "effects {}\nclobbers {} {\nreturn x%y\n}\n" TAIL;
  CHECK(compile_and_run("narrow", narrow, NULL) == 255,
        "u8 arithmetic normalizes result width");
  CHECK(compile_and_run("signed", signed_value, NULL) == UINT64_MAX,
        "i8 parameter and return are sign extended");
  CHECK(compile_and_run("division", division, NULL) == 43,
        "runtime quotient and remainder execute");
  const char *signed_compare =
      HEAD "export fn main()->bool\n"
           "effects {}\nclobbers {} {\nreturn less(-2,1)\n}\n"
           "fn less(in a:i64 @rcx,in b:i64 @rdx)->bool\n"
           "effects {}\nclobbers {} {\nreturn a<b\n}\n" TAIL;
  CHECK(compile_and_run("signed_compare", signed_compare, NULL) == 1,
        "signed comparison uses signed condition codes");
}
static void abi_alignment_and_nested_arguments(void) {
  const char *nested = HEAD
      "export fn main()->u64\n"
      "effects {}\nclobbers {} {\nreturn add(identity(20),identity(22))\n}\n"
      "fn identity(in x:u64 @rcx)->u64\n"
      "effects {}\nclobbers {} {\nreturn x\n}\n"
      "fn add(in a:u64 @rcx,in b:u64 @rdx)->u64\n"
      "effects {}\nclobbers {} {\nreturn a+b\n}\n" TAIL;
  const char *alignment =
      HEAD "export fn main()->u64\n"
           "effects {}\nclobbers {} {\nreturn alignment()\n}\n"
           "fn alignment()->u64\n"
           "effects {}\nclobbers {} {\nreturn rsp&15\n}\n" TAIL;
  CHECK(compile_and_run("nested_args", nested, NULL) == 42,
        "nested call arguments use distinct spill levels");
  CHECK(compile_and_run("alignment", alignment, NULL) == 0,
        "callee stack is 16-byte aligned after generated prologue");
  const char *register_preservation =
      HEAD "export fn main()->u64\n"
           "effects {}\nclobbers {rcx} {\n"
           "mov rcx,123\ncall helper()\nreturn rcx\n}\n"
           "fn helper()->u64\n"
           "effects {}\nclobbers {} {\nreturn identity(7)\n}\n"
           "fn identity(in x:u64 @rcx)->u64\n"
           "effects {}\nclobbers {} {\nreturn x\n}\n" TAIL;
  CHECK(compile_and_run("register_preservation", register_preservation, NULL) ==
            123,
        "pure callee preserves compiler scratch and argument registers");
}
static void wide_register_abi_executes(void) {
  const char *source =
      HEAD "export fn main()->u64\n"
           "effects {}\nclobbers {} {\nreturn five(1,2,3,4,5)\n}\n"
           "fn five(in a:u64 @rcx,in b:u64 @rdx,in c:u64 @r8,"
           "in d:u64 @r9,in e:u64 @r10)->u64\n"
           "effects {}\nclobbers {} {\nreturn a+b+c+d+e\n}\n" TAIL;
  NtFInput input = {"five", source, strlen(source)};
  NtFProgram program;
  CHECK(nt_frontend_compile(&input, 1, &program),
        "five-register signature is valid typed IR");
  NtCodeImage image = {0};
  NtCodeDiagnostic diagnostic = {0};
  int ok = nt_codegen_x64(&program, &image, &diagnostic);
  CHECK(ok, "five explicit register arguments lower successfully");
  if (ok)
    CHECK(execute(&image) == 15,
          "all five arguments reach their bound registers");
  nt_code_image_free(&image);
  nt_frontend_free(&program);
  const char *wide = HEAD
      "export fn main()->u64\n"
      "effects {}\nclobbers {} {\nreturn "
      "sum14(1,2,3,4,5,6,7,8,9,10,11,12,13,identity(14))\n}\n"
      "fn identity(in n:u64 @rcx)->u64\neffects {}\nclobbers {} {\nreturn "
      "n\n}\n"
      "fn sum14(in a:u64 @rax,in b:u64 @rcx,in c:u64 @rdx,in d:u64 @rbx,"
      "in e:u64 @rsi,in f:u64 @rdi,in g:u64 @r8,in h:u64 @r9,"
      "in i:u64 @r10,in j:u64 @r11,in k:u64 @r12,in l:u64 @r13,"
      "in m:u64 @r14,in n:u64 @r15)->u64\n"
      "effects {}\nclobbers {} {\nreturn a+b+c+d+e+f+g+h+i+j+k+l+m+n\n}\n" TAIL;
  CHECK(compile_and_run("fourteen_arguments", wide, NULL) == 105,
        "fourteen bound arguments including nested call execute");
}
static void static_data_is_real_and_executable(void) {
  const char *source = "module static_data\n"
                       "target x86_64-nexora-none\n"
                       "section .rdata {\n"
                       "data answer:u64=42\n"
                       "}\n"
                       "section .text {\n"
                       "export fn main()->u64\n"
                       "effects {reads_mem(user)}\n"
                       "clobbers {rax} {\n"
                       "load rax,[answer]:ptr<user,u64>\n"
                       "return rax\n"
                       "}\n}\n";
  NtCodeImage image = {0};
  CHECK(compile_and_run("static_data", source, &image) == 42,
        "RIP-relative static data load executes");
  CHECK(image.rdata.size == 8 && image.data.size == 0,
        "read-only data is emitted outside executable text");
  if (image.rdata.size == 8) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; i++)
      value |= (uint64_t)image.rdata.bytes[i] << (8 * i);
    CHECK(value == 42,
          "static data bytes match independent little-endian value");
  }
  nt_code_image_free(&image);
  const char *writable_source =
      "module writable\n"
      "target x86_64-nexora-none\n"
      "section .data {\ndata counter:u64=40\n}\n"
      "section .text {\n"
      "export fn main()->u64\n"
      "effects {reads_mem(user),writes_mem(user),changes_flags}\n"
      "clobbers {rax,rflags} {\n"
      "load rax,[counter]:ptr<user,u64>\n"
      "add rax,2\n"
      "store [counter]:ptr<user,u64>,rax\n"
      "load rax,[counter]:ptr<user,u64>\n"
      "return rax\n}\n}\n";
  CHECK(compile_and_run("writable_data", writable_source, NULL) == 42,
        "writable data remains non-executable and stores execute");
  const char *string_source = "module strings\n"
                              "target x86_64-nexora-none\n"
                              "section .rdata {\n"
                              "data message:array<u8,4> =\"A\\nB\\0\"\n"
                              "}\n"
                              "section .text {\n"
                              "export fn main()->u64\n"
                              "effects {}\nclobbers {} {\nreturn 0\n}\n}\n";
  NtCodeImage string_image = {0};
  CHECK(compile_and_run("string_data", string_source, &string_image) == 0,
        "string data module compiles and executes");
  const uint8_t expected[] = {0x41, 0x0a, 0x42, 0x00};
  CHECK(string_image.rdata.size == sizeof expected &&
            !memcmp(string_image.rdata.bytes, expected, sizeof expected),
        "string escapes lower to exact data bytes");
  nt_code_image_free(&string_image);
}
static void sib_address_lowers_to_encoder(void) {
  const char *source =
      HEAD "export fn main(in base:ptr<user,u64> @r13,in index:u64 @r12)->u64\n"
           "effects {reads_mem(user)}\nclobbers {rax} {\n"
           "load rax,[r13+r12*8-16]:ptr<user,u64>\n"
           "return rax\n}\n" TAIL;
  NtFInput input = {"sib", source, strlen(source)};
  NtFProgram program;
  CHECK(nt_frontend_compile(&input, 1, &program), "SIB source verifies");
  NtCodeImage image = {0};
  NtCodeDiagnostic diagnostic = {0};
  CHECK(nt_codegen_x64(&program, &image, &diagnostic), "SIB source lowers");
  const uint8_t golden[] = {0x4b, 0x8b, 0x44, 0xe5, 0xf0};
  int found = 0;
  for (size_t i = 0; i + sizeof golden <= image.size; i++)
    if (!memcmp(image.bytes + i, golden, sizeof golden))
      found = 1;
  CHECK(found, "generated function contains independent SIB load golden");
  nt_code_image_free(&image);
  nt_frontend_free(&program);
}
static void large_rdata_keeps_data_relocation_canonical(void) {
  const char *prefix = "module large\n"
                       "target x86_64-nexora-none\n"
                       "section .rdata {\n"
                       "data padding:array<u8,4090> =\"";
  const char *suffix = "\"\n}\n"
                       "section .data {\ndata answer:u64=42\n}\n"
                       "section .text {\n"
                       "export fn main()->u64\n"
                       "effects {reads_mem(user)}\nclobbers {rax} {\n"
                       "load rax,[answer]:ptr<user,u64>\nreturn rax\n}\n}\n";
  size_t source_size = strlen(prefix) + 4090 + strlen(suffix);
  char *source = (char *)malloc(source_size + 1);
  CHECK(source != NULL, "large rdata source allocation");
  if (!source)
    return;
  size_t at = 0;
  memcpy(source + at, prefix, strlen(prefix));
  at += strlen(prefix);
  memset(source + at, 'A', 4090);
  at += 4090;
  memcpy(source + at, suffix, strlen(suffix) + 1);
  NtCodeImage image = {0};
  CHECK(compile_and_run("large_rdata", source, &image) == 42,
        "large rdata plus writable data executes");
  size_t instruction = SIZE_MAX;
  for (size_t i = 0; i + 7 <= image.size; i++)
    if (image.bytes[i] == 0x48 && image.bytes[i + 1] == 0x8b &&
        image.bytes[i + 2] == 0x05) {
      instruction = i;
      break;
    }
  CHECK(instruction != SIZE_MAX, "large-data program contains RIP load");
  if (instruction != SIZE_MAX) {
    uint32_t bits = 0;
    for (unsigned i = 0; i < 4; i++)
      bits |= (uint32_t)image.bytes[instruction + 3 + i] << (8 * i);
    int32_t displacement = (int32_t)bits;
    size_t target = (size_t)((int64_t)(instruction + 7) + displacement);
    size_t rdata_base = (image.size + 4095u) & ~(size_t)4095u;
    size_t expected_data =
        (rdata_base + 8 + image.rdata.size + 4095u) & ~(size_t)4095u;
    CHECK(target == expected_data,
          "RIP relocation follows aligned large rdata section");
  }
  nt_code_image_free(&image);
  free(source);
}
static void rejects_unverified_ir(void) {
  const char *source =
      "module bad\ntarget x86_64-nexora-none\nmacro x()->u64 {}\n";
  NtFInput input = {"bad", source, strlen(source)};
  NtFProgram program;
  CHECK(!nt_frontend_compile(&input, 1, &program),
        "negative source stays unverified");
  NtCodeImage image = {0};
  NtCodeDiagnostic diagnostic = {0};
  CHECK(!nt_codegen_x64(&program, &image, &diagnostic) &&
            diagnostic.code == NTCG_E_UNVERIFIED,
        "codegen refuses unverified frontend output");
  CHECK(!image.bytes && !image.size, "failure emits no partial machine code");
  nt_code_image_free(&image);
  nt_frontend_free(&program);
}
static void missing_entry_is_transactional(void) {
  const char *source = HEAD "fn helper()->u64\n"
                            "effects {}\nclobbers {} {\nreturn 1\n}\n" TAIL;
  NtFInput input = {"no_entry", source, strlen(source)};
  NtFProgram program;
  CHECK(nt_frontend_compile(&input, 1, &program), "entry-negative IR verifies");
  NtCodeImage image = {0};
  NtCodeDiagnostic diagnostic = {0};
  CHECK(!nt_codegen_x64(&program, &image, &diagnostic) &&
            diagnostic.code == NTCG_E_ENTRY,
        "missing exported main is rejected");
  CHECK(!image.bytes && !image.rdata.bytes && !image.data.bytes,
        "missing entry emits no buffers");
  nt_code_image_free(&image);
  nt_frontend_free(&program);
}
static void duplicate_entry_is_rejected(void) {
  const char *a =
      "module a\ntarget x86_64-nexora-none\nsection .text {\n"
      "export fn main()->u64\neffects {}\nclobbers {} {\nreturn 1\n}\n}\n";
  const char *b =
      "module b\ntarget x86_64-nexora-none\nsection .text {\n"
      "export fn main()->u64\neffects {}\nclobbers {} {\nreturn 2\n}\n}\n";
  NtFInput inputs[] = {{"a", a, strlen(a)}, {"b", b, strlen(b)}};
  NtFProgram program;
  CHECK(nt_frontend_compile(inputs, 2, &program),
        "duplicate-entry IR verifies structurally");
  NtCodeImage image = {0};
  NtCodeDiagnostic diagnostic = {0};
  CHECK(!nt_codegen_x64(&program, &image, &diagnostic) &&
            diagnostic.code == NTCG_E_ENTRY,
        "multiple exported main functions are rejected");
  CHECK(!image.bytes, "duplicate entry emits no machine code");
  nt_code_image_free(&image);
  nt_frontend_free(&program);
}
static void macro_ast_executes_and_is_hygienic(void) {
  const char *generated_return =
      "module macro_exec\n"
      "target x86_64-nexora-none\n"
      "macro ret(v:ast.expr)->ast.stmt {\n"
      "return quote(stmt) { return $v+1; };\n}\n"
      "section .text {\n"
      "export fn main()->u64\neffects {}\nclobbers {} {\n"
      "expand ret(41)\n}\n}\n";
  const char *hygiene = "module macro_hygiene\n"
                        "target x86_64-nexora-none\n"
                        "macro bind(v:ast.expr)->ast.stmt {\n"
                        "return quote(stmt) { let temp:u64=$v; };\n}\n"
                        "section .text {\n"
                        "export fn main()->u64\neffects {}\nclobbers {} {\n"
                        "let temp:u64=5\n"
                        "expand bind(7)\n"
                        "return temp\n}\n}\n";
  const char *forged_name = "module macro_forgery\n"
                            "target x86_64-nexora-none\n"
                            "macro bind(v:ast.expr)->ast.stmt {\n"
                            "return quote(stmt) { let temp:u64=$v; };\n}\n"
                            "section .text {\n"
                            "export fn main()->u64\neffects {}\nclobbers {} {\n"
                            "let __ntm1_e1_temp:u64=9\n"
                            "expand bind(7)\n"
                            "return __ntm1_e1_temp\n}\n}\n";
  CHECK(compile_and_run("macro_return", generated_return, NULL) == 42,
        "macro-generated return AST executes");
  CHECK(compile_and_run("macro_hygiene", hygiene, NULL) == 5,
        "macro-introduced local cannot capture caller local");
  CHECK(compile_and_run("macro_forgery", forged_name, NULL) == 9,
        "source identifier cannot forge deterministic hygiene identity");
  const char *instruction =
      "module macro_instruction\n"
      "target x86_64-nexora-none\n"
      "macro increment(reg:ast.expr)->ast.stmt {\n"
      "return quote(stmt) { add $reg,1; };\n}\n"
      "section .text {\n"
      "export fn main()->u64\n"
      "effects {changes_flags}\nclobbers {rax,rflags} {\n"
      "mov rax,41\nexpand increment(rax)\nreturn rax\n}\n}\n";
  CHECK(compile_and_run("macro_instruction", instruction, NULL) == 42,
        "macro-generated instruction AST executes");
  const char *block =
      "module macro_block\n"
      "target x86_64-nexora-none\n"
      "macro finish(v:ast.expr)->ast.block {\n"
      "return quote(block) {\nlet temp:u64=$v;\nreturn temp+1;\n};\n}\n"
      "section .text {\n"
      "export fn main()->u64\neffects {}\nclobbers {} {\n"
      "expand finish(41)\n}\n}\n";
  CHECK(compile_and_run("macro_block", block, NULL) == 42,
        "macro-generated AST block executes");
}
static void stack_contract_prevents_leaf_misalignment(void) {
  const char *source =
      HEAD "export fn main()->u64\n"
           "effects {}\nclobbers {} {\nreturn helper()\n}\n"
           "fn helper()->u64\n"
           "effects {}\nclobbers {}\n"
           "requires { stack_aligned(16) } {\nreturn 42\n}\n" TAIL;
  NtCodeImage image = {0};
  CHECK(compile_and_run("stack_contract_leaf", source, &image) == 42,
        "stack-contracted helper executes");
  CHECK(image.size && image.bytes[0] == 0x9c,
        "stack-contracted literal helper receives aligned frame prologue");
  nt_code_image_free(&image);
}
static void raw_label_cfg_executes(void) {
  const char *source = HEAD "export fn main()->u64\n"
                            "effects {changes_flags}\nclobbers {rax,rflags} {\n"
                            "mov rax,40\nadd rax,2\ncmp rax,42\nje done\n"
                            "return 0\ndone:\nreturn rax\n}\n" TAIL;
  CHECK(compile_and_run("raw_label_cfg", source, NULL) == 42,
        "raw local label and conditional rel32 branch execute");
}
static void macro_generates_256_entry_block(void) {
  const char *prefix =
      "module macro_table\n"
      "target x86_64-nexora-none\n"
      "macro table(v:ast.expr)->ast.block {\nreturn quote(block) {\n";
  const char *row = "let slot:u64=$v;\n";
  const char *suffix = "return $v;\n};\n}\nsection .text {\n"
                       "export fn main()->u64\neffects {}\nclobbers {} {\n"
                       "expand table(42)\n}\n}\n";
  size_t length = strlen(prefix) + 256 * strlen(row) + strlen(suffix);
  char *source = (char *)malloc(length + 1);
  CHECK(source != NULL, "256-entry macro source allocation");
  if (!source)
    return;
  size_t at = 0;
  memcpy(source + at, prefix, strlen(prefix));
  at += strlen(prefix);
  for (unsigned i = 0; i < 256; i++) {
    memcpy(source + at, row, strlen(row));
    at += strlen(row);
  }
  memcpy(source + at, suffix, strlen(suffix) + 1);
  CHECK(compile_and_run("macro_table_256", source, NULL) == 42,
        "256-entry hygienic macro block compiles and executes");
  free(source);
}
static void nested_macro_executes(void) {
  const char *source = "module nested_macro\ntarget x86_64-nexora-none\n"
                       "macro inner(v:ast.expr)->ast.stmt {\n"
                       "return quote(stmt) { return $v+1; };\n}\n"
                       "macro outer(v:ast.expr)->ast.block {\n"
                       "return quote(block) { expand inner($v); };\n}\n"
                       "section .text {\n"
                       "export fn main()->u64\neffects {}\nclobbers {} {\n"
                       "expand outer(41)\n}\n}\n";
  CHECK(compile_and_run("nested_macro", source, NULL) == 42,
        "nested AST macro call executes");
}
static void macro_list_emit_executes(void) {
  const char *source = "module macro_emit\ntarget x86_64-nexora-none\n"
                       "macro pair(v:ast.expr)->list<ast.stmt> {\n"
                       "emit quote(stmt) { let temp:u64=$v; };\n"
                       "emit quote(stmt) { return temp+1; };\n}\n"
                       "section .text {\n"
                       "export fn main()->u64\neffects {}\nclobbers {} {\n"
                       "expand pair(41)\n}\n}\n";
  CHECK(compile_and_run("macro_list_emit", source, NULL) == 42,
        "list<ast.stmt> emit macro executes");
}
static void macro_meta_if_executes(void) {
  const char *true_source = "module macro_if_true\ntarget x86_64-nexora-none\n"
                            "macro choose(flag:ast.expr)->list<ast.stmt> {\n"
                            "if $flag {\nemit quote(stmt) { return 42; };\n"
                            "} else {\nemit quote(stmt) { return 0; };\n}\n}\n"
                            "section .text {\n"
                            "export fn main()->u64\neffects {}\nclobbers {} {\n"
                            "expand choose(true)\n}\n}\n";
  const char *false_source =
      "module macro_if_false\ntarget x86_64-nexora-none\n"
      "macro choose(flag:ast.expr)->list<ast.stmt> {\n"
      "if $flag {\nemit quote(stmt) { return 42; };\n"
      "} else {\nemit quote(stmt) { return 0; };\n}\n}\n"
      "section .text {\n"
      "export fn main()->u64\neffects {}\nclobbers {} {\n"
      "expand choose(false)\n}\n}\n";
  CHECK(compile_and_run("macro_if_true", true_source, NULL) == 42,
        "meta if true branch emits AST");
  CHECK(compile_and_run("macro_if_false", false_source, NULL) == 0,
        "meta if false branch emits AST");
}
static void macro_meta_for_executes(void) {
  const char *source =
      "module macro_for\ntarget x86_64-nexora-none\n"
      "macro rows(n:ast.expr)->list<ast.stmt> {\n"
      "for i in $n {\nemit quote(stmt) { add rax,$i; };\n}\n}\n"
      "section .text {\n"
      "export fn main()->u64\n"
      "effects {changes_flags}\nclobbers {rax,rflags} {\n"
      "mov rax,0\nexpand rows(4)\nreturn rax\n}\n}\n";
  CHECK(compile_and_run("macro_meta_for", source, NULL) == 6,
        "meta for emits four AST instructions");
}
static void macro_type_decl_executes(void) {
  const char *source =
      "module macro_decl\ntarget x86_64-nexora-none\n"
      "macro make(T:ast.type,v:ast.expr)->ast.decl {\n"
      "return quote(decl) { export data answer:$T=$v; };\n}\n"
      "section .rdata {\n"
      "expand make(quote(type) { u64 },42)\n}\n"
      "section .text {\n"
      "export fn main()->u64\neffects {reads_mem(user)}\nclobbers {rax} {\n"
      "load rax,[answer]:ptr<user,u64>\nreturn rax\n}\n}\n";
  CHECK(compile_and_run("macro_type_decl", source, NULL) == 42,
        "ast.type splice generates executable ast.decl data");
}
static void macro_decl_cross_reference_executes(void) {
  const char *source =
      "module macro_decls\ntarget x86_64-nexora-none\n"
      "macro make(T:ast.type,v:ast.expr)->list<ast.decl> {\n"
      "emit quote(decl) { const base:$T=$v; };\n"
      "emit quote(decl) { export data answer:$T=base+1; };\n}\n"
      "section .rdata {\n"
      "expand make(quote(type) { u64 },41)\n}\n"
      "section .text {\n"
      "export fn main()->u64\neffects {reads_mem(user)}\nclobbers {rax} {\n"
      "load rax,[answer]:ptr<user,u64>\nreturn rax\n}\n}\n";
  CHECK(compile_and_run("macro_decl_crossref", source, NULL) == 42,
        "hygienic generated declaration cross-reference executes");
}
static void codegen_metadata_is_typed(void) {
  const char *source =
      "module metadata\ntarget x86_64-nexora-none\n"
      "section .rdata {\ndata answer:u64=42\n}\n"
      "section .text {\n"
      "export fn main()->u64\neffects {reads_mem(user)}\nclobbers {rax} {\n"
      "return helper()\n}\n"
      "fn helper()->u64\neffects {reads_mem(user)}\nclobbers {rax} {\n"
      "load rax,[answer]:ptr<user,u64>\nreturn rax\n}\n}\n";
  NtCodeImage image = {0};
  CHECK(compile_and_run("typed_metadata", source, &image) == 42,
        "metadata fixture executes");
  unsigned functions = 0, variables = 0, call_reloc = 0, data_reloc = 0;
  for (size_t i = 0; i < image.symbol_count; i++) {
    functions += image.symbols[i].function_id != 0;
    variables += image.symbols[i].variable_id != 0;
  }
  for (size_t i = 0; i < image.relocation_count; i++) {
    call_reloc += image.relocations[i].kind == NT_CODE_RELOC_REL32 &&
                  image.relocations[i].target_function != 0;
    data_reloc += image.relocations[i].kind == NT_CODE_RELOC_REL32 &&
                  image.relocations[i].target_variable != 0;
  }
  CHECK(functions == 2 && variables == 1,
        "typed symbol records cover functions and data");
  CHECK(call_reloc == 1 && data_reloc == 1,
        "typed REL32 records preserve call and data targets");
  nt_code_image_free(&image);
}
static void structured_stub_executes(void) {
  const char *source =
      "module stubs\ntarget x86_64-nexora-none\n"
      "stub_table routes kind syscall {\n"
      "stub 0 {\nhandler dispatch\nentry_abi nx64-abi-v0\n"
      "exit_abi nx64-abi-v0\nexit return\nframe syscall.frame\n"
      "bindings {}\nhardware_error_code false\nsave {}\nclobbers {}\n"
      "stack_align 16\nswapgs never\ninterrupts preserve\n"
      "section .text\nvisibility export\nsymbol main\n}\n}\n"
      "section .text {\n"
      "fn dispatch()->u64\neffects {}\nclobbers {} {\nreturn 42\n}\n}\n";
  CHECK(compile_and_run("structured_stub", source, NULL) == 42,
        "structured safe-return syscall stub executes handler");
}
static void structured_stub_table_256(void) {
  const char *prefix = "module table256\ntarget x86_64-nexora-none\n"
                       "stub_table routes kind syscall {\n";
  const char *tail =
      "}\nsection .text {\n"
      "fn dispatch()->u64\neffects {}\nclobbers {} {\nreturn 42\n}\n}\n";
  size_t capacity = strlen(prefix) + strlen(tail) + 256 * 360 + 1;
  char *source = (char *)malloc(capacity);
  CHECK(source != NULL, "stub table source allocation");
  if (!source)
    return;
  size_t at = 0;
  memcpy(source + at, prefix, strlen(prefix));
  at += strlen(prefix);
  for (unsigned i = 0; i < 256; i++) {
    int written = snprintf(
        source + at, capacity - at,
        "stub %u {\nhandler dispatch\nentry_abi nx64-abi-v0\n"
        "exit_abi nx64-abi-v0\nexit return\nframe syscall.frame\n"
        "bindings {}\nhardware_error_code false\nsave {}\nclobbers {}\n"
        "stack_align 16\nswapgs never\ninterrupts preserve\nsection .text\n"
        "visibility %s\nsymbol %s%u\n}\n",
        i, i ? "local" : "export", i ? "stub" : "main", i ? i : 0);
    if (written < 0 || (size_t)written >= capacity - at) {
      free(source);
      CHECK(0, "stub table source capacity");
      return;
    }
    at += (size_t)written;
  }
  memcpy(source + at, tail, strlen(tail) + 1);
  /* The first symbol above is main0; canonical entry requires exactly main.
   * Rewrite its trailing digit without changing the rest of generated text. */
  char *main0 = strstr(source, "symbol main0");
  CHECK(main0 != NULL, "generated main stub marker");
  if (main0)
    memmove(main0 + 11, main0 + 12, strlen(main0 + 12) + 1);
  CHECK(compile_and_run("stub_table_256", source, NULL) == 42,
        "256-entry structured safe stub table executes");
  free(source);
}
static void machine_register_semantics(void) {
  const char *narrow = HEAD "export fn main()->u64\n"
                            "effects {}\nclobbers {rax} {\n"
                            "mov rax,4660\nmov al,42\nreturn rax\n}\n" TAIL;
  const char *independent = HEAD "export fn main()->u64\n"
                                 "effects {}\nclobbers {rax,rcx} {\n"
                                 "mov rax,42\nmov rcx,7\nreturn rax\n}\n" TAIL;
  const char *high_byte = HEAD "export fn main()->u8\n"
                               "effects {}\nclobbers {rax} {\n"
                               "mov rax,10752\nreturn ah\n}\n" TAIL;
  CHECK(compile_and_run("narrow_register_write", narrow, NULL) == 4650,
        "mov al preserves upper bytes of rax");
  CHECK(compile_and_run("independent_register_write", independent, NULL) == 42,
        "mov rcx immediate leaves rax unchanged");
  CHECK(compile_and_run("high_byte_read", high_byte, NULL) == 42,
        "explicit ah expression reads bits 8 through 15");
}
static void macro_function_declaration_executes(void) {
  const char *source = "module function_macro\ntarget x86_64-nexora-none\n"
                       "macro make(v:ast.expr)->ast.decl {\n"
                       "return quote(decl) { export fn helper()->u64\n"
                       "effects {}\nclobbers {} {\nreturn $v+1;\n}\n};\n}\n"
                       "section .text {\nexpand make(41)\n"
                       "export fn main()->u64\neffects {}\nclobbers {} "
                       "{\nreturn helper()\n}\n}\n";
  NtCodeImage image = {0};
  CHECK(compile_and_run("generated_function", source, &image) == 42,
        "macro-generated exported function executes via typed call");
  unsigned found = 0;
  for (size_t i = 0; i < image.symbol_count; i++)
    if (!strcmp(image.symbols[i].name, "helper") &&
        image.symbols[i].function_id && image.symbols[i].size)
      found++;
  CHECK(found == 1, "generated helper exports one typed function symbol");
  nt_code_image_free(&image);
}
static void expression_temporaries_are_transparent(void) {
  const char *sum =
      HEAD "export fn main()->u64\neffects {}\nclobbers {rax,rcx} {\n"
           "mov rax,22\nmov rcx,20\nreturn rcx+rax\n}\n" TAIL;
  const char *local =
      HEAD "export fn main()->u64\neffects {}\nclobbers {rax} {\n"
           "mov rax,42\nlet x:u64=rax+1\nreturn rax\n}\n" TAIL;
  const char *flags = HEAD "export fn main()->u64\neffects "
                           "{changes_flags}\nclobbers {rax,rflags} {\n"
                           "mov rax,42\ncmp rax,42\nlet x:u64=rax+1\nje "
                           "done\nreturn 0\ndone:\nreturn 42\n}\n" TAIL;
  const char *clobber =
      HEAD "export fn main()->u64\neffects {}\nclobbers {rcx} {\n"
           "mov rcx,10\nlet result:u64=change()\nreturn rcx\n}\n"
           "fn change()->u64\neffects {}\nclobbers {rcx} {\nmov rcx,42\nreturn "
           "0\n}\n" TAIL;
  const char *left_call =
      HEAD "export fn main()->u64\neffects {}\nclobbers {rax} {\n"
           "mov rax,40\nreturn f()+rax\n}\n"
           "fn f()->u64\neffects {}\nclobbers {} {\nreturn 2\n}\n" TAIL;
  const char *right_call =
      HEAD "export fn main()->u64\neffects {}\nclobbers {rax} {\n"
           "mov rax,40\nreturn rax+f()\n}\n"
           "fn f()->u64\neffects {}\nclobbers {} {\nreturn 2\n}\n" TAIL;
  CHECK(compile_and_run("register_expression", sum, NULL) == 42,
        "binary register inputs snapshot expression entry");
  CHECK(compile_and_run("local_transparency", local, NULL) == 42,
        "let temporary does not change live rax");
  CHECK(compile_and_run("flags_transparency", flags, NULL) == 42,
        "let preserves user comparison flags");
  CHECK(compile_and_run("real_callee_clobber", clobber, NULL) == 42,
        "declared callee clobber remains observable");
  CHECK(compile_and_run("call_then_register", left_call, NULL) == 42,
        "f()+rax uses entry snapshot and call result");
  CHECK(compile_and_run("register_then_call", right_call, NULL) == 42,
        "rax+f() uses entry snapshot and call result");
}
static void symbolic_memory_immediates_execute(void) {
  const char *source =
      "module immediate_data\ntarget x86_64-nexora-none\n"
      "section .data {\ndata answer:u64=0\n}\nsection .text {\n"
      "export fn main()->u64\neffects "
      "{reads_mem(user),writes_mem(user),changes_flags}\n"
      "clobbers {rax,rflags} {\nstore [answer]:ptr<user,u64>,40\n"
      "add [answer]:ptr<user,u64>,2\nload rax,[answer]:ptr<user,u64>\nreturn "
      "rax\n}\n}\n";
  CHECK(
      compile_and_run("rip_immediate", source, NULL) == 42,
      "RIP memory immediates relocate displacement before trailing immediate");
  const char *signed_source =
      "module signed_data\ntarget x86_64-nexora-none\n"
      "const NEG:i64=-1\nsection .data {\ndata answer:i64=0\n}\nsection .text "
      "{\n"
      "export fn main()->u64\neffects {reads_mem(user),writes_mem(user)}\n"
      "clobbers {rax} {\nstore [answer]:ptr<user,i64>,NEG\n"
      "load rax,[answer]:ptr<user,i64>\nreturn rax\n}\n}\n";
  CHECK(compile_and_run("signed_memory_immediate", signed_source, NULL) ==
            UINT64_MAX,
        "named negative constant retains signed immediate encoding");
}
static void unsigned_word_normalization(void) {
  const char *source = HEAD "export fn main()->u32\neffects {}\nclobbers {} "
                            "{\nreturn identity(4294967295)\n}\n"
                            "fn identity(in n:u32 @ecx)->u32\neffects "
                            "{}\nclobbers {} {\nreturn n\n}\n" TAIL;
  CHECK(compile_and_run("u32_runtime", source, NULL) == UINT32_MAX,
        "u32 runtime normalization uses zero extension without signed "
        "immediate restriction");
}
static void named_instruction_operand_executes(void) {
  const char *source =
      HEAD "export fn main()->u64\neffects {}\nclobbers {rax,rcx} {\n"
           "mov rax,4660\nlet value:u8=42\nmov ah,value\nmov rcx,rax\nreturn "
           "rcx\n}\n" TAIL;
  CHECK(compile_and_run("named_instruction_operand", source, NULL) == 10804,
        "named scalar mov evaluates transparently and writes only AH byte");
}
static void machine_flags_and_forms_execute(void) {
  const char *inc_adc =
      HEAD "export fn main()->u64\neffects {changes_flags}\nclobbers "
           "{rax,rflags} {\n"
           "mov rax,40\ncmp rax,41\ninc rax\nadc rax,0\nreturn rax\n}\n" TAIL;
  const char *conditional = HEAD
      "export fn main()->u64\neffects {changes_flags}\nclobbers "
      "{rax,rcx,rflags} {\n"
      "mov rax,1\nmov rcx,42\ncmp rcx,42\ncmove rax,rcx\nreturn rax\n}\n" TAIL;
  const char *set =
      HEAD "export fn main()->u64\neffects {changes_flags}\nclobbers "
           "{rax,rflags} {\n"
           "mov rax,0\ncmp rax,1\nsetb al\nmovzx eax,al\nreturn rax\n}\n" TAIL;
  const char *bits = HEAD "export fn main()->u64\neffects "
                          "{changes_flags}\nclobbers {rax,rflags} {\n"
                          "mov rax,42\nbtr rax,1\nbtc rax,1\nbswap rax\nbswap "
                          "rax\nreturn rax\n}\n" TAIL;
  CHECK(compile_and_run("inc_adc", inc_adc, NULL) == 42,
        "INC preserves carry for ADC and uses valid opcode");
  CHECK(compile_and_run("conditional_move", conditional, NULL) == 42,
        "CMOVE reads established comparison flags");
  CHECK(compile_and_run("set_condition", set, NULL) == 1,
        "SETB and zero extension execute");
  CHECK(compile_and_run("bit_operations", bits, NULL) == 42,
        "bit clear/complement and byte swaps execute");
}
static void high_byte_parameter_binding(void) {
  const char *live = HEAD
      "export fn main()->u8\neffects {}\nclobbers {} {\nreturn bound(42)\n}\n"
      "fn bound(in x:u8 @ah)->u8\neffects {}\nclobbers {} {\nreturn "
      "ah\n}\n" TAIL;
  const char *named = HEAD
      "export fn main()->u8\neffects {}\nclobbers {} {\nreturn bound(42)\n}\n"
      "fn bound(in x:u8 @ah)->u8\neffects {}\nclobbers {} {\nreturn "
      "x\n}\n" TAIL;
  CHECK(compile_and_run("high_byte_bound_live", live, NULL) == 42,
        "AH argument binding reaches actual AH register");
  CHECK(compile_and_run("high_byte_bound_named", named, NULL) == 42,
        "AH parameter snapshot reads its high byte");
}
static void lexical_block_keeps_frame_requirements(void) {
  const char *source = HEAD "export fn main()->u64\neffects {}\nclobbers {} {\n"
                            "return id(id(id(42)))\n}\n"
                            "fn id(in x:u64 @rcx)->u64\neffects {}\nclobbers "
                            "{} {\nreturn x\n}\n" TAIL;
  NtFInput input = {"nested_block_frame", source, strlen(source)};
  NtFProgram p;
  CHECK(nt_frontend_compile(&input, 1, &p), "nested call program verifies");
  NtCodeImage original = {0}, wrapped = {0};
  NtCodeDiagnostic diagnostic;
  int first = nt_codegen_x64(&p, &original, &diagnostic);
  CHECK(first, "original nested calls lower");
  NtFId main_body = 0;
  for (size_t i = 0; i < p.function_count; i++)
    if (!strcmp(p.functions[i].name, "main"))
      main_body = p.functions[i].body;
  NtFStmt *grown =
      realloc(p.statements, (p.statement_count + 1) * sizeof(*grown));
  CHECK(grown != NULL && main_body, "extra lexical block allocation");
  if (grown && main_body) {
    p.statements = grown;
    NtFStmt block = {0};
    block.kind = NTF_S_BLOCK;
    block.first = p.statements[main_body - 1].first;
    block.span = p.statements[main_body - 1].span;
    p.statements[p.statement_count++] = block;
    p.statements[main_body - 1].first = (NtFId)p.statement_count;
    int second = nt_codegen_x64(&p, &wrapped, &diagnostic);
    CHECK(second, "wrapped nested calls lower");
    CHECK(first && second && original.size == wrapped.size &&
              !memcmp(original.bytes, wrapped.bytes, original.size),
          "empty lexical scope does not change nested call frame layout");
    if (first && second && original.size == wrapped.size)
      CHECK(execute(&wrapped) == 42, "nested block call frame executes");
  }
  nt_code_image_free(&original);
  nt_code_image_free(&wrapped);
  nt_frontend_free(&p);
}
static void raw_interrupt_stub_is_exact_and_guarded(void) {
  const char *source = "module interrupts\ntarget x86_64-nexora-none\n"
                       "stub_table faults kind interrupt {\nstub 6 {\nhandler "
                       "advance_saved_rip_2\n"
                       "entry_abi nx64-interrupt-same-cpl-v0\nexit_abi "
                       "nx64-iretq-same-cpl-v0\nexit iretq\n"
                       "frame x86_64.frame.no_error.same_cpl\nbindings "
                       "{}\nhardware_error_code false\n"
                       "save {rax}\nclobbers {}\nstack_align 0\nswapgs "
                       "never\ninterrupts preserve\n"
                       "section .text\nvisibility export\nsymbol isr6\n}\n}\n"
                       "section .text {\nexport fn main()->u64\neffects "
                       "{}\nclobbers {} {\nreturn 42\n}\n}\n";
  NtFInput input = {"raw_interrupt_stub", source, strlen(source)};
  NtFProgram p;
  int front = nt_frontend_compile(&input, 1, &p);
  CHECK(front, "structured interrupt source verifies");
  if (!front) {
    nt_frontend_free(&p);
    return;
  }
  NtFId stub = 0;
  for (size_t i = 0; i < p.function_count; i++)
    if (p.functions[i].generated_stub)
      stub = (NtFId)(i + 1);
  CHECK(stub != 0, "structured declaration tags its generated raw stub");
  NtCodeImage image = {0};
  NtCodeDiagnostic d = {0};
  int ok = nt_codegen_x64(&p, &image, &d);
  CHECK(ok, "verified raw interrupt stub lowers");
  static const uint8_t golden[] = {0x50, 0x48, 0x8b, 0x44, 0x24, 0x08,
                                   0x48, 0x83, 0xc0, 0x02, 0x48, 0x89,
                                   0x44, 0x24, 0x08, 0x58, 0x48, 0xcf};
  unsigned found = 0;
  for (size_t i = 0; i < image.symbol_count; i++)
    if (!strcmp(image.symbols[i].name, "isr6")) {
      NtCodeSymbol *symbol = &image.symbols[i];
      found++;
      CHECK(symbol->size == sizeof(golden) &&
                !memcmp(image.bytes + symbol->offset, golden, sizeof(golden)),
            "raw interrupt stub is exact18 bytes with no normal prologue");
      if (symbol->size != sizeof(golden) ||
          memcmp(image.bytes + symbol->offset, golden, sizeof(golden))) {
        fprintf(stderr, " RAW_STUB size=%zu bytes=", symbol->size);
        for (size_t j = 0; j < symbol->size; j++)
          fprintf(stderr, "%02x", image.bytes[symbol->offset + j]);
        fputc('\n', stderr);
      }
    }
  CHECK(found == 1, "raw stub exports one typed function symbol");
  nt_code_image_free(&image);
  if (stub) {
    NtFFunction *f = &p.functions[stub - 1];
    NtFFunction *normal_main = NULL;
    for (size_t i = 0; i < p.function_count; i++)
      if (!strcmp(p.functions[i].name, "main"))
        normal_main = &p.functions[i];
    if (normal_main) {
      const char *old_stub = f->name, *old_main = normal_main->name;
      f->name = "main";
      normal_main->name = "ordinary_entry";
      CHECK(!nt_codegen_x64(&p, &image, &d) && d.code == NTCG_E_ENTRY &&
                !image.bytes,
            "interrupt ABI cannot impersonate a normal executable entry");
      nt_code_image_free(&image);
      f->name = old_stub;
      normal_main->name = old_main;
    }
    f->stub_vector = 7;
    CHECK(!nt_codegen_x64(&p, &image, &d) && d.code == NTCG_E_UNSUPPORTED &&
              !image.bytes,
          "raw stub vector mutation is rejected transactionally");
    nt_code_image_free(&image);
    f->stub_vector = 6;
    f->generated_stub = 0;
    CHECK(!nt_codegen_x64(&p, &image, &d) && d.code == NTCG_E_UNSUPPORTED &&
              !image.bytes,
          "ordinary function cannot impersonate the raw interrupt ABI");
    nt_code_image_free(&image);
    f->generated_stub = 1;
    f->clobbers = 1;
    CHECK(!nt_codegen_x64(&p, &image, &d) && d.code == NTCG_E_UNSUPPORTED &&
              !image.bytes,
          "raw stub rejects a mismatched preservation contract");
    nt_code_image_free(&image);
    f->clobbers = 0;
    NtFId statement = p.statements[f->body - 1].first;
    for (unsigned i = 0; i < 2; i++)
      statement = p.statements[statement - 1].next;
    NtFId amount =
        p.expressions[p.statements[statement - 1].expression - 1].next;
    p.expressions[amount - 1].value = 3;
    CHECK(!nt_codegen_x64(&p, &image, &d) && d.code == NTCG_E_UNSUPPORTED &&
              !image.bytes,
          "raw stub rejects changed saved-RIP advancement");
    nt_code_image_free(&image);
  }
  nt_frontend_free(&p);
}
static void high_level_mode_is_explicit_and_small(void) {
  const char *source = HEAD "export fn main()->u64\neffects {}\nclobbers {} {\n"
                            "let a:u64=20\nreturn id(a)+id(22)\n}\n"
                            "fn id(in x:u64 @rcx)->u64\neffects {}\nclobbers "
                            "{} {\nreturn x\n}\n" TAIL;
  NtFInput input = {"high_level_mode", source, strlen(source)};
  NtFProgram p;
  CHECK(nt_frontend_compile(&input, 1, &p), "mode fixture frontend");
  NtCodeImage normal = {0}, fast = {0};
  NtCodeDiagnostic d;
  int a = nt_codegen_x64(&p, &normal, &d);
  CHECK(a, "default machine semantics lower");
  for (size_t i = 0; i < p.function_count; i++)
    p.functions[i].high_level_expressions = 1;
  int b = nt_codegen_x64(&p, &fast, &d);
  CHECK(b, "explicit high-level mode lowers");
  if (a && b) {
    CHECK(execute(&normal) == 42 && execute(&fast) == 42,
          "both expression modes retain named-value and call results");
    CHECK(fast.size * 2 < normal.size,
          "high-level mode removes bulk machine snapshots");
    static const uint8_t prefix[] = {0x41, 0x57, 0x48, 0x83, 0xec, 0x20, 0x49,
                                     0xbf, 77,   0,    0,    0,    0,    0,
                                     0,    0,    0xe8, 0,    0,    0,    0};
    static const uint8_t suffix[] = {0x48, 0x83, 0xf8, 42,   0x0f, 0x94, 0xc2,
                                     0x49, 0x83, 0xff, 77,   0x0f, 0x94, 0xc0,
                                     0x20, 0xd0, 0x0f, 0xb6, 0xc0, 0x48, 0x83,
                                     0xc4, 0x20, 0x41, 0x5f, 0xc3};
    NtCodeImage wrapper = {0};
    wrapper.size = sizeof(prefix) + sizeof(suffix) + fast.size;
    wrapper.bytes = calloc(wrapper.size, 1);
    CHECK(wrapper.bytes, "R15 ABI witness allocation");
    if (wrapper.bytes) {
      memcpy(wrapper.bytes, prefix, sizeof(prefix));
      memcpy(wrapper.bytes + sizeof(prefix), suffix, sizeof(suffix));
      memcpy(wrapper.bytes + sizeof(prefix) + sizeof(suffix), fast.bytes,
             fast.size);
      int32_t delta =
          (int32_t)(sizeof(prefix) + sizeof(suffix) + fast.entry - 21);
      for (unsigned i = 0; i < 4; i++)
        wrapper.bytes[17 + i] = (uint8_t)((uint32_t)delta >> (8 * i));
      CHECK(execute(&wrapper) == 1,
            "high-level mode preserves private R15 context and return result");
      free(wrapper.bytes);
    }
  }
  nt_code_image_free(&normal);
  nt_code_image_free(&fast);
  nt_frontend_free(&p);
}
static void raw_gp_error_stub_is_guarded(void) {
  const char *source =
      "module gp\ntarget x86_64-nexora-none\nstub_table faults kind interrupt "
      "{\nstub 13 {\n"
      "handler advance_saved_rip_2\nentry_abi "
      "nx64-interrupt-same-cpl-error-v0\n"
      "exit_abi nx64-iretq-same-cpl-error-v0\nexit iretq\nframe "
      "x86_64.frame.error.same_cpl\n"
      "bindings {}\nhardware_error_code true\nsave {rax}\nclobbers "
      "{}\nstack_align 0\nswapgs never\ninterrupts preserve\n"
      "section .text\nvisibility export\nsymbol isr13\n}\n}\nsection .text {\n"
      "export fn main()->u64\neffects {}\nclobbers {} {\nreturn 42\n}\n}\n";
  NtFInput input = {"gp_stub", source, strlen(source)};
  NtFProgram p;
  NtCodeImage image = {0};
  NtCodeDiagnostic d;
  int front = nt_frontend_compile(&input, 1, &p);
  CHECK(front, "GP source verifies");
  if (!front) {
    nt_frontend_free(&p);
    return;
  }
  int ok = nt_codegen_x64(&p, &image, &d);
  CHECK(ok, "GP raw profile lowers");
  static const uint8_t golden[] = {
      0x50, 0x48, 0x8b, 0x44, 0x24, 0x10, 0x48, 0x83, 0xc0, 2,    0x48,
      0x89, 0x44, 0x24, 0x10, 0x58, 0x48, 0x83, 0xc4, 8,    0x48, 0xcf};
  unsigned found = 0;
  for (size_t i = 0; i < image.symbol_count; i++)
    if (!strcmp(image.symbols[i].name, "isr13")) {
      NtCodeSymbol *s = &image.symbols[i];
      found++;
      CHECK(s->size == sizeof(golden) &&
                !memcmp(image.bytes + s->offset, golden, sizeof(golden)),
            "GP profile is exact22 bytes including error discard");
    }
  CHECK(found == 1, "GP raw symbol is exported");
  nt_code_image_free(&image);
  for (size_t i = 0; i < p.function_count; i++)
    if (p.functions[i].generated_stub == 2) {
      NtFFunction *f = &p.functions[i];
      NtFId load = p.statements[p.statements[f->body - 1].first - 1].next;
      NtFId mem = p.expressions[p.statements[load - 1].expression - 1].next;
      p.expressions[mem - 1].memory.displacement = 8;
      CHECK(!nt_codegen_x64(&p, &image, &d) && d.code == NTCG_E_UNSUPPORTED &&
                !image.bytes,
            "GP rejects UD frame offset");
      nt_code_image_free(&image);
      p.expressions[mem - 1].memory.displacement = 16;
      NtFId discard = p.statements[f->body - 1].first;
      for (unsigned n = 0; n < 5; n++)
        discard = p.statements[discard - 1].next;
      NtFId amount =
          p.expressions[p.statements[discard - 1].expression - 1].next;
      p.expressions[amount - 1].value = 16;
      CHECK(!nt_codegen_x64(&p, &image, &d) && d.code == NTCG_E_UNSUPPORTED &&
                !image.bytes,
            "GP rejects wrong error-code discard size");
      nt_code_image_free(&image);
    }
  nt_frontend_free(&p);
}
int main(void) {
  const char *partial_output =
      HEAD "export fn main()->u64\neffects {}\nclobbers {rcx} {\n"
           "mov rcx,0x1122334455667788\ncall byte_out()\nreturn rcx\n}\n"
           "fn byte_out(out v:u8 @ch)->u64\neffects {}\nclobbers {} {\nmov "
           "rcx,0\nmov ch,42\nreturn 0\n}\n" TAIL;
  CHECK(compile_and_run("partial_output_preservation", partial_output, NULL) ==
            UINT64_C(0x1122334455662a88),
        "narrow OUT preserves every unrelated byte of its register family");
  raw_gp_error_stub_is_guarded();
  const char *pointer_out =
      "module out_pointer\ntarget x86_64-nexora-none\nsection .data {\ndata "
      "answer:u64=42\n}\n"
      "section .text {\nexport fn main()->u64\neffects "
      "{reads_mem(user)}\nclobbers {rax,rcx} {\n"
      "call address()\nload rax,[rcx]:ptr<user,u64>\nreturn rax\n}\n"
      "fn address(out p:ptr<user,u64> @rcx)->u64\neffects {}\nclobbers {} {\n"
      "lea rcx,[answer]:ptr<user,u64>\nreturn 0\n}\n}\n";
  const char *pointer_inout =
      "module inout_pointer\ntarget x86_64-nexora-none\nsection .data {\ndata "
      "a:u64=40\ndata b:u64=42\n}\n"
      "section .text {\nexport fn main()->u64\neffects "
      "{reads_mem(user)}\nclobbers {rax,rcx} {\n"
      "call advance(intrinsic address_of(a))\nload "
      "rax,[rcx]:ptr<user,u64>\nreturn rax\n}\n"
      "fn advance(inout p:ptr<user,u64> @rcx)->u64\neffects {}\nclobbers {} {\n"
      "lea rcx,[rcx+8]:ptr<user,u64>\nreturn 0\n}\n}\n";
  CHECK(compile_and_run("pointer_out", pointer_out, NULL) == 42,
        "OUT pointer retains LEA provenance through call and real dereference");
  CHECK(compile_and_run("pointer_inout", pointer_inout, NULL) == 42,
        "INOUT pointer advances using typed LEA and dereferences");
  const char *out_forward =
      HEAD "export fn main()->u64\neffects {}\nclobbers {rcx} {\ncall "
           "relay()\nreturn rcx\n}\n"
           "fn relay(out v:u64 @rcx)->u64\neffects {}\nclobbers {} {\ncall "
           "produce()\nreturn 0\n}\n"
           "fn produce(out v:u64 @rcx)->u64\neffects {}\nclobbers {} {\nmov "
           "ecx,42\nreturn 0\n}\n" TAIL;
  const char *out_high =
      HEAD "export fn main()->u8\neffects {}\nclobbers {rcx} {\ncall "
           "produce()\nreturn ch\n}\n"
           "fn produce(out v:u8 @ch)->u64\neffects {}\nclobbers {} {\nmov "
           "ch,42\nreturn 0\n}\n" TAIL;
  const char *out_branches =
      HEAD "export fn main()->u64\neffects {}\nclobbers {rcx} {\ncall "
           "choose(0)\nreturn rcx\n}\n"
           "fn choose(in n:u64 @rdx,out v:u64 @rcx)->u64\neffects {}\nclobbers "
           "{} {\n"
           "if n==0 {\nmov rcx,42\n} else {\nmov rcx,43\n}\nreturn 0\n}\n" TAIL;
  CHECK(compile_and_run("out_forward", out_forward, NULL) == 42,
        "OUT forwarding and ECX zero-extension preserve complete RCX output");
  CHECK(compile_and_run("out_high_byte", out_high, NULL) == 42,
        "OUT CH writes and returns the actual high byte");
  CHECK(compile_and_run("out_all_paths", out_branches, NULL) == 42,
        "both structured branches establish OUT before return");
  const char *output_source =
      HEAD "export fn main()->u64\neffects {}\nclobbers {rcx} {\n"
           "call produce()\nreturn rcx\n}\n"
           "fn produce(out value:u64 @rcx)->u64\neffects {}\nclobbers {} "
           "{\nmov rcx,42\nreturn 0\n}\n" TAIL;
  const char *inout_source = HEAD
      "export fn main()->u64\neffects {changes_flags}\nclobbers {rcx,rflags} "
      "{\n"
      "call bump(41)\nreturn rcx\n}\n"
      "fn bump(inout value:u64 @rcx)->u64\neffects {changes_flags}\nclobbers "
      "{rflags} {\nadd rcx,1\nreturn value\n}\n" TAIL;
  CHECK(compile_and_run("out_register", output_source, NULL) == 42,
        "OUT consumes no argument and returns actual bound register");
  CHECK(compile_and_run("inout_register", inout_source, NULL) == 42,
        "INOUT receives argument and exposes updated live binding");
  high_level_mode_is_explicit_and_small();
  raw_interrupt_stub_is_exact_and_guarded();
  const char *address_alias =
      "module address_alias\ntarget x86_64-nexora-none\n"
      "section .data {\ndata answer:u64=42\n}\nconst P:ptr<user,u64> = "
      "intrinsic address_of(answer)\n"
      "section .data {\ndata pointer:ptr<user,u64> = P\n}\nsection .text {\n"
      "export fn main()->u64\neffects {reads_mem(user)}\nclobbers {rax} "
      "{\nreturn read(P)\n}\n"
      "fn read(in p:ptr<user,u64> @rcx)->u64\neffects "
      "{reads_mem(user)}\nclobbers {rax} {\n"
      "load rax,[rcx]:ptr<user,u64>\nreturn rax\n}\n}\n";
  CHECK(compile_and_run("symbolic_const_alias", address_alias, NULL) == 42,
        "symbolic pointer constant alias supports runtime and static "
        "initializer");
  const char *address_runtime =
      "module addresses\ntarget x86_64-nexora-none\n"
      "section .data {\ndata answer:u64=42\n}\nsection .text {\n"
      "export fn main()->u64\neffects {reads_mem(user)}\nclobbers {rax} {\n"
      "return read(intrinsic address_of(answer))\n}\n"
      "fn read(in p:ptr<user,u64> @rcx)->u64\neffects "
      "{reads_mem(user)}\nclobbers {rax} {\n"
      "load rax,[rcx]:ptr<user,u64>\nreturn rax\n}\n}\n";
  CHECK(compile_and_run("runtime_symbol_address", address_runtime, NULL) == 42,
        "symbol address is a typed RIP LEA passed to a real memory load");
  const char *address_static =
      "module addresses\ntarget x86_64-nexora-none\n"
      "section .data {\ndata answer:u64=42\ndata pointer:ptr<user,u64> = "
      "intrinsic address_of(answer)\n}\n"
      "section .text {\nexport fn main()->u64\neffects {}\nclobbers {} "
      "{\nreturn 42\n}\n}\n";
  NtCodeImage pointer_image = {0};
  CHECK(compile_and_run("static_symbol_address", address_static,
                        &pointer_image) == 42,
        "symbolic pointer initializer generates a relocatable data slot");
  unsigned pointer_relocations = 0;
  for (size_t i = 0; i < pointer_image.relocation_count; i++) {
    NtCodeReloc *r = &pointer_image.relocations[i];
    if (r->kind == NT_CODE_RELOC_DIR64 &&
        r->source_section == NT_CODE_SECTION_DATA && r->offset == 8 &&
        r->target_variable == 1)
      pointer_relocations++;
  }
  CHECK(pointer_relocations == 1,
        "static pointer retains typed DIR64 target metadata");
  CHECK(pointer_image.data.size == 16 &&
            !memcmp(pointer_image.data.bytes + 8, (uint8_t[8]){0}, 8),
        "absolute pointer slot remains zero until the image materializer runs");
  nt_code_image_free(&pointer_image);
  const char *cfg_loop = HEAD
      "export fn main()->u64\neffects {changes_flags}\nclobbers {rax,rflags} "
      "{\n"
      "mov rax,0\nloop:\ninc rax\ncmp rax,42\njb loop\nreturn rax\n}\n" TAIL;
  const char *cfg_merge =
      HEAD "export fn main()->u64\neffects {changes_flags}\nclobbers "
           "{rax,rflags} {\n"
           "mov rax,0\nif rax==0 {\ncmp rax,0\n} else {\ncmp rax,1\n}\n"
           "je done\nreturn 0\ndone:\nreturn 42\n}\n" TAIL;
  const char *cfg_literal =
      HEAD "export fn main()->u64\neffects {}\nclobbers {} {\n"
           "if true {\nreturn 42\n}\n}\n" TAIL;
  CHECK(compile_and_run("cfg_loop", cfg_loop, NULL) == 42,
        "verified finite CFG loop executes42");
  CHECK(compile_and_run("cfg_merge", cfg_merge, NULL) == 42,
        "flags from both IF arms survive generated branches");
  CHECK(compile_and_run("cfg_literal", cfg_literal, NULL) == 42,
        "literal true branch has no feasible fallthrough");
  lexical_block_keeps_frame_requirements();
  high_byte_parameter_binding();
  machine_flags_and_forms_execute();
  named_instruction_operand_executes();
  unsigned_word_normalization();
  symbolic_memory_immediates_execute();
  expression_temporaries_are_transparent();
  macro_function_declaration_executes();
  machine_register_semantics();
  literal_golden();
  calls_arithmetic_and_forward();
  parameter_entry_snapshot();
  branches_and_values();
  scalar_widths_and_division();
  abi_alignment_and_nested_arguments();
  wide_register_abi_executes();
  static_data_is_real_and_executable();
  sib_address_lowers_to_encoder();
  large_rdata_keeps_data_relocation_canonical();
  rejects_unverified_ir();
  missing_entry_is_transactional();
  duplicate_entry_is_rejected();
  macro_ast_executes_and_is_hygienic();
  stack_contract_prevents_leaf_misalignment();
  raw_label_cfg_executes();
  macro_generates_256_entry_block();
  nested_macro_executes();
  macro_list_emit_executes();
  macro_meta_if_executes();
  macro_meta_for_executes();
  macro_type_decl_executes();
  macro_decl_cross_reference_executes();
  codegen_metadata_is_typed();
  structured_stub_executes();
  structured_stub_table_256();
  printf("CODEGEN_TESTS checks=%u failures=%u\n", checks, failures);
  return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
