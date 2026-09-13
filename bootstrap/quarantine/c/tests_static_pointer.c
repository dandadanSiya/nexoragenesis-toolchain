#include "ntasm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(c,m) do{++checks;if(!(c)){++failures;fprintf(stderr,"FAIL: %s\n",m);}}while(0)
static uint64_t u64(const uint8_t *p){uint64_t n=0;for(unsigned j=0;j<8;j++)n|=(uint64_t)p[j]<<(8*j);return n;}
int main(void){
 const char *source="module static_pointer\ntarget x86_64-nexora-uefi\nsection .data {\ndata answer:u64=42\ndata saved:ptr<user,u64> = intrinsic address_of(answer)\n}\nsection .text {\nexport fn main()->u64\neffects {}\nclobbers {} {\nreturn 42\n}\n}\n";
 NtArtifact a={0};int ok=nt_compile(source,strlen(source),&a);CHECK(ok,"compile real typed static address source");
 if(ok){CHECK(a.base_relocation_count==1&&a.base_relocations[0].section==3&&a.base_relocations[0].offset==8,"direct compiler retains data DIR64 site");CHECK(a.data.size==16&&u64(a.data.bytes+8)==UINT64_C(0x140003000),"direct compiler materializes preferred address before PE writer");CHECK(nt_make_pe(&a),"direct typed address produces PE");}
 else fprintf(stderr,"E%u %s\n",a.error.code,a.error.message);
 nt_artifact_free(&a);printf("NTASM_STATIC_POINTER_TESTS checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
