#include "nova.h"
#include "nova/run_host.h"
#include "nova/runtime_v2.h"
#include "object.h"
#include "x64.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures, cases;
#define CHECK(c,m) do { checks++; if (!(c)) { failures++; fprintf(stderr,"FAIL %s\n",m); } } while (0)

static char *loadf(const char *path,size_t *size) {
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    fseek(f,0,SEEK_END);long z=ftell(f);fseek(f,0,SEEK_SET);
    char *b=z>=0?malloc((size_t)z+1):NULL;
    if(!b||fread(b,1,(size_t)z,f)!=(size_t)z){free(b);fclose(f);return NULL;}
    fclose(f);b[z]=0;*size=(size_t)z;return b;
}
static uint64_t r64(const uint8_t *b) {
    uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)b[i]<<(i*8);return v;
}
static size_t encode_(uint8_t *out,const char *name,NtX64Operand *op,size_t n) {
    NtX64Instruction e={0};NtX64Context ctx={1,3};
    CHECK(nt_x64_encode(name,op,n,ctx,&e)==0,"C x64 oracle");
    memcpy(out,e.bytes,e.length);return e.length;
}
static uint64_t run(const NovaHostImage *image,const char *source,uint8_t *out,size_t capacity) {
    NovaContextV2 c={0};CHECK(nova_context_init_v2(&c),"context init");
    uint64_t sh=nova_borrow_v2(&c,(void *)source,strlen(source),0);
    uint64_t oh=nova_borrow_v2(&c,out,capacity,1);
    uint64_t args[]={sh,strlen(source),oh,capacity};
    uint64_t code=image->entry(&c,args,4);
    CHECK(c.error==0,"runtime clean");CHECK(nova_context_destroy_v2(&c),"context cleanup");
    cases++;return code;
}
int main(void) {
    const char *paths[]={
        "toolchain/ntasm-nova/tests/x64-lower-nxo-data-entry.nova",
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
    char *sources[11]={0};NtFInput inputs[11];
    for(unsigned i=0;i<11;i++){size_t n=0;sources[i]=loadf(paths[i],&n);inputs[i]=(NtFInput){paths[i],sources[i],n};}
    int all=1;for(unsigned i=0;i<11;i++)if(!sources[i])all=0;
    CHECK(all,"source load");
    NtArtifact artifact={0};int compiled=all&&nova_compile_many_v2(inputs,11,&artifact);
    CHECK(compiled,"Nova modules compile");
    if(!compiled)fprintf(stderr,"E%u %zu:%zu %s\n",artifact.error.code,artifact.error.line,artifact.error.column,artifact.error.message);
    NovaHostImage image={0};CHECK(compiled&&nova_host_map(&artifact,&image),"host map");
    if(image.memory) {
        char source[8192]="module m\ntarget x86_64-nexora-uefi\nsection .rdata {\nexport data zeta:u16=4660\nexport data alpha:u8=42\n}\nsection .data {\nexport data beta:u32=2018915346\n}\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\nstart:\njmp far\n";
        for(unsigned i=0;i<140;i++)strcat(source,"ud2\n");
        strcat(source,"far:\nje start\nud2\nreturn 0\n}\n}\n");
        uint8_t expected_text[293];size_t text_size=0;
        NtX64Operand forward={.kind=NT_X64_REL,.imm=280,.is_signed=1};
        text_size+=encode_(expected_text+text_size,"jmp",&forward,1);
        for(unsigned i=0;i<140;i++)text_size+=encode_(expected_text+text_size,"ud2",NULL,0);
        NtX64Operand backward={.kind=NT_X64_REL,.imm=UINT64_MAX-290,.is_signed=1};
        text_size+=encode_(expected_text+text_size,"je",&backward,1);
        text_size+=encode_(expected_text+text_size,"ud2",NULL,0);
        CHECK(text_size==293,"oracle text size");
        uint8_t ro[]={0x34,0x12,42}, data[]={0x12,0x34,0x56,0x78};
        NtObjectSymbol symbols[]={
            {"alpha","v0",2,NT_OBJECT_EXPORT|NT_OBJECT_VARIABLE,2,1},
            {"beta","v0",3,NT_OBJECT_EXPORT|NT_OBJECT_VARIABLE,0,4},
            {"main","v0",1,NT_OBJECT_EXPORT|NT_OBJECT_FUNCTION,0,293},
            {"zeta","v0",2,NT_OBJECT_EXPORT|NT_OBJECT_VARIABLE,0,2}
        };
        NtObject reference={0};reference.target=1;reference.entry_symbol=3;
        reference.sections[0]=(NtBuffer){expected_text,text_size,text_size};
        reference.sections[1]=(NtBuffer){ro,sizeof ro,sizeof ro};
        reference.sections[2]=(NtBuffer){data,sizeof data,sizeof data};
        reference.symbols=symbols;reference.symbol_count=4;
        NtBuffer expected={0};NtDiagnostic error={0};
        CHECK(nt_object_write(&reference,&expected,&error),"C NXO oracle write");
        uint8_t output[8192];memset(output,0xcc,sizeof output);
        uint64_t code=run(&image,source,output,sizeof output);
        uint64_t size=r64(output);
        CHECK(code==0&&size==expected.size&&!memcmp(output+8,expected.bytes,expected.size),"NXO byte parity");
        NtObject readback={0};
        int read_ok=code==0&&size<=sizeof output-8&&nt_object_read(output+8,(size_t)size,&readback,&error);
        CHECK(read_ok,"C NXO read");
        if(read_ok){CHECK(nt_object_validate(&readback,&error),"C NXO validate");CHECK(readback.symbol_count==4&&readback.entry_symbol==3,"symbols and entry");CHECK(readback.sections[0].size==293&&readback.sections[1].size==3&&readback.sections[2].size==4,"section sizes");nt_object_free(&readback);}
        free(expected.bytes);
        char bare_source[8192];strcpy(bare_source,source);
        char *target_name=strstr(bare_source,"x86_64-nexora-uefi");
        CHECK(target_name!=NULL,"bare target setup");
        if(target_name)memcpy(target_name+14,"none",4);
        reference.target=2;expected=(NtBuffer){0};
        CHECK(nt_object_write(&reference,&expected,&error),"C bare NXO oracle write");
        memset(output,0xcc,sizeof output);
        code=run(&image,bare_source,output,sizeof output);size=r64(output);
        CHECK(code==0&&size==expected.size&&!memcmp(output+8,expected.bytes,expected.size),"bare NXO byte parity");
        free(expected.bytes);
        uint8_t short_out[128];memset(short_out,0xcc,sizeof short_out);
        code=run(&image,source,short_out,sizeof short_out);
        int intact=code==800;for(size_t i=0;i<sizeof short_out;i++)intact&=short_out[i]==0xcc;
        CHECK(intact,"short output transactional");
        char unsupported[8192];strcpy(unsupported,source);
        char *literal=strstr(unsupported,"2018915346");
        CHECK(literal!=NULL,"unsupported setup");
        if(literal)memcpy(literal,"1+2       ",10);
        uint8_t error_out[8192];memset(error_out,0xcc,sizeof error_out);
        code=run(&image,unsupported,error_out,sizeof error_out);
        intact=code==800;for(size_t i=0;i<sizeof error_out;i++)intact&=error_out[i]==0xcc;
        CHECK(intact,"unsupported expression transactional");
        const char *unsupported_stmt="module m\ntarget x86_64-nexora-uefi\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\nlet x:u64=1\nreturn 0\n}\n}\n";
        memset(error_out,0xcc,sizeof error_out);
        code=run(&image,unsupported_stmt,error_out,sizeof error_out);
        intact=code==800;for(size_t i=0;i<sizeof error_out;i++)intact&=error_out[i]==0xcc;
        CHECK(intact,"unsupported statement transactional");
        const char *unsupported_contract="module m\ntarget x86_64-nexora-uefi\nsection .text {\nfn main()->u64\neffects {}\nclobbers {rax} {\nud2\nreturn 0\n}\n}\n";
        memset(error_out,0xcc,sizeof error_out);
        code=run(&image,unsupported_contract,error_out,sizeof error_out);
        intact=code==800;for(size_t i=0;i<sizeof error_out;i++)intact&=error_out[i]==0xcc;
        CHECK(intact,"unsupported contract transactional");
    }
    if(image.memory)nova_host_unmap(&image);
    if(compiled)nt_artifact_free(&artifact);
    for(unsigned i=0;i<11;i++)free(sources[i]);
    printf("NTASM_NOVA_X64_LOWER_NXO_DATA checks=%u failures=%u cases=%u\n",checks,failures,cases);
    return failures?1:0;
}
