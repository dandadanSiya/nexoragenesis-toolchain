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
static char*loadf(const char*p,size_t*n){FILE*f=fopen(p,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long z=ftell(f);fseek(f,0,SEEK_SET);char*b=z>=0?malloc((size_t)z+1):NULL;if(!b||fread(b,1,(size_t)z,f)!=(size_t)z){free(b);fclose(f);return NULL;}fclose(f);b[z]=0;*n=(size_t)z;return b;}
typedef struct{uint64_t returned,runtime,out[4];}Run;
static Run run(const NovaHostImage*i,const char*s,uint64_t words){Run r={.out={guard,guard,guard,guard}};NovaContextV2 c={0};CHECK(nova_context_init_v2(&c),"context init");uint64_t sh=nova_borrow_v2(&c,(void*)s,strlen(s),0),oh=nova_borrow_v2(&c,r.out,words*8,1);uint64_t a[]={sh,strlen(s),oh,words};r.returned=i->entry(&c,a,4);r.runtime=c.error;CHECK(nova_context_destroy_v2(&c),"context cleanup");return r;}
static void ok(const NovaHostImage*i,const char*n,const char*s,uint64_t data,uint64_t locals,uint64_t checked,uint64_t deferred){Run r=run(i,s,4);cases++;checks++;if(r.returned||r.runtime||r.out[0]!=data||r.out[1]!=locals||r.out[2]!=checked||r.out[3]!=deferred){failures++;fprintf(stderr,"FAIL %s ret=%llu runtime=%llu out=%llu,%llu,%llu,%llu\n",n,(unsigned long long)r.returned,(unsigned long long)r.runtime,(unsigned long long)r.out[0],(unsigned long long)r.out[1],(unsigned long long)r.out[2],(unsigned long long)r.out[3]);}}
static void err(const NovaHostImage*i,const char*n,const char*s,uint64_t code,uint64_t kind){Run r=run(i,s,4);cases++;checks++;if(r.returned!=code||r.runtime||r.out[0]==0||r.out[1]==0||r.out[2]!=kind||r.out[3]!=0){failures++;fprintf(stderr,"FAIL %s ret=%llu runtime=%llu out=%llu,%llu,%llu,%llu\n",n,(unsigned long long)r.returned,(unsigned long long)r.runtime,(unsigned long long)r.out[0],(unsigned long long)r.out[1],(unsigned long long)r.out[2],(unsigned long long)r.out[3]);}}
static void err_code(const NovaHostImage*i,const char*n,const char*s,uint64_t code){Run r=run(i,s,4);cases++;checks++;if(r.returned!=code||r.runtime||r.out[0]==0||r.out[1]==0){failures++;fprintf(stderr,"FAIL %s ret=%llu runtime=%llu out=%llu,%llu,%llu,%llu\n",n,(unsigned long long)r.returned,(unsigned long long)r.runtime,(unsigned long long)r.out[0],(unsigned long long)r.out[1],(unsigned long long)r.out[2],(unsigned long long)r.out[3]);}}
static void bad(const NovaHostImage*i,const char*n,const char*s,uint64_t words){Run r=run(i,s,words);cases++;checks++;if(r.returned!=800||r.runtime||r.out[0]!=guard||r.out[1]!=guard){failures++;fprintf(stderr,"FAIL %s ret=%llu runtime=%llu\n",n,(unsigned long long)r.returned,(unsigned long long)r.runtime);}}
int main(void){const char*paths[]={"toolchain/ntasm-nova/tests/literal-initializers-entry.nova","toolchain/ntasm-nova/literal_initializers.nova","toolchain/ntasm-nova/types.nova","toolchain/ntasm-nova/parser.nova","toolchain/ntasm-nova/ast.nova","toolchain/ntasm-nova/lexer.nova"};char*sources[6]={0};NtFInput inputs[6];for(unsigned i=0;i<6;i++){size_t n=0;sources[i]=loadf(paths[i],&n);inputs[i]=(NtFInput){paths[i],sources[i],n};}int all=1;for(unsigned i=0;i<6;i++)if(!sources[i])all=0;CHECK(all,"sources load");NtArtifact a={0};int compiled=all&&nova_compile_many_v2(inputs,6,&a);CHECK(compiled,"modules compile");if(!compiled)fprintf(stderr,"E%u %zu:%zu %s\n",a.error.code,a.error.line,a.error.column,a.error.message);NovaHostImage image={0};CHECK(compiled&&nova_host_map(&a,&image),"image maps");if(image.memory){
const char*both="module m\ntarget x86_64-nexora-none\nsection .rdata {\ndata answer:u64=42\n}\nsection .text {\nfn f()->u16\neffects {}\nclobbers {} {\nlet x:u16=7\nreturn x\n}\n}\n";ok(&image,"data and local",both,1,1,2,0);
const char*bools="module m\ntarget x86_64-nexora-none\nsection .rdata {\ndata yes:bool=true\n}\nsection .text {\nfn f()->bool\neffects {}\nclobbers {} {\nlet no:bool=false\nreturn no\n}\n}\n";ok(&image,"bools",bools,1,1,2,0);
const char*deferred="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f(in p:u64 @rcx)->u64\neffects {}\nclobbers {} {\nlet x:u64=p\nreturn x\n}\n}\n";ok(&image,"nonliteral deferred",deferred,0,1,0,1);
const char*u8ok="module m\ntarget x86_64-nexora-none\nsection .rdata {\ndata x:u8=255\n}\n";ok(&image,"u8 max",u8ok,1,0,1,0);
const char*u8bad="module m\ntarget x86_64-nexora-none\nsection .rdata {\ndata x:u8=256\n}\n";err(&image,"u8 overflow",u8bad,356,24);
const char*boolint="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {}\nclobbers {} {\nlet x:bool=1\nreturn 0\n}\n}\n";err(&image,"bool from integer",boolint,355,27);
const char*intbool="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {}\nclobbers {} {\nlet x:u64=true\nreturn 0\n}\n}\n";err(&image,"integer from bool",intbool,355,27);
const char*i8ok="module m\ntarget x86_64-nexora-none\nsection .rdata {\ndata x:i8=127\n}\n";ok(&image,"i8 max",i8ok,1,0,1,0);
const char*i8bad="module m\ntarget x86_64-nexora-none\nsection .rdata {\ndata x:i8=128\n}\n";err(&image,"i8 overflow",i8bad,356,24);
const char*unknown="module m\ntarget x86_64-nexora-none\nsection .rdata {\ndata x:telepathy=1\n}\n";err_code(&image,"unknown type first",unknown,350);
bad(&image,"short",both,3);bad(&image,"invalid","target x86_64-nexora-none\n",4);
}if(image.memory)nova_host_unmap(&image);if(compiled)nt_artifact_free(&a);for(unsigned i=0;i<6;i++)free(sources[i]);printf("NTASM_NOVA_LITERAL_INITIALIZERS checks=%u failures=%u cases=%u\n",checks,failures,cases);return failures?1:0;}
