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
static char *loadf(const char *p,size_t *n){FILE*f=fopen(p,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long z=ftell(f);fseek(f,0,SEEK_SET);char*b=z>=0?malloc((size_t)z+1):NULL;if(!b||fread(b,1,(size_t)z,f)!=(size_t)z){free(b);fclose(f);return NULL;}fclose(f);b[z]=0;*n=(size_t)z;return b;}
static uint64_t r64(const uint8_t*b){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)b[i]<<(i*8);return v;}
static uint64_t run(const NovaHostImage*i,const char*source,const NtBuffer*definition,uint8_t*out,size_t capacity){
    NovaContextV2 c={0};CHECK(nova_context_init_v2(&c),"context init");
    uint64_t sh=nova_borrow_v2(&c,(void*)source,strlen(source),0);
    uint64_t dh=nova_borrow_v2(&c,definition->bytes,definition->size,0);
    uint64_t oh=nova_borrow_v2(&c,out,capacity,1);
    uint64_t args[]={sh,strlen(source),dh,definition->size,oh,capacity};
    uint64_t code=i->entry(&c,args,6);
    CHECK(c.error==0,"runtime clean");CHECK(nova_context_destroy_v2(&c),"context cleanup");
    cases++;return code;
}
static NtBuffer write_object(const NtObject*object){
    NtBuffer out={0};NtDiagnostic error={0};
    CHECK(nt_object_write(object,&out,&error),"C object write");return out;
}
static void rejected(const NovaHostImage*i,const char*source,const NtBuffer*right,size_t capacity,
                     uint64_t expected,const char*label){
    uint8_t out[65536];memset(out,0xcc,sizeof out);
    uint64_t code=run(i,source,right,out,capacity);int intact=code==expected;
    for(size_t j=0;j<capacity;j++)intact&=out[j]==0xcc;
    CHECK(intact,label);
}
int main(void){
    const char*paths[]={
        "toolchain/ntasm-nova/tests/x64-lower-nxo-pe-entry.nova",
        "toolchain/ntasm-nova/x64_lower_nxo_pe.nova",
        "toolchain/ntasm-nova/x64_lower_nxo.nova",
        "toolchain/ntasm-nova/x64_lower_control.nova",
        "toolchain/ntasm-nova/x64_lower_fixed.nova",
        "toolchain/ntasm-nova/x64_fixed.nova",
        "toolchain/ntasm-nova/register_widths.nova",
        "toolchain/ntasm-nova/nxo_link_pe.nova",
        "toolchain/ntasm-nova/nxo_merge.nova",
        "toolchain/ntasm-nova/nxo_pe.nova",
        "toolchain/ntasm-nova/nxo.nova",
        "toolchain/ntasm-nova/pe.nova",
        "toolchain/ntasm-nova/parser.nova",
        "toolchain/ntasm-nova/ast.nova",
        "toolchain/ntasm-nova/lexer.nova"
    };
    char*sources[15]={0};NtFInput inputs[15];
    for(unsigned i=0;i<15;i++){size_t n=0;sources[i]=loadf(paths[i],&n);inputs[i]=(NtFInput){paths[i],sources[i],n};}
    int all=1;for(unsigned i=0;i<15;i++)if(!sources[i])all=0;CHECK(all,"sources load");
    NtArtifact compiler={0};int compiled=all&&nova_compile_many_v2(inputs,15,&compiler);
    CHECK(compiled,"Nova modules compile");
    if(!compiled)fprintf(stderr,"E%u %zu:%zu %s\n",compiler.error.code,compiler.error.line,compiler.error.column,compiler.error.message);
    NovaHostImage image={0};CHECK(compiled&&nova_host_map(&compiler,&image),"host map");
    if(image.memory){
        const char*source="module m\ntarget x86_64-nexora-uefi\nimport api.echo as helper: fn()->u64\neffects {}\nclobbers {}\nsection .rdata {\nexport data zeta:u16=4660\nexport data alpha:u8=42\n}\nsection .data {\nexport data beta:u32=2018915346\n}\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\ncall helper()\nret\n}\n}\n";
        uint8_t caller[]={0xe8,0,0,0,0,0xc3},callee[]={0xb8,42,0,0,0,0xc3};
        uint8_t ro[]={0x34,0x12,42},data[]={0x12,0x34,0x56,0x78};
        NtObjectSymbol left_symbols[]={
            {"alpha","v0",2,NT_OBJECT_EXPORT|NT_OBJECT_VARIABLE,2,1},
            {"beta","v0",3,NT_OBJECT_EXPORT|NT_OBJECT_VARIABLE,0,4},
            {"helper","v0",0,NT_OBJECT_IMPORT|NT_OBJECT_FUNCTION,0,0},
            {"main","v0",1,NT_OBJECT_EXPORT|NT_OBJECT_FUNCTION,0,6},
            {"zeta","v0",2,NT_OBJECT_EXPORT|NT_OBJECT_VARIABLE,0,2}
        };
        NtObjectReloc call={1,NT_OBJECT_REL32,3,1,0};
        NtObject left={0};left.target=1;left.entry_symbol=4;
        left.sections[0]=(NtBuffer){caller,sizeof caller,sizeof caller};
        left.sections[1]=(NtBuffer){ro,sizeof ro,sizeof ro};
        left.sections[2]=(NtBuffer){data,sizeof data,sizeof data};
        left.symbols=left_symbols;left.symbol_count=5;left.relocations=&call;left.relocation_count=1;
        NtObjectSymbol helper={"helper","v0",1,NT_OBJECT_EXPORT|NT_OBJECT_FUNCTION,0,6};
        NtObject right={0};right.target=1;right.sections[0]=(NtBuffer){callee,sizeof callee,sizeof callee};
        right.symbols=&helper;right.symbol_count=1;
        NtBuffer right_file=write_object(&right);
        NtObject pair[]={left,right},linked={0};NtArtifact oracle={0};NtDiagnostic error={0};
        int linked_ok=nt_object_link(pair,2,&linked,&error);
        CHECK(linked_ok,"C object link");
        int materialized=linked_ok&&nt_object_materialize(&linked,&oracle,&error);
        CHECK(materialized,"C materialize");
        CHECK(materialized&&nt_make_pe(&oracle),"C make PE");
        uint8_t out[65536];memset(out,0xcc,sizeof out);
        uint64_t code=run(&image,source,&right_file,out,sizeof out),size=r64(out);
        int parity=code==0&&size==oracle.pe.size&&!memcmp(out+8,oracle.pe.bytes,oracle.pe.size);
        if(!parity){size_t first=0,limit=size<oracle.pe.size?(size_t)size:oracle.pe.size;
            while(first<limit&&out[8+first]==oracle.pe.bytes[first])first++;
            fprintf(stderr,"PE mismatch code=%llu nova=%llu C=%zu first=%zu novaByte=%u CByte=%u\n",
                    (unsigned long long)code,(unsigned long long)size,oracle.pe.size,first,
                    first<size?out[8+first]:0,first<oracle.pe.size?oracle.pe.bytes[first]:0);}
        CHECK(parity,"Nova PE byte parity");
        CHECK(oracle.text.size>=6&&oracle.text.bytes[0]==0xe8&&oracle.text.bytes[1]!=0&&
              oracle.text.bytes[5]==0xc3,"REL32 call patched");
        CHECK(oracle.rdata.size>=3&&oracle.data.size>=4&&
              !memcmp(oracle.rdata.bytes,ro,3)&&!memcmp(oracle.data.bytes,data,4),"data sections preserved");
        CHECK(oracle.entry==0,"main entry offset");
        NtObjectSymbol bad_contract={"helper","v1",1,NT_OBJECT_EXPORT|NT_OBJECT_FUNCTION,0,6};
        right.symbols=&bad_contract;NtBuffer wrong=write_object(&right);
        rejected(&image,source,&wrong,sizeof out,825,"contract mismatch transactional");
        right.symbols=&helper;right.target=2;NtBuffer wrong_target=write_object(&right);
        rejected(&image,source,&wrong_target,sizeof out,820,"target mismatch transactional");
        right.target=1;NtObjectSymbol other={"other","v0",1,NT_OBJECT_EXPORT|NT_OBJECT_FUNCTION,0,6};
        right.symbols=&other;NtBuffer missing=write_object(&right);
        rejected(&image,source,&missing,sizeof out,824,"missing helper transactional");
        rejected(&image,source,&right_file,8,827,"short output transactional");
        free(right_file.bytes);free(wrong.bytes);free(wrong_target.bytes);free(missing.bytes);
        nt_artifact_free(&oracle);if(linked_ok)nt_object_free(&linked);
    }
    if(image.memory)nova_host_unmap(&image);
    if(compiled)nt_artifact_free(&compiler);
    for(unsigned i=0;i<15;i++)free(sources[i]);
    printf("NTASM_NOVA_X64_LOWER_NXO_PE checks=%u failures=%u cases=%u\n",checks,failures,cases);
    return failures?1:0;
}
