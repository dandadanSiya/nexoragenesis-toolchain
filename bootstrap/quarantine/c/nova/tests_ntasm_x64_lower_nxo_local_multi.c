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
static char *loadf(const char*p,size_t*n){FILE*f=fopen(p,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long z=ftell(f);fseek(f,0,SEEK_SET);char*b=z>=0?malloc((size_t)z+1):NULL;if(!b||fread(b,1,(size_t)z,f)!=(size_t)z){free(b);fclose(f);return NULL;}fclose(f);b[z]=0;*n=(size_t)z;return b;}
static uint64_t r64(const uint8_t*b){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)b[i]<<(i*8);return v;}
static uint64_t run(const NovaHostImage*i,const char*s,uint8_t*out,size_t cap){
    NovaContextV2 c={0};CHECK(nova_context_init_v2(&c),"context init");
    uint64_t sh=nova_borrow_v2(&c,(void*)s,strlen(s),0),oh=nova_borrow_v2(&c,out,cap,1);
    uint64_t args[]={sh,strlen(s),oh,cap};uint64_t result=i->entry(&c,args,4);
    CHECK(c.error==0,"runtime clean");CHECK(nova_context_destroy_v2(&c),"context cleanup");cases++;return result;
}
static char *source_for(unsigned calls,int helper_first,int bad_call,int mode){
    size_t cap=32768,at=0;char*s=malloc(cap);if(!s)return NULL;
    at+=(size_t)snprintf(s+at,cap-at,"module m\ntarget x86_64-nexora-uefi\nsection .text {\n");
    if(helper_first)at+=(size_t)snprintf(s+at,cap-at,"fn helper()->u64\neffects {}\nclobbers {} {\nret\n}\n");
    at+=(size_t)snprintf(s+at,cap-at,"fn main()->u64\neffects {}\nclobbers {} {\n");
    for(unsigned i=0;i<calls;i++){
        if(mode==1&&i==0)at+=(size_t)snprintf(s+at,cap-at,"call helper(1)\n");
        else if((int)i==bad_call)at+=(size_t)snprintf(s+at,cap-at,"call missing()\n");
        else at+=(size_t)snprintf(s+at,cap-at,"call helper()\n");
    }
    at+=(size_t)snprintf(s+at,cap-at,"ret\n");
    if(mode==2)at+=(size_t)snprintf(s+at,cap-at,"call helper()\n");
    at+=(size_t)snprintf(s+at,cap-at,"}\n");
    if(!helper_first)at+=(size_t)snprintf(s+at,cap-at,"fn helper()->u64\neffects {}\nclobbers {} {\nret\n}\n");
    at+=(size_t)snprintf(s+at,cap-at,"}\n");CHECK(at<cap,"generated source bounded");return s;
}
static NtObject make_object(unsigned calls,int helper_first,NtObjectReloc*relocs){
    size_t text_size=(size_t)calls*5+2;uint8_t*text=calloc(text_size,1);
    size_t call_at=helper_first?1:0;uint64_t main_offset=helper_first?1:0;
    uint64_t helper_offset=helper_first?0:(uint64_t)(calls*5+1);
    for(unsigned i=0;i<calls;i++)text[call_at+(size_t)i*5]=0xe8;
    text[call_at+(size_t)calls*5]=0xc3;if(!helper_first)text[helper_offset]=0xc3;else text[0]=0xc3;
    for(unsigned i=0;i<calls;i++)relocs[i]=(NtObjectReloc){1,NT_OBJECT_REL32,1,call_at+(uint64_t)i*5+1,0};
    static NtObjectSymbol symbols[2];symbols[0]=(NtObjectSymbol){"helper","v0",1,NT_OBJECT_FUNCTION,helper_offset,1};
    symbols[1]=(NtObjectSymbol){"main","v0",1,NT_OBJECT_FUNCTION|NT_OBJECT_EXPORT,main_offset,calls*5+1};
    NtObject object={0};object.target=1;object.entry_symbol=2;object.sections[0]=(NtBuffer){text,text_size,text_size};
    object.symbols=symbols;object.symbol_count=2;object.relocations=relocs;object.relocation_count=calls;return object;
}
static void positive(const NovaHostImage*i,unsigned calls,int helper_first){
    char*source=source_for(calls,helper_first,-1,0);CHECK(source!=NULL,"source allocation");if(!source)return;
    NtObjectReloc*relocs=calloc(calls,sizeof *relocs);CHECK(relocs!=NULL,"relocation oracle allocation");if(!relocs){free(source);return;}
    NtObject object=make_object(calls,helper_first,relocs);NtBuffer expected={0};NtDiagnostic error={0};
    CHECK(nt_object_write(&object,&expected,&error),"C NXO writer");uint8_t out[65536];memset(out,0xcc,sizeof out);
    uint64_t code=run(i,source,out,sizeof out),size=r64(out);
    CHECK(code==0&&size==expected.size&&!memcmp(out+8,expected.bytes,expected.size),"NXO byte parity");
    NtObject readback={0};int read_ok=code==0&&size<=sizeof out-8&&nt_object_read(out+8,(size_t)size,&readback,&error);
    CHECK(read_ok,"NXO readback");if(read_ok){CHECK(nt_object_validate(&readback,&error),"NXO validate");
        CHECK(readback.entry_symbol==2&&readback.symbol_count==2&&readback.relocation_count==calls,"symbols, entry, relocation count");
        CHECK(readback.symbols[0].offset==(helper_first?0:calls*5+1)&&readback.symbols[1].offset==(helper_first?1:0)&&
              readback.symbols[1].size==calls*5+1,"function offsets and main size");
        for(unsigned k=0;k<calls;k++)CHECK(readback.relocations[k].offset==(helper_first?2:1)+k*5&&readback.relocations[k].target==1,"sorted REL32 site and helper index");
        nt_object_free(&readback);}
    free(expected.bytes);free(object.sections[0].bytes);free(relocs);free(source);
}
static void rejected(const NovaHostImage*i,char*source,size_t cap,uint64_t expected,const char*label){
    CHECK(source!=NULL,"negative source allocation");if(!source)return;uint8_t out[65536];memset(out,0xcc,sizeof out);
    uint64_t code=run(i,source,out,cap);int intact=code==expected;for(size_t j=0;j<cap;j++)intact&=out[j]==0xcc;
    CHECK(intact,label);free(source);
}
int main(void){
    const char*paths[]={"toolchain/ntasm-nova/tests/x64-lower-nxo-local-multi-entry.nova","toolchain/ntasm-nova/x64_lower_nxo.nova","toolchain/ntasm-nova/x64_lower_control.nova","toolchain/ntasm-nova/x64_lower_fixed.nova","toolchain/ntasm-nova/x64_fixed.nova","toolchain/ntasm-nova/register_widths.nova","toolchain/ntasm-nova/nxo.nova","toolchain/ntasm-nova/pe.nova","toolchain/ntasm-nova/parser.nova","toolchain/ntasm-nova/ast.nova","toolchain/ntasm-nova/lexer.nova"};
    char*sources[11]={0};NtFInput inputs[11];int all=1;for(unsigned j=0;j<11;j++){size_t n=0;sources[j]=loadf(paths[j],&n);inputs[j]=(NtFInput){paths[j],sources[j],n};if(!sources[j])all=0;}
    CHECK(all,"sources load");NtArtifact compiler={0};int compiled=all&&nova_compile_many_v2(inputs,11,&compiler);CHECK(compiled,"modules compile");
    if(!compiled)fprintf(stderr,"E%u %zu:%zu %s\n",compiler.error.code,compiler.error.line,compiler.error.column,compiler.error.message);
    NovaHostImage image={0};CHECK(compiled&&nova_host_map(&compiler,&image),"host map");
    if(image.memory){positive(&image,2,0);positive(&image,2,1);positive(&image,64,0);positive(&image,64,1);
        rejected(&image,source_for(0,0,-1,0),65536,800,"zero calls rejected transactionally");
        rejected(&image,source_for(65,0,-1,0),65536,800,"65 calls rejected transactionally");
        rejected(&image,source_for(2,0,0,0),65536,800,"invalid first call target rejected");
        rejected(&image,source_for(2,1,1,0),65536,800,"invalid last call target rejected");
        rejected(&image,source_for(2,0,-1,1),65536,800,"call arguments rejected");
        rejected(&image,source_for(2,0,-1,2),65536,800,"statement after ret rejected");
        rejected(&image,source_for(2,0,-1,0),8,827,"short NXO output rejected transactionally");}
    if(image.memory)nova_host_unmap(&image);
    if(compiled)nt_artifact_free(&compiler);
    for(unsigned j=0;j<11;j++)free(sources[j]);
    printf("NTASM_NOVA_X64_LOWER_NXO_LOCAL_MULTI checks=%u failures=%u cases=%u\n",checks,failures,cases);return failures?1:0;
}
