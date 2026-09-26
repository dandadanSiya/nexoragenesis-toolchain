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
static char *loadf(const char *p,size_t *n){
    FILE*f=fopen(p,"rb");if(!f)return NULL;
    fseek(f,0,SEEK_END);long z=ftell(f);fseek(f,0,SEEK_SET);
    char*b=z>=0?malloc((size_t)z+1):NULL;
    if(!b||fread(b,1,(size_t)z,f)!=(size_t)z){free(b);fclose(f);return NULL;}
    fclose(f);b[z]=0;*n=(size_t)z;return b;
}
static uint64_t r64(const uint8_t*b){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)b[i]<<(i*8);return v;}
static uint64_t run(const NovaHostImage*i,const char*source,uint8_t*out,size_t cap){
    NovaContextV2 c={0};CHECK(nova_context_init_v2(&c),"context init");
    uint64_t sh=nova_borrow_v2(&c,(void*)source,strlen(source),0);
    uint64_t oh=nova_borrow_v2(&c,out,cap,1);uint64_t args[]={sh,strlen(source),oh,cap};
    uint64_t code=i->entry(&c,args,4);if(c.error)fprintf(stderr,"Nova runtime error=%llu source=%llu offset=%llu return=%llu\n",(unsigned long long)c.error,(unsigned long long)c.error_source,(unsigned long long)c.error_offset,(unsigned long long)code);CHECK(c.error==0,"runtime clean");
    CHECK(nova_context_destroy_v2(&c),"context cleanup");cases++;return code;
}
static void parity_case(const NovaHostImage*image,const char*source,int helper_first){
    uint8_t text[]={195,0,0,0,0,0,195};
    uint64_t site=helper_first?2:1;
    if(helper_first){text[0]=195;text[1]=0xe8;text[6]=195;}
    else{text[0]=0xe8;text[5]=195;text[6]=195;}
    NtObjectSymbol symbols[]={
        {"helper","v0",1,NT_OBJECT_FUNCTION,helper_first?0:6,1},
        {"main","v0",1,NT_OBJECT_FUNCTION|NT_OBJECT_EXPORT,helper_first?1:0,6}
    };
    NtObjectReloc relocation={1,NT_OBJECT_REL32,1,site,0};
    NtObject object={0};object.target=1;object.entry_symbol=2;
    object.sections[0]=(NtBuffer){text,sizeof text,sizeof text};
    object.symbols=symbols;object.symbol_count=2;object.relocations=&relocation;object.relocation_count=1;
    NtDiagnostic error={0};NtBuffer expected_nxo={0};
    CHECK(nt_object_write(&object,&expected_nxo,&error),"C NXO writer");
    NtArtifact executable={0};
    CHECK(nt_object_materialize(&object,&executable,&error),"C materialize");
    CHECK(nt_make_pe(&executable),"C PE writer");
    uint8_t out[65536];memset(out,0xcc,sizeof out);uint64_t code=run(image,source,out,sizeof out);
    uint64_t nxo_size=r64(out),pe_size=r64(out+8);
    if(code!=0||nxo_size!=expected_nxo.size||pe_size!=executable.pe.size)fprintf(stderr,"result code=%llu nxo=%llu/%zu pe=%llu/%zu\n",(unsigned long long)code,(unsigned long long)nxo_size,expected_nxo.size,(unsigned long long)pe_size,executable.pe.size);
    CHECK(code==0&&nxo_size==expected_nxo.size&&
          !memcmp(out+16,expected_nxo.bytes,expected_nxo.size),"NXO byte parity");
    CHECK(code==0&&pe_size==executable.pe.size&&
          !memcmp(out+16+nxo_size,executable.pe.bytes,executable.pe.size),"PE byte parity");
    CHECK(executable.text.size==7&&executable.text.bytes[site-1]==0xe8&&
          executable.text.bytes[site]==(helper_first?0xfa:1)&&
          executable.text.bytes[site+1]==(helper_first?0xff:0),"REL32 displacement");
    CHECK(executable.entry==(helper_first?1:0),"entry follows main source order");
    CHECK(object.sections[0].size==7&&object.symbol_count==2&&object.relocation_count==1,
          "NXO sections symbols relocation");
    free(expected_nxo.bytes);nt_artifact_free(&executable);
}
static void rejected(const NovaHostImage*i,const char*source,size_t cap,uint64_t expected,const char*label){
    uint8_t out[65536];memset(out,0xcc,sizeof out);uint64_t code=run(i,source,out,cap);
    int intact=code==expected;for(size_t j=0;j<cap;j++)intact&=out[j]==0xcc;CHECK(intact,label);
}
int main(void){
    const char*paths[]={
        "toolchain/ntasm-nova/x64_lower_nxo.nova",
        "toolchain/ntasm-nova/x64_lower_control.nova",
        "toolchain/ntasm-nova/x64_lower_fixed.nova",
        "toolchain/ntasm-nova/x64_fixed.nova",
        "toolchain/ntasm-nova/register_widths.nova",
        "toolchain/ntasm-nova/nxo.nova",
        "toolchain/ntasm-nova/pe.nova",
        "toolchain/ntasm-nova/nxo_link_pe.nova",
        "toolchain/ntasm-nova/nxo_merge.nova",
        "toolchain/ntasm-nova/parser.nova",
        "toolchain/ntasm-nova/ast.nova",
        "toolchain/ntasm-nova/lexer.nova",
        "toolchain/ntasm-nova/tests/x64-lower-nxo-local-pe-entry.nova",
        "toolchain/ntasm-nova/x64_lower_nxo_pe.nova",
        "toolchain/ntasm-nova/nxo_pe.nova"
    };
    char*sources[15]={0};NtFInput inputs[15];int all=1;
    for(unsigned i=0;i<15;i++){size_t n=0;sources[i]=loadf(paths[i],&n);inputs[i]=(NtFInput){paths[i],sources[i],n};if(!sources[i])all=0;}
    CHECK(all,"sources load");NtArtifact compiler={0};
    int compiled=all&&nova_compile_many_v2(inputs,15,&compiler);CHECK(compiled,"Nova modules compile");
    if(!compiled)fprintf(stderr,"E%u %zu:%zu %s\n",compiler.error.code,compiler.error.line,compiler.error.column,compiler.error.message);
    NovaHostImage image={0};
    CHECK(compiled&&nova_host_map(&compiler,&image),"PE entry host map");
    if(image.memory){
        const char*main_first="module m\ntarget x86_64-nexora-uefi\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\ncall helper()\nret\n}\nfn helper()->u64\neffects {}\nclobbers {} {\nret\n}\n}\n";
        const char*helper_first="module m\ntarget x86_64-nexora-uefi\nsection .text {\nfn helper()->u64\neffects {}\nclobbers {} {\nret\n}\nfn main()->u64\neffects {}\nclobbers {} {\ncall helper()\nret\n}\n}\n";
        parity_case(&image,main_first,0);parity_case(&image,helper_first,1);
        const char*bad="module m\ntarget x86_64-nexora-uefi\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\nret\n}\n}\n";
        rejected(&image,bad,65536,800,"invalid local-call shape transactional");
        rejected(&image,main_first,8,827,"short PE output transactional");
    }
    if(image.memory)nova_host_unmap(&image);
    if(compiled)nt_artifact_free(&compiler);
    for(unsigned i=0;i<15;i++)free(sources[i]);
    printf("NTASM_NOVA_X64_LOWER_NXO_LOCAL_PE checks=%u failures=%u cases=%u\n",checks,failures,cases);
    return failures?1:0;
}
