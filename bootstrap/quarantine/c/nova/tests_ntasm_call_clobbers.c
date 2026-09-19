#include "nova.h"
#include "nova/run_host.h"
#include "nova/runtime_v2.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures,cases;static const uint64_t guard=UINT64_C(0xcccccccccccccccc);
#define CHECK(c,m) do{checks++;if(!(c)){failures++;fprintf(stderr,"FAIL %s\n",m);}}while(0)
static char*load(const char*p,size_t*n){FILE*f=fopen(p,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long z=ftell(f);fseek(f,0,SEEK_SET);char*b=z>=0?malloc((size_t)z+1):NULL;if(!b||fread(b,1,(size_t)z,f)!=(size_t)z){free(b);fclose(f);return NULL;}fclose(f);b[z]=0;*n=(size_t)z;return b;}
typedef struct{uint64_t returned,runtime,out[4];}Run;
static Run run(const NovaHostImage*i,const char*s,uint64_t words){Run r={.out={guard,guard,guard,guard}};NovaContextV2 c={0};CHECK(nova_context_init_v2(&c),"context init");uint64_t sh=nova_borrow_v2(&c,(void*)s,strlen(s),0),oh=nova_borrow_v2(&c,r.out,words*8,1);uint64_t a[]={sh,strlen(s),oh,words};r.returned=i->entry(&c,a,4);r.runtime=c.error;CHECK(nova_context_destroy_v2(&c),"context cleanup");return r;}
static void ok(const NovaHostImage*i,const char*n,const char*s,uint64_t d,uint64_t c,uint64_t v){Run r=run(i,s,4);cases++;checks++;if(r.returned||r.runtime||r.out[0]!=d||r.out[1]!=c||r.out[2]!=v||r.out[3]){failures++;fprintf(stderr,"FAIL ok %s ret=%llu\n",n,(unsigned long long)r.returned);}}
static void err(const NovaHostImage*i,const char*n,const char*s,uint64_t code,uint64_t missing){Run r=run(i,s,4);cases++;checks++;if(r.returned!=code||r.runtime||r.out[0]==guard||r.out[1]==guard||(code==331&&(r.out[2]!=1||r.out[3]!=missing))){failures++;fprintf(stderr,"FAIL err %s ret=%llu out=%llu,%llu\n",n,(unsigned long long)r.returned,(unsigned long long)r.out[2],(unsigned long long)r.out[3]);}}
static void bad(const NovaHostImage*i,const char*n,const char*s,uint64_t words){Run r=run(i,s,words);cases++;checks++;if(r.returned!=800||r.runtime||r.out[0]!=guard||r.out[1]!=guard){failures++;fprintf(stderr,"FAIL invalid %s\n",n);}}
int main(void){const char*paths[]={"toolchain/ntasm-nova/tests/call-clobbers-entry.nova","toolchain/ntasm-nova/call_clobbers.nova","toolchain/ntasm-nova/calls.nova","toolchain/ntasm-nova/refs.nova","toolchain/ntasm-nova/clobbers.nova","toolchain/ntasm-nova/register_widths.nova","toolchain/ntasm-nova/callable_names.nova","toolchain/ntasm-nova/semantics.nova","toolchain/ntasm-nova/parser.nova","toolchain/ntasm-nova/ast.nova","toolchain/ntasm-nova/lexer.nova"};char*sources[11]={0};NtFInput inputs[11];for(unsigned i=0;i<11;i++){size_t n=0;sources[i]=load(paths[i],&n);inputs[i]=(NtFInput){paths[i],sources[i],n};}int all=1;for(unsigned i=0;i<11;i++)if(!sources[i])all=0;CHECK(all,"sources load");NtArtifact a={0};int compiled=all&&nova_compile_many_v2(inputs,11,&a);CHECK(compiled,"modules compile");if(!compiled)fprintf(stderr,"E%u %zu:%zu %s\n",a.error.code,a.error.line,a.error.column,a.error.message);NovaHostImage image={0};CHECK(compiled&&nova_host_map(&a,&image),"image maps");if(image.memory){
const char*none="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {}\nclobbers {} {\nreturn 0\n}\n}\n";ok(&image,"none",none,1,0,0);
const char*alias="module m\ntarget x86_64-nexora-none\nsection .text {\nfn leaf()->u64\neffects {}\nclobbers {rax} {\nreturn 0\n}\nfn main()->u64\neffects {}\nclobbers {eax} {\ncall leaf()\nreturn 0\n}\n}\n";ok(&image,"alias",alias,2,1,1);
const char*missing="module m\ntarget x86_64-nexora-none\nsection .text {\nfn leaf()->u64\neffects {}\nclobbers {rax} {\nreturn 0\n}\nfn main()->u64\neffects {}\nclobbers {} {\ncall leaf()\nreturn 0\n}\n}\n";err(&image,"missing",missing,331,1);
const char*r10="module m\ntarget x86_64-nexora-none\nsection .text {\nfn leaf()->u64\neffects {}\nclobbers {r10} {\nreturn 0\n}\nfn main()->u64\neffects {}\nclobbers {r10d} {\ncall leaf()\nreturn 0\n}\n}\n";ok(&image,"r10",r10,2,1,1);
const char*superset="module m\ntarget x86_64-nexora-none\nsection .text {\nfn leaf()->u64\neffects {}\nclobbers {rax,rcx} {\nreturn 0\n}\nfn main()->u64\neffects {}\nclobbers {rax,rcx,rdx} {\ncall leaf()\nreturn 0\n}\n}\n";ok(&image,"superset",superset,2,1,1);
const char*imported="module m\ntarget x86_64-nexora-none\nimport a.f as f: fn()->u64\neffects {}\nclobbers {r11}\nsection .text {\nfn main()->u64\neffects {}\nclobbers {r11d} {\ncall f()\nreturn 0\n}\n}\n";ok(&image,"import",imported,2,1,1);
const char*chain="module m\ntarget x86_64-nexora-none\nsection .text {\nfn leaf()->u64\neffects {}\nclobbers {rdx} {\nreturn 0\n}\nfn mid()->u64\neffects {}\nclobbers {rdx} {\ncall leaf()\nreturn 0\n}\nfn main()->u64\neffects {}\nclobbers {rdx} {\nreturn mid()\n}\n}\n";ok(&image,"chain",chain,3,2,2);
const char*unknown="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {}\nclobbers {foo} {\nreturn 0\n}\n}\n";err(&image,"unknown",unknown,330,0);
bad(&image,"short",none,3);bad(&image,"invalid","target x86_64-nexora-none\n",4);
}if(image.memory)nova_host_unmap(&image);if(compiled)nt_artifact_free(&a);for(unsigned i=0;i<11;i++)free(sources[i]);printf("NTASM_NOVA_CALL_CLOBBERS checks=%u failures=%u cases=%u\n",checks,failures,cases);return failures?1:0;}
