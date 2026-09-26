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
static uint64_t run(const NovaHostImage*i,const char*source,uint8_t*out,size_t cap){
    NovaContextV2 c={0};CHECK(nova_context_init_v2(&c),"context init");
    uint64_t sh=nova_borrow_v2(&c,(void*)source,strlen(source),0),oh=nova_borrow_v2(&c,out,cap,1);
    uint64_t args[]={sh,strlen(source),oh,cap};uint64_t code=i->entry(&c,args,4);
    if(c.error)fprintf(stderr,"Nova runtime error=%llu source=%llu offset=%llu\n",(unsigned long long)c.error,(unsigned long long)c.error_source,(unsigned long long)c.error_offset);
    CHECK(c.error==0,"runtime clean");CHECK(nova_context_destroy_v2(&c),"context cleanup");cases++;return code;
}
static void rejected(const NovaHostImage*i,const char*source,size_t cap,uint64_t expected,const char*label){
    uint8_t out[65536];memset(out,0xcc,sizeof out);uint64_t code=run(i,source,out,cap);int intact=code==expected;
    for(size_t j=0;j<cap;j++)intact&=out[j]==0xcc;
    CHECK(intact,label);
}
static NtObject make_object(int helper_first){
    static uint8_t text_a[]={0xe8,0,0,0,0,0xc3,0xc3},text_b[]={0xc3,0xe8,0,0,0,0,0xc3};
    static uint8_t ro[]={0x34,0x12,0x2a},data[]={0x12,0x34,0x56,0x78};
    static NtObjectSymbol symbols[]={
        {"alpha","v0",2,NT_OBJECT_VARIABLE,2,1},{"beta","v0",3,NT_OBJECT_EXPORT|NT_OBJECT_VARIABLE,0,4},
        {"helper","v0",1,NT_OBJECT_FUNCTION,0,1},{"main","v0",1,NT_OBJECT_FUNCTION|NT_OBJECT_EXPORT,0,6},
        {"zeta","v0",2,NT_OBJECT_EXPORT|NT_OBJECT_VARIABLE,0,2}
    };
    static NtObjectReloc relocation;symbols[2].offset=helper_first?0:6;symbols[3].offset=helper_first?1:0;
    relocation=(NtObjectReloc){1,NT_OBJECT_REL32,3,helper_first?2:1,0};
    NtObject object={0};object.target=1;object.entry_symbol=4;
    object.sections[0]=(NtBuffer){helper_first?text_b:text_a,7,7};object.sections[1]=(NtBuffer){ro,3,3};
    object.sections[2]=(NtBuffer){data,4,4};object.symbols=symbols;object.symbol_count=5;
    object.relocations=&relocation;object.relocation_count=1;return object;
}
static const char*source_a="module m\ntarget x86_64-nexora-uefi\nsection .rdata {\nexport data zeta:u16=4660\ndata alpha:u8=42\n}\nsection .data {\nexport data beta:u32=2018915346\n}\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\ncall helper()\nret\n}\nfn helper()->u64\neffects {}\nclobbers {} {\nret\n}\n}\n";
static const char*source_b="module m\ntarget x86_64-nexora-uefi\nsection .text {\nfn helper()->u64\neffects {}\nclobbers {} {\nret\n}\nfn main()->u64\neffects {}\nclobbers {} {\ncall helper()\nret\n}\n}\nsection .data {\nexport data beta:u32=2018915346\n}\nsection .rdata {\nexport data zeta:u16=4660\ndata alpha:u8=42\n}\n";
int main(void){
    const char*paths[]={
        "toolchain/ntasm-nova/tests/x64-lower-nxo-local-data-pe-entry.nova",
        "toolchain/ntasm-nova/x64_lower_nxo_pe.nova","toolchain/ntasm-nova/x64_lower_nxo.nova",
        "toolchain/ntasm-nova/x64_lower_control.nova","toolchain/ntasm-nova/x64_lower_fixed.nova",
        "toolchain/ntasm-nova/x64_fixed.nova","toolchain/ntasm-nova/register_widths.nova",
        "toolchain/ntasm-nova/nxo_link_pe.nova","toolchain/ntasm-nova/nxo_merge.nova",
        "toolchain/ntasm-nova/nxo_pe.nova","toolchain/ntasm-nova/nxo.nova",
        "toolchain/ntasm-nova/pe.nova","toolchain/ntasm-nova/parser.nova",
        "toolchain/ntasm-nova/ast.nova","toolchain/ntasm-nova/lexer.nova"
    };
    char*sources[15]={0};NtFInput inputs[15];int all=1;
    for(unsigned i=0;i<15;i++){size_t n=0;sources[i]=loadf(paths[i],&n);inputs[i]=(NtFInput){paths[i],sources[i],n};if(!sources[i])all=0;}
    CHECK(all,"sources load");NtArtifact compiler={0};int compiled=all&&nova_compile_many_v2(inputs,15,&compiler);
    CHECK(compiled,"Nova modules compile");if(!compiled)fprintf(stderr,"E%u %zu:%zu %s\n",compiler.error.code,compiler.error.line,compiler.error.column,compiler.error.message);
    NovaHostImage image={0};CHECK(compiled&&nova_host_map(&compiler,&image),"host map");
    if(image.memory){const char*fixtures[]={source_a,source_b};
        for(unsigned c=0;c<2;c++){
            NtObject object=make_object(c==1);NtBuffer expected_nxo={0};NtDiagnostic error={0};
            CHECK(nt_object_write(&object,&expected_nxo,&error),"C NXO writer");
            NtArtifact expected_pe={0};CHECK(nt_object_materialize(&object,&expected_pe,&error),"C materialize");
            CHECK(nt_make_pe(&expected_pe),"C PE writer");
            uint8_t out[65536];memset(out,0xcc,sizeof out);uint64_t code=run(&image,fixtures[c],out,sizeof out);
            uint64_t nxo_size=r64(out),pe_size=r64(out+8);
            CHECK(code==0&&nxo_size==expected_nxo.size&&!memcmp(out+16,expected_nxo.bytes,expected_nxo.size),"NXO byte parity");
            CHECK(code==0&&pe_size==expected_pe.pe.size&&!memcmp(out+16+nxo_size,expected_pe.pe.bytes,expected_pe.pe.size),"PE byte parity");
            uint64_t site=c==1?2:1;CHECK(expected_pe.text.size==7&&expected_pe.text.bytes[site-1]==0xe8&&
                expected_pe.text.bytes[site]==(c==1?0xfa:1)&&expected_pe.text.bytes[site+1]==(c==1?0xff:0),"patched local REL32");
            CHECK(expected_pe.entry==(c==1?1:0),"entry matches main position");
            CHECK(expected_pe.rdata.size==3&&!memcmp(expected_pe.rdata.bytes,"\x34\x12\x2a",3)&&
                  expected_pe.data.size==4&&!memcmp(expected_pe.data.bytes,"\x12\x34\x56\x78",4),"data sections materialized");
            free(expected_nxo.bytes);nt_artifact_free(&expected_pe);
        }
        const char*bad_range="module m\ntarget x86_64-nexora-uefi\nsection .rdata {\ndata alpha:u8=256\n}\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\ncall helper()\nret\n}\nfn helper()->u64\neffects {}\nclobbers {} {\nret\n}\n}\n";
        const char*bad_duplicate="module m\ntarget x86_64-nexora-uefi\nsection .rdata {\ndata helper:u8=1\n}\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\ncall helper()\nret\n}\nfn helper()->u64\neffects {}\nclobbers {} {\nret\n}\n}\n";
        const char*bad_duplicate_data="module m\ntarget x86_64-nexora-uefi\nsection .rdata {\ndata alpha:u8=1\n}\nsection .data {\ndata alpha:u8=2\n}\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\ncall helper()\nret\n}\nfn helper()->u64\neffects {}\nclobbers {} {\nret\n}\n}\n";
        const char*bad_main_collision="module m\ntarget x86_64-nexora-uefi\nsection .rdata {\ndata main:u8=1\n}\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\ncall helper()\nret\n}\nfn helper()->u64\neffects {}\nclobbers {} {\nret\n}\n}\n";
        const char*bad_section="module m\ntarget x86_64-nexora-uefi\nsection .bss {\ndata alpha:u8=1\n}\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\ncall helper()\nret\n}\nfn helper()->u64\neffects {}\nclobbers {} {\nret\n}\n}\n";
        rejected(&image,bad_range,65536,800,"out-of-range scalar transactional");
        rejected(&image,bad_duplicate,65536,800,"duplicate symbol transactional");
        rejected(&image,bad_duplicate_data,65536,800,"duplicate data names transactional");
        rejected(&image,bad_main_collision,65536,800,"main/data name collision transactional");
        rejected(&image,bad_section,65536,800,"unsupported section transactional");
        rejected(&image,source_a,16,827,"short PE output transactional");
        size_t capacity=4096;char*many=malloc(capacity);CHECK(many!=NULL,"65-data fixture allocation");
        if(many){size_t at=(size_t)sprintf(many,"module m\ntarget x86_64-nexora-uefi\nsection .rdata {\n");
            for(unsigned i=0;i<65;i++)at+=(size_t)sprintf(many+at,"data item%02u:u8=%u\n",i,i);
            at+=(size_t)sprintf(many+at,"}\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\ncall helper()\nret\n}\nfn helper()->u64\neffects {}\nclobbers {} {\nret\n}\n}\n");
            CHECK(at<capacity,"65-data fixture bounded");rejected(&image,many,65536,800,"more than 64 data transactional");free(many);}
    }
    if(image.memory)nova_host_unmap(&image);
    if(compiled)nt_artifact_free(&compiler);
    for(unsigned i=0;i<15;i++)free(sources[i]);
    printf("NTASM_NOVA_X64_LOWER_NXO_LOCAL_DATA_PE checks=%u failures=%u cases=%u\n",checks,failures,cases);
    return failures?1:0;
}
