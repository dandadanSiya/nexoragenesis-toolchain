#include "object.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(c,m) do{++checks;if(!(c)){++failures;fprintf(stderr,"FAIL: %s\n",m);}}while(0)
int main(void){
 const char *source="module obj\ntarget x86_64-nexora-uefi\nsection .rdata {\ndata answer:u64=42\n}\nsection .text {\nfn helper()->u64\neffects {reads_mem(user)}\nclobbers {rax} {\nload rax,[answer]:ptr<user,u64>\nreturn rax\n}\nexport fn main()->u64\neffects {reads_mem(user)}\nclobbers {rax} {\nreturn helper()\n}\n}\n";
 NtFInput input={"object-build.ntasm",source,strlen(source)};NtObject object={0},decoded={0};NtDiagnostic error;NtBuffer file={0};
 int ok=nt_object_compile(&input,1,&object,&error);CHECK(ok,"real typed source creates a symbol/relocation object");
 if(!ok)fprintf(stderr,"E%u %s\n",error.code,error.message);
 if(ok){
  CHECK(object.symbol_count==3&&object.entry_symbol==3,"canonical data/helper/main symbols and entry");
  CHECK(!strcmp(object.symbols[0].name,"obj.answer")&&!strcmp(object.symbols[1].name,"obj.helper")&&!strcmp(object.symbols[2].name,"obj.main"),"qualified sorted symbol names");
  CHECK(strstr(object.symbols[1].contract,"u64")&&strstr(object.symbols[1].contract,"effects"),"typed function ABI and effects contract retained");
  CHECK(object.relocation_count==2&&object.relocations[0].kind==NT_OBJECT_REL32&&object.relocations[1].kind==NT_OBJECT_REL32,"call and data references retained as real REL32 records");
  CHECK(object.sections[1].size==8&&object.sections[1].bytes[0]==42,"rdata logical payload excludes PE anchor");
  CHECK(nt_object_write(&object,&file,&error),"serialize compiler-produced object");
  CHECK(nt_object_read(file.bytes,file.size,&decoded,&error),"independently read compiler-produced NXO");
  CHECK(decoded.symbol_count==3&&decoded.relocation_count==2&&decoded.entry_symbol==3,"NXO retains source-generated symbol and relocation metadata");
  NtArtifact materialized={0},reference={0};
  int linked=nt_object_materialize(&decoded,&materialized,&error);
  CHECK(linked,"materialize actual NXO symbol relocations into artifact");
  if(linked){
   CHECK(nt_make_pe(&materialized),"write PE from materialized NXO");
   CHECK(nt_compile(source,strlen(source),&reference)&&nt_make_pe(&reference),"compile independent direct-PE reference");
   CHECK(materialized.pe.size==reference.pe.size&&!memcmp(materialized.pe.bytes,reference.pe.bytes,reference.pe.size),"NXO roundtrip PE byte-identical to direct pipeline");
  }
  nt_artifact_free(&materialized);nt_artifact_free(&reference);
 }
 nt_object_free(&decoded);nt_object_free(&object);free(file.bytes);
 printf("NTASM_NXO_BUILD_TESTS checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
