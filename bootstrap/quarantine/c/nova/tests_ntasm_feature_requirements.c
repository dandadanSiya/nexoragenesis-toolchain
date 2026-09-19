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
static void ok(const NovaHostImage*i,const char*n,const char*s,uint64_t p,uint64_t a,uint64_t m){Run r=run(i,s,4);cases++;checks++;if(r.returned||r.runtime||r.out[0]!=p||r.out[1]!=a||r.out[2]!=m||r.out[3]){failures++;fprintf(stderr,"FAIL ok %s ret=%llu\n",n,(unsigned long long)r.returned);}}
static void err(const NovaHostImage*i,const char*n,const char*s,uint64_t code){Run r=run(i,s,4);cases++;checks++;if(r.returned!=code||r.runtime||r.out[0]==guard||r.out[1]==guard){failures++;fprintf(stderr,"FAIL err %s ret=%llu\n",n,(unsigned long long)r.returned);}}
static void bad(const NovaHostImage*i,const char*n,const char*s,uint64_t words){Run r=run(i,s,words);cases++;checks++;if(r.returned!=800||r.runtime||r.out[0]!=guard||r.out[1]!=guard){failures++;fprintf(stderr,"FAIL invalid %s\n",n);}}
int main(void){const char*paths[]={"toolchain/ntasm-nova/tests/feature-requirements-entry.nova","toolchain/ntasm-nova/feature_requirements.nova","toolchain/ntasm-nova/features.nova","toolchain/ntasm-nova/contracts.nova","toolchain/ntasm-nova/parser.nova","toolchain/ntasm-nova/ast.nova","toolchain/ntasm-nova/lexer.nova"};char*sources[7]={0};NtFInput inputs[7];for(unsigned i=0;i<7;i++){size_t n=0;sources[i]=loadf(paths[i],&n);inputs[i]=(NtFInput){paths[i],sources[i],n};}int all=1;for(unsigned i=0;i<7;i++)if(!sources[i])all=0;CHECK(all,"sources load");NtArtifact a={0};int compiled=all&&nova_compile_many_v2(inputs,7,&a);CHECK(compiled,"modules compile");if(!compiled)fprintf(stderr,"E%u %zu:%zu %s\n",a.error.code,a.error.line,a.error.column,a.error.message);NovaHostImage image={0};CHECK(compiled&&nova_host_map(&a,&image),"image maps");if(image.memory){
const char*none="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {}\nclobbers {} {\nreturn 0\n}\n}\n";ok(&image,"none",none,0,0,0);
const char*sse="module m\ntarget x86_64-nexora-none\nfeatures {sse2}\nsection .text {\nfn f()->u64\neffects {}\nclobbers {}\nrequires {feature(sse2)} {\nreturn 0\n}\n}\n";ok(&image,"sse",sse,1,1,8);
const char*missing="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {}\nclobbers {}\nrequires {feature(sse2)} {\nreturn 0\n}\n}\n";err(&image,"missing",missing,344);
const char*unknown="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {}\nclobbers {}\nrequires {feature(avx512)} {\nreturn 0\n}\n}\n";err(&image,"unknown",unknown,343);
const char*lm="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {}\nclobbers {}\nrequires {feature(lm)} {\nreturn 0\n}\n}\n";ok(&image,"lm",lm,1,1,1);
const char*ensures="module m\ntarget x86_64-nexora-none\nfeatures {tsc}\nsection .text {\nfn f()->u64\neffects {}\nclobbers {}\nensures {feature(tsc)} {\nreturn 0\n}\n}\n";ok(&image,"ensures",ensures,1,1,16);
const char*imported="module m\ntarget x86_64-nexora-none\nfeatures {msr}\nimport a.f as f: fn()->u64\neffects {}\nclobbers {}\nrequires {feature(msr)}\n";ok(&image,"import",imported,1,1,2);
bad(&image,"short",none,3);bad(&image,"invalid","target x86_64-nexora-none\n",4);
}if(image.memory)nova_host_unmap(&image);if(compiled)nt_artifact_free(&a);for(unsigned i=0;i<7;i++)free(sources[i]);printf("NTASM_NOVA_FEATURE_REQUIREMENTS checks=%u failures=%u cases=%u\n",checks,failures,cases);return failures?1:0;}
