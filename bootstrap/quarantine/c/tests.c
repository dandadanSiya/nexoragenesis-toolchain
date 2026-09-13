#define _GNU_SOURCE
#include "ntasm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

static unsigned checks, failures;
#define CHECK(condition, message)                                              \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(condition)) {                                                        \
      ++failures;                                                              \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, message);        \
    }                                                                          \
  } while (0)
static uint32_t u32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
static uint64_t u64(const uint8_t *p) {
  return u32(p) | (uint64_t)u32(p + 4) << 32;
}

static uint64_t execute(const NtArtifact *a) {
  uint64_t result = 0;
  if (!a->text.size)
    return 0;
  size_t rdata_offset = (a->text.size + 4095u) & ~(size_t)4095u;
  size_t data_offset =
      (rdata_offset + 8u + a->rdata.size + 4095u) & ~(size_t)4095u;
  size_t mapping_size =
      a->rdata.size || a->data.size ? data_offset + 4096u : a->text.size;
#ifdef _WIN32
  void *memory = VirtualAlloc(NULL, mapping_size, MEM_COMMIT | MEM_RESERVE,
                              PAGE_READWRITE);
  DWORD prior;
  CHECK(memory != NULL, "executable allocation");
  if (!memory)
    return 0;
  memcpy(memory, a->text.bytes, a->text.size);
  if (a->rdata.size)
    memcpy((uint8_t *)memory + rdata_offset + 8, a->rdata.bytes, a->rdata.size);
  if (a->data.size)
    memcpy((uint8_t *)memory + data_offset, a->data.bytes, a->data.size);
  CHECK(VirtualProtect(memory, mapping_size, PAGE_EXECUTE_READ, &prior),
        "seal executable RX");
  FlushInstructionCache(GetCurrentProcess(), memory, a->text.size);
  result = ((uint64_t (*)(void))((uint8_t *)memory + a->entry))();
  VirtualFree(memory, 0, MEM_RELEASE);
#else
  void *memory = mmap(NULL, mapping_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(memory != MAP_FAILED, "executable allocation");
  if (memory == MAP_FAILED)
    return 0;
  memcpy(memory, a->text.bytes, a->text.size);
  if (a->rdata.size)
    memcpy((uint8_t *)memory + rdata_offset + 8, a->rdata.bytes, a->rdata.size);
  if (a->data.size)
    memcpy((uint8_t *)memory + data_offset, a->data.bytes, a->data.size);
  CHECK(mprotect(memory, mapping_size, PROT_READ | PROT_EXEC) == 0,
        "seal executable RX");
  result = ((uint64_t(__attribute__((ms_abi)) *)(void))((uint8_t *)memory +
                                                        a->entry))();
  munmap(memory, mapping_size);
#endif
  return result;
}

static void return_literal(void) {
  const char *source =
      "module example\ntarget x86_64-nexora-uefi\nsection .text {\nexport fn "
      "main() -> u64\neffects {}\nclobbers {rax} {\nreturn 42;\n}\n}\n";
  const uint8_t expected[] = {0x48, 0xb8, 0x2a, 0, 0, 0, 0, 0, 0, 0, 0xc3};
  NtArtifact a;
  int ok = nt_compile(source, strlen(source), &a);
  CHECK(ok, "compile a complete typed NTASM module returning 42");
  if (ok) {
    CHECK(a.text.size == sizeof(expected) &&
              memcmp(a.text.bytes, expected, sizeof(expected)) == 0,
          "exact x86-64 MOV imm64/RET independent golden");
    CHECK(execute(&a) == 42, "native execution returns 42");
  } else
    fprintf(stderr, "DIAGNOSTIC E%u: %s\n", a.error.code, a.error.message);
  nt_artifact_free(&a);
}
static void pe_container(void) {
  const char *source =
      "module executable\ntarget x86_64-nexora-uefi\nsection .text {\nexport "
      "fn main() -> u64\neffects {}\nclobbers {rax} {\nreturn 43;\n}\n}\n";
  NtArtifact a;
  int ok = nt_compile(source, strlen(source), &a);
  CHECK(ok, "compile PE source");
  if (ok) {
    int made = nt_make_pe(&a);
    CHECK(made, "produce PE32+ application from actual machine code");
    if (made) {
      const uint8_t *pe = a.pe.bytes;
      CHECK(a.pe.size == 3072, "four-section PE expected file size");
      CHECK(pe[0] == 'M' && pe[1] == 'Z' && u32(pe + 60) == 128 &&
                u32(pe + 128) == 0x4550,
            "DOS and PE signatures");
      CHECK(pe[132] == 0x64 && pe[133] == 0x86 && pe[134] == 4 &&
                pe[152] == 0x0b && pe[153] == 2,
            "AMD64 PE32+ four sections");
      CHECK(u32(pe + 168) == 4096 && u64(pe + 176) == UINT64_C(0x140000000),
            "entry RVA and image base");
      CHECK(pe[220] == 10 && pe[221] == 0 && u32(pe + 136) == 0,
            "EFI application with deterministic timestamp");
      CHECK(memcmp(pe + 1024, a.text.bytes, a.text.size) == 0 && pe[1026] == 43,
            "PE contains generated code not a fixed golden");
      CHECK(u32(pe + 304) == 16384 && u32(pe + 308) == 12,
            "base relocation data directory");
      CHECK(u64(pe + 1536) == UINT64_C(0x140001000),
            "absolute anchor references generated entry");
      CHECK(u32(pe + 2560) == 8192 && u32(pe + 2564) == 12 && pe[2568] == 0 &&
                pe[2569] == 0xa0,
            "DIR64 relocation covers anchor");
      for (unsigned i = 0; i < 4; ++i) {
        uint32_t flags = u32(pe + 392 + i * 40 + 36);
        CHECK((flags & UINT32_C(0xa0000000)) != UINT32_C(0xa0000000),
              "no writable executable section");
      }
      NtBuffer original = {0};
      original.bytes = malloc(a.pe.size);
      original.size = a.pe.size;
      memcpy(original.bytes, a.pe.bytes, a.pe.size);
      CHECK(nt_make_pe(&a) && original.size == a.pe.size &&
                !memcmp(original.bytes, a.pe.bytes, a.pe.size),
            "PE emission deterministic on repeat");
      free(original.bytes);
    }
  }
  nt_artifact_free(&a);
}
static void linked_frontend_codegen(void) {
  const char *source =
      "module linked\n"
      "target x86_64-nexora-uefi\n"
      "section .text {\n"
      "export fn main()->u64\n"
      "effects {}\nclobbers {} {\nreturn combine(5,7)\n}\n"
      "fn combine(in a:u64 @rcx,in b:u64 @rdx)->u64\n"
      "effects {}\nclobbers {} {\nlet twice:u64=b*2\nreturn a+twice\n}\n"
      "}\n";
  NtArtifact a;
  int ok = nt_compile(source, strlen(source), &a);
  CHECK(ok, "nt_compile links the typed frontend and x64 backend");
  if (ok) {
    CHECK(a.text.size > 11,
          "multi-function source emits more than literal tracer");
    CHECK(execute(&a) == 19,
          "linked call, parameters, local and arithmetic execute");
    CHECK(nt_make_pe(&a), "linked multi-function program packages as PE");
    if (a.pe.size)
      CHECK(u32(a.pe.bytes + 168) == 4096 + (uint32_t)a.entry,
            "PE entry follows codegen entry offset");
  } else
    fprintf(stderr, "DIAGNOSTIC E%u %zu:%zu %s\n", a.error.code, a.error.line,
            a.error.column, a.error.message);
  nt_artifact_free(&a);
}
static void frontend_failure_is_transactional(void) {
  const char *source = "module bad\n"
                       "target x86_64-nexora-uefi\n"
                       "section .text {\n"
                       "export fn main()->u64\n"
                       "effects {}\nclobbers {} {\nreturn missing\n}\n}\n";
  NtArtifact a;
  CHECK(!nt_compile(source, strlen(source), &a), "unknown symbol is rejected");
  CHECK(a.error.code == 301 && a.error.line == 7,
        "frontend diagnostic and position propagate");
  CHECK(!a.text.bytes && !a.text.size && !a.pe.bytes,
        "failed compile emits no partial artifact");
  nt_artifact_free(&a);
}
static void cross_module_import(void) {
  const char *app =
      "module app\n"
      "target x86_64-nexora-uefi\n"
      "import math.add as add: fn(in a:u64 @rcx,in b:u64 @rdx)->u64\n"
      "effects {}\nclobbers {}\n"
      "section .text {\n"
      "export fn main()->u64\n"
      "effects {}\nclobbers {} {\nreturn add(20,22)\n}\n}\n";
  const char *math = "module math\n"
                     "target x86_64-nexora-uefi\n"
                     "section .text {\n"
                     "export fn add(in a:u64 @rcx,in b:u64 @rdx)->u64\n"
                     "effects {}\nclobbers {} {\nreturn a+b\n}\n}\n";
  NtFInput inputs[] = {{"app.ntasm", app, strlen(app)},
                       {"math.ntasm", math, strlen(math)}};
  NtArtifact a;
  int ok = nt_compile_many(inputs, 2, &a);
  CHECK(ok, "nt_compile_many resolves a typed cross-module import");
  if (ok) {
    CHECK(execute(&a) == 42, "cross-module imported call executes");
    CHECK(nt_make_pe(&a), "cross-module program packages as PE");
    NtFInput reversed[] = {inputs[1], inputs[0]};
    NtArtifact b;
    int reversed_ok = nt_compile_many(reversed, 2, &b);
    CHECK(reversed_ok && nt_make_pe(&b),
          "reversed source enumeration also packages");
    if (reversed_ok && b.pe.size)
      CHECK(a.pe.size == b.pe.size &&
                !memcmp(a.pe.bytes, b.pe.bytes, a.pe.size),
            "module input permutation emits byte-identical PE");
    nt_artifact_free(&b);
  } else
    fprintf(stderr, "DIAGNOSTIC E%u %zu:%zu %s\n", a.error.code, a.error.line,
            a.error.column, a.error.message);
  nt_artifact_free(&a);
}
static void multi_source_diagnostic_index(void) {
  const char *good = "module good\n"
                     "target x86_64-nexora-uefi\n"
                     "section .text {\n"
                     "export fn main()->u64\n"
                     "effects {}\nclobbers {} {\nreturn 1\n}\n}\n";
  const char *bad = "module bad\ntarget x86_64-unknown-none\n";
  NtFInput inputs[] = {{"good.ntasm", good, strlen(good)},
                       {"bad.ntasm", bad, strlen(bad)}};
  NtArtifact a;
  CHECK(!nt_compile_many(inputs, 2, &a) && a.error.code == NTF_E_TARGET,
        "second source diagnostic propagates");
  CHECK(a.error.source_index == 2,
        "diagnostic identifies one-based failing input index");
  nt_artifact_free(&a);
}
static void static_data_pe(void) {
  const char *source = "module static_data\n"
                       "target x86_64-nexora-uefi\n"
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
  NtArtifact a;
  int ok = nt_compile(source, strlen(source), &a);
  CHECK(ok && a.rdata.size == 8 && !a.data.size,
        "nt_compile carries real read-only data section");
  if (ok) {
    CHECK(execute(&a) == 42, "artifact static data load executes");
    CHECK(nt_make_pe(&a), "static data artifact packages as PE");
    if (a.pe.size) {
      uint32_t rdata_raw = u32(a.pe.bytes + 432 + 20);
      CHECK(u32(a.pe.bytes + rdata_raw + 8) == 42,
            "PE read-only section contains emitted data after DIR64 anchor");
      CHECK(u32(a.pe.bytes + 432 + 8) == 16,
            "PE read-only virtual size covers anchor and emitted data");
    }
  }
  nt_artifact_free(&a);
}
static void macro_comptime_pe(void) {
  const char *source = "module macro_pe\n"
                       "target x86_64-nexora-uefi\n"
                       "comptime(fuel=16,call_depth=4,arena_bytes=262144,"
                       "ast_nodes=256,expansion_depth=4,constant_bytes=64) {\n"
                       "return (5+3)*4;\n}\n"
                       "macro ret(v:ast.expr)->ast.stmt {\n"
                       "return quote(stmt) { return $v+1; };\n}\n"
                       "section .text {\n"
                       "export fn main()->u64\neffects {}\nclobbers {} {\n"
                       "expand ret(41)\n}\n}\n";
  NtArtifact artifact;
  int ok = nt_compile(source, strlen(source), &artifact);
  CHECK(ok, "bounded comptime and AST macro compile through nt_compile");
  if (ok) {
    CHECK(execute(&artifact) == 42, "macro-generated integrated code executes");
    CHECK(nt_make_pe(&artifact), "macro-generated program packages as PE");
  }
  nt_artifact_free(&artifact);
}
static void macro_decl_type_pe(void) {
  const char *source =
      "module macro_decl_pe\ntarget x86_64-nexora-uefi\n"
      "macro make(T:ast.type,v:ast.expr)->list<ast.decl> {\n"
      "emit quote(decl) { const base:$T=$v; };\n"
      "emit quote(decl) { export data answer:$T=base+1; };\n}\n"
      "section .rdata {\nexpand make(quote(type) { u64 },41)\n}\n"
      "section .text {\n"
      "export fn main()->u64\neffects {reads_mem(user)}\nclobbers {rax} {\n"
      "load rax,[answer]:ptr<user,u64>\nreturn rax\n}\n}\n";
  NtArtifact artifact;
  int ok = nt_compile(source, strlen(source), &artifact);
  CHECK(ok, "ast.type/list<ast.decl> compile through nt_compile");
  if (ok) {
    CHECK(execute(&artifact) == 42, "generated declaration PE input executes");
    CHECK(nt_make_pe(&artifact), "generated declaration packages as PE");
  }
  nt_artifact_free(&artifact);
}
int main(void) {
  return_literal();
  pe_container();
  linked_frontend_codegen();
  frontend_failure_is_transactional();
  cross_module_import();
  multi_source_diagnostic_index();
  static_data_pe();
  macro_comptime_pe();
  macro_decl_type_pe();
  printf("NTASM_C_TESTS checks=%u failures=%u\n", checks, failures);
  return failures ? 1 : 0;
}
