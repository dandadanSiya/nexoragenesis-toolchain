#include "object.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(c,m) do{++checks;if(!(c)){++failures;fprintf(stderr,"FAIL: %s\n",m);}}while(0)
static uint64_t r64(const uint8_t *p){uint64_t n=0;for(unsigned j=0;j<8;j++)n|=(uint64_t)p[j]<<(8*j);return n;}
static void w64(uint8_t *p,uint64_t n){for(unsigned j=0;j<8;j++)p[j]=(uint8_t)(n>>(8*j));}
#ifdef NTO_WRAP_ALLOC
static unsigned allocation,fail_at;
void *__real_malloc(size_t);
void *__real_calloc(size_t,size_t);
void *__wrap_malloc(size_t n){if(fail_at&&++allocation==fail_at)return NULL;return __real_malloc(n);}
void *__wrap_calloc(size_t n,size_t s){if(fail_at&&++allocation==fail_at)return NULL;return __real_calloc(n,s);}
#endif
int main(void){
 uint8_t code[]={0x48,0xb8,42,0,0,0,0,0,0,0,0xc3},data[8]={0};
 NtObjectSymbol symbols[]={{"a","data:ptr",2,NT_OBJECT_VARIABLE,0,8},{"b","fn:u64",1,NT_OBJECT_FUNCTION,0,11}};
 NtObjectReloc reloc={2,NT_OBJECT_DIR64,2,0,0};
 NtObject input={0};input.target=1;input.entry_symbol=2;input.symbols=symbols;input.symbol_count=2;input.relocations=&reloc;input.relocation_count=1;
 input.sections[0]=(NtBuffer){code,sizeof(code),sizeof(code)};input.sections[1]=(NtBuffer){data,8,8};
 NtBuffer file={0};NtDiagnostic error;CHECK(nt_object_write(&input,&file,&error),"write fixture with real relocation");if(!file.bytes)return 1;
 struct{size_t offset;uint64_t value;unsigned width;const char *name;} cases[]={
 {0,0,1,"magic"},{8,2,4,"version"},{12,3,4,"target"},{16,2,4,"ABI"},{20,4,4,"sections"},
 {24,0xffffffffu,4,"symbol count"},{28,0xffffffffu,4,"relocation count"},{32,0,8,"section offset"},
 {40,0,8,"symbol offset"},{48,0,8,"relocation offset"},{56,0,8,"strings offset"},{64,UINT64_MAX,8,"string size"},
 {72,0,8,"payload offset"},{80,0,8,"file size"},{88,3,4,"entry"},{92,1,4,"reserved header"},
 {100,7,4,"W+X"},{104,1,8,"section alignment"},{112,UINT64_MAX,8,"section size"},{120,0,8,"section payload"},
 {128,1,8,"section reserved"},{248,4,4,"symbol section"},{252,16,4,"symbol flags"},{256,UINT64_MAX,8,"symbol range"},
 {280,2,4,"symbol ABI"},{284,1,4,"symbol reserved"}};
 for(size_t j=0;j<sizeof(cases)/sizeof(cases[0]);j++){
  uint8_t saved[8];memcpy(saved,file.bytes+cases[j].offset,cases[j].width);
  for(unsigned k=0;k<cases[j].width;k++)file.bytes[cases[j].offset+k]=(uint8_t)(cases[j].value>>(8*k));
  NtObject out={0};CHECK(!nt_object_read(file.bytes,file.size,&out,&error)&&error.code==800,cases[j].name);CHECK(!out._owned&&!out.symbols&&!out.relocations,"failed read leaves no partial object");nt_object_free(&out);
  memcpy(file.bytes+cases[j].offset,saved,cases[j].width);
 }
 size_t ra=(size_t)r64(file.bytes+48);
 for(unsigned j=0;j<4;j++){size_t at=ra+(j==0?4:j==1?8:j==2?16:20);unsigned width=j==1?8:4;uint8_t saved[8];memcpy(saved,file.bytes+at,width);uint64_t v=j==0?3:j==1?7:j==2?3:1;for(unsigned k=0;k<width;k++)file.bytes[at+k]=(uint8_t)(v>>(k*8));NtObject out={0};CHECK(!nt_object_read(file.bytes,file.size,&out,&error),"bad relocation rejected");nt_object_free(&out);memcpy(file.bytes+at,saved,width);}
 NtObject out={0};CHECK(!nt_object_read(file.bytes,file.size-1,&out,&error),"truncated object rejected");
 uint8_t *overlay=malloc(file.size+1);memcpy(overlay,file.bytes,file.size);overlay[file.size]=0;CHECK(!nt_object_read(overlay,file.size+1,&out,&error),"overlay rejected");free(overlay);
 size_t strings=(size_t)r64(file.bytes+56);uint8_t saved=file.bytes[strings];file.bytes[strings]=0;CHECK(!nt_object_read(file.bytes,file.size,&out,&error),"embedded NUL name rejected");file.bytes[strings]=saved;
 size_t symbol1name=(size_t)strings+(size_t)(file.bytes[304]|(unsigned)file.bytes[305]<<8);saved=file.bytes[symbol1name];file.bytes[symbol1name]='a';CHECK(!nt_object_read(file.bytes,file.size,&out,&error),"duplicate symbol rejected");file.bytes[symbol1name]=saved;
 /* Preserve signed addends without host-dependent serialization. */
 w64(file.bytes+ra+24,UINT64_MAX);CHECK(nt_object_read(file.bytes,file.size,&out,&error)&&out.relocations[0].addend==-1,"negative relocation addend roundtrip");nt_object_free(&out);w64(file.bytes+ra+24,0);
#ifdef NTO_WRAP_ALLOC
 unsigned exercised=0;
 for(unsigned j=1;j<32;j++){allocation=0;fail_at=j;int ok=nt_object_read(file.bytes,file.size,&out,&error);fail_at=0;if(ok){nt_object_free(&out);break;}exercised++;CHECK(!out._owned&&!out.symbols&&!out.relocations,"allocation failure cleaned completely");nt_object_free(&out);}
 CHECK(exercised>=7,"allocation failures exercised at every reader allocation");
#endif
 free(file.bytes);printf("NTASM_NXO_ERROR_TESTS checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
