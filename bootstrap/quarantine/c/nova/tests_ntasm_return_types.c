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
static void ok(const NovaHostImage*i,const char*n,const char*s,uint64_t f,uint64_t rts,uint64_t c){Run r=run(i,s,4);cases++;checks++;if(r.returned||r.runtime||r.out[0]!=f||r.out[1]!=rts||r.out[2]!=c||r.out[3]){failures++;fprintf(stderr,"FAIL ok %s ret=%llu out=%llu,%llu,%llu\n",n,(unsigned long long)r.returned,(unsigned long long)r.out[0],(unsigned long long)r.out[1],(unsigned long long)r.out[2]);}}
static void err(const NovaHostImage*i,const char*n,const char*s,uint64_t code,uint64_t reason){Run r=run(i,s,4);cases++;checks++;if(r.returned!=code||r.runtime||r.out[0]==guard||r.out[1]==guard||r.out[2]!=reason){failures++;fprintf(stderr,"FAIL err %s ret=%llu reason=%llu\n",n,(unsigned long long)r.returned,(unsigned long long)r.out[2]);}}
static void bad(const NovaHostImage*i,const char*n,const char*s,uint64_t words){Run r=run(i,s,words);cases++;checks++;if(r.returned!=800||r.runtime||r.out[0]!=guard||r.out[1]!=guard){failures++;fprintf(stderr,"FAIL invalid %s\n",n);}}
int main(void){const char*paths[]={"toolchain/ntasm-nova/tests/return-types-entry.nova","toolchain/ntasm-nova/return_types.nova","toolchain/ntasm-nova/control_flow.nova","toolchain/ntasm-nova/callable_names.nova","toolchain/ntasm-nova/semantics.nova","toolchain/ntasm-nova/parser.nova","toolchain/ntasm-nova/ast.nova","toolchain/ntasm-nova/lexer.nova"};char*sources[8]={0};NtFInput inputs[8];for(unsigned i=0;i<8;i++){size_t n=0;sources[i]=loadf(paths[i],&n);inputs[i]=(NtFInput){paths[i],sources[i],n};}int all=1;for(unsigned i=0;i<8;i++)if(!sources[i])all=0;CHECK(all,"sources load");NtArtifact a={0};int compiled=all&&nova_compile_many_v2(inputs,8,&a);CHECK(compiled,"modules compile");if(!compiled)fprintf(stderr,"E%u %zu:%zu %s\n",a.error.code,a.error.line,a.error.column,a.error.message);NovaHostImage image={0};CHECK(compiled&&nova_host_map(&a,&image),"image maps");if(image.memory){
const char*u8ok="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u8\neffects {}\nclobbers {} {\nreturn 255\n}\n}\n";ok(&image,"u8",u8ok,1,1,1);
const char*u8bad="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u8\neffects {}\nclobbers {} {\nreturn 256\n}\n}\n";err(&image,"u8 range",u8bad,353,1);
const char*boolok="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->bool\neffects {}\nclobbers {} {\nreturn true\n}\n}\n";ok(&image,"bool",boolok,1,1,1);
const char*boolbad="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->bool\neffects {}\nclobbers {} {\nreturn 1\n}\n}\n";err(&image,"bool int",boolbad,352,0);
const char*param="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f(in x:u32 @edi)->u32\neffects {}\nclobbers {} {\nreturn x\n}\n}\n";ok(&image,"param",param,1,1,1);
const char*parambad="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f(in x:u32 @edi)->u64\neffects {}\nclobbers {} {\nreturn x\n}\n}\n";err(&image,"param mismatch",parambad,352,0);
const char*local="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u16\neffects {}\nclobbers {} {\nlet x:u16=1\nreturn x\n}\n}\n";ok(&image,"local",local,1,1,1);
const char*call="module m\ntarget x86_64-nexora-none\nsection .text {\nfn g()->u32\neffects {}\nclobbers {} {\nreturn 1\n}\nfn f()->u32\neffects {}\nclobbers {} {\nreturn g()\n}\n}\n";ok(&image,"call",call,2,2,2);
const char*callbad="module m\ntarget x86_64-nexora-none\nsection .text {\nfn g()->u32\neffects {}\nclobbers {} {\nreturn 1\n}\nfn f()->u64\neffects {}\nclobbers {} {\nreturn g()\n}\n}\n";err(&image,"call mismatch",callbad,352,0);
const char*expression="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {changes_flags}\nclobbers {} {\nreturn 1+2\n}\n}\n";err(&image,"expression",expression,353,2);
const char*i8bad="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->i8\neffects {}\nclobbers {} {\nreturn 128\n}\n}\n";err(&image,"i8 range",i8bad,353,1);
const char*pointer="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f(in p:ptr<user,u8> @rdi)->ptr<user,u8>\neffects {}\nclobbers {} {\nreturn p\n}\n}\n";ok(&image,"pointer",pointer,1,1,1);
bad(&image,"short",u8ok,3);bad(&image,"invalid","target x86_64-nexora-none\n",4);
}if(image.memory)nova_host_unmap(&image);if(compiled)nt_artifact_free(&a);for(unsigned i=0;i<8;i++)free(sources[i]);printf("NTASM_NOVA_RETURN_TYPES checks=%u failures=%u cases=%u\n",checks,failures,cases);return failures?1:0;}
