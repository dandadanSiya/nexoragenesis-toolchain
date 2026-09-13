#define _POSIX_C_SOURCE 200809L
#include "nova/run_host.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
typedef uint64_t (*Kernel)(uint64_t,uint64_t,const uint8_t *,uint64_t);
#define DECL(n) uint64_t n##_o0(uint64_t,uint64_t,const uint8_t *,uint64_t); uint64_t n##_o2(uint64_t,uint64_t,const uint8_t *,uint64_t)
DECL(xorshift);DECL(buffer_hash);DECL(decimal);
typedef struct {
  const char *name; Kernel c[2]; uint64_t work;
  NtArtifact artifact; NovaHostImage image; NovaContextV2 context; uint64_t args[4];
} Case;
static volatile uint64_t consumed;
static uint8_t data[8192];
static unsigned checks;
static double now(void) {
#ifdef _WIN32
  LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return (double)t.QuadPart/(double)f.QuadPart;
#else
  struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))abort();return (double)t.tv_sec+t.tv_nsec*1e-9;
#endif
}
static int compile(Case *c,const char *directory) {
  char path[2048];snprintf(path,sizeof(path),"%s/%s.nova",directory,c->name);
  FILE *f=fopen(path,"rb");if(!f)return 0;
  if(fseek(f,0,SEEK_END)){fclose(f);return 0;}long n=ftell(f);
  if(n<0 || n>65536 || fseek(f,0,SEEK_SET)){fclose(f);return 0;}
  char *source=malloc((size_t)n+1);if(!source){fclose(f);return 0;}
  size_t got=fread(source,1,(size_t)n,f);fclose(f);source[got]=0;
  int ok=got==(size_t)n && nova_compile_v2(source,got,&c->artifact);free(source);
  if(!ok){fprintf(stderr,"compile %s E%u %s\n",c->name,c->artifact.error.code,c->artifact.error.message);return 0;}
  if(!nova_host_map(&c->artifact,&c->image) || !nova_context_init_v2(&c->context))return 0;
  c->args[2]=nova_borrow_v2(&c->context,data,sizeof(data),0);c->args[3]=sizeof(data);
  printf("COMPILED %s nova_text_bytes=%zu\n",c->name,c->artifact.text.size);
  return c->args[2]!=0;
}
static uint64_t invoke(Case *c,unsigned variant,uint64_t n,uint64_t seed) {
  if(variant<2)return c->c[variant](n,seed,data,c->args[3]);
  c->args[0]=n;c->args[1]=seed;
  return c->image.entry(&c->context,c->args,4);
}
static int same(Case *c,uint64_t n,uint64_t seed) {
  uint64_t a=invoke(c,0,n,seed),b=invoke(c,1,n,seed),v=invoke(c,2,n,seed);checks++;
  if(a!=b || a!=v || c->context.error || (!n && a!=seed)) {
    fprintf(stderr,"MISMATCH %s n=%"PRIu64" seed=%"PRIu64" O0=%"PRIu64" O2=%"PRIu64" nova=%"PRIu64" error=%"PRIu64"\n",c->name,n,seed,a,b,v,c->context.error);return 0;
  }return 1;
}
static int expected(Case *c,uint64_t value) {
  for(unsigned v=0;v<3;v++){checks++;if(invoke(c,v,1,0)!=value || c->context.error)return 0;}return 1;
}
static double batch(Case *c,unsigned variant,size_t iterations,uint64_t expected_value) {
  uint64_t last=0,aggregate=0;double start=now();
  for(size_t i=0;i<iterations;i++){last=invoke(c,variant,c->work,UINT64_C(88172645463325252));aggregate^=last+(uint64_t)i;}
  double elapsed=now()-start;consumed^=aggregate;
  if(last!=expected_value || c->context.error){fputs("timed result mismatch\n",stderr);exit(1);}return elapsed;
}
int main(int argc,char **argv) {
  if(argc!=3){fputs("compare SOURCE_DIRECTORY verify|NEW.csv\n",stderr);return 2;}
  for(size_t i=0;i<sizeof(data);i++)data[i]=(uint8_t)(i*17+3);
  for(uint64_t i=0;i<256;i++){
    uint64_t value=i==0?42:i==1?43:(UINT64_C(6364136223846793005)*i+1442695040888963407u);
    char text[21];snprintf(text,sizeof(text),"%020"PRIu64,value);memcpy(data+i*20,text,20);
  }
  Case cases[3]={0};
  cases[0].name="xorshift";cases[0].c[0]=xorshift_o0;cases[0].c[1]=xorshift_o2;cases[0].work=100000;
  cases[1].name="buffer_hash";cases[1].c[0]=buffer_hash_o0;cases[1].c[1]=buffer_hash_o2;cases[1].work=4;
  cases[2].name="decimal";cases[2].c[0]=decimal_o0;cases[2].c[1]=decimal_o2;cases[2].work=4096;
  int ok=1;for(unsigned j=0;j<3;j++)if(!compile(cases+j,argv[1])){ok=0;goto done;}
  const uint64_t ns[]={0,1,2,17},seeds[]={0,42,UINT64_MAX};
  for(unsigned j=0;j<3;j++)for(unsigned n=0;n<4;n++)for(unsigned s=0;s<3;s++)if(!same(cases+j,ns[n],seeds[s])){ok=0;goto done;}
  if(!expected(cases+2,42)){ok=0;goto done;}
  uint8_t first[20];memcpy(first,data,20);data[19]='x';
  if(!expected(cases+2,UINT64_MAX)){ok=0;goto done;}
  memset(data,'9',20);if(!expected(cases+2,UINT64_MAX-1)){ok=0;goto done;}memcpy(data,first,20);
  printf("CORRECTNESS checks=%u failures=0\n",checks);
  if(!strcmp(argv[2],"verify"))goto done;
  FILE *csv=fopen(argv[2],"wx");if(!csv){perror(argv[2]);ok=0;goto done;}
  fputs("benchmark,implementation,sample,iterations,work,buffer_bytes,ns_per_call,result\n",csv);
  const char *variants[]={"C_O0","C_O2","Nova_ABI2"};
  for(unsigned j=0;j<3;j++){
    Case *c=cases+j;size_t repetitions[3];uint64_t value=invoke(c,0,c->work,UINT64_C(88172645463325252));
    for(unsigned v=0;v<3;v++){
      batch(c,v,1,value); /* Untimed outcome consumed; warms instruction/data caches. */
      double t=batch(c,v,1,value);
      double count=t>0?0.075/t:1000000;
      repetitions[v]=count<1?1:count>1000000?1000000:(size_t)count;
    }
    for(unsigned sample=0;sample<7;sample++)for(unsigned order=0;order<3;order++){
      unsigned v=(order+sample)%3;double t=batch(c,v,repetitions[v],value);
      fprintf(csv,"%s,%s,%u,%zu,%"PRIu64",%zu,%.3f,%"PRIu64"\n",c->name,variants[v],sample,repetitions[v],c->work,sizeof(data),t*1e9/repetitions[v],value);
    }
    printf("MEASURED %s\n",c->name);
  }
  int flush_error=fflush(csv),close_error=fclose(csv);if(flush_error || close_error)ok=0;
done:
  for(unsigned j=0;j<3;j++){nova_host_unmap(&cases[j].image);if(cases[j].context.runtime && !nova_context_destroy_v2(&cases[j].context))ok=0;nt_artifact_free(&cases[j].artifact);}
  return ok?0:1;
}
