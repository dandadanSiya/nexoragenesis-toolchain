#include "nova.h"
#include "nova/run_host.h"
#include "nova/runtime_v2.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, failures, cases;
#define CHECK(c,m) do{checks++;if(!(c)){failures++;fprintf(stderr,"FAIL %s\n",m);}}while(0)
static const uint64_t guard=UINT64_C(0xcccccccccccccccc);
static char *load(const char *path,size_t *length){FILE*f=fopen(path,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);char*p=n>=0?malloc((size_t)n+1):NULL;if(!p||fread(p,1,(size_t)n,f)!=(size_t)n){free(p);fclose(f);return NULL;}fclose(f);p[n]=0;*length=(size_t)n;return p;}
typedef struct{uint64_t returned,runtime,out[4];}Run;
static Run run(const NovaHostImage*image,const char*source,uint64_t words){Run r={.out={guard,guard,guard,guard}};NovaContextV2 c={0};CHECK(nova_context_init_v2(&c),"context init");uint64_t sh=nova_borrow_v2(&c,(void*)source,strlen(source),0),oh=nova_borrow_v2(&c,r.out,words*8,1);uint64_t args[]={sh,strlen(source),oh,words};r.returned=image->entry(&c,args,4);r.runtime=c.error;CHECK(nova_context_destroy_v2(&c),"context cleanup");return r;}
static void expect_ok(const NovaHostImage*i,const char*n,const char*s,uint64_t f,uint64_t m){Run r=run(i,s,3);cases++;checks++;if(r.returned||r.runtime||r.out[0]!=f||r.out[1]!=m||r.out[2]!=f+m){failures++;fprintf(stderr,"FAIL ok %s\n",n);}}
static void expect_error(const NovaHostImage*i,const char*n,const char*s,uint64_t words,uint64_t code,int untouched){Run r=run(i,s,words);cases++;checks++;if(r.returned!=code||r.runtime||(untouched&&(r.out[0]!=guard||r.out[1]!=guard))||(!untouched&&r.out[0]==guard)){failures++;fprintf(stderr,"FAIL error %s ret=%llu\n",n,(unsigned long long)r.returned);}}
int main(void){
 const char*paths[]={"toolchain/ntasm-nova/tests/callable-names-entry.nova","toolchain/ntasm-nova/callable_names.nova","toolchain/ntasm-nova/semantics.nova","toolchain/ntasm-nova/parser.nova","toolchain/ntasm-nova/ast.nova","toolchain/ntasm-nova/lexer.nova"};
 char*sources[6]={0};NtFInput inputs[6];for(unsigned i=0;i<6;i++){size_t n=0;sources[i]=load(paths[i],&n);inputs[i]=(NtFInput){paths[i],sources[i],n};}CHECK(sources[0]&&sources[1]&&sources[2]&&sources[3]&&sources[4]&&sources[5],"sources load");NtArtifact artifact={0};int compiled=sources[0]&&sources[1]&&sources[2]&&sources[3]&&sources[4]&&sources[5]&&nova_compile_many_v2(inputs,6,&artifact);CHECK(compiled,"modules compile");if(!compiled)fprintf(stderr,"E%u %zu:%zu %s\n",artifact.error.code,artifact.error.line,artifact.error.column,artifact.error.message);NovaHostImage image={0};CHECK(compiled&&nova_host_map(&artifact,&image),"image maps");
 if(image.memory){
  const char*unique="module m\ntarget x86_64-nexora-none\nimport math.add as add: fn()->u64\neffects {}\nclobbers {}\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\nreturn 0\n}\n}\n";expect_ok(&image,"unique",unique,1,1);
  const char*aliases="module m\ntarget x86_64-nexora-none\nimport a.f as x: fn()->u64\neffects {}\nclobbers {}\nimport b.f as x: fn()->u64\neffects {}\nclobbers {}\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\nreturn 0\n}\n}\n";expect_error(&image,"duplicate aliases",aliases,3,313,0);
  const char*collision="module m\ntarget x86_64-nexora-none\nimport a.f as main: fn()->u64\neffects {}\nclobbers {}\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\nreturn 0\n}\n}\n";expect_error(&image,"alias function",collision,3,313,0);
  const char*different="module m\ntarget x86_64-nexora-none\nimport a.f as first: fn()->u64\neffects {}\nclobbers {}\nimport a.f as second: fn()->u64\neffects {}\nclobbers {}\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\nreturn 0\n}\n}\n";expect_ok(&image,"different aliases",different,1,2);
  expect_error(&image,"short",unique,2,800,1);expect_error(&image,"invalid","target x86_64-nexora-none\n",3,800,1);
 }
 if(image.memory) nova_host_unmap(&image);
 if(compiled) nt_artifact_free(&artifact);
 for(unsigned i=0;i<6;i++) free(sources[i]);
 printf("NTASM_NOVA_CALLABLE_NAMES checks=%u failures=%u cases=%u\n",checks,failures,cases);
 return failures?1:0;
}
