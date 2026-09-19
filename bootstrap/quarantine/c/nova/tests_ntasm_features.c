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
static void ok(const NovaHostImage*i,const char*n,const char*s,uint64_t sets,uint64_t entries,uint64_t mask){Run r=run(i,s,4);cases++;checks++;if(r.returned||r.runtime||r.out[0]!=sets||r.out[1]!=entries||r.out[2]!=mask||r.out[3]){failures++;fprintf(stderr,"FAIL ok %s ret=%llu out=%llu,%llu,%llu\n",n,(unsigned long long)r.returned,(unsigned long long)r.out[0],(unsigned long long)r.out[1],(unsigned long long)r.out[2]);}}
static void err(const NovaHostImage*i,const char*n,const char*s,uint64_t code,uint64_t reason){Run r=run(i,s,4);cases++;checks++;if(r.returned!=code||r.runtime||r.out[0]==guard||r.out[1]==guard||r.out[2]!=reason){failures++;fprintf(stderr,"FAIL err %s ret=%llu\n",n,(unsigned long long)r.returned);}}
static void bad(const NovaHostImage*i,const char*n,const char*s,uint64_t words){Run r=run(i,s,words);cases++;checks++;if(r.returned!=800||r.runtime||r.out[0]!=guard||r.out[1]!=guard){failures++;fprintf(stderr,"FAIL invalid %s\n",n);}}
int main(void){const char*paths[]={"toolchain/ntasm-nova/tests/features-entry.nova","toolchain/ntasm-nova/features.nova","toolchain/ntasm-nova/parser.nova","toolchain/ntasm-nova/ast.nova","toolchain/ntasm-nova/lexer.nova"};char*sources[5]={0};NtFInput inputs[5];for(unsigned i=0;i<5;i++){size_t n=0;sources[i]=loadf(paths[i],&n);inputs[i]=(NtFInput){paths[i],sources[i],n};}int all=1;for(unsigned i=0;i<5;i++)if(!sources[i])all=0;CHECK(all,"sources load");NtArtifact a={0};int compiled=all&&nova_compile_many_v2(inputs,5,&a);CHECK(compiled,"modules compile");if(!compiled)fprintf(stderr,"E%u %zu:%zu %s\n",a.error.code,a.error.line,a.error.column,a.error.message);NovaHostImage image={0};CHECK(compiled&&nova_host_map(&a,&image),"image maps");if(image.memory){
const char*none="module m\ntarget x86_64-nexora-none\n";ok(&image,"none",none,0,0,1);
const char*all="module m\ntarget x86_64-nexora-none\nfeatures {lm,msr,syscall,sse2,tsc}\n";ok(&image,"all",all,1,5,31);
const char*lm="module m\ntarget x86_64-nexora-none\nfeatures {lm,lm}\n";ok(&image,"lm repeat",lm,1,2,1);
const char*unknown="module m\ntarget x86_64-nexora-none\nfeatures {avx512}\n";err(&image,"unknown",unknown,341,0);
const char*qualified="module m\ntarget x86_64-nexora-none\nfeatures {cpu.sse2}\n";err(&image,"qualified",qualified,341,0);
const char*duplicate="module m\ntarget x86_64-nexora-none\nfeatures {sse2,sse2}\n";err(&image,"duplicate",duplicate,342,1);
const char*two="module m\ntarget x86_64-nexora-none\nfeatures {sse2}\nfeatures {tsc}\n";err(&image,"two sets",two,342,2);
bad(&image,"short",none,3);bad(&image,"invalid","target x86_64-nexora-none\n",4);
}if(image.memory)nova_host_unmap(&image);if(compiled)nt_artifact_free(&a);for(unsigned i=0;i<5;i++)free(sources[i]);printf("NTASM_NOVA_FEATURES checks=%u failures=%u cases=%u\n",checks,failures,cases);return failures?1:0;}
