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
static const uint8_t guard=0xcc;
static char*load(const char*p,size_t*n){FILE*f=fopen(p,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long z=ftell(f);fseek(f,0,SEEK_SET);char*b=z>=0?malloc((size_t)z+1):NULL;if(!b||fread(b,1,(size_t)z,f)!=(size_t)z){free(b);fclose(f);return NULL;}fclose(f);b[z]=0;*n=(size_t)z;return b;}
static uint64_t r64(const uint8_t*p){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)p[i]<<(i*8);return v;}
static NtBuffer make_object(uint32_t target,const uint8_t*t,size_t tn,const uint8_t*r,size_t rn,const uint8_t*d,size_t dn,int symbol){NtObject o={0};o.target=target;o.sections[0]=(NtBuffer){(uint8_t*)t,tn,tn};o.sections[1]=(NtBuffer){(uint8_t*)r,rn,rn};o.sections[2]=(NtBuffer){(uint8_t*)d,dn,dn};NtObjectSymbol s={"entry","fn:u64",1,NT_OBJECT_FUNCTION,0,tn};if(symbol){o.symbols=&s;o.symbol_count=1;o.entry_symbol=1;}NtBuffer file={0};NtDiagnostic e={0};CHECK(nt_object_write(&o,&file,&e),"oracle writes NXO");return file;}
typedef struct{uint64_t returned,runtime;uint8_t out[2048];}Run;
static Run run(const NovaHostImage*i,const NtBuffer*l,uint64_t ln,const NtBuffer*r,uint64_t rn,uint64_t outn){Run x={0};memset(x.out,guard,sizeof(x.out));NovaContextV2 c={0};CHECK(nova_context_init_v2(&c),"context init");uint64_t lh=nova_borrow_v2(&c,l->bytes,ln,0),rh=nova_borrow_v2(&c,r->bytes,rn,0),oh=nova_borrow_v2(&c,x.out,outn,1);uint64_t a[]={lh,ln,rh,rn,oh,outn};x.returned=i->entry(&c,a,6);x.runtime=c.error;CHECK(nova_context_destroy_v2(&c),"context cleanup");return x;}
static void expect_error(const NovaHostImage*i,const char*n,const NtBuffer*l,uint64_t ln,const NtBuffer*r,uint64_t rn,uint64_t outn,uint64_t code){Run x=run(i,l,ln,r,rn,outn);cases++;checks++;if(x.returned!=code||x.runtime||x.out[0]!=guard||x.out[1]!=guard){failures++;fprintf(stderr,"FAIL error %s ret=%llu runtime=%llu\n",n,(unsigned long long)x.returned,(unsigned long long)x.runtime);}}
int main(void){const char*paths[]={"toolchain/ntasm-nova/tests/nxo-merge-entry.nova","toolchain/ntasm-nova/nxo_merge.nova","toolchain/ntasm-nova/nxo.nova","toolchain/ntasm-nova/pe.nova"};char*sources[4]={0};NtFInput inputs[4];for(unsigned i=0;i<4;i++){size_t n=0;sources[i]=load(paths[i],&n);inputs[i]=(NtFInput){paths[i],sources[i],n};}CHECK(sources[0]&&sources[1]&&sources[2]&&sources[3],"sources load");NtArtifact a={0};int compiled=sources[0]&&sources[1]&&sources[2]&&sources[3]&&nova_compile_many_v2(inputs,4,&a);CHECK(compiled,"modules compile");if(!compiled)fprintf(stderr,"E%u %zu:%zu %s\n",a.error.code,a.error.line,a.error.column,a.error.message);NovaHostImage image={0};CHECK(compiled&&nova_host_map(&a,&image),"image maps");
 if(image.memory){const uint8_t lt[]={1,2},lr[]={3},rt[]={4},rd[]={5,6};NtBuffer left=make_object(1,lt,2,lr,1,NULL,0,0),right=make_object(1,rt,1,NULL,0,rd,2,0),other=make_object(2,rt,1,NULL,0,NULL,0,0),with_symbol=make_object(1,rt,1,NULL,0,NULL,0,1);
  Run x=run(&image,&left,left.size,&right,right.size,sizeof(x.out));cases++;CHECK(x.returned==0&&x.runtime==0,"merge success");uint64_t size=r64(x.out);NtObject merged={0};NtDiagnostic e={0};CHECK(size>0&&size+8<=sizeof(x.out)&&nt_object_read(x.out+8,size,&merged,&e),"merged NXO reads");if(merged._owned){CHECK(merged.target==1&&merged.entry_symbol==0&&merged.symbol_count==0&&merged.relocation_count==0,"merged metadata");CHECK(merged.sections[0].size==17&&merged.sections[0].bytes[0]==1&&merged.sections[0].bytes[1]==2&&merged.sections[0].bytes[16]==4,"merged text");CHECK(merged.sections[1].size==1&&merged.sections[1].bytes[0]==3,"merged rdata");CHECK(merged.sections[2].size==2&&merged.sections[2].bytes[0]==5&&merged.sections[2].bytes[1]==6,"merged data");nt_object_free(&merged);}
  Run y=run(&image,&left,left.size,&right,right.size,sizeof(y.out));cases++;CHECK(y.returned==0&&!memcmp(x.out,y.out,(size_t)size+8),"merge deterministic");
  expect_error(&image,"target mismatch",&left,left.size,&other,other.size,sizeof(x.out),820);
  Run entry=run(&image,&left,left.size,&with_symbol,with_symbol.size,sizeof(entry.out));cases++;
  CHECK(entry.returned==0&&entry.runtime==0,"right entry accepted");
  uint64_t entry_size=r64(entry.out);NtObject entry_object={0};NtDiagnostic entry_error={0};
  CHECK(entry_size>0&&nt_object_read(entry.out+8,entry_size,&entry_object,&entry_error),"entry NXO reads");
  if(entry_object._owned){CHECK(entry_object.entry_symbol==1&&entry_object.symbol_count==1&&entry_object.symbols[0].offset==16,"right entry remapped");nt_object_free(&entry_object);}
  expect_error(&image,"two entries",&with_symbol,with_symbol.size,&with_symbol,with_symbol.size,sizeof(x.out),826);
  expect_error(&image,"short output",&left,left.size,&right,right.size,8,822);expect_error(&image,"truncated",&left,left.size-1,&right,right.size,sizeof(x.out),800);
  const uint8_t left_data[]={7,8};
  NtObjectSymbol left_symbol={"b","fn:u64",1,NT_OBJECT_FUNCTION,0,1};
  NtObjectSymbol right_symbol={"a","data:u8",3,NT_OBJECT_VARIABLE,1,1};
  NtObject left_object={0},right_object={0};left_object.target=right_object.target=1;
  left_object.sections[0]=(NtBuffer){(uint8_t*)lt,2,2};left_object.sections[2]=(NtBuffer){(uint8_t*)left_data,2,2};
  right_object.sections[2]=(NtBuffer){(uint8_t*)rd,2,2};
  left_object.symbols=&left_symbol;left_object.symbol_count=1;
  right_object.symbols=&right_symbol;right_object.symbol_count=1;
  NtBuffer left_symbols={0},right_symbols={0};NtDiagnostic symbol_error={0};
  CHECK(nt_object_write(&left_object,&left_symbols,&symbol_error),"oracle writes left symbols");
  CHECK(nt_object_write(&right_object,&right_symbols,&symbol_error),"oracle writes right symbols");
  Run z=run(&image,&left_symbols,left_symbols.size,&right_symbols,right_symbols.size,sizeof(z.out));cases++;
  CHECK(z.returned==0&&z.runtime==0,"symbol merge success");
  uint64_t symbol_size=r64(z.out);NtObject symbol_merged={0};
  CHECK(symbol_size>0&&nt_object_read(z.out+8,symbol_size,&symbol_merged,&symbol_error),"symbol merge reads");
  if(symbol_merged._owned){CHECK(symbol_merged.symbol_count==2&&!strcmp(symbol_merged.symbols[0].name,"a")&&!strcmp(symbol_merged.symbols[1].name,"b"),"symbols globally sorted");CHECK(symbol_merged.symbols[0].offset==9&&symbol_merged.symbols[1].offset==0,"right symbol offset adjusted");nt_object_free(&symbol_merged);}
  NtObjectSymbol duplicate_symbol={"b","data:u8",3,NT_OBJECT_VARIABLE,0,1};
  right_object.symbols=&duplicate_symbol;NtBuffer duplicate_file={0};
  CHECK(nt_object_write(&right_object,&duplicate_file,&symbol_error),"oracle writes duplicate symbol");
  expect_error(&image,"duplicate symbol",&left_symbols,left_symbols.size,&duplicate_file,duplicate_file.size,sizeof(x.out),823);
  NtObjectSymbol import_symbol={"external","fn:u64",0,NT_OBJECT_FUNCTION|NT_OBJECT_IMPORT,0,0};
  right_object.symbols=&import_symbol;right_object.sections[2]=(NtBuffer){0};NtBuffer import_file={0};
  CHECK(nt_object_write(&right_object,&import_file,&symbol_error),"oracle writes import symbol");
  expect_error(&image,"unresolved import",&left_symbols,left_symbols.size,&import_file,import_file.size,sizeof(x.out),824);
  free(left_symbols.bytes);free(right_symbols.bytes);free(duplicate_file.bytes);free(import_file.bytes);
  const uint8_t reloc_text[8]={0};
  NtObjectSymbol left_reloc_symbol={"z","fn:u64",1,NT_OBJECT_FUNCTION,0,8};
  NtObjectSymbol right_reloc_symbol={"y","fn:u64",1,NT_OBJECT_FUNCTION,0,8};
  NtObjectReloc left_reloc={1,NT_OBJECT_REL32,1,0,-4};
  NtObjectReloc right_reloc={1,NT_OBJECT_DIR64,1,0,5};
  NtObject left_reloc_object={0},right_reloc_object={0};
  left_reloc_object.target=right_reloc_object.target=1;
  left_reloc_object.sections[0]=(NtBuffer){(uint8_t*)reloc_text,8,8};
  right_reloc_object.sections[0]=(NtBuffer){(uint8_t*)reloc_text,8,8};
  left_reloc_object.symbols=&left_reloc_symbol;left_reloc_object.symbol_count=1;
  right_reloc_object.symbols=&right_reloc_symbol;right_reloc_object.symbol_count=1;
  left_reloc_object.relocations=&left_reloc;left_reloc_object.relocation_count=1;
  right_reloc_object.relocations=&right_reloc;right_reloc_object.relocation_count=1;
  NtBuffer left_reloc_file={0},right_reloc_file={0};NtDiagnostic reloc_error={0};
  CHECK(nt_object_write(&left_reloc_object,&left_reloc_file,&reloc_error),"oracle writes left relocation");
  CHECK(nt_object_write(&right_reloc_object,&right_reloc_file,&reloc_error),"oracle writes right relocation");
  Run rel=run(&image,&left_reloc_file,left_reloc_file.size,&right_reloc_file,right_reloc_file.size,sizeof(rel.out));cases++;
  CHECK(rel.returned==0&&rel.runtime==0,"relocation merge success");
  uint64_t reloc_size=r64(rel.out);NtObject reloc_merged={0};
  CHECK(reloc_size>0&&nt_object_read(rel.out+8,reloc_size,&reloc_merged,&reloc_error),"relocation merge reads");
  if(reloc_merged._owned){CHECK(reloc_merged.symbol_count==2&&!strcmp(reloc_merged.symbols[0].name,"y")&&!strcmp(reloc_merged.symbols[1].name,"z"),"relocation symbols remapped order");CHECK(reloc_merged.relocation_count==2&&reloc_merged.relocations[0].offset==0&&reloc_merged.relocations[0].target==2&&reloc_merged.relocations[0].kind==NT_OBJECT_REL32&&reloc_merged.relocations[0].addend==-4,"left relocation remapped");CHECK(reloc_merged.relocations[1].offset==16&&reloc_merged.relocations[1].target==1&&reloc_merged.relocations[1].kind==NT_OBJECT_DIR64&&reloc_merged.relocations[1].addend==5,"right relocation shifted");nt_object_free(&reloc_merged);}
  free(left_reloc_file.bytes);free(right_reloc_file.bytes);
  NtObjectSymbol imported={"target","fn:u64",0,NT_OBJECT_FUNCTION|NT_OBJECT_IMPORT,0,0};
  NtObjectSymbol exported={"target","fn:u64",1,NT_OBJECT_FUNCTION|NT_OBJECT_EXPORT,0,8};
  NtObjectReloc import_reloc={1,NT_OBJECT_REL32,1,0,0};
  NtObject importer={0},provider={0};importer.target=provider.target=1;
  importer.sections[0]=(NtBuffer){(uint8_t*)reloc_text,8,8};provider.sections[0]=(NtBuffer){(uint8_t*)reloc_text,8,8};
  importer.symbols=&imported;importer.symbol_count=1;importer.relocations=&import_reloc;importer.relocation_count=1;
  provider.symbols=&exported;provider.symbol_count=1;
  NtBuffer importer_file={0},provider_file={0};NtDiagnostic import_error={0};
  CHECK(nt_object_write(&importer,&importer_file,&import_error),"oracle writes importer");
  CHECK(nt_object_write(&provider,&provider_file,&import_error),"oracle writes provider");
  Run linked=run(&image,&importer_file,importer_file.size,&provider_file,provider_file.size,sizeof(linked.out));cases++;
  CHECK(linked.returned==0&&linked.runtime==0,"import resolves");
  uint64_t linked_size=r64(linked.out);NtObject linked_object={0};
  CHECK(linked_size>0&&nt_object_read(linked.out+8,linked_size,&linked_object,&import_error),"resolved NXO reads");
  if(linked_object._owned){CHECK(linked_object.symbol_count==1&&!strcmp(linked_object.symbols[0].name,"target")&&(linked_object.symbols[0].flags&NT_OBJECT_IMPORT)==0&&linked_object.symbols[0].offset==16,"definition kept and shifted");CHECK(linked_object.relocation_count==1&&linked_object.relocations[0].target==1&&linked_object.relocations[0].offset==0,"import relocation resolved");nt_object_free(&linked_object);}
  NtObjectSymbol private_definition={"target","fn:u64",1,NT_OBJECT_FUNCTION,0,8};provider.symbols=&private_definition;NtBuffer private_file={0};
  CHECK(nt_object_write(&provider,&private_file,&import_error),"oracle writes private provider");
  expect_error(&image,"definition not exported",&importer_file,importer_file.size,&private_file,private_file.size,sizeof(x.out),824);
  NtObjectSymbol wrong_contract={"target","fn:u32",1,NT_OBJECT_FUNCTION|NT_OBJECT_EXPORT,0,8};provider.symbols=&wrong_contract;NtBuffer contract_file={0};
  CHECK(nt_object_write(&provider,&contract_file,&import_error),"oracle writes wrong contract");
  expect_error(&image,"contract mismatch",&importer_file,importer_file.size,&contract_file,contract_file.size,sizeof(x.out),825);
  NtObjectSymbol wrong_role={"target","data:u64",3,NT_OBJECT_VARIABLE|NT_OBJECT_EXPORT,0,8};provider.symbols=&wrong_role;provider.sections[0]=(NtBuffer){0};provider.sections[2]=(NtBuffer){(uint8_t*)reloc_text,8,8};NtBuffer role_file={0};
  CHECK(nt_object_write(&provider,&role_file,&import_error),"oracle writes wrong role");
  expect_error(&image,"role mismatch",&importer_file,importer_file.size,&role_file,role_file.size,sizeof(x.out),825);
  free(importer_file.bytes);free(provider_file.bytes);free(private_file.bytes);free(contract_file.bytes);free(role_file.bytes);
  free(left.bytes);free(right.bytes);free(other.bytes);free(with_symbol.bytes);}
 if(image.memory) nova_host_unmap(&image);
 if(compiled) nt_artifact_free(&a);
 for(unsigned i=0;i<4;i++) free(sources[i]);
 printf("NTASM_NOVA_NXO_MERGE checks=%u failures=%u cases=%u\n",checks,failures,cases);
 return failures?1:0;
}
