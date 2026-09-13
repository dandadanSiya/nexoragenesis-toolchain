#include "nova.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, failures;
static void reject(const char *s, size_t n, unsigned expected) {
  NtArtifact a;
  int ok = nova_compile(s, n, &a);
  checks++;
  if (ok || !a.error.code || (expected && a.error.code != expected) ||
      a.text.bytes || a.pe.bytes) {
    failures++;
    fprintf(stderr, "FAIL reject code=%u expected=%u\n", a.error.code,
            expected);
  }
  nt_artifact_free(&a);
}
int main(void) {
  const char *source = "fn main(b:buf<u8>)->u64{let mut i:u64=0;while "
                       "i<len(b){if b[i]<48{return 2;}i=i+1;}return 42;}";
  size_t n = strlen(source);
  for (size_t i = 0; i < n; i++)
    reject(source, i, 0);
  char mutated[256];
  for (unsigned value = 0; value < 256; value++)
    if (value < 32 || value > 126) {
      memcpy(mutated, source, n);
      mutated[3] = (char)value;
      reject(mutated, n, 0);
    }
  char deep[1024];
  size_t at = 0;
  const char *head = "fn main()->u64{return ";
  memcpy(deep, head, strlen(head));
  at = strlen(head);
  for (unsigned i = 0; i < 200; i++)
    deep[at++] = '(';
  deep[at++] = '1';
  for (unsigned i = 0; i < 200; i++)
    deep[at++] = ')';
  deep[at++] = ';';
  deep[at++] = '}';
  reject(deep, at, 201);
  char *many = malloc(500000);
  at = 0;
  memcpy(many, "fn main()->u64{", 15);
  at = 15;
  for (unsigned i = 0; i < 5000; i++)
    at += (size_t)sprintf(many + at, "let v%u:u64=0;", i);
  memcpy(many + at, "return 0;}", 10);
  at += 10;
  reject(many, at, 201);
  free(many);
  printf("NOVA_LIMIT_TESTS checks=%u failures=%u\n", checks, failures);
  return failures ? 1 : 0;
}
