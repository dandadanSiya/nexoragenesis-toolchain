#include "nova.h"
#include "nova/run_host.h"
#include "nova/runtime_v2.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures,cases;
#define CHECK(c,m) do{checks++;if(!(c)){failures++;fprintf(stderr,"FAIL %s\n",m);}}while(0)
static const uint64_t guard=UINT64_C(0xcccccccccccccccc);
static char*load(const char*p,size_t*n){FILE*f=fopen(p,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long z=ftell(f);fseek(f,0,SEEK_SET);char*b=z>=0?malloc((size_t)z+1):NULL;if(!b||fread(b,1,(size_t)z,f)!=(size_t)z){free(b);fclose(f);return NULL;}fclose(f);b[z]=0;*n=(size_t)z;return b;}
typedef struct{uint64_t returned,runtime,out[4];}Run;
static Run run(const NovaHostImage*i,const char*s,uint64_t words){Run r={.out={guard,guard,guard,guard}};NovaContextV2 c={0};CHECK(nova_context_init_v2(&c),"context init");uint64_t sh=nova_borrow_v2(&c,(void*)s,strlen(s),0),oh=nova_borrow_v2(&c,r.out,words*8,1);uint64_t a[]={sh,strlen(s),oh,words};r.returned=i->entry(&c,a,4);r.runtime=c.error;CHECK(nova_context_destroy_v2(&c),"context cleanup");return r;}
static void expect(const NovaHostImage*i,const char*n,const char*s,uint64_t words,uint64_t code,int untouched){Run r=run(i,s,words);cases++;checks++;if(r.returned!=code||r.runtime||(code==0&&(r.out[0]!=11||r.out[1]==0||r.out[2]==0||r.out[3]!=0))||(untouched&&(r.out[0]!=guard||r.out[1]!=guard))){failures++;fprintf(stderr,"FAIL %s ret=%llu runtime=%llu out=%llu,%llu,%llu,%llu\n",n,(unsigned long long)r.returned,(unsigned long long)r.runtime,(unsigned long long)r.out[0],(unsigned long long)r.out[1],(unsigned long long)r.out[2],(unsigned long long)r.out[3]);}}
int main(void){const char*paths[]={"toolchain/ntasm-nova/tests/validate-entry.nova","toolchain/ntasm-nova/validate.nova","toolchain/ntasm-nova/calls.nova","toolchain/ntasm-nova/refs.nova","toolchain/ntasm-nova/clobbers.nova","toolchain/ntasm-nova/register_widths.nova","toolchain/ntasm-nova/register_bindings.nova","toolchain/ntasm-nova/bindings.nova","toolchain/ntasm-nova/locals.nova","toolchain/ntasm-nova/parameters.nova","toolchain/ntasm-nova/symbol_names.nova","toolchain/ntasm-nova/callable_names.nova","toolchain/ntasm-nova/semantics.nova","toolchain/ntasm-nova/parser.nova","toolchain/ntasm-nova/ast.nova","toolchain/ntasm-nova/lexer.nova"};char*sources[16]={0};NtFInput inputs[16];for(unsigned i=0;i<16;i++){size_t n=0;sources[i]=load(paths[i],&n);inputs[i]=(NtFInput){paths[i],sources[i],n};}int all=1;for(unsigned i=0;i<16;i++)if(!sources[i])all=0;CHECK(all,"sources load");NtArtifact a={0};int compiled=all&&nova_compile_many_v2(inputs,16,&a);CHECK(compiled,"modules compile");if(!compiled)fprintf(stderr,"E%u %zu:%zu %s\n",a.error.code,a.error.line,a.error.column,a.error.message);NovaHostImage image={0};CHECK(compiled&&nova_host_map(&a,&image),"image maps");
 if(image.memory){
  const char*valid="module m\ntarget x86_64-nexora-none\nsection .rdata {\ndata answer:u64=42\n}\nsection .text {\nfn helper(in x:u64 @rcx)->u64\neffects {}\nclobbers {rax} {\nlet y:u64=x\nreturn y\n}\nfn main()->u64\neffects {}\nclobbers {} {\ncall helper(1)\nreturn 0\n}\n}\n";expect(&image,"valid",valid,4,0,0);
  const char*dupfn="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {}\nclobbers {} {\nreturn 0\n}\nfn f()->u64\neffects {}\nclobbers {} {\nreturn 1\n}\n}\n";expect(&image,"300",dupfn,4,300,0);
  const char*aliases="module m\ntarget x86_64-nexora-none\nimport a.f as x: fn()->u64\neffects {}\nclobbers {}\nimport b.f as x: fn()->u64\neffects {}\nclobbers {}\n";expect(&image,"313",aliases,4,313,0);
  const char*symbols="module m\ntarget x86_64-nexora-none\nsection .rdata {\ndata same:u64=1\n}\nsection .text {\nfn same()->u64\neffects {}\nclobbers {} {\nreturn 0\n}\n}\n";expect(&image,"319",symbols,4,319,0);
  const char*params="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f(in x:u64 @rcx,in x:u64 @rdx)->u64\neffects {}\nclobbers {} {\nreturn x\n}\n}\n";expect(&image,"314",params,4,314,0);
  const char*locals="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {}\nclobbers {} {\nlet x:u64=1\nlet x:u64=2\nreturn x\n}\n}\n";expect(&image,"315",locals,4,315,0);
  const char*bindings="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f(in x:u64 @rcx)->u64\neffects {}\nclobbers {} {\nlet x:u64=1\nreturn x\n}\n}\n";expect(&image,"316",bindings,4,316,0);
  const char*registers="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f(in a:u64 @rcx,in b:u64 @rcx)->u64\neffects {}\nclobbers {} {\nreturn a\n}\n}\n";expect(&image,"317",registers,4,317,0);
  const char*width="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f(in x:u64 @ecx)->u64\neffects {}\nclobbers {} {\nreturn x\n}\n}\n";expect(&image,"318",width,4,318,0);
  const char*clobber="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {}\nclobbers {rax,rax} {\nreturn 0\n}\n}\n";expect(&image,"320",clobber,4,320,0);
  const char*missing="module m\ntarget x86_64-nexora-none\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\ncall absent()\nreturn 0\n}\n}\n";expect(&image,"310",missing,4,310,0);
  const char*arity="module m\ntarget x86_64-nexora-none\nsection .text {\nfn helper(in x:u64 @rcx)->u64\neffects {}\nclobbers {} {\nreturn x\n}\nfn main()->u64\neffects {}\nclobbers {} {\ncall helper()\nreturn 0\n}\n}\n";expect(&image,"311",arity,4,311,0);
  const char*param_alias="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f(in a:u64 @rcx,in b:u32 @ecx)->u64\neffects {}\nclobbers {} {\nreturn a\n}\n}\n";expect(&image,"317 alias",param_alias,4,317,0);
  const char*clobber_alias="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {}\nclobbers {r10,r10d} {\nreturn 0\n}\n}\n";expect(&image,"320 alias",clobber_alias,4,320,0);
  expect(&image,"short",valid,3,800,1);expect(&image,"invalid","target x86_64-nexora-none\n",4,800,1);
 }
 if(image.memory) nova_host_unmap(&image);
 if(compiled) nt_artifact_free(&a);
 for(unsigned i=0;i<16;i++) free(sources[i]);
 printf("NTASM_NOVA_VALIDATE checks=%u failures=%u cases=%u\n",checks,failures,cases);
 return failures?1:0;
}
