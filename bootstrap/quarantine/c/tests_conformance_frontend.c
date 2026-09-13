#include "frontend.h"
#include <stdio.h>
#include <string.h>
static unsigned cases, failures;
static void check(const char *name, const char *source, unsigned expected) {
  NtFInput input = {name, source, strlen(source)};
  NtFProgram p;
  int ok = nt_frontend_compile(&input, 1, &p), found = 0;
  for (size_t i = 0; i < p.diagnostic_count; i++)
    if (p.diagnostics[i].code == expected)
      found = 1;
  cases++;
  if (expected ? (!ok && found && !p.verified)
               : (ok && p.verified && !p.diagnostic_count)) {
  } else {
    failures++;
    fprintf(stderr, "FAIL CFG source %s expected%u verified%d\n", name,
            expected, p.verified);
    for (size_t i = 0; i < p.diagnostic_count; i++)
      fprintf(stderr, " E%u %s\n", p.diagnostics[i].code,
              p.diagnostics[i].message);
  }
  nt_frontend_free(&p);
}
#define HEAD "module cfg\ntarget x86_64-nexora-none\nsection .text {\n"
#define FN                                                                     \
  "export fn main()->u64\neffects {changes_flags}\nclobbers {rax,rflags} {\n"
#define END "}\n}\n"
int main(void) {
  check("finite_loop",
        HEAD FN
        "mov rax,0\nloop:\ninc rax\ncmp rax,42\njb loop\nreturn rax\n" END,
        0);
  check("infinite_never",
        HEAD
        "fn loop()->never\neffects {}\nclobbers {} {\nagain:\njmp again\n" END,
        0);
  check("literal_true_return", HEAD FN "if true {\nreturn 42\n}\n" END, 0);
  check("literal_false_fallthrough", HEAD FN "if false {\nreturn 42\n}\n" END,
        NTF_E_FLOW);
  check("bypassed_local",
        HEAD FN "jmp use\nlet value:u64=42\nuse:\nreturn value\n" END,
        NTF_E_FLOW);
  check("bypassed_flags",
        HEAD FN
        "jmp use\ncmp rax,0\nuse:\nje done\nreturn 0\ndone:\nreturn 42\n" END,
        NTF_E_CONTRACT);
  check("both_arms_flags",
        HEAD FN "mov rax,0\nif rax==0 {\ncmp rax,0\n} else {\ncmp rax,1\n}\nje "
                "done\nreturn 0\ndone:\nreturn 42\n" END,
        0);
  check("one_arm_flags",
        HEAD FN "mov rax,0\nif rax==0 {\ncmp rax,0\n} else {\nnop\n}\nje "
                "done\nreturn 0\ndone:\nreturn 42\n" END,
        NTF_E_CONTRACT);
  check("let_call_clobber",
        HEAD FN "mov rax,0\ncmp rax,0\nlet x:u64=change()\nje done\nreturn "
                "0\ndone:\nreturn 42\n}\n"
                "fn change()->u64\neffects {changes_flags}\nclobbers "
                "{rax,rflags} {\nmov rax,1\nadd rax,1\nreturn rax\n}\n}\n",
        NTF_E_CONTRACT);
  printf("CFG_FRONTEND_TESTS cases=%u failures=%u\n", cases, failures);
  return failures ? 1 : 0;
}
