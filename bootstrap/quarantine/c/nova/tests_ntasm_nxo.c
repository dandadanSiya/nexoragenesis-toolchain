#include "nova.h"
#include "nova/run_host.h"
#include "object.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures,cases;
#define CHECK(c,m) do {checks++;if(!(c)){failures++;fprintf(stderr,"FAIL %s\n",m);}}while(0)
static char *load(const char *path,size_t *length) {
  FILE *f=fopen(path,"rb");if(!f)return NULL;
  if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long n=ftell(f);
  if(n<0 || n>4*1024*1024 || fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
  char *p=malloc((size_t)n+1);if(!p){fclose(f);return NULL;}
  size_t got=fread(p,1,(size_t)n,f);fclose(f);if(got!=(size_t)n){free(p);return NULL;}p[got]=0;*length=got;return p;
}
static void parity(NovaHostImage *image,const uint8_t *input,size_t size,unsigned mode) {
  NtObject reference={0};NtDiagnostic error={0};NtBuffer reencoded={0};
  int accepted=nt_object_read(input,size,&reference,&error);
  if(accepted && mode){if(mode==1)reference.target=3;if(mode==2)reference.entry_symbol=65537;if(mode==3 && reference.symbol_count)reference.symbols[0].flags=16;}
  accepted=accepted && nt_object_write(&reference,&reencoded,&error);
  size_t capacity=size?size:1;
  uint8_t *out=malloc(capacity+16);CHECK(out!=NULL,"output allocation");
  if(!out){nt_object_free(&reference);free(reencoded.bytes);return;}
  memset(out,0xcc,capacity+16);
  NovaContextV2 context={0};int ready=nova_context_init_v2(&context);CHECK(ready,"context init");
  if(ready){
    uint64_t in_handle=nova_borrow_v2(&context,(void *)input,size,0);
    uint64_t out_handle=nova_borrow_v2(&context,out,capacity,1);
    uint64_t args[]={in_handle,size,out_handle,capacity,mode};
    uint64_t returned=image->entry(&context,args,5);
    cases++;
    int match=!context.error && (accepted?(returned==reencoded.size && !memcmp(out,reencoded.bytes,reencoded.size)):(returned==(UINT64_C(1)<<63)+800));
    if(!match)fprintf(stderr,"case%u size%zu mode%u expected%d ret%"PRIu64" runtime%"PRIu64"\n",cases,size,mode,accepted,returned,context.error);
    CHECK(match,"Nova NXO read/write matches independent C");
    int intact=1;for(size_t j=capacity;j<capacity+16;j++)if(out[j]!=0xcc)intact=0;
    if(!accepted)for(size_t j=0;j<capacity;j++)if(out[j]!=0xcc)intact=0;
    CHECK(intact,"failure transaction and output guards");
    CHECK(nova_context_destroy_v2(&context),"context cleanup");
  }
  nt_object_free(&reference);free(reencoded.bytes);free(out);
}
int main(void) {
  size_t sizes[3]={0};char *sources[3]={load("toolchain/ntasm-nova/tests/nxo-entry.nova",sizes),load("toolchain/ntasm-nova/nxo.nova",sizes+1),load("toolchain/ntasm-nova/tests/fixtures/pe-dependency.nova",sizes+2)};
  CHECK(sources[0] && sources[1] && sources[2],"Nova modules load");
  NtFInput inputs[]={{"nxo-entry.nova",sources[0],sizes[0]},{"nxo.nova",sources[1],sizes[1]},{"pe.nova",sources[2],sizes[2]}};
  NtArtifact artifact={0};NovaHostImage image={0};
  int compiled=sources[0] && sources[1] && sources[2] && nova_compile_many_v2(inputs,3,&artifact);
  CHECK(compiled,"NXO Nova modules compile");
  if(!compiled)fprintf(stderr,"E%u %zu:%zu %s\n",artifact.error.code,artifact.error.line,artifact.error.column,artifact.error.message);
  if(compiled)CHECK(nova_host_map(&artifact,&image),"map NXO machine code");
  if(image.memory){
    uint8_t code[]={0x48,0xb8,42,0,0,0,0,0,0,0,0xc3},data[8]={0};
    NtObjectSymbol symbols[]={{"a","data:ptr",2,NT_OBJECT_VARIABLE,0,8},{"b","fn:u64",1,NT_OBJECT_FUNCTION,0,sizeof(code)}};
    NtObjectReloc relocation={2,NT_OBJECT_DIR64,2,0,-1};
    NtObject object={0};object.target=1;object.entry_symbol=2;object.symbols=symbols;object.symbol_count=2;object.relocations=&relocation;object.relocation_count=1;
    object.sections[0]=(NtBuffer){code,sizeof(code),sizeof(code)};object.sections[1]=(NtBuffer){data,sizeof(data),sizeof(data)};
    NtBuffer file={0};NtDiagnostic error={0};CHECK(nt_object_write(&object,&file,&error),"C fixture serialization");
    if(file.bytes){
      parity(&image,file.bytes,file.size,0);
      for(unsigned mode=1;mode<=3;mode++)parity(&image,file.bytes,file.size,mode);
      for(size_t at=0;at<file.size;at++){file.bytes[at]^=0x80;parity(&image,file.bytes,file.size,0);file.bytes[at]^=0x80;}
      for(size_t length=0;length<file.size;length+=17)parity(&image,file.bytes,length,0);
      free(file.bytes);
    }
    object.target=2;object.entry_symbol=0;object.symbol_count=0;object.relocation_count=0;object.sections[0]=(NtBuffer){0};object.sections[1]=(NtBuffer){0};
    NtBuffer empty={0};CHECK(nt_object_write(&object,&empty,&error),"empty bare library fixture");if(empty.bytes)parity(&image,empty.bytes,empty.size,0);free(empty.bytes);
  }
  nova_host_unmap(&image);nt_artifact_free(&artifact);free(sources[0]);free(sources[1]);free(sources[2]);
  printf("NTASM_NOVA_NXO checks=%u failures=%u cases=%u\n",checks,failures,cases);return failures?1:0;
}
