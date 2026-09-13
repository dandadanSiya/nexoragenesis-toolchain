/* BOOTSTRAP_C_TEST: compile and inspect the exact A6 source; never create stub
 * machine code from this harness. Output must be a fresh proof directory. */
#include "object.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int write_file(const char *path,const void *bytes,size_t length){FILE *f=fopen(path,"wbx");if(!f)return 0;size_t n=fwrite(bytes,1,length,f);return fclose(f)==0&&n==length;}
int main(int argc,char **argv){
 if(argc!=5){fputs("build_witness source output.nxo output.efi output-rva.bin\n",stderr);return 2;}
 FILE *f=fopen(argv[1],"rb");if(!f)return 2;char source[16384];size_t n=fread(source,1,sizeof(source),f);int more=fgetc(f);fclose(f);if(more!=EOF||n==sizeof(source))return 2;
 NtFInput input={argv[1],source,n};NtObject object={0},readback={0};NtBuffer file={0};NtArtifact artifact={0};NtDiagnostic error={0};int result=1;
 if(!nt_object_compile(&input,1,&object,&error)||!nt_object_write(&object,&file,&error)||!nt_object_read(file.bytes,file.size,&readback,&error)){fprintf(stderr,"E%u %s\n",error.code,error.message);goto done;}
 /* Explicit test-carrier adapter only: preserve target2 in the saved NXO and
  * typed contracts, wrap identical x64 code in a loadable UEFI test image.
  * The production materializer continues to reject this implicit conversion. */
 NtObject carrier=readback;carrier._owned=0;carrier.target=1;
 if(!nt_object_materialize(&carrier,&artifact,&error)||!nt_make_pe(&artifact)){fprintf(stderr,"E%u %s\n",error.code,error.message);goto done;}
 uint64_t rva=0;size_t matches=0;
 const uint8_t golden[]={0x50,0x48,0x8b,0x44,0x24,0x08,0x48,0x83,0xc0,0x02,0x48,0x89,0x44,0x24,0x08,0x58,0x48,0xcf};
 for(size_t j=0;j<readback.symbol_count;j++){
  const NtObjectSymbol *s=readback.symbols+j;size_t length=strlen(s->name);
  if(length>=5&&!strcmp(s->name+length-5,".isr6")){
   if(s->section!=1||s->size!=sizeof(golden)||s->offset>artifact.text.size||sizeof(golden)>artifact.text.size-s->offset||memcmp(artifact.text.bytes+s->offset,golden,sizeof(golden))){fputs("ISR golden mismatch\n",stderr);goto done;}
   matches++;rva=4096+s->offset;
  }
 }
 if(matches!=1){fputs("expected exactly one isr6\n",stderr);goto done;}
 uint8_t rva_bytes[8];for(unsigned j=0;j<8;j++)rva_bytes[j]=(uint8_t)(rva>>(j*8));
 if(!write_file(argv[2],file.bytes,file.size)||!write_file(argv[3],artifact.pe.bytes,artifact.pe.size)||!write_file(argv[4],rva_bytes,8))goto done;
 printf("A6_SOURCE_NXO_PE_READY bytes=%zu isr_rva=%llu\n",artifact.pe.size,(unsigned long long)rva);result=0;
done:nt_artifact_free(&artifact);nt_object_free(&readback);nt_object_free(&object);free(file.bytes);return result;
}
