#include "nova.h"
#include "nova/pe_oracle.h"
#include "nova/run_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, failures;
static NovaHostImage program;
static const char *evidence;
#define CHECK(c, m)                                                            \
  do {                                                                         \
    checks++;                                                                  \
    if (!(c)) {                                                                \
      failures++;                                                              \
      fprintf(stderr, "FAIL %s\n", m);                                         \
    }                                                                          \
  } while (0)
static uint64_t read64(const unsigned char *p) {
  uint64_t n = 0;
  for (unsigned i = 0; i < 8; i++)
    n |= (uint64_t)p[i] << (i * 8);
  return n;
}
static void save(const char *label, const char *kind,
                 const unsigned char *bytes, size_t size) {
  if (!evidence)
    return;
  char name[4096];
  int n = snprintf(name, sizeof name, "%s/%s.%s.efi", evidence, label, kind);
  CHECK(n > 0 && (size_t)n < sizeof name, "artifact path");
  if (n <= 0 || (size_t)n >= sizeof name)
    return;
  FILE *f = fopen(name, "wbx");
  CHECK(f != NULL, "fresh PE evidence output");
  if (f) {
    CHECK(fwrite(bytes, 1, size, f) == size, "complete PE evidence write");
    CHECK(fclose(f) == 0, "PE evidence close");
  }
}
static char *source_file(const char *name, size_t *n) {
  FILE *f = fopen(name, "rb");
  if (!f)
    return NULL;
  if (fseek(f, 0, SEEK_END)) {
    fclose(f);
    return NULL;
  }
  long size = ftell(f);
  if (size < 0 || size > 4 * 1024 * 1024 || fseek(f, 0, SEEK_SET)) {
    fclose(f);
    return NULL;
  }
  char *s = malloc((size_t)size + 1);
  if (!s) {
    fclose(f);
    return NULL;
  }
  *n = fread(s, 1, (size_t)size, f);
  fclose(f);
  s[*n] = 0;
  return s;
}
static void testcase(const char *label, size_t text, size_t ro, size_t data,
                     size_t entry, const NovaOracleBaseReloc *relocs,
                     size_t count, unsigned input_error) {
  unsigned char *t = calloc(text ? text : 1, 1), *r = calloc(ro ? ro : 1, 1),
                *d = calloc(data ? data : 1, 1);
  uint64_t *bases = calloc(count ? count * 2 : 1, 8);
  if (!t || !r || !d || !bases)
    exit(2);
  for (size_t i = 0; i < text; i++)
    t[i] = (unsigned char)(i * 17 + 3);
  for (size_t i = 0; i < ro; i++)
    r[i] = (unsigned char)(i * 7 + 5);
  for (size_t i = 0; i < data; i++)
    d[i] = (unsigned char)(i * 11 + 9);
  if (entry < text && text - entry >= 11) {
    t[entry] = 0x48;
    t[entry + 1] = 0xb8;
    for (unsigned i = 0; i < 8; i++)
      t[entry + 2 + i] = (unsigned char)(UINT64_C(42) >> (8 * i));
    t[entry + 10] = 0xc3;
  }
  for (size_t i = 0; i < count; i++) {
    bases[i * 2] = relocs[i].section;
    bases[i * 2 + 1] = relocs[i].offset;
  }
  NovaOraclePeImage oracle = {.text = {t, text, text},
                              .entry = entry,
                              .rdata = {r, ro, ro},
                              .data = {d, data, data},
                              .base_relocations = (NovaOracleBaseReloc *)relocs,
                              .base_relocation_count = count};
  oracle.error.code = input_error;
  int wanted = nova_oracle_make_pe(&oracle);
  NovaContextV2 c;
  if (!nova_context_init_v2(&c))
    exit(2);
  uint64_t args[] = {nova_borrow_v2(&c, t, text, 0),
                     text,
                     nova_borrow_v2(&c, r, ro, 0),
                     ro,
                     nova_borrow_v2(&c, d, data, 0),
                     data,
                     nova_borrow_v2(&c, bases, count * 16, 0),
                     count * 2,
                     entry,
                     input_error};
  CHECK(!c.error, "register PE inputs");
  uint64_t result = program.entry(&c, args, 10);
  CHECK(!c.error, "Nova PE producer has no runtime failure");
  uint64_t code = 999, out = 0, n = 0;
  unsigned char *bytes = NULL;
  if (!c.error) {
    unsigned char *p =
        (unsigned char *)(uintptr_t)c.runtime->calls[NV2_RESOLVE](&c, result, 0,
                                                                  16);
    if (p) {
      code = read64(p);
      out = read64(p + 8);
      n = c.runtime->calls[NV2_LENGTH](&c, out, 0, 0);
      bytes = (unsigned char *)(uintptr_t)c.runtime->calls[NV2_RESOLVE](&c, out,
                                                                        0, n);
    }
  }
  int pass = (!c.error) && ((code == 0) == wanted) &&
             (wanted ? (n == oracle.pe.size && bytes &&
                        !memcmp(bytes, oracle.pe.bytes, (size_t)n))
                     : n == 0);
  CHECK(pass, label);
  printf("NOVA_PE_CASE %s oracle=%d code=%llu bytes=%llu pass=%d\n", label,
         wanted, (unsigned long long)code, (unsigned long long)n, pass);
  if (wanted && pass) {
    save(label, "nova", bytes, (size_t)n);
    save(label, "c", oracle.pe.bytes, oracle.pe.size);
  }
  nova_context_destroy_v2(&c);
  free(oracle.pe.bytes);
  free(t);
  free(r);
  free(d);
  free(bases);
}
int main(int argc, char **argv) {
  if (argc < 3 || argc > 5)
    return 2;
  int large = argc > 3 && !strcmp(argv[3], "--large");
  if (argc > 3 && !large)
    evidence = argv[3];
  if (argc > 4)
    evidence = argv[4];
  size_t a, b;
  char *entry = source_file(argv[1], &a), *library = source_file(argv[2], &b);
  if (!entry || !library) {
    free(entry);
    free(library);
    return 2;
  }
  NtFInput inputs[] = {{argv[1], entry, a}, {argv[2], library, b}};
  NtArtifact artifact;
  int ok = nova_compile_many_v2(inputs, 2, &artifact);
  CHECK(ok, "PE module compiles");
  if (!ok) {
    fprintf(stderr, "E%u %zu:%zu %s\n", artifact.error.code,
            artifact.error.line, artifact.error.column, artifact.error.message);
    nt_artifact_free(&artifact);
    free(entry);
    free(library);
    return 1;
  }
  CHECK(nova_host_map(&artifact, &program), "map Nova PE producer");
  testcase("return42", 11, 0, 0, 0, NULL, 0, 0);
  testcase("entry-offset", 1024, 17, 23, 113, NULL, 0, 0);
  testcase("cross-page-sections", 8193, 8197, 12001, 4113, NULL, 0, 0);
  NovaOracleBaseReloc many[] = {{3, 8192}, {1, 4092}, {2, 4088}, {3, 0},
                                {1, 64},   {2, 8},    {3, 4096}};
  testcase("unsorted-multipage-dir64", 8200, 8200, 16384, 17, many, 7, 0);
  NovaOracleBaseReloc duplicate[] = {{2, 8}, {2, 8}},
                      overlap[] = {{3, 4}, {3, 11}}, outside[] = {{3, 57}},
                      invalid[] = {{4, 0}};
  testcase("duplicate", 32, 64, 64, 0, duplicate, 2, 0);
  testcase("overlap", 32, 64, 64, 0, overlap, 2, 0);
  testcase("outside", 32, 64, 64, 0, outside, 1, 0);
  testcase("section-id", 32, 64, 64, 0, invalid, 1, 0);
  testcase("empty-text", 0, 0, 0, 0, NULL, 0, 0);
  testcase("entry-at-end", 11, 0, 0, 11, NULL, 0, 0);
  testcase("prior-error", 11, 0, 0, 0, NULL, 0, 400);
  NovaOracleBaseReloc anchor_overlap[] = {{2, 0}}, near_end[] = {{3, 56}},
                      crossing[] = {{1, 4093}};
  testcase("anchor-adjacent", 32, 64, 64, 0, anchor_overlap, 1, 0);
  testcase("last-valid-qword", 32, 64, 64, 0, near_end, 1, 0);
  testcase("qword-crosses-page", 8200, 0, 0, 17, crossing, 1, 0);
  testcase("text-cap-plus-one", 16777217, 0, 0, 0, NULL, 0, 0);
  testcase("rdata-cap-plus-one", 11, 16777217, 0, 0, NULL, 0, 0);
  testcase("data-cap-plus-one", 11, 0, 16777217, 0, NULL, 0, 0);
  if (large) {
    testcase("three-max-sections", 16777216, 16777216, 16777216, 113, NULL, 0,
             0);
    size_t maximum = 1048576;
    NovaOracleBaseReloc *many = calloc(maximum + 1, sizeof *many);
    if (!many)
      return 2;
    for (size_t i = 0; i < maximum; i++) {
      many[i].section = 3;
      many[i].offset = (maximum - i - 1) * 8;
    }
    testcase("one-million-dir64", 11, 0, maximum * 8, 0, many, maximum, 0);
    testcase("dir64-cap-plus-one", 11, 0, maximum * 8, 0, many, maximum + 1, 0);
    free(many);
  }
  nova_host_unmap(&program);
  nt_artifact_free(&artifact);
  free(entry);
  free(library);
  printf("NOVA_PE_TESTS checks=%u failures=%u\n", checks, failures);
  return failures ? 1 : 0;
}
