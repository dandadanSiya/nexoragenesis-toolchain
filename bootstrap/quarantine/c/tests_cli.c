#include "cli.h"
#include "object.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, failures;
#define CHECK(c, m)                                                            \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(c)) {                                                                \
      ++failures;                                                              \
      fprintf(stderr, "FAIL: %s\n", m);                                        \
    }                                                                          \
  } while (0)
int main(int argc, char **argv) {
  if (argc != 2) {
    fputs("tests_cli expects an existing fresh evidence directory\n", stderr);
    return 2;
  }
  char input[1024], output[1024];
  snprintf(input, sizeof(input), "%s/hello.ntasm", argv[1]);
  snprintf(output, sizeof(output), "%s/hello.efi", argv[1]);
  FILE *f = fopen(input, "wbx");
  if (!f) {
    perror(input);
    return 2;
  }
  const char *source =
      "module hello\ntarget x86_64-nexora-uefi\nsection .text {\nexport fn "
      "main() -> u64\neffects {}\nclobbers {rax} {\nreturn 42;\n}\n}\n";
  fwrite(source, 1, strlen(source), f);
  fclose(f);
  char *args[] = {"ntasm", "assemble", input, "-o", output};
  CHECK(nt_cli(5, args) == 0, "assemble command succeeds");
  f = fopen(output, "rb");
  CHECK(f != NULL, "CLI creates executable output");
  if (f) {
    unsigned char bytes[3073];
    size_t size = fread(bytes, 1, sizeof(bytes), f);
    fclose(f);
    CHECK(size == 3072 && bytes[0] == 'M' && bytes[1] == 'Z' &&
              bytes[1026] == 42,
          "CLI emits actual NTASM PE");
  }
  char *check_args[] = {"ntasm", "check", input};
  CHECK(nt_cli(3, check_args) == 0,
        "check command verifies source without producing an artifact");
  CHECK(nt_cli(5, args) == 3,
        "existing executable is refused without overwrite");
  f = fopen(output, "rb");
  if (f) {
    unsigned char bytes[3073];
    size_t size = fread(bytes, 1, sizeof(bytes), f);
    fclose(f);
    CHECK(size == 3072 && bytes[1026] == 42,
          "existing executable is preserved");
  }
  char *inspect_args[] = {"ntasm", "inspect", output};
  CHECK(nt_cli(3, inspect_args) == 0,
        "inspect command validates the emitted binary");
  char invalid[1024], invalid_output[1024];
  snprintf(invalid, sizeof(invalid), "%s/invalid.ntasm", argv[1]);
  snprintf(invalid_output, sizeof(invalid_output), "%s/invalid.efi", argv[1]);
  f = fopen(invalid, "wbx");
  if (!f)
    return 2;
  fputs("module broken\ntarget x86_64-nexora-uefi\nsection .text { fn", f);
  fclose(f);
  char *bad_args[] = {"ntasm", "assemble", invalid, "-o", invalid_output};
  CHECK(nt_cli(5, bad_args) == 1, "invalid source rejected by compiler");
  f = fopen(invalid_output, "rb");
  CHECK(!f, "invalid source creates no binary");
  if (f)
    fclose(f);
  char *bad_inspect[] = {"ntasm", "inspect", invalid};
  CHECK(nt_cli(3, bad_inspect) == 1,
        "inspect rejects source text presented as PE");
  char *version_args[] = {"ntasm", "--version"};
  CHECK(nt_cli(2, version_args) == 0, "version exposes bootstrap provenance");
  char app[1024], math[1024], linked[1024];
  snprintf(app, sizeof(app), "%s/app.ntasm", argv[1]);
  snprintf(math, sizeof(math), "%s/math.ntasm", argv[1]);
  snprintf(linked, sizeof(linked), "%s/linked.efi", argv[1]);
  f = fopen(app, "wbx");
  if (!f)
    return 2;
  fputs("module app\ntarget x86_64-nexora-uefi\nimport math.add as add: fn(in "
        "a:u64 @rcx,in b:u64 @rdx)->u64\neffects {}\nclobbers {}\nsection "
        ".text {\nexport fn main()->u64\neffects {}\nclobbers {} {\nreturn "
        "add(20,22)\n}\n}\n",
        f);
  fclose(f);
  f = fopen(math, "wbx");
  if (!f)
    return 2;
  fputs("module math\ntarget x86_64-nexora-uefi\nsection .text {\nexport fn "
        "add(in a:u64 @rcx,in b:u64 @rdx)->u64\neffects {}\nclobbers {} "
        "{\nreturn a+b\n}\n}\n",
        f);
  fclose(f);
  char *many_args[] = {"ntasm", "assemble", app, math, "-o", linked};
  CHECK(nt_cli(6, many_args) == 0,
        "assemble resolves imports across actual input files");
  char *many_check[] = {"ntasm", "check", app, math};
  CHECK(nt_cli(4, many_check) == 0, "check resolves all input modules");
  char *library_check[] = {"ntasm", "check", math};
  CHECK(nt_cli(3, library_check) == 0,
        "check accepts a valid library without executable main");
  char object_path[1024], linked_object[1024];
  snprintf(object_path, sizeof(object_path), "%s/hello.nxo", argv[1]);
  snprintf(linked_object, sizeof(linked_object), "%s/from-object.efi", argv[1]);
  char *object_args[] = {"ntasm", "object", input, "-o", object_path};
  CHECK(nt_cli(5, object_args) == 0, "CLI writes a typed NXO object");
  char *link_args[] = {"ntasm", "link", object_path, "-o", linked_object};
  CHECK(nt_cli(5, link_args) == 0,
        "CLI materializes object relocations and writes PE");
  char *object_inspect[] = {"ntasm", "inspect", object_path};
  CHECK(nt_cli(3, object_inspect) == 0, "inspect validates NXO records");
  CHECK(nt_cli(5, object_args) == 3, "object refuses existing output");
  CHECK(nt_cli(5, link_args) == 3, "link refuses existing output");
  char bad_link_output[1024];
  snprintf(bad_link_output, sizeof(bad_link_output), "%s/bad-link.efi",
           argv[1]);
  char *bad_link[] = {"ntasm", "link", invalid, "-o", bad_link_output};
  CHECK(nt_cli(5, bad_link) == 1, "link rejects invalid object bytes");
  f = fopen(bad_link_output, "rb");
  CHECK(!f, "rejected object creates no executable");
  if (f)
    fclose(f);
  FILE *left = fopen(output, "rb"), *right = fopen(linked_object, "rb");
  CHECK(left && right, "both direct and object-linked PE exist");
  if (left && right) {
    unsigned char a[4096], b[4096];
    size_t na = fread(a, 1, sizeof(a), left),
           nb = fread(b, 1, sizeof(b), right);
    CHECK(na == nb && !memcmp(a, b, na),
          "CLI object-link PE matches direct PE exactly");
  }
  if (left)
    fclose(left);
  if (right)
    fclose(right);
  char library_path[1024], combined_path[1024];
  snprintf(library_path, sizeof(library_path), "%s/library.nxo", argv[1]);
  snprintf(combined_path, sizeof(combined_path), "%s/combined.efi", argv[1]);
  uint8_t library_code[] = {0xb8, 42, 0, 0, 0, 0xc3};
  NtObjectSymbol library_symbol = {"lib.answer",        "fn()->u64", 1, 3, 0,
                                   sizeof(library_code)};
  NtObject library = {0};
  NtBuffer library_bytes = {0};
  NtDiagnostic library_error = {0};
  library.target = 1;
  library.symbols = &library_symbol;
  library.symbol_count = 1;
  library.sections[0] =
      (NtBuffer){library_code, sizeof(library_code), sizeof(library_code)};
  CHECK(nt_object_write(&library, &library_bytes, &library_error),
        "library fixture serializes");
  f = fopen(library_path, "wbx");
  if (!f) {
    free(library_bytes.bytes);
    return 2;
  }
  CHECK(fwrite(library_bytes.bytes, 1, library_bytes.size, f) ==
            library_bytes.size,
        "library object fixture saved");
  fclose(f);
  free(library_bytes.bytes);
  char *combined_args[] = {"ntasm",      "link", object_path,
                           library_path, "-o",   combined_path};
  CHECK(nt_cli(6, combined_args) == 0,
        "CLI links two independently encoded objects");
  char *combined_inspect[] = {"ntasm", "inspect", combined_path};
  CHECK(nt_cli(3, combined_inspect) == 0, "combined PE passes inspector");
  char app_nxo[1024], math_nxo[1024], separate_pe[1024];
  snprintf(app_nxo, sizeof(app_nxo), "%s/app-source.nxo", argv[1]);
  snprintf(math_nxo, sizeof(math_nxo), "%s/math-source.nxo", argv[1]);
  snprintf(separate_pe, sizeof(separate_pe), "%s/separate-source.efi", argv[1]);
  char *app_object[] = {"ntasm", "object", app, "-o", app_nxo};
  char *math_object[] = {"ntasm", "object", math, "-o", math_nxo};
  CHECK(nt_cli(5, app_object) == 0,
        "CLI compiles source with unresolved import to NXO");
  CHECK(nt_cli(5, math_object) == 0,
        "CLI compiles source library without main to NXO");
  char *inspect_app[] = {"ntasm", "inspect", app_nxo};
  CHECK(nt_cli(3, inspect_app) == 0, "CLI inspects external import metadata");
  char *separate_link[] = {"ntasm",  "link", app_nxo,
                           math_nxo, "-o",   separate_pe};
  CHECK(nt_cli(6, separate_link) == 0,
        "CLI links separately compiled source modules");
  char *inspect_separate[] = {"ntasm", "inspect", separate_pe};
  CHECK(nt_cli(3, inspect_separate) == 0,
        "separately compiled PE passes inspector");
  f = fopen(linked, "rb");
  CHECK(f != NULL, "multi-file executable exists");
  if (f)
    fclose(f);
  printf("NTASM_C_CLI_TESTS checks=%u failures=%u\n", checks, failures);
  return failures ? 1 : 0;
}
