#include "nova.h"
#include "nova/run_host.h"
#include "nova/runtime_v2.h"
#include "object.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures,cases;
#define CHECK(c,m) do{checks++;if(!(c)){failures++;fprintf(stderr,"FAIL %s\n",m);}}while(0)
static char*load(const char*p,size_t*n){FILE*f=fopen(p,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long z=ftell(f);fseek(f,0,SEEK_SET);char*b=z>=0?malloc((size_t)z+1):NULL;if(!b||fread(b,1,(size_t)z,f)!=(size_t)z){free(b);fclose(f);return NULL;}fclose(f);b[z]=0;*n=(size_t)z;return b;}
static uint64_t r64(const uint8_t*p){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)p[i]<<(i*8);return v;}
static NtBuffer write_object(const NtObject*o){NtBuffer f={0};NtDiagnostic e={0};CHECK(nt_object_write(o,&f,&e),"write NXO fixture");return f;}
typedef struct{uint64_t returned,runtime;uint8_t*out;}Run;
static Run run(const NovaHostImage*i,const NtBuffer*l,const NtBuffer*r,size_t capacity){Run x={0};x.out=malloc(capacity+16);CHECK(x.out!=NULL,"output allocation");if(!x.out)return x;memset(x.out,0xcc,capacity+16);NovaContextV2 c={0};CHECK(nova_context_init_v2(&c),"context init");uint64_t lh=nova_borrow_v2(&c,l->bytes,l->size,0),rh=nova_borrow_v2(&c,r->bytes,r->size,0),oh=nova_borrow_v2(&c,x.out,capacity,1);uint64_t a[]={lh,l->size,rh,r->size,oh,capacity};x.returned=i->entry(&c,a,6);x.runtime=c.error;CHECK(nova_context_destroy_v2(&c),"context cleanup");return x;}
static void expect_error(const NovaHostImage*i,const char*n,const NtBuffer*l,const NtBuffer*r,size_t cap,uint64_t code){Run x=run(i,l,r,cap);cases++;CHECK(x.returned==code&&x.runtime==0,"expected link PE error");if(x.out){CHECK(x.out[0]==0xcc&&x.out[1]==0xcc,"error output transactional");free(x.out);}if(x.returned!=code)fprintf(stderr,"FAIL %s ret=%llu\n",n,(unsigned long long)x.returned);}
int main(void){const char*paths[]={"toolchain/ntasm-nova/tests/nxo-link-pe-entry.nova","toolchain/ntasm-nova/nxo_link_pe.nova","toolchain/ntasm-nova/nxo_merge.nova","toolchain/ntasm-nova/nxo_pe.nova","toolchain/ntasm-nova/nxo.nova","toolchain/ntasm-nova/pe.nova"};char*sources[6]={0};NtFInput inputs[6];for(unsigned i=0;i<6;i++){size_t n=0;sources[i]=load(paths[i],&n);inputs[i]=(NtFInput){paths[i],sources[i],n};}CHECK(sources[0]&&sources[1]&&sources[2]&&sources[3]&&sources[4]&&sources[5],"sources load");NtArtifact compiler={0};int compiled=sources[0]&&sources[1]&&sources[2]&&sources[3]&&sources[4]&&sources[5]&&nova_compile_many_v2(inputs,6,&compiler);CHECK(compiled,"modules compile");if(!compiled)fprintf(stderr,"E%u %zu:%zu %s\n",compiler.error.code,compiler.error.line,compiler.error.column,compiler.error.message);NovaHostImage image={0};CHECK(compiled&&nova_host_map(&compiler,&image),"image maps");
 if(image.memory){uint8_t caller[]={0xe8,0,0,0,0,0xc3},callee[]={0xb8,0x2a,0,0,0,0xc3};NtObjectSymbol left_symbols[]={{"helper","fn:u64",0,NT_OBJECT_FUNCTION|NT_OBJECT_IMPORT,0,0},{"main","fn:u64",1,NT_OBJECT_FUNCTION|NT_OBJECT_EXPORT,0,6}};NtObjectReloc call={1,NT_OBJECT_REL32,1,1,0};NtObject left={0};left.target=1;left.entry_symbol=2;left.sections[0]=(NtBuffer){caller,6,6};left.symbols=left_symbols;left.symbol_count=2;left.relocations=&call;left.relocation_count=1;NtObjectSymbol helper={"helper","fn:u64",1,NT_OBJECT_FUNCTION|NT_OBJECT_EXPORT,0,6};NtObject right={0};right.target=1;right.sections[0]=(NtBuffer){callee,6,6};right.symbols=&helper;right.symbol_count=1;NtBuffer lf=write_object(&left),rf=write_object(&right);
  NtObject pair[]={left,right},linked={0};NtArtifact oracle={0};NtDiagnostic error={0};CHECK(nt_object_link(pair,2,&linked,&error)&&nt_object_materialize(&linked,&oracle,&error)&&nt_make_pe(&oracle),"C oracle links PE");Run ok=run(&image,&lf,&rf,65536);cases++;CHECK(ok.returned==0&&ok.runtime==0,"Nova link PE success");if(ok.out){uint64_t size=r64(ok.out);int parity=size==oracle.pe.size&&!memcmp(ok.out+8,oracle.pe.bytes,oracle.pe.size);if(!parity){size_t first=0,limit=size<oracle.pe.size?(size_t)size:oracle.pe.size;while(first<limit&&ok.out[8+first]==oracle.pe.bytes[first])first++;fprintf(stderr,"PE mismatch nova=%llu oracle=%zu first=%zu novaByte=%u oracleByte=%u\n",(unsigned long long)size,oracle.pe.size,first,first<size?ok.out[8+first]:0,first<oracle.pe.size?oracle.pe.bytes[first]:0);}CHECK(parity,"Nova link PE byte parity");int guards=1;for(size_t i=8+size;i<65552;i++)if(ok.out[i]!=0xcc)guards=0;CHECK(guards,"success output guards");free(ok.out);}nt_artifact_free(&oracle);nt_object_free(&linked);
  expect_error(&image,"short output",&lf,&rf,8,827);
  NtObjectSymbol wrong={"helper","fn:u32",1,NT_OBJECT_FUNCTION|NT_OBJECT_EXPORT,0,6};right.symbols=&wrong;NtBuffer wrongf=write_object(&right);expect_error(&image,"contract mismatch",&lf,&wrongf,65536,825);
  right.symbols=&helper;right.entry_symbol=1;NtBuffer twoentry=write_object(&right);expect_error(&image,"two entries",&lf,&twoentry,65536,826);
  right.entry_symbol=0;right.target=2;NtBuffer targetf=write_object(&right);expect_error(&image,"target mismatch",&lf,&targetf,65536,820);
  free(lf.bytes);free(rf.bytes);free(wrongf.bytes);free(twoentry.bytes);free(targetf.bytes);}
 if(image.memory) nova_host_unmap(&image);
 if(compiled) nt_artifact_free(&compiler);
 for(unsigned i=0;i<6;i++) free(sources[i]);
 printf("NTASM_NOVA_NXO_LINK_PE checks=%u failures=%u cases=%u\n",checks,failures,cases);
 return failures?1:0;
}
