#include "nova.h"
#include "nova/run_host.h"
#include "nova/runtime_v2.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures,cases;static const uint64_t guard=UINT64_C(0xcccccccccccccccc);
#define CHECK(c,m) do{checks++;if(!(c)){failures++;fprintf(stderr,"FAIL %s\n",m);}}while(0)
static char*loadf(const char*p,size_t*n){FILE*f=fopen(p,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long z=ftell(f);fseek(f,0,SEEK_SET);char*b=z>=0?malloc((size_t)z+1):NULL;if(!b||fread(b,1,(size_t)z,f)!=(size_t)z){free(b);fclose(f);return NULL;}fclose(f);b[z]=0;*n=(size_t)z;return b;}
typedef struct{uint64_t returned,runtime,out[4];}Run;static Run run(const NovaHostImage*i,const char*s,uint64_t words){Run r={.out={guard,guard,guard,guard}};NovaContextV2 c={0};CHECK(nova_context_init_v2(&c),"context init");uint64_t sh=nova_borrow_v2(&c,(void*)s,strlen(s),0),oh=nova_borrow_v2(&c,r.out,words*8,1);uint64_t a[]={sh,strlen(s),oh,words};r.returned=i->entry(&c,a,4);r.runtime=c.error;CHECK(nova_context_destroy_v2(&c),"context cleanup");return r;}
static void ok(const NovaHostImage*i,const char*n,const char*s,uint64_t f,uint64_t t,uint64_t returns){Run r=run(i,s,4);cases++;checks++;if(r.returned||r.runtime||r.out[0]!=f||r.out[1]!=t||r.out[2]!=returns||r.out[3]){failures++;fprintf(stderr,"FAIL ok %s ret=%llu out=%llu,%llu,%llu,%llu\n",n,(unsigned long long)r.returned,(unsigned long long)r.out[0],(unsigned long long)r.out[1],(unsigned long long)r.out[2],(unsigned long long)r.out[3]);}}
static void err(const NovaHostImage*i,const char*n,const char*s,uint64_t code,uint64_t reason){Run r=run(i,s,4);cases++;checks++;if(r.returned!=code||r.runtime||r.out[0]==guard||r.out[1]==guard||r.out[2]!=reason){failures++;fprintf(stderr,"FAIL err %s ret=%llu reason=%llu\n",n,(unsigned long long)r.returned,(unsigned long long)r.out[2]);}}
static void bad(const NovaHostImage*i,const char*n,const char*s,uint64_t words){Run r=run(i,s,words);cases++;checks++;if(r.returned!=800||r.runtime||r.out[0]!=guard||r.out[1]!=guard){failures++;fprintf(stderr,"FAIL invalid %s\n",n);}}
int main(void){const char*paths[]={"toolchain/ntasm-nova/tests/control-flow-entry.nova","toolchain/ntasm-nova/control_flow.nova","toolchain/ntasm-nova/callable_names.nova","toolchain/ntasm-nova/semantics.nova","toolchain/ntasm-nova/parser.nova","toolchain/ntasm-nova/ast.nova","toolchain/ntasm-nova/lexer.nova"};char*sources[7]={0};NtFInput inputs[7];for(unsigned i=0;i<7;i++){size_t n=0;sources[i]=loadf(paths[i],&n);inputs[i]=(NtFInput){paths[i],sources[i],n};}int all=1;for(unsigned i=0;i<7;i++)if(!sources[i])all=0;CHECK(all,"sources load");NtArtifact a={0};int compiled=all&&nova_compile_many_v2(inputs,7,&a);CHECK(compiled,"modules compile");if(!compiled)fprintf(stderr,"E%u %zu:%zu %s\n",a.error.code,a.error.line,a.error.column,a.error.message);NovaHostImage image={0};CHECK(compiled&&nova_host_map(&a,&image),"image maps");if(image.memory){
const char*value="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {}\nclobbers {} {\nreturn 1\n}\n}\n";ok(&image,"value",value,1,1,1);
const char*fall="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {}\nclobbers {} {\nnop\n}\n}\n";err(&image,"fall",fall,338,1);
const char*bare="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {}\nclobbers {} {\nreturn\n}\n}\n";err(&image,"bare",bare,339,0);
const char*never_ok="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->never\neffects {privileged,changes_flags,control,no_return}\nclobbers {} {\nsysretq\n}\n}\n";ok(&image,"never",never_ok,1,1,0);
const char*never_return="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->never\neffects {no_return}\nclobbers {} {\nreturn 0\n}\n}\n";err(&image,"never return",never_return,337,0);
const char*never_fall="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->never\neffects {no_return}\nclobbers {} {\nnop\n}\n}\n";err(&image,"never fall",never_fall,338,2);
const char*branches="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {}\nclobbers {} {\nif feature(sse2) {\nreturn 1\n} else {\nreturn 2\n}\n}\n}\n";ok(&image,"branches",branches,1,1,2);
const char*one_branch="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {}\nclobbers {} {\nif feature(sse2) {\nreturn 1\n}\n}\n}\n";err(&image,"one branch",one_branch,338,1);
const char*never_branches="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->never\neffects {no_return}\nclobbers {} {\nif feature(sse2) {\nud2\n} else {\nud2\n}\n}\n}\n";ok(&image,"never branches",never_branches,1,1,0);
const char*two="module m\ntarget x86_64-nexora-none\nsection .text {\nfn a()->u64\neffects {}\nclobbers {} {\nreturn 1\n}\nfn b()->u64\neffects {}\nclobbers {} {\nreturn 2\n}\n}\n";ok(&image,"two",two,2,2,2);
const char*never_call="module m\ntarget x86_64-nexora-none\nsection .text {\nfn halt()->never\neffects {no_return}\nclobbers {} {\nud2\n}\nfn main()->never\neffects {no_return}\nclobbers {} {\ncall halt()\n}\n}\n";ok(&image,"never call",never_call,2,2,0);
const char*never_import="module m\ntarget x86_64-nexora-none\nimport a.halt as halt: fn()->never\neffects {no_return}\nclobbers {}\nsection .text {\nfn main()->never\neffects {no_return}\nclobbers {} {\ncall halt()\n}\n}\n";ok(&image,"never import",never_import,1,1,0);
const char*value_call="module m\ntarget x86_64-nexora-none\nsection .text {\nfn value()->u64\neffects {}\nclobbers {} {\nreturn 1\n}\nfn main()->never\neffects {no_return}\nclobbers {} {\ncall value()\n}\n}\n";err(&image,"value call",value_call,338,2);
bad(&image,"short",value,3);bad(&image,"invalid","target x86_64-nexora-none\n",4);
}if(image.memory)nova_host_unmap(&image);if(compiled)nt_artifact_free(&a);for(unsigned i=0;i<7;i++)free(sources[i]);printf("NTASM_NOVA_CONTROL_FLOW checks=%u failures=%u cases=%u\n",checks,failures,cases);return failures?1:0;}
