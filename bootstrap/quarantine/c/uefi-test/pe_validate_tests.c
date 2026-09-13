/* BOOTSTRAP_C_TEST fixtures; never a language producer. */
#include "pe_validate.h"
#include <stdio.h>
#include <string.h>
static uint8_t p[8192];
static void w16(size_t n, uint16_t v) { p[n]=(uint8_t)v; p[n+1]=(uint8_t)(v>>8); }
static void w32(size_t n,uint32_t v){ w16(n,(uint16_t)v); w16(n+2,(uint16_t)(v>>16)); }
static void fixture(void) {
 memset(p,0,sizeof p); w16(0,0x5a4d);w32(60,128);w32(128,0x4550);
 w16(132,0x8664);w16(134,1);w16(148,240);w16(152,0x20b);
 w32(168,4096);w32(208,8192);w32(212,512);w16(220,10);
 w32(400,16);w32(404,4096);w32(408,512);w32(412,512);w32(428,0x60000020);
}
static int checks, failures;
static void check(const char *name,int want,size_t len,int mapped) {
 uint32_t rva=0xdead; int result=ng_pe_validate(p,len,mapped,&rva); checks++;
 if(result!=want || (result==0 && rva!=4096) || (result!=0 && rva!=0)) {
  printf("FAIL %s: result=%d want=%d rva=%x\n",name,result,want,rva); failures++;
 }
}
int main(void) {
 fixture();check("valid-file",0,1024,0);check("valid-loaded",0,8192,1);
 check("short-dos",1,16,0);fixture();w32(60,0xffffffff);check("huge-nt-offset",4,1024,0);
 fixture();w16(0,0);check("bad-dos",2,1024,0);fixture();w32(128,0);check("bad-pe",2,1024,0);
 fixture();w16(132,0x14c);check("wrong-machine",3,1024,0);
 fixture();w16(152,0x10b);check("pe32-not-plus",3,1024,0);
 fixture();w16(134,0);check("zero-sections",4,1024,0);
 fixture();w16(134,97);check("too-many-sections",4,1024,0);
 fixture();w16(148,0);check("short-optional",4,1024,0);
 fixture();w32(212,400);check("sections-outside-headers",4,1024,0);
 fixture();w32(208,0x80000000);check("huge-image",5,1024,0);
 fixture();check("truncated-raw",5,900,0);fixture();check("truncated-loaded",5,7000,1);
 fixture();w32(404,8190);check("virtual-overrun",5,1024,0);
 fixture();w32(412,0xfffffff0);check("raw-overrun",5,1024,0);
 fixture();w32(168,2048);check("entry-outside-code",6,1024,0);
 fixture();w32(428,0x40000040);check("entry-non-executable",6,1024,0);
 fixture();w32(428,0xe0000020);check("entry-writable",6,1024,0);
 fixture();w16(220,3);check("wrong-subsystem",3,1024,0);
 printf("NG_PE_VALIDATOR_TESTS checks=%d failures=%d\n",checks,failures);
 return failures?1:0;
}
