#include "nova.h"
#include "nova/run_host.h"
#include <stdio.h>
#include <string.h>
static unsigned checks, failures;
#define CHECK(c, m)                                                            \
  do {                                                                         \
    checks++;                                                                  \
    if (!(c)) {                                                                \
      failures++;                                                              \
      fprintf(stderr, "FAIL %s\n", m);                                         \
    }                                                                          \
  } while (0)
static void run(const char *s, int pass, uint64_t result, uint64_t error) {
  NtArtifact a;
  int ok = nova_compile_v2(s, strlen(s), &a);
  CHECK(ok == pass, "aggregate compile expectation");
  if (!ok) {
    if (pass)
      fprintf(stderr, "E%u %zu:%zu %s\n", a.error.code, a.error.line,
              a.error.column, a.error.message);
    CHECK(a.error.code && !a.text.bytes, "aggregate failure transactional");
  } else {
    NovaContextV2 c;
    NovaHostImage image;
    CHECK(nova_context_init_v2(&c) && nova_host_map(&a, &image),
          "aggregate execution setup");
    uint64_t actual = image.entry(&c, NULL, 0);
    CHECK(actual == result && c.error == error,
          "aggregate actual compiled result");
    nova_host_unmap(&image);
    nova_context_destroy_v2(&c);
  }
  nt_artifact_free(&a);
}
int main(void) {
  run("fn main()->u64{let left:u64=42;let right:u64=41;if left>right{return "
      "left;}return 0;}",
      1, 42, 0);
  run("struct Flag{ok:bool}fn main()->u64{let f:Flag=Flag{ok:true};if "
      "f.ok{return 42;}return 0;}",
      1, 42, 0);
  run("struct N{value:u64,next:N}fn main()->u64{let "
      "tail:N=N{value:2,next:null};let mut n:N=N{value:40,next:tail};let mut "
      "sum:u64=0;while n!=null{sum=sum+n.value;n=n.next;}return sum;}",
      1, 42, 0);
  run("fn main()->u64{let mut i:u64=0;let mut total:u64=0;while i<10{i=i+1;if "
      "i<5{continue;}total=total+i;if total>20{break;}}return total;}",
      1, 26, 0);
  run("fn main()->u64{let "
      "a:buf<u64>=alloc(runtime(),3);a[0]=18446744073709551615;a[1]=40;a[2]=2;"
      "return a[1]+a[2];}",
      1, 42, 0);
  run("struct Pair{left:u64,right:u64}fn main()->u64{let "
      "p:Pair=Pair{left:40,right:2};p.left=p.left+1;return p.left+p.right;}",
      1, 43, 0);
  run("struct Node{value:u64,next:Node}fn node()->Node{return "
      "Node{value:42,next:null};}fn main()->u64{let n:Node=node();return "
      "n.value;}",
      1, 42, 0);
  run("struct Token{kind:u64,text:buf<u8>}fn main()->u64{let "
      "t:Token=Token{kind:3,text:\"42\"};let b:buf<u8>=t.text;return "
      "(b[0]-48)*10+b[1]-48;}",
      1, 42, 0);
  run("struct Item{value:u64}fn main()->u64{let "
      "a:buf<Item>=alloc(runtime(),2);a[0]=Item{value:40};a[1]=Item{value:2};"
      "return a[0].value+a[1].value;}",
      1, 42, 0);
  run("struct Item{value:u64}fn main()->u64{let x:Item=null;return x.value;}",
      1, 0, NV2_LIFETIME);
  run("struct Pair{left:u64,left:u64}fn main()->u64{return 0;}", 0, 0, 0);
  run("struct Pair{left:u64,right:u64}fn main()->u64{let "
      "p:Pair=Pair{left:1};return 0;}",
      0, 0, 0);
  run("struct Pair{left:u64}fn main()->u64{let p:Pair=Pair{left:true};return "
      "0;}",
      0, 0, 0);
  printf("NOVA_AGGREGATE_TESTS checks=%u failures=%u\n", checks, failures);
  return failures ? 1 : 0;
}
