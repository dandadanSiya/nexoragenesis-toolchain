#include "nova.h"
#include "nova/run_host.h"
#include "nova/runtime_v2.h"
#include "object.h"
#include "x64.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures,cases;static const uint8_t guard=0xcc;
#define CHECK(c,m) do{checks++;if(!(c)){failures++;fprintf(stderr,"FAIL %s\n",m);}}while(0)
static char*loadf(const char*p,size_t*n){FILE*f=fopen(p,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long z=ftell(f);fseek(f,0,SEEK_SET);char*b=z>=0?malloc((size_t)z+1):NULL;if(!b||fread(b,1,(size_t)z,f)!=(size_t)z){free(b);fclose(f);return NULL;}fclose(f);b[z]=0;*n=(size_t)z;return b;}
static uint64_t r64(const uint8_t*b){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)b[i]<<(i*8);return v;}
static size_t encode_(uint8_t*out,const char*name,NtX64Operand*ops,size_t count){NtX64Instruction e={0};NtX64Context c={1,3};CHECK(nt_x64_encode(name,ops,count,c,&e)==0,"oracle encode");memcpy(out,e.bytes,e.length);return e.length;}
int main(void){const char*paths[]={"toolchain/ntasm-nova/tests/x64-lower-nxo-entry.nova","toolchain/ntasm-nova/x64_lower_nxo.nova","toolchain/ntasm-nova/x64_lower_control.nova","toolchain/ntasm-nova/x64_lower_fixed.nova","toolchain/ntasm-nova/x64_fixed.nova","toolchain/ntasm-nova/register_widths.nova","toolchain/ntasm-nova/nxo.nova","toolchain/ntasm-nova/pe.nova","toolchain/ntasm-nova/parser.nova","toolchain/ntasm-nova/ast.nova","toolchain/ntasm-nova/lexer.nova"};char*sources[11]={0};NtFInput inputs[11];for(unsigned i=0;i<11;i++){size_t n=0;sources[i]=loadf(paths[i],&n);inputs[i]=(NtFInput){paths[i],sources[i],n};}int all=1;for(unsigned i=0;i<11;i++)if(!sources[i])all=0;CHECK(all,"sources load");NtArtifact artifact={0};int compiled=all&&nova_compile_many_v2(inputs,11,&artifact);CHECK(compiled,"modules compile");if(!compiled)fprintf(stderr,"E%u %zu:%zu %s\n",artifact.error.code,artifact.error.line,artifact.error.column,artifact.error.message);NovaHostImage image={0};CHECK(compiled&&nova_host_map(&artifact,&image),"image maps");if(image.memory){const char*source="module m\ntarget x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {}\nclobbers {} {\nstart:\nmov rax,rbx\nje done\nadd rax,1\njmp start\ndone:\nud2\nreturn 0\n}\n}\n";uint8_t out[4096];memset(out,guard,sizeof out);NovaContextV2 c={0};CHECK(nova_context_init_v2(&c),"context init");uint64_t sh=nova_borrow_v2(&c,(void*)source,strlen(source),0),oh=nova_borrow_v2(&c,out,sizeof out,1);uint64_t args[]={sh,strlen(source),oh,sizeof out};uint64_t returned=image.entry(&c,args,4);cases++;uint64_t size=r64(out);NtObject object={0};NtDiagnostic error={0};int read_ok=returned==0&&!c.error&&size<=sizeof out-8&&nt_object_read(out+8,(size_t)size,&object,&error);CHECK(read_ok,"NXO read");uint8_t expected[64]={0};size_t total=0;NtX64Operand mov[2]={{.kind=NT_X64_REG,.width=64,.reg=0},{.kind=NT_X64_REG,.width=64,.reg=3}};total+=encode_(expected+total,"mov",mov,2);NtX64Operand add[2]={{.kind=NT_X64_REG,.width=64,.reg=0},{.kind=NT_X64_IMM,.imm=1}};NtX64Instruction add_e={0};NtX64Context ctx={1,3};CHECK(nt_x64_encode("add",add,2,ctx,&add_e)==0,"add oracle");NtX64Operand je={.kind=NT_X64_REL,.imm=add_e.length+5,.is_signed=1};total+=encode_(expected+total,"je",&je,1);memcpy(expected+total,add_e.bytes,add_e.length);total+=add_e.length;NtX64Operand jmp={.kind=NT_X64_REL,.imm=UINT64_MAX-(total+4),.is_signed=1};total+=encode_(expected+total,"jmp",&jmp,1);total+=encode_(expected+total,"ud2",NULL,0);if(read_ok){CHECK(nt_object_validate(&object,&error),"NXO validate");CHECK(object.target==1&&object.entry_symbol==1,"NXO target entry");CHECK(object.sections[0].size==total&&!memcmp(object.sections[0].bytes,expected,total),"NXO text bytes");CHECK(object.symbol_count==1&&!strcmp(object.symbols[0].name,"main")&&!strcmp(object.symbols[0].contract,"v0")&&object.symbols[0].section==1&&object.symbols[0].flags==3&&object.symbols[0].offset==0&&object.symbols[0].size==total,"NXO main symbol");nt_object_free(&object);}CHECK(nova_context_destroy_v2(&c),"context cleanup");uint8_t short_out[8];memset(short_out,guard,sizeof short_out);CHECK(nova_context_init_v2(&c),"short context");sh=nova_borrow_v2(&c,(void*)source,strlen(source),0);oh=nova_borrow_v2(&c,short_out,sizeof short_out,1);uint64_t short_args[]={sh,strlen(source),oh,sizeof short_out};returned=image.entry(&c,short_args,4);cases++;int intact=returned==800&&!c.error;for(unsigned i=0;i<sizeof short_out;i++)intact&=short_out[i]==guard;CHECK(intact,"short output transactional");CHECK(nova_context_destroy_v2(&c),"short cleanup");
char large[8192]="module m\n target x86_64-nexora-none\nsection .text {\nfn f()->u64\neffects {}\nclobbers {} {\nstart:\njmp far\n";
for(unsigned i=0;i<140;i++)strcat(large,"ud2\n");
strcat(large,"far:\nje start\nud2\nreturn 0\n}\n}\n");
uint8_t wide_out[4096];memset(wide_out,guard,sizeof wide_out);
CHECK(nova_context_init_v2(&c),"wide context");
sh=nova_borrow_v2(&c,large,strlen(large),0);
oh=nova_borrow_v2(&c,wide_out,sizeof wide_out,1);
uint64_t wide_args[]={sh,strlen(large),oh,sizeof wide_out};
returned=image.entry(&c,wide_args,4);cases++;
size=r64(wide_out);memset(&object,0,sizeof object);
read_ok=returned==0&&!c.error&&size<=sizeof wide_out-8&&nt_object_read(wide_out+8,(size_t)size,&object,&error);
CHECK(read_ok,"wide NXO read");
uint8_t wide_expected[293];total=0;
NtX64Operand forward={.kind=NT_X64_REL,.imm=280,.is_signed=1};
total+=encode_(wide_expected+total,"jmp",&forward,1);
for(unsigned i=0;i<140;i++)total+=encode_(wide_expected+total,"ud2",NULL,0);
NtX64Operand backward={.kind=NT_X64_REL,.imm=UINT64_MAX-290,.is_signed=1};
total+=encode_(wide_expected+total,"je",&backward,1);
total+=encode_(wide_expected+total,"ud2",NULL,0);
CHECK(total==293,"wide oracle length");
if(read_ok){CHECK(nt_object_validate(&object,&error),"wide NXO validate");CHECK(object.sections[0].size==total&&!memcmp(object.sections[0].bytes,wide_expected,total),"wide NXO text bytes");CHECK(object.symbols[0].size==total,"wide main symbol size");nt_object_free(&object);}
CHECK(nova_context_destroy_v2(&c),"wide cleanup");
uint8_t wide_short[128];memset(wide_short,guard,sizeof wide_short);
CHECK(nova_context_init_v2(&c),"wide short context");
sh=nova_borrow_v2(&c,large,strlen(large),0);
oh=nova_borrow_v2(&c,wide_short,sizeof wide_short,1);
uint64_t wide_short_args[]={sh,strlen(large),oh,sizeof wide_short};
returned=image.entry(&c,wide_short_args,4);cases++;
int wide_intact=returned==800&&!c.error;
for(unsigned i=0;i<sizeof wide_short;i++)wide_intact&=wide_short[i]==guard;
CHECK(wide_intact,"wide short output transactional");
CHECK(nova_context_destroy_v2(&c),"wide short cleanup");
char missing[8192];strcpy(missing,large);
char *target=strstr(missing,"jmp far");
CHECK(target!=NULL,"missing target setup");
if(target)memcpy(target,"jmp bad",7);
uint8_t failed_out[4096];memset(failed_out,guard,sizeof failed_out);
CHECK(nova_context_init_v2(&c),"missing context");
sh=nova_borrow_v2(&c,missing,strlen(missing),0);
oh=nova_borrow_v2(&c,failed_out,sizeof failed_out,1);
uint64_t missing_args[]={sh,strlen(missing),oh,sizeof failed_out};
returned=image.entry(&c,missing_args,4);cases++;
int missing_intact=returned==821&&!c.error;
for(unsigned i=0;i<sizeof failed_out;i++)missing_intact&=failed_out[i]==guard;
CHECK(missing_intact,"wide unresolved label transactional");
CHECK(nova_context_destroy_v2(&c),"missing cleanup");}if(image.memory)nova_host_unmap(&image);if(compiled)nt_artifact_free(&artifact);for(unsigned i=0;i<11;i++)free(sources[i]);printf("NTASM_NOVA_X64_LOWER_NXO checks=%u failures=%u cases=%u\n",checks,failures,cases);return failures?1:0;}
