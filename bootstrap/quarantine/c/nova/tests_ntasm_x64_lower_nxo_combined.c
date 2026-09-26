#include "nova.h"
#include "nova/run_host.h"
#include "nova/runtime_v2.h"
#include "object.h"
#include "x64.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks,failures,cases;
#define CHECK(c,m) do{checks++;if(!(c)){failures++;fprintf(stderr,"FAIL %s\n",m);}}while(0)
static char *loadf(const char *p,size_t *n){FILE*f=fopen(p,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long z=ftell(f);fseek(f,0,SEEK_SET);char*b=z>=0?malloc((size_t)z+1):NULL;if(!b||fread(b,1,(size_t)z,f)!=(size_t)z){free(b);fclose(f);return NULL;}fclose(f);b[z]=0;*n=(size_t)z;return b;}
static uint64_t r64(const uint8_t*b){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)b[i]<<(i*8);return v;}
static size_t encode_(uint8_t*out,const char*name,NtX64Operand*op,size_t n){
    NtX64Instruction encoded={0};NtX64Context ctx={1,3};
    CHECK(nt_x64_encode(name,op,n,ctx,&encoded)==0,"C x64 oracle");
    memcpy(out,encoded.bytes,encoded.length);return encoded.length;
}
static uint64_t run(const NovaHostImage*i,const char*source,uint8_t*out,size_t capacity){
    NovaContextV2 c={0};CHECK(nova_context_init_v2(&c),"context init");
    uint64_t sh=nova_borrow_v2(&c,(void*)source,strlen(source),0),oh=nova_borrow_v2(&c,out,capacity,1);
    uint64_t args[]={sh,strlen(source),oh,capacity};uint64_t code=i->entry(&c,args,4);
    CHECK(c.error==0,"runtime clean");CHECK(nova_context_destroy_v2(&c),"context cleanup");
    cases++;return code;
}
static void rejected(const NovaHostImage*i,const char*source,const char*label){
    uint8_t out[2048];memset(out,0xcc,sizeof out);
    uint64_t code=run(i,source,out,sizeof out);int intact=code==800;
    for(size_t j=0;j<sizeof out;j++)intact&=out[j]==0xcc;
    CHECK(intact,label);
}
static void parity(const NovaHostImage*i,const char*source,NtObject*oracle,
                   uint32_t expected_entry,uint32_t expected_target){
    NtBuffer expected={0};NtDiagnostic error={0};
    CHECK(nt_object_write(oracle,&expected,&error),"C NXO oracle write");
    uint8_t out[4096];memset(out,0xcc,sizeof out);
    uint64_t code=run(i,source,out,sizeof out),size=r64(out);
    CHECK(code==0&&size==expected.size&&!memcmp(out+8,expected.bytes,expected.size),"combined NXO byte parity");
    NtObject readback={0};
    int read_ok=code==0&&size<=sizeof out-8&&nt_object_read(out+8,(size_t)size,&readback,&error);
    CHECK(read_ok,"C NXO read");
    if(read_ok){
        CHECK(nt_object_validate(&readback,&error),"C NXO validate");
        CHECK(readback.target==expected_target&&readback.entry_symbol==expected_entry&&
              readback.symbol_count==5&&readback.relocation_count==1,"target entry tables");
        CHECK(readback.sections[0].size==6&&readback.sections[1].size==3&&
              readback.sections[2].size==4,"section sizes");
        CHECK(readback.relocations[0].section==1&&readback.relocations[0].kind==NT_OBJECT_REL32&&
              readback.relocations[0].offset==1&&readback.relocations[0].target==oracle->relocations[0].target&&
              readback.relocations[0].addend==0,"REL32 canonical site");
        nt_object_free(&readback);
    }
    free(expected.bytes);
}
int main(void){
    const char*paths[]={
        "toolchain/ntasm-nova/tests/x64-lower-nxo-combined-entry.nova",
        "toolchain/ntasm-nova/x64_lower_nxo.nova",
        "toolchain/ntasm-nova/x64_lower_control.nova",
        "toolchain/ntasm-nova/x64_lower_fixed.nova",
        "toolchain/ntasm-nova/x64_fixed.nova",
        "toolchain/ntasm-nova/register_widths.nova",
        "toolchain/ntasm-nova/nxo.nova",
        "toolchain/ntasm-nova/pe.nova",
        "toolchain/ntasm-nova/parser.nova",
        "toolchain/ntasm-nova/ast.nova",
        "toolchain/ntasm-nova/lexer.nova"
    };
    char*sources[11]={0};NtFInput inputs[11];
    for(unsigned i=0;i<11;i++){size_t n=0;sources[i]=loadf(paths[i],&n);inputs[i]=(NtFInput){paths[i],sources[i],n};}
    int all=1;for(unsigned i=0;i<11;i++)if(!sources[i])all=0;CHECK(all,"sources load");
    NtArtifact artifact={0};int compiled=all&&nova_compile_many_v2(inputs,11,&artifact);
    CHECK(compiled,"Nova modules compile");
    if(!compiled)fprintf(stderr,"E%u %zu:%zu %s\n",artifact.error.code,artifact.error.line,artifact.error.column,artifact.error.message);
    NovaHostImage image={0};CHECK(compiled&&nova_host_map(&artifact,&image),"image maps");
    if(image.memory){
        const char*source="module m\ntarget x86_64-nexora-uefi\nimport api.echo as zz: fn()->u64\neffects {}\nclobbers {}\nsection .rdata {\nexport data zeta:u16=4660\nexport data alpha:u8=42\n}\nsection .data {\nexport data beta:u32=2018915346\n}\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\ncall zz()\nret\n}\n}\n";
        uint8_t text[6]={0};size_t text_size=0;
        NtX64Operand target={.kind=NT_X64_REL,.imm=0,.is_signed=1};
        text_size+=encode_(text+text_size,"call",&target,1);
        text_size+=encode_(text+text_size,"ret",NULL,0);
        CHECK(text_size==6,"C call ret size");
        uint8_t ro[]={0x34,0x12,42},data[]={0x12,0x34,0x56,0x78};
        NtObjectSymbol symbols[]={
            {"alpha","v0",2,NT_OBJECT_EXPORT|NT_OBJECT_VARIABLE,2,1},
            {"beta","v0",3,NT_OBJECT_EXPORT|NT_OBJECT_VARIABLE,0,4},
            {"main","v0",1,NT_OBJECT_EXPORT|NT_OBJECT_FUNCTION,0,6},
            {"zeta","v0",2,NT_OBJECT_EXPORT|NT_OBJECT_VARIABLE,0,2},
            {"zz","v0",0,NT_OBJECT_IMPORT|NT_OBJECT_FUNCTION,0,0}
        };
        NtObjectReloc relocation={1,NT_OBJECT_REL32,5,1,0};
        NtObject oracle={0};oracle.target=1;oracle.entry_symbol=3;
        oracle.sections[0]=(NtBuffer){text,sizeof text,sizeof text};
        oracle.sections[1]=(NtBuffer){ro,sizeof ro,sizeof ro};
        oracle.sections[2]=(NtBuffer){data,sizeof data,sizeof data};
        oracle.symbols=symbols;oracle.symbol_count=5;
        oracle.relocations=&relocation;oracle.relocation_count=1;
        parity(&image,source,&oracle,3,1);
        const char*bare="module m\ntarget x86_64-nexora-none\nimport api.echo as aa: fn()->u64\neffects {}\nclobbers {}\nsection .rdata {\nexport data zeta:u16=4660\nexport data alpha:u8=42\n}\nsection .data {\nexport data beta:u32=2018915346\n}\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\ncall aa()\nret\n}\n}\n";
        NtObjectSymbol bare_symbols[]={
            {"aa","v0",0,NT_OBJECT_IMPORT|NT_OBJECT_FUNCTION,0,0},
            {"alpha","v0",2,NT_OBJECT_EXPORT|NT_OBJECT_VARIABLE,2,1},
            {"beta","v0",3,NT_OBJECT_EXPORT|NT_OBJECT_VARIABLE,0,4},
            {"main","v0",1,NT_OBJECT_EXPORT|NT_OBJECT_FUNCTION,0,6},
            {"zeta","v0",2,NT_OBJECT_EXPORT|NT_OBJECT_VARIABLE,0,2}
        };
        oracle.target=2;oracle.entry_symbol=4;oracle.symbols=bare_symbols;relocation.target=1;
        parity(&image,bare,&oracle,4,2);
        uint8_t short_out[128];memset(short_out,0xcc,sizeof short_out);
        uint64_t code=run(&image,source,short_out,sizeof short_out);int intact=code==800;
        for(size_t j=0;j<sizeof short_out;j++)intact&=short_out[j]==0xcc;
        CHECK(intact,"short output transactional");
        const char*missing="module m\ntarget x86_64-nexora-uefi\nimport api.echo as zz: fn()->u64\neffects {}\nclobbers {}\nsection .rdata {\nexport data zeta:u16=4660\nexport data alpha:u8=42\n}\nsection .data {\nexport data beta:u32=2018915346\n}\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\ncall bad()\nret\n}\n}\n";
        rejected(&image,missing,"missing alias transactional");
        const char*arity="module m\ntarget x86_64-nexora-uefi\nimport api.echo as zz: fn()->u64\neffects {}\nclobbers {}\nsection .rdata {\nexport data zeta:u16=4660\nexport data alpha:u8=42\n}\nsection .data {\nexport data beta:u32=2018915346\n}\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\ncall zz(1)\nret\n}\n}\n";
        rejected(&image,arity,"arity transactional");
        const char*range="module m\ntarget x86_64-nexora-uefi\nimport api.echo as zz: fn()->u64\neffects {}\nclobbers {}\nsection .rdata {\nexport data zeta:u16=4660\nexport data alpha:u8=256\n}\nsection .data {\nexport data beta:u32=2018915346\n}\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\ncall zz()\nret\n}\n}\n";
        rejected(&image,range,"data range transactional");
    }
    if(image.memory)nova_host_unmap(&image);
    if(compiled)nt_artifact_free(&artifact);
    for(unsigned i=0;i<11;i++)free(sources[i]);
    printf("NTASM_NOVA_X64_LOWER_NXO_COMBINED checks=%u failures=%u cases=%u\n",checks,failures,cases);
    return failures?1:0;
}
