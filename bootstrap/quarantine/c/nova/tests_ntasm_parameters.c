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
static void ok(const NovaHostImage*i,const char*n,const char*s,uint64_t f,uint64_t m,uint64_t p){Run r=run(i,s,3);cases++;checks++;if(r.returned||r.runtime||r.out[0]!=f||r.out[1]!=m||r.out[2]!=p){failures++;fprintf(stderr,"FAIL ok %s\n",n);}}
static void err(const NovaHostImage*i,const char*n,const char*s,uint64_t words,uint64_t code,int untouched){Run r=run(i,s,words);cases++;checks++;if(r.returned!=code||r.runtime||(untouched&&(r.out[0]!=guard||r.out[1]!=guard))||(!untouched&&r.out[0]==guard)){failures++;fprintf(stderr,"FAIL error %s ret=%llu\n",n,(unsigned long long)r.returned);}}
int main(void){const char*paths[]={"toolchain/ntasm-nova/tests/parameters-entry.nova","toolchain/ntasm-nova/parameters.nova","toolchain/ntasm-nova/callable_names.nova","toolchain/ntasm-nova/semantics.nova","toolchain/ntasm-nova/parser.nova","toolchain/ntasm-nova/ast.nova","toolchain/ntasm-nova/lexer.nova"};char*sources[7]={0};NtFInput inputs[7];for(unsigned i=0;i<7;i++){size_t n=0;sources[i]=load(paths[i],&n);inputs[i]=(NtFInput){paths[i],sources[i],n};}CHECK(sources[0]&&sources[1]&&sources[2]&&sources[3]&&sources[4]&&sources[5]&&sources[6],"sources load");NtArtifact a={0};int compiled=sources[0]&&sources[1]&&sources[2]&&sources[3]&&sources[4]&&sources[5]&&sources[6]&&nova_compile_many_v2(inputs,7,&a);CHECK(compiled,"modules compile");if(!compiled)fprintf(stderr,"E%u %zu:%zu %s\n",a.error.code,a.error.line,a.error.column,a.error.message);NovaHostImage image={0};CHECK(compiled&&nova_host_map(&a,&image),"image maps");
 if(image.memory){const char*unique="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f(in a:u64 @rcx,in b:u64 @rdx)->u64\neffects {}\nclobbers {} {\nreturn a\n}\n}\n";ok(&image,"unique",unique,1,0,2);const char*dupfn="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f(in x:u64 @rcx,in x:u64 @rdx)->u64\neffects {}\nclobbers {} {\nreturn x\n}\n}\n";err(&image,"duplicate function parameter",dupfn,3,314,0);const char*dupimport="module m\ntarget x86_64-nexora-none\nimport a.f as f: fn(in x:u64 @rcx,in x:u64 @rdx)->u64\neffects {}\nclobbers {}\n";err(&image,"duplicate import parameter",dupimport,3,314,0);const char*scoped="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f(in x:u64 @rcx)->u64\neffects {}\nclobbers {} {\nreturn x\n}\nfn g(in x:u64 @rdx)->u64\neffects {}\nclobbers {} {\nreturn x\n}\n}\n";ok(&image,"same name separate functions",scoped,2,0,2);const char*zero="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {}\nclobbers {} {\nreturn 0\n}\n}\n";ok(&image,"zero",zero,1,0,0);err(&image,"short",unique,2,800,1);err(&image,"invalid","target x86_64-nexora-none\n",3,800,1);}
 if(image.memory) nova_host_unmap(&image);
 if(compiled) nt_artifact_free(&a);
 for(unsigned i=0;i<7;i++) free(sources[i]);
 printf("NTASM_NOVA_PARAMETERS checks=%u failures=%u cases=%u\n",checks,failures,cases);
 return failures?1:0;
}
