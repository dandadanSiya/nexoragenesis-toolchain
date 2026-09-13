#include "frontend.h"
#include <stdio.h>
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
    fprintf(stderr, "FAIL provenance %s expected%u got%d\n", name, error, ok);
    for (size_t i = 0; i < p.diagnostic_count; i++)
      fprintf(stderr, " E%u %s\n", p.diagnostics[i].code,
              p.diagnostics[i].message);
  }
  nt_frontend_free(&p);
}
#define H "module pointers\ntarget x86_64-nexora-none\nsection .text {\n"
#define F                                                                      \
  "fn read(in p:ptr<user,u64> @rcx)->u64\neffects "                            \
  "{reads_mem(user)}\nclobbers {rax,rcx,rdx} {\n"
#define E "}\n}\n"
int main(void) {
  check("unbound_address",
        H "fn main()->u64\neffects {reads_mem(user)}\nclobbers {rax} {\n"
          "load rax,[r13+r12*8-16]:ptr<user,u64>\nreturn rax\n" E,
        NTF_E_MEMORY);
  check("entry", H F "load rax,[rcx]:ptr<user,u64>\nreturn rax\n" E, 0);
  check("integer_overwrite",
        H F "mov rcx,0\nload rax,[rcx]:ptr<user,u64>\nreturn rax\n" E,
        NTF_E_MEMORY);
  check("alias_overwrite",
        H F "mov ecx,0\nload rax,[rcx]:ptr<user,u64>\nreturn rax\n" E,
        NTF_E_MEMORY);
  check("copy", H F "mov rdx,rcx\nload rax,[rdx]:ptr<user,u64>\nreturn rax\n" E,
        0);
  check("lea",
        H F "lea rdx,[rcx+8]:ptr<user,u64>\nload "
            "rax,[rdx]:ptr<user,u64>\nreturn rax\n" E,
        0);
  check("pointer_load",
        H "fn read(in p:ptr<user,ptr<user,u64>> @rcx)->u64\neffects "
          "{reads_mem(user)}\nclobbers {rax,rdx} {\n"
          "load rdx,[rcx]:ptr<user,ptr<user,u64>>\nload "
          "rax,[rdx]:ptr<user,u64>\nreturn rax\n" E,
        0);
  check("forged_return",
        H "fn forge()->ptr<user,u64>\neffects {}\nclobbers {rax} {\nmov "
          "rax,1\nreturn rax\n" E,
        NTF_E_TYPE);
  check("pointer_return",
        H "fn identity(in p:ptr<user,u64> @rcx)->ptr<user,u64>\neffects "
          "{}\nclobbers {rax} {\nmov rax,rcx\nreturn rax\n" E,
        0);
  check("call_clobber",
        H F "call destroy()\nload rax,[rcx]:ptr<user,u64>\nreturn rax\n}\nfn "
            "destroy()->u64\neffects {}\nclobbers {rcx} {\nmov rcx,0\nreturn "
            "0\n}\n}\n",
        NTF_E_MEMORY);
  check("restore_entry_snapshot",
        H F "call destroy()\nmov rcx,p\nload rax,[rcx]:ptr<user,u64>\nreturn "
            "rax\n}\nfn destroy()->u64\neffects {}\nclobbers {rcx} {\nmov "
            "rcx,0\nreturn 0\n}\n}\n",
        0);
  check("join_same",
        H F "if rax==0 {\nmov rdx,rcx\n} else {\nmov rdx,rcx\n}\nload "
            "rax,[rdx]:ptr<user,u64>\nreturn rax\n" E,
        0);
  check("join_lost",
        H F "if rax==0 {\nmov rdx,rcx\n} else {\nmov rdx,0\n}\nload "
            "rax,[rdx]:ptr<user,u64>\nreturn rax\n" E,
        NTF_E_MEMORY);
  check("loop_invalidation",
        H F "again:\nload rax,[rcx]:ptr<user,u64>\nmov ecx,0\njmp again\n" E,
        NTF_E_MEMORY);
  check("raw_pointer_add_loses_provenance",
        H "fn read(in p:ptr<user,u64> @rcx)->u64\neffects "
          "{reads_mem(user),changes_flags}\nclobbers {rax,rcx,rflags} {\n"
          "add rcx,8\nload rax,[rcx]:ptr<user,u64>\nreturn rax\n" E,
        NTF_E_MEMORY);
  check(
      "call_pointer_result",
      H F
      "call echo(p)\nmov rdx,rax\nload rax,[rdx]:ptr<user,u64>\nreturn rax\n}\n"
      "fn echo(in p:ptr<user,u64> @rcx)->ptr<user,u64>\neffects {}\nclobbers "
      "{} {\nreturn p\n}\n}\n",
      0);
  check("pure_call_preserves",
        H F "call pure()\nload rax,[rcx]:ptr<user,u64>\nreturn rax\n}\n"
            "fn pure()->u64\neffects {}\nclobbers {} {\nreturn 0\n}\n}\n",
        0);
  check("wrong_pointer_store",
        H "fn store(in slot:ptr<user,ptr<kernel,u64>> @rcx,in p:ptr<user,u64> "
          "@rdx)->u64\n"
          "effects {writes_mem(user)}\nclobbers {} {\nstore "
          "[rcx]:ptr<user,ptr<kernel,u64>>,rdx\nreturn 0\n" E,
        NTF_E_TYPE);
  check("different_space_join",
        H "fn choose(in p:ptr<user,u64> @rcx,in q:ptr<kernel,u64> @r8)->u64\n"
          "effects {reads_mem(user)}\nclobbers {rax,rdx} {\nif rax==0 {\nmov "
          "rdx,rcx\n} else {\nmov rdx,r8\n}\n"
          "load rax,[rdx]:ptr<user,u64>\nreturn rax\n" E,
        NTF_E_MEMORY);
  check("xchg_pointer_store",
        H
        "fn swap(in slot:ptr<user,ptr<user,u64>> @rcx)->u64\n"
        "effects {reads_mem(user),writes_mem(user),memory_ordering}\nclobbers "
        "{rax} {\n"
        "mov rax,0\nxchg rax,[rcx]:ptr<user,ptr<user,u64>>\nreturn 0\n" E,
        NTF_E_TYPE);
  printf("REGISTER_PROVENANCE_TESTS cases=%u failures=%u\n", cases, failures);
  return failures ? 1 : 0;
}
