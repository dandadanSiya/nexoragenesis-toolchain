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
static char *loadf(const char *p,size_t *n){
    FILE*f=fopen(p,"rb");if(!f)return NULL;
    fseek(f,0,SEEK_END);long z=ftell(f);fseek(f,0,SEEK_SET);
    char*b=z>=0?malloc((size_t)z+1):NULL;
    if(!b||fread(b,1,(size_t)z,f)!=(size_t)z){free(b);fclose(f);return NULL;}
    fclose(f);b[z]=0;*n=(size_t)z;return b;
}
static uint64_t r64(const uint8_t*b){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)b[i]<<(i*8);return v;}
static size_t encode_(uint8_t*out,const char*name,NtX64Operand*op,size_t n){
    NtX64Instruction e={0};NtX64Context ctx={1,3};
    CHECK(nt_x64_encode(name,op,n,ctx,&e)==0,"C x64 oracle");
    memcpy(out,e.bytes,e.length);return e.length;
}
static uint64_t run(const NovaHostImage*i,const char*source,uint8_t*out,size_t cap){
    NovaContextV2 c={0};CHECK(nova_context_init_v2(&c),"context init");
    uint64_t sh=nova_borrow_v2(&c,(void*)source,strlen(source),0),oh=nova_borrow_v2(&c,out,cap,1);
    uint64_t args[]={sh,strlen(source),oh,cap};uint64_t code=i->entry(&c,args,4);
    CHECK(c.error==0,"runtime clean");CHECK(nova_context_destroy_v2(&c),"context cleanup");
    cases++;return code;
}
static void case_ok(const NovaHostImage*i,const char*source,int helper_first){
    uint8_t text[7]={0};size_t length=0;NtX64Operand target={.kind=NT_X64_REL,.imm=0,.is_signed=1};
    if(helper_first)length+=encode_(text+length,"ret",NULL,0);
    length+=encode_(text+length,"call",&target,1);
    length+=encode_(text+length,"ret",NULL,0);
    if(!helper_first)length+=encode_(text+length,"ret",NULL,0);
    CHECK(length==7,"text length");
    uint64_t main_offset=helper_first?1:0,helper_offset=helper_first?0:6,site=helper_first?2:1;
    NtObjectSymbol symbols[]={
        {"helper","v0",1,NT_OBJECT_FUNCTION,helper_offset,1},
        {"main","v0",1,NT_OBJECT_FUNCTION|NT_OBJECT_EXPORT,main_offset,6}
    };
    NtObjectReloc relocation={1,NT_OBJECT_REL32,1,site,0};
    NtObject object={0};object.target=1;object.entry_symbol=2;
    object.sections[0]=(NtBuffer){text,sizeof text,sizeof text};
    object.symbols=symbols;object.symbol_count=2;object.relocations=&relocation;object.relocation_count=1;
    NtDiagnostic error={0};NtBuffer expected={0};
    CHECK(nt_object_write(&object,&expected,&error),"C NXO write");
    NtArtifact executable={0};
    CHECK(nt_object_materialize(&object,&executable,&error),"C NXO materialize");
    CHECK(nt_make_pe(&executable),"C PE write");
    uint8_t out[65536];memset(out,0xcc,sizeof out);
    uint64_t code=run(i,source,out,sizeof out),nxo_size=r64(out),pe_size=r64(out+8);
    CHECK(code==0&&nxo_size==expected.size&&!memcmp(out+16,expected.bytes,expected.size),"NXO byte parity");
    CHECK(code==0&&pe_size==executable.pe.size&&
          !memcmp(out+16+nxo_size,executable.pe.bytes,executable.pe.size),"PE byte parity");
    NtObject readback={0};
    int read_ok=code==0&&nxo_size<=sizeof out-16&&nt_object_read(out+16,(size_t)nxo_size,&readback,&error);
    CHECK(read_ok,"C NXO read");
    if(read_ok){CHECK(nt_object_validate(&readback,&error),"C NXO validate");CHECK(readback.entry_symbol==2&&readback.symbol_count==2&&readback.relocation_count==1&&readback.relocations[0].offset==site&&readback.relocations[0].target==1,"symbol and REL32 site");nt_object_free(&readback);}
    uint8_t displacement=helper_first?0xfa:1;
    CHECK(executable.text.size==7&&executable.text.bytes[site]==displacement&&
          executable.text.bytes[site+1]==(helper_first?0xff:0),"local REL32 patched");
    CHECK(executable.entry==main_offset,"main entry offset");
    free(expected.bytes);nt_artifact_free(&executable);
}
static void rejected(const NovaHostImage*i,const char*source,const char*label){
    uint8_t out[65536];memset(out,0xcc,sizeof out);
    uint64_t code=run(i,source,out,sizeof out);int intact=code==800;
    for(size_t j=0;j<sizeof out;j++)intact&=out[j]==0xcc;
    CHECK(intact,label);
}
int main(void){
    const char*paths[]={
        "toolchain/ntasm-nova/tests/x64-lower-nxo-local-entry.nova",
        "toolchain/ntasm-nova/x64_lower_nxo.nova",
        "toolchain/ntasm-nova/x64_lower_control.nova",
        "toolchain/ntasm-nova/x64_lower_fixed.nova",
        "toolchain/ntasm-nova/x64_fixed.nova",
        "toolchain/ntasm-nova/register_widths.nova",
        "toolchain/ntasm-nova/nxo_pe.nova",
        "toolchain/ntasm-nova/nxo.nova",
        "toolchain/ntasm-nova/pe.nova",
        "toolchain/ntasm-nova/parser.nova",
        "toolchain/ntasm-nova/ast.nova",
        "toolchain/ntasm-nova/lexer.nova"
    };
    char*sources[12]={0};NtFInput inputs[12];
    for(unsigned i=0;i<12;i++){size_t n=0;sources[i]=loadf(paths[i],&n);inputs[i]=(NtFInput){paths[i],sources[i],n};}
    int all=1;for(unsigned i=0;i<12;i++)if(!sources[i])all=0;CHECK(all,"sources load");
    NtArtifact compiler={0};int compiled=all&&nova_compile_many_v2(inputs,12,&compiler);
    CHECK(compiled,"Nova modules compile");
    if(!compiled)fprintf(stderr,"E%u %zu:%zu %s\n",compiler.error.code,compiler.error.line,compiler.error.column,compiler.error.message);
    NovaHostImage image={0};CHECK(compiled&&nova_host_map(&compiler,&image),"host map");
    if(image.memory){
        const char*main_first="module m\ntarget x86_64-nexora-uefi\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\ncall helper()\nret\n}\nfn helper()->u64\neffects {}\nclobbers {} {\nret\n}\n}\n";
        const char*helper_first="module m\ntarget x86_64-nexora-uefi\nsection .text {\nfn helper()->u64\neffects {}\nclobbers {} {\nret\n}\nfn main()->u64\neffects {}\nclobbers {} {\ncall helper()\nret\n}\n}\n";
        case_ok(&image,main_first,0);case_ok(&image,helper_first,1);
        const char*missing="module m\ntarget x86_64-nexora-uefi\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\ncall other()\nret\n}\nfn helper()->u64\neffects {}\nclobbers {} {\nret\n}\n}\n";
        rejected(&image,missing,"missing target transactional");
        const char*arity="module m\ntarget x86_64-nexora-uefi\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\ncall helper(1)\nret\n}\nfn helper()->u64\neffects {}\nclobbers {} {\nret\n}\n}\n";
        rejected(&image,arity,"arity transactional");
        const char*third="module m\ntarget x86_64-nexora-uefi\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\ncall helper()\nret\n}\nfn helper()->u64\neffects {}\nclobbers {} {\nret\n}\nfn extra()->u64\neffects {}\nclobbers {} {\nret\n}\n}\n";
        rejected(&image,third,"third function transactional");
        const char*exported="module m\ntarget x86_64-nexora-uefi\nsection .text {\nfn main()->u64\neffects {}\nclobbers {} {\ncall helper()\nret\n}\nexport fn helper()->u64\neffects {}\nclobbers {} {\nret\n}\n}\n";
        rejected(&image,exported,"exported helper transactional");
        uint8_t short_out[16];memset(short_out,0xcc,sizeof short_out);
        uint64_t code=run(&image,main_first,short_out,sizeof short_out);int intact=code==800;
        for(size_t j=0;j<sizeof short_out;j++)intact&=short_out[j]==0xcc;
        CHECK(intact,"short output transactional");
    }
    if(image.memory)nova_host_unmap(&image);
    if(compiled)nt_artifact_free(&compiler);
    for(unsigned i=0;i<12;i++)free(sources[i]);
    printf("NTASM_NOVA_X64_LOWER_NXO_LOCAL checks=%u failures=%u cases=%u\n",checks,failures,cases);
    return failures?1:0;
}
