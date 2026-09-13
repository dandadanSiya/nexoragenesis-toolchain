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
static uint64_t execute(const NtCodeImage *image) {
#ifdef _WIN32
  void *memory =
      VirtualAlloc(NULL, image->size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  DWORD old = 0;
  if (!memory)
    return 0;
  memcpy(memory, image->bytes, image->size);
  if (!VirtualProtect(memory, image->size, PAGE_EXECUTE_READ, &old)) {
    VirtualFree(memory, 0, MEM_RELEASE);
    return 0;
  }
  uint64_t value = ((uint64_t (*)(void))((uint8_t *)memory + image->entry))();
  VirtualFree(memory, 0, MEM_RELEASE);
  return value;
#else
  void *memory = mmap(NULL, image->size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (memory == MAP_FAILED)
    return 0;
  memcpy(memory, image->bytes, image->size);
  if (mprotect(memory, image->size, PROT_READ | PROT_EXEC)) {
    munmap(memory, image->size);
    return 0;
  }
  uint64_t value = ((uint64_t(__attribute__((ms_abi)) *)(void))(
      (uint8_t *)memory + image->entry))();
  munmap(memory, image->size);
  return value;
#endif
}
int main(void) {
  const char *source = "module macro_fn\ntarget x86_64-nexora-none\n"
                       "macro word()->ast.type {\n"
                       "return quote(type) { u64 };\n}\n"
                       "macro plus(v:ast.expr)->ast.expr {\n"
                       "return quote(expr) { $v+1 };\n}\n"
                       "macro next(v:u64)->ast.expr {\n"
                       "return quote(expr) { $v+1 };\n}\n"
                       "macro finish(v:ast.expr)->ast.stmt {\n"
                       "return quote(stmt) { return $v; };\n}\n"
                       "macro make(T:ast.type)->ast.decl {\n"
                       "return quote(decl) {\n"
                       " export fn add(in x:$T @rcx,in y:$T @rdx)->$T\n"
                       " effects {}\nclobbers {} {\n"
                       "let sum:$T=x+y\nexpand finish(sum)\n}\n"
                       "};\n}\n"
                       "macro computed(v:ast.expr)->ast.stmt {\n"
                       "let first=$v+1;\nlet second=$first+1;\n"
                       "return quote(stmt) { return $second; };\n}\n"
                       "section .text {\n"
                       "expand make(expand word())\n"
                       "export fn main()->u64\neffects {}\nclobbers {} {\n"
                       "expand computed(expand plus(add(17,expand next(21))))\n"
                       "}\n}\n";
  NtFInput input = {"macro-fn", source, strlen(source)};
  NtFProgram program;
  if (!nt_frontend_compile(&input, 1, &program)) {
    for (size_t i = 0; i < program.diagnostic_count; i++)
      fprintf(stderr, "E%u %s\n", program.diagnostics[i].code,
              program.diagnostics[i].message);
    nt_frontend_free(&program);
    return 1;
  }
  NtCodeImage image = {0};
  NtCodeDiagnostic diagnostic = {0};
  int generated = nt_codegen_x64(&program, &image, &diagnostic);
  uint64_t value = generated ? execute(&image) : 0;
  unsigned function_symbols = 0;
  for (size_t i = 0; i < image.symbol_count; i++)
    function_symbols += image.symbols[i].function_id != 0;
  printf("A5_EXEC value=%llu functions=%u\n", (unsigned long long)value,
         function_symbols);
  nt_code_image_free(&image);
  nt_frontend_free(&program);
  return generated && value == 42 && function_symbols == 2 ? 0 : 1;
}
