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
static void ok(const NovaHostImage*i,const char*n,const char*s,uint64_t f,uint64_t m,uint64_t b){Run r=run(i,s,4);cases++;checks++;if(r.returned||r.runtime||r.out[0]!=f||r.out[1]!=m||r.out[2]!=b||r.out[3]!=0){failures++;fprintf(stderr,"FAIL ok %s\n",n);}}
static void err(const NovaHostImage*i,const char*n,const char*s,uint64_t words,uint64_t code,int untouched,uint64_t expected,uint64_t actual){Run r=run(i,s,words);cases++;checks++;if(r.returned!=code||r.runtime||(untouched&&(r.out[0]!=guard||r.out[1]!=guard))||(!untouched&&(r.out[2]!=expected||r.out[3]!=actual))){failures++;fprintf(stderr,"FAIL error %s ret=%llu out=%llu,%llu\n",n,(unsigned long long)r.returned,(unsigned long long)r.out[2],(unsigned long long)r.out[3]);}}
int main(void){const char*paths[]={"toolchain/ntasm-nova/tests/register-widths-entry.nova","toolchain/ntasm-nova/register_widths.nova","toolchain/ntasm-nova/register_bindings.nova","toolchain/ntasm-nova/parameters.nova","toolchain/ntasm-nova/callable_names.nova","toolchain/ntasm-nova/semantics.nova","toolchain/ntasm-nova/parser.nova","toolchain/ntasm-nova/ast.nova","toolchain/ntasm-nova/lexer.nova"};char*sources[9]={0};NtFInput inputs[9];for(unsigned i=0;i<9;i++){size_t n=0;sources[i]=load(paths[i],&n);inputs[i]=(NtFInput){paths[i],sources[i],n};}int all=1;for(unsigned i=0;i<9;i++)if(!sources[i])all=0;CHECK(all,"sources load");NtArtifact a={0};int compiled=all&&nova_compile_many_v2(inputs,9,&a);CHECK(compiled,"modules compile");if(!compiled)fprintf(stderr,"E%u %zu:%zu %s\n",a.error.code,a.error.line,a.error.column,a.error.message);NovaHostImage image={0};CHECK(compiled&&nova_host_map(&a,&image),"image maps");
 if(image.memory){const char*valid="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f(in a:u8 @cl,in b:u16 @dx,in c:u32 @ecx,in d:u64 @r8)->u64\neffects {}\nclobbers {} {\nreturn d\n}\n}\n";ok(&image,"valid widths",valid,1,0,4);const char*mismatch="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f(in x:u64 @ecx)->u64\neffects {}\nclobbers {} {\nreturn x\n}\n}\n";err(&image,"mismatch",mismatch,4,318,0,64,32);const char*imported="module m\ntarget x86_64-nexora-none\nimport a.f as f: fn(in x:u16 @r9w)->u64\neffects {}\nclobbers {}\n";ok(&image,"import",imported,0,1,1);const char*unbound="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f(in x:u64)->u64\neffects {}\nclobbers {} {\nreturn x\n}\n}\n";ok(&image,"unbound",unbound,1,0,0);const char*pointer="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f(in x:ptr<user,u64> @rcx)->u64\neffects {}\nclobbers {} {\nreturn 0\n}\n}\n";ok(&image,"pointer skipped",pointer,1,0,0);const char*extended="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f(in x:u32 @r10d)->u32\neffects {}\nclobbers {} {\nreturn x\n}\n}\n";ok(&image,"extended",extended,1,0,1);err(&image,"short",valid,3,800,1,0,0);err(&image,"invalid","target x86_64-nexora-none\n",4,800,1,0,0);}
 if(image.memory) nova_host_unmap(&image);
 if(compiled) nt_artifact_free(&a);
 for(unsigned i=0;i<9;i++) free(sources[i]);
 printf("NTASM_NOVA_REGISTER_WIDTHS checks=%u failures=%u cases=%u\n",checks,failures,cases);
 return failures?1:0;
}
