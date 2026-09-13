#include "cli.h"
#include "ntasm.h"
#include "object.h"
#include "pe.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_input(const char *path, NtFInput *result) {
  memset(result, 0, sizeof(*result));
  result->name = path;
  FILE *input = fopen(path, "rb");
  if (!input) {
    fprintf(stderr, "%s: E001: %s\n", path, strerror(errno));
    return 0;
  }
  if (fseek(input, 0, SEEK_END) != 0) {
    fclose(input);
    fputs("E002: input is not seekable\n", stderr);
    return 0;
  }
  long length = ftell(input);
  if (length < 0 || length > 16 * 1024 * 1024 ||
      fseek(input, 0, SEEK_SET) != 0) {
    fclose(input);
    fputs("E002: invalid source size\n", stderr);
    return 0;
  }
  char *source = malloc((size_t)length + 1);
  if (!source) {
    fclose(input);
    fputs("E003: allocation failed\n", stderr);
    return 0;
  }
  size_t count = fread(source, 1, (size_t)length, input);
  int extra = fgetc(input), read_error = ferror(input),
      close_error = fclose(input);
  if (count != (size_t)length || extra != EOF || read_error || close_error) {
    free(source);
    fputs("E002: source changed or read failed\n", stderr);
    return 0;
  }
  source[count] = 0;
  result->text = source;
  result->length = count;
  return 1;
}
static void release_inputs(NtFInput *inputs, size_t count) {
  for (size_t j = 0; j < count; ++j)
    free((void *)inputs[j].text);
}

static int write_output(const char *path, const NtBuffer *bytes,
                        const char *marker) {
  FILE *output = fopen(path, "wbx");
  if (!output) {
    fprintf(stderr, "%s: E700: %s\n", path, strerror(errno));
    return 3;
  }
  size_t written = fwrite(bytes->bytes, 1, bytes->size, output);
  int flush_error = fflush(output), close_error = fclose(output);
  if (written != bytes->size || flush_error || close_error) {
    fputs("E701: incomplete output, do not execute\n", stderr);
    return 3;
  }
  printf("%s bytes=%zu provenance=BOOTSTRAP_EXTERNAL_C\n", marker, bytes->size);
  return 0;
}

