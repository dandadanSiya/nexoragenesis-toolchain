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
static void ok(const NovaHostImage*i,const char*n,const char*s,uint64_t constants,uint64_t checked,uint64_t bools,uint64_t integers){Run r=run(i,s,4);cases++;checks++;if(r.returned||r.runtime||r.out[0]!=constants||r.out[1]!=checked||r.out[2]!=bools||r.out[3]!=integers){failures++;fprintf(stderr,"FAIL %s ret=%llu runtime=%llu out=%llu,%llu,%llu,%llu\n",n,(unsigned long long)r.returned,(unsigned long long)r.runtime,(unsigned long long)r.out[0],(unsigned long long)r.out[1],(unsigned long long)r.out[2],(unsigned long long)r.out[3]);}}
static void err(const NovaHostImage*i,const char*n,const char*s,uint64_t code){Run r=run(i,s,4);cases++;checks++;if(r.returned!=code||r.runtime||r.out[0]==0||r.out[1]==0||r.out[2]!=0||r.out[3]!=0){failures++;fprintf(stderr,"FAIL %s ret=%llu runtime=%llu out=%llu,%llu,%llu,%llu\n",n,(unsigned long long)r.returned,(unsigned long long)r.runtime,(unsigned long long)r.out[0],(unsigned long long)r.out[1],(unsigned long long)r.out[2],(unsigned long long)r.out[3]);}}
static void bad(const NovaHostImage*i,const char*n,const char*s,uint64_t words){Run r=run(i,s,words);cases++;checks++;if(r.returned!=800||r.runtime||r.out[0]!=guard||r.out[1]!=guard){failures++;fprintf(stderr,"FAIL %s\n",n);}}
int main(void){const char*paths[]={"toolchain/ntasm-nova/tests/constant-types-entry.nova","toolchain/ntasm-nova/constant_types.nova","toolchain/ntasm-nova/types.nova","toolchain/ntasm-nova/parser.nova","toolchain/ntasm-nova/ast.nova","toolchain/ntasm-nova/lexer.nova"};char*sources[6]={0};NtFInput inputs[6];for(unsigned i=0;i<6;i++){size_t n=0;sources[i]=loadf(paths[i],&n);inputs[i]=(NtFInput){paths[i],sources[i],n};}int all=1;for(unsigned i=0;i<6;i++)if(!sources[i])all=0;CHECK(all,"sources load");NtArtifact a={0};int compiled=all&&nova_compile_many_v2(inputs,6,&a);CHECK(compiled,"modules compile");if(!compiled)fprintf(stderr,"E%u %zu:%zu %s\n",a.error.code,a.error.line,a.error.column,a.error.message);NovaHostImage image={0};CHECK(compiled&&nova_host_map(&a,&image),"image maps");if(image.memory){
const char*valid="module m\ntarget x86_64-nexora-none\nconst answer:u64=42\nconst ready:bool=true\n";ok(&image,"valid",valid,2,2,1,1);
const char*u8ok="module m\ntarget x86_64-nexora-none\nconst byte:u8=255\n";ok(&image,"u8 max",u8ok,1,1,0,1);
const char*range="module m\ntarget x86_64-nexora-none\nconst byte:u8=256\n";err(&image,"range",range,362);
const char*boolint="module m\ntarget x86_64-nexora-none\nconst ready:bool=1\n";err(&image,"bool int",boolint,361);
const char*intbool="module m\ntarget x86_64-nexora-none\nconst count:u64=false\n";err(&image,"int bool",intbool,361);
const char*expression="module m\ntarget x86_64-nexora-none\nconst sum:u64=1+2\n";err(&image,"expression",expression,363);
const char*i64ok="module m\ntarget x86_64-nexora-none\nconst top:i64=9223372036854775807\n";ok(&image,"i64 max",i64ok,1,1,0,1);
const char*i64bad="module m\ntarget x86_64-nexora-none\nconst top:i64=9223372036854775808\n";err(&image,"i64 range",i64bad,362);
bad(&image,"short",valid,3);bad(&image,"invalid","target x86_64-nexora-none\n",4);
}if(image.memory)nova_host_unmap(&image);if(compiled)nt_artifact_free(&a);for(unsigned i=0;i<6;i++)free(sources[i]);printf("NTASM_NOVA_CONSTANT_TYPES checks=%u failures=%u cases=%u\n",checks,failures,cases);return failures?1:0;}
