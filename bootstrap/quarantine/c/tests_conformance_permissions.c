#include "frontend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned cases, failures;
static void check(const char *name, const char *source, unsigned error) {
  NtFInput input = {name, source, strlen(source)};
  NtFProgram p;
  int ok = nt_frontend_compile(&input, 1, &p), found = 0;
  for (size_t i = 0; i < p.diagnostic_count; i++)
    if (p.diagnostics[i].code == error)
      found = 1;
  cases++;
  if (error ? (!ok && found && !p.verified)
            : (ok && p.verified && !p.diagnostic_count)) {
  } else {
    failures++;
    fprintf(stderr, "FAIL permissions %s expected%u got%d\n", name, error, ok);
    for (size_t i = 0; i < p.diagnostic_count; i++)
      fprintf(stderr, " E%u %s\n", p.diagnostics[i].code,
              p.diagnostics[i].message);
  }
  nt_frontend_free(&p);
}
#define HEAD                                                                   \
  "module permissions\ntarget x86_64-nexora-none\nsection .rdata {\ndata "     \
  "immutable:u64=42\n}\nsection .data {\ndata mutable:u64=42\n}\nsection "     \
  ".text {\n"
#define BAD "fn main()->u64\neffects {writes_mem(user)}\nclobbers {rcx} {\n"
#define END "}\n}\n"
int main(void) {
  check("readonly_alias",
        HEAD BAD "lea rcx,[immutable]:ptr<user,u64>\nstore "
                 "[rcx]:ptr<user,u64>,0\nreturn 0\n" END,
        NTF_E_MEMORY);
  check("readonly_call",
        HEAD "fn main()->u64\neffects {writes_mem(user)}\nclobbers {} "
             "{\nreturn mutate(intrinsic address_of(immutable))\n}\n"
             "fn mutate(in p:ptr<user,u64> @rcx)->u64\neffects "
             "{writes_mem(user)}\nclobbers {} {\nstore "
             "[rcx]:ptr<user,u64>,0\nreturn 0\n}\n}\n",
        NTF_E_MEMORY);
  check(
      "readonly_return",
      HEAD BAD
      "call address()\nmov rcx,rax\nstore [rcx]:ptr<user,u64>,0\nreturn 0\n}\n"
      "fn address()->ptr<user,u64>\neffects {}\nclobbers {} {\nreturn "
      "intrinsic address_of(immutable)\n}\n}\n",
      NTF_E_MEMORY);
  check("readonly_identity",
        HEAD BAD "call identity(intrinsic address_of(immutable))\nmov "
                 "rcx,rax\nstore [rcx]:ptr<user,u64>,0\nreturn 0\n}\n"
                 "fn identity(in p:ptr<user,u64> @rcx)->ptr<user,u64>\neffects "
                 "{}\nclobbers {} {\nreturn p\n}\n}\n",
        NTF_E_MEMORY);
  check("mutable_alias",
        HEAD BAD "lea rcx,[mutable]:ptr<user,u64>\nstore "
                 "[rcx]:ptr<user,u64>,0\nreturn 0\n" END,
        0);
  check(
      "mutable_return",
      HEAD BAD
      "call address()\nmov rcx,rax\nstore [rcx]:ptr<user,u64>,0\nreturn 0\n}\n"
      "fn address()->ptr<user,u64>\neffects {}\nclobbers {} {\nreturn "
      "intrinsic address_of(mutable)\n}\n}\n",
      0);
  check("readonly_read",
        HEAD "fn main()->u64\neffects {reads_mem(user)}\nclobbers {rax} "
             "{\nreturn read(intrinsic address_of(immutable))\n}\n"
             "fn read(in p:ptr<user,u64> @rcx)->u64\neffects "
             "{reads_mem(user)}\nclobbers {rax} {\nload "
             "rax,[rcx]:ptr<user,u64>\nreturn rax\n}\n}\n",
        0);
  char *large = calloc(1, 60000);
  if (!large)
    return 1;
  size_t at = (size_t)snprintf(
      large, 60000,
      HEAD BAD
      "call f10(0)\nmov rcx,rax\nstore [rcx]:ptr<user,u64>,0\nreturn 0\n}\n"
      "fn f0(in n:u64 @r9)->ptr<user,u64>\neffects {}\nclobbers {} {\nreturn "
      "intrinsic address_of(mutable)\n}\n");
  for (unsigned i = 1; i <= 10; i++) {
    at += (size_t)snprintf(
        large + at, 60000 - at,
        "fn f%u(in n:u64 @r9)->ptr<user,u64>\neffects {}\nclobbers {} {\n", i);
    for (unsigned branch = 0; branch < 9; branch++)
      at += (size_t)snprintf(large + at, 60000 - at,
                             "if n==%u {\nreturn f%u(n)\n}\n", branch, i - 1);
    at += (size_t)snprintf(large + at, 60000 - at, "return f%u(n)\n}\n", i - 1);
  }
  snprintf(large + at, 60000 - at, "}\n");
  check("bounded_summary", large, NTF_E_LIMIT);
  free(large);
  printf("POINTER_PERMISSION_TESTS cases=%u failures=%u\n", cases, failures);
  return failures ? 1 : 0;
}