int nt_cli(int argc, char **argv) {
  if (argc == 2 && !strcmp(argv[1], "--version")) {
    puts("ntasm 0.0.1-c-bootstrap provenance=BOOTSTRAP_EXTERNAL_C "
         "B2.1=not-delivered");
    return 0;
  }
  int check_only = argc >= 3 && !strcmp(argv[1], "check");
  int inspect = argc == 3 && !strcmp(argv[1], "inspect");
  int object_mode = argc >= 5 && !strcmp(argv[1], "object");
  int link_mode = argc >= 5 && !strcmp(argv[1], "link");
  int assemble = argc >= 5 &&
                 (!strcmp(argv[1], "assemble") || object_mode || link_mode) &&
                 !strcmp(argv[argc - 2], "-o");
  size_t input_count = inspect      ? 1
                       : check_only ? (size_t)(argc - 2)
                       : assemble   ? (size_t)(argc - 4)
                                    : 0;
  if (!input_count || input_count > 64) {
    fputs("usage: ntasm assemble SOURCE.ntasm [MORE.ntasm ...] -o OUTPUT.efi\n "
          "      ntasm check SOURCE.ntasm [MORE.ntasm ...]\n       ntasm "
          "inspect PROGRAM.efi|UNIT.nxo\n"
          "       ntasm object SOURCE.ntasm [MORE.ntasm ...] -o UNIT.nxo\n"
          "       ntasm link UNIT.nxo [MORE.nxo ...] -o OUTPUT.efi\n",
          stderr);
    return 2;
  }
  NtFInput inputs[64] = {{0}};
  size_t total = 0;
  for (size_t j = 0; j < input_count; ++j) {
    if (!read_input(argv[2 + j], inputs + j)) {
      release_inputs(inputs, j);
      return 3;
    }
    total += inputs[j].length;
    if (total > 64 * 1024 * 1024) {
      release_inputs(inputs, j + 1);
      fputs("E002: input unit exceeds 64 MiB\n", stderr);
      return 3;
    }
  }
  if (inspect) {
    if (inputs[0].length >= 8 && !memcmp(inputs[0].text, "NXOV0001", 8)) {
      NtObject object = {0};
      NtDiagnostic error = {0};
      int ok = nt_object_read((const uint8_t *)inputs[0].text, inputs[0].length,
                              &object, &error);
      release_inputs(inputs, input_count);
      if (!ok) {
        fprintf(stderr, "%s:offset%zu: E%03u: %s\n", argv[2], error.offset,
                error.code, error.message);
        nt_object_free(&object);
        return 1;
      }
      printf("{\"format\":\"NXOV0001\",\"target\":%u,\"symbols\":%zu,"
             "\"relocations\":%zu,\"entry_symbol\":%u}\n",
             object.target, object.symbol_count, object.relocation_count,
             object.entry_symbol);
      nt_object_free(&object);
      return 0;
    }
    NtPeInfo info;
    NtDiagnostic diagnostic;
    int inspected = nt_pe_inspect((const uint8_t *)inputs[0].text,
                                  inputs[0].length, &info, &diagnostic);
    release_inputs(inputs, input_count);
    if (!inspected) {
      fprintf(stderr, "%s:offset%zu: E%03u: %s\n", argv[2], diagnostic.offset,
              diagnostic.code, diagnostic.message);
      return 1;
    }
    printf("{\"format\":\"PE32+\",\"machine\":\"x86_64\",\"subsystem\":"
           "\"UEFI\",\"entry_rva\":%u,\"image_base\":%" PRIu64
           ",\"image_size\":%u,\"sections\":%u,\"dir64_count\":%u}\n",
           info.entry_rva, info.image_base, info.image_size, info.section_count,
           info.relocation_count);
    return 0;
  }
  if (object_mode || link_mode) {
    NtObject object = {0};
    NtBuffer encoded = {0};
    NtArtifact artifact = {0};
    NtDiagnostic error = {0};
    NtObject link_inputs[64] = {{0}};
    int ok;
    if (object_mode) {
      ok = nt_object_compile(inputs, input_count, &object, &error) &&
           nt_object_write(&object, &encoded, &error);
    } else {
      ok = 1;
      for (size_t j = 0; j < input_count; ++j) {
        if (!nt_object_read((const uint8_t *)inputs[j].text, inputs[j].length,
                            link_inputs + j, &error)) {
          error.source_index = (uint32_t)(j + 1);
          ok = 0;
          break;
        }
      }
      ok = ok && nt_object_link(link_inputs, input_count, &object, &error) &&
           nt_object_materialize(&object, &artifact, &error) &&
           nt_make_pe(&artifact);
    }
    release_inputs(inputs, input_count);
    int result = 1;
    if (ok) {
      result =
          write_output(argv[argc - 1], object_mode ? &encoded : &artifact.pe,
                       object_mode ? "NTASM_C_OBJECT" : "NTASM_C_LINKED");
    } else {
      const char *name =
          error.source_index >= 1 && error.source_index <= input_count
              ? argv[1 + error.source_index]
              : "<build>";
      fprintf(stderr, "%s:%zu:%zu: E%03u: %s\n", name, error.line, error.column,
              error.code ? error.code : 600,
              error.message[0] ? error.message : "PE generation failed");
    }
    free(encoded.bytes);
    nt_artifact_free(&artifact);
    nt_object_free(&object);
    for (size_t j = 0; j < input_count; ++j)
      nt_object_free(link_inputs + j);
    return result;
  }
  if (check_only) {
    NtFProgram program = {0};
    int checked = nt_frontend_compile(inputs, input_count, &program);
    release_inputs(inputs, input_count);
    if (!checked) {
      if (program.diagnostic_count) {
        const NtFDiagnostic *d = &program.diagnostics[0];
        const char *name = d->span.source >= 1 && d->span.source <= input_count
                               ? argv[1 + d->span.source]
                               : "<build>";
        fprintf(stderr, "%s:%u:%u: E%03u: %s\n", name, d->span.line,
                d->span.column, d->code, d->message);
      } else {
        fputs("E900: frontend check failed\n", stderr);
      }
      nt_frontend_free(&program);
      return 1;
    }
    nt_frontend_free(&program);
    puts("NTASM_C_CHECK_OK phase=frontend");
    return 0;
  }
  NtArtifact a;
  int ok = nt_compile_many(inputs, input_count, &a);
  release_inputs(inputs, input_count);
  if (!ok) {
    const char *error_source =
        a.error.source_index >= 1 && a.error.source_index <= input_count
            ? argv[1 + a.error.source_index]
            : "<build>";
    fprintf(stderr, "%s:%zu:%zu: E%03u: %s\n", error_source, a.error.line,
            a.error.column, a.error.code, a.error.message);
    nt_artifact_free(&a);
    return 1;
  }
  if (!nt_make_pe(&a)) {
    nt_artifact_free(&a);
    fputs("E600: PE generation failed\n", stderr);
    return 1;
  }
  int result = write_output(argv[argc - 1], &a.pe, "NTASM_C_ASSEMBLED");
  nt_artifact_free(&a);
  return result;
}
