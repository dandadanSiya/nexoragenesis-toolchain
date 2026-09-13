#include "ntasm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(c,m) do{++checks;if(!(c)){++failures;fprintf(stderr,"FAIL: %s\n",m);}}while(0)
static unsigned r16(const uint8_t *p){return p[0]|(unsigned)p[1]<<8;}
static unsigned r32(const uint8_t *p){return r16(p)|r16(p+2)<<16;}
static void save(const char *path,const NtArtifact *a){FILE *f=fopen(path,"wbx");CHECK(f!=NULL,"create fresh PE proof");if(f){CHECK(fwrite(a->pe.bytes,1,a->pe.size,f)==a->pe.size,"write PE proof");CHECK(fclose(f)==0,"close PE proof");}}
int main(int argc,char **argv){
 NtArtifact a={0};uint8_t code[]={0x48,0x8b,0x05,0x01,0x10,0,0,0x48,0x8b,0,0xc3};
 a.text.bytes=malloc(sizeof code);a.text.size=a.text.capacity=sizeof code;memcpy(a.text.bytes,code,sizeof code);
 a.rdata.bytes=calloc(8,1);a.rdata.size=a.rdata.capacity=8;
 a.data.bytes=calloc(8,1);a.data.size=a.data.capacity=8;a.data.bytes[0]=42;
 uint64_t pointer=UINT64_C(0x140003000);memcpy(a.rdata.bytes,&pointer,8);
 a.base_relocations=calloc(1,sizeof(*a.base_relocations));a.base_relocation_count=1;a.base_relocations[0]=(NtBaseReloc){2,0};
 CHECK(nt_make_pe(&a),"emit PE with a real data pointer relocation");
 if(a.pe.size){unsigned reloc_raw=r32(a.pe.bytes+512+20);CHECK(r16(a.pe.bytes+reloc_raw+8)==0xa000&&r16(a.pe.bytes+reloc_raw+10)==0xa008,"anchor and data pointer both have DIR64");}
 if(argc>1)save(argv[1],&a);
 uint8_t *original=malloc(a.pe.size);size_t original_size=a.pe.size;memcpy(original,a.pe.bytes,a.pe.size);
 a.base_relocations[0].section=4;CHECK(!nt_make_pe(&a),"invalid relocation section rejected");
 a.base_relocations[0].section=2;a.base_relocations[0].offset=1;CHECK(!nt_make_pe(&a),"relocation must fit logical data payload");
 a.base_relocations[0].offset=0;
 a.base_relocations=realloc(a.base_relocations,2*sizeof(*a.base_relocations));a.base_relocations[1]=a.base_relocations[0];a.base_relocation_count=2;
 CHECK(!nt_make_pe(&a),"duplicate pointer relocation rejected");
 CHECK(a.pe.size==original_size&&!memcmp(a.pe.bytes,original,original_size),"failed emission preserves previous valid PE");free(original);
 nt_artifact_free(&a);
 a.text.bytes=malloc(sizeof code);a.text.size=a.text.capacity=sizeof code;memcpy(a.text.bytes,code,sizeof code);
 unsigned displacement=8192+8+599*8-(4096+7);for(unsigned i=0;i<4;i++)a.text.bytes[3+i]=(uint8_t)(displacement>>(8*i));
 a.rdata.bytes=calloc(600,8);a.rdata.size=a.rdata.capacity=600*8;
 a.data.bytes=calloc(8,1);a.data.size=a.data.capacity=8;a.data.bytes[0]=42;
 a.base_relocations=calloc(600,sizeof(*a.base_relocations));a.base_relocation_count=600;
 pointer=UINT64_C(0x140004000);
 for(unsigned i=0;i<600;i++){memcpy(a.rdata.bytes+8*i,&pointer,8);a.base_relocations[i]=(NtBaseReloc){2,8*i};}
 CHECK(nt_make_pe(&a),"multiple relocation pages emitted");
 if(a.pe.size){
  unsigned raw=r32(a.pe.bytes+512+20),size=r32(a.pe.bytes+308),count=0,blocks=0;
  for(unsigned at=0;at<size;){unsigned block=r32(a.pe.bytes+raw+at+4);if(block<8||block>size-at)break;for(unsigned i=8;i<block;i+=2)if(r16(a.pe.bytes+raw+at+i)>>12==10)count++;at+=block;blocks++;}
  CHECK(count==601&&blocks==2,"all600 pointers plus anchor in two blocks");
  CHECK(size==1220&&a.pe.size==8704&&r32(a.pe.bytes+208)==24576,"relocation raw/image growth exact");
 }
 if(argc>2)save(argv[2],&a);
 nt_artifact_free(&a);
 printf("NTASM_PE_RELOCATION_TESTS checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
