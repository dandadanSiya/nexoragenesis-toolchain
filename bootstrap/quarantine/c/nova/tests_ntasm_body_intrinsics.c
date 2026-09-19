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
static void ok(const NovaHostImage*i,const char*n,const char*s,uint64_t f,uint64_t c,uint64_t v){Run r=run(i,s,4);cases++;checks++;if(r.returned||r.runtime||r.out[0]!=f||r.out[1]!=c||r.out[2]!=v||r.out[3]){failures++;fprintf(stderr,"FAIL ok %s ret=%llu\n",n,(unsigned long long)r.returned);}}
static void err(const NovaHostImage*i,const char*n,const char*s,uint64_t missing){Run r=run(i,s,4);cases++;checks++;if(r.returned!=334||r.runtime||r.out[0]==guard||r.out[1]==guard||r.out[2]!=1||r.out[3]!=missing){failures++;fprintf(stderr,"FAIL err %s ret=%llu miss=%llu\n",n,(unsigned long long)r.returned,(unsigned long long)r.out[3]);}}
static void bad(const NovaHostImage*i,const char*n,const char*s,uint64_t words){Run r=run(i,s,words);cases++;checks++;if(r.returned!=800||r.runtime||r.out[0]!=guard||r.out[1]!=guard){failures++;fprintf(stderr,"FAIL invalid %s\n",n);}}
int main(void){const char*paths[]={"toolchain/ntasm-nova/tests/body-intrinsics-entry.nova","toolchain/ntasm-nova/body_intrinsics.nova","toolchain/ntasm-nova/effects.nova","toolchain/ntasm-nova/callable_names.nova","toolchain/ntasm-nova/semantics.nova","toolchain/ntasm-nova/parser.nova","toolchain/ntasm-nova/ast.nova","toolchain/ntasm-nova/lexer.nova"};char*sources[8]={0};NtFInput inputs[8];for(unsigned i=0;i<8;i++){size_t n=0;sources[i]=loadf(paths[i],&n);inputs[i]=(NtFInput){paths[i],sources[i],n};}int all=1;for(unsigned i=0;i<8;i++)if(!sources[i])all=0;CHECK(all,"sources load");NtArtifact a={0};int compiled=all&&nova_compile_many_v2(inputs,8,&a);CHECK(compiled,"modules compile");if(!compiled)fprintf(stderr,"E%u %zu:%zu %s\n",a.error.code,a.error.line,a.error.column,a.error.message);NovaHostImage image={0};CHECK(compiled&&nova_host_map(&a,&image),"image maps");if(image.memory){
const char*none="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {}\nclobbers {} {\nnop\nreturn 0\n}\n}\n";ok(&image,"none",none,1,0,0);
const char*clock="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {reads_clock}\nclobbers {rax,rdx} {\nrdtsc\nreturn 0\n}\n}\n";ok(&image,"clock",clock,1,1,1);
const char*clock_bad="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {}\nclobbers {rax,rdx} {\nrdtsc\nreturn 0\n}\n}\n";err(&image,"clock missing",clock_bad,512);
const char*cli="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {privileged,interrupt_state}\nclobbers {} {\ncli\nreturn 0\n}\n}\n";ok(&image,"cli",cli,1,1,1);
const char*cli_bad="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {interrupt_state}\nclobbers {} {\ncli\nreturn 0\n}\n}\n";err(&image,"cli privileged",cli_bad,1);
const char*syscall_bad="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {control}\nclobbers {rcx,r11} {\nsyscall\nreturn 0\n}\n}\n";err(&image,"syscall flags",syscall_bad,2);
const char*sysret="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->never\neffects {privileged,changes_flags,control,no_return}\nclobbers {} {\nsysretq\n}\n}\n";ok(&image,"sysret",sysret,1,1,1);
const char*sysret_bad="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->never\neffects {privileged,changes_flags,control}\nclobbers {} {\nsysretq\n}\n}\n";err(&image,"sysret no return",sysret_bad,128);
const char*msr="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {privileged,msr}\nclobbers {rax,rdx} {\nrdmsr\nreturn 0\n}\n}\n";ok(&image,"msr",msr,1,1,1);
const char*io="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {privileged,io_port}\nclobbers {rax} {\nin rax,1\nreturn rax\n}\n}\n";ok(&image,"io",io,1,1,1);
const char*add="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f(in x:u64 @rax)->u64\neffects {changes_flags}\nclobbers {r10} {\nadd r10,rax\nreturn r10\n}\n}\n";ok(&image,"add flags",add,1,1,1);
const char*add_bad="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f(in x:u64 @rax)->u64\neffects {}\nclobbers {r10} {\nadd r10,rax\nreturn r10\n}\n}\n";err(&image,"add missing",add_bad,2);
const char*compare="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f(in x:u64 @rax,in y:u64 @rbx)->u64\neffects {changes_flags}\nclobbers {} {\ncmp rax,rbx\nreturn 0\n}\n}\n";ok(&image,"cmp flags",compare,1,1,1);
const char*shift_bad="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f(in x:u64 @rax)->u64\neffects {}\nclobbers {rax} {\nshl rax,1\nreturn rax\n}\n}\n";err(&image,"shift missing",shift_bad,2);
const char*mov="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f(in x:u64 @rbx)->u64\neffects {}\nclobbers {rax} {\nmov rax,rbx\nreturn rax\n}\n}\n";ok(&image,"mov no flags",mov,1,0,0);
const char*xchg="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f(in x:u64 @rax,in y:u64 @rcx)->u64\neffects {}\nclobbers {rax,rcx} {\nxchg rax,rcx\nreturn rax\n}\n}\n";ok(&image,"xchg no flags",xchg,1,0,0);
bad(&image,"short",none,3);bad(&image,"invalid","target x86_64-nexora-none\n",4);
}if(image.memory)nova_host_unmap(&image);if(compiled)nt_artifact_free(&a);for(unsigned i=0;i<8;i++)free(sources[i]);printf("NTASM_NOVA_BODY_INTRINSICS checks=%u failures=%u cases=%u\n",checks,failures,cases);return failures?1:0;}
