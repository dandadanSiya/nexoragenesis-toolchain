#include "../nova.h"
#include "run_host.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
static void quote(FILE *f, const char *s) {
  fputc('"', f);
  for (; *s; s++) {
    unsigned c = (unsigned char)*s;
    if (c == '"' || c == '\\') {
      fputc('\\', f);
      fputc((int)c, f);
    } else if (c < 32)
      fprintf(f, "\\u%04x", c);
    else
      fputc((int)c, f);
  }
  fputc('"', f);
}
static int report(unsigned code, const char *message) {
  fprintf(stderr, "{\"ok\":false,\"code\":%u,\"message\":", code);
  quote(stderr, message);
  fputs("}\n", stderr);
  return 1;
}
static void diagnostic(const NtDiagnostic *d, const NtFInput *inputs,
                       size_t count) {
  fprintf(stderr,
          "{\"ok\":false,\"code\":%u,\"source\":%u,\"line\":%zu,\"column\":%zu,"
          "\"path\":",
          d->code, d->source_index, d->line, d->column);
  quote(stderr, d->source_index && d->source_index <= count
                    ? inputs[d->source_index - 1].name
                    : "<global>");
  fputs(",\"message\":", stderr);
  quote(stderr, d->message);
  fputs("}\n", stderr);
}
static char *read_source(const char *path, size_t *size) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  if (fseek(f, 0, SEEK_END)) {
    fclose(f);
    return NULL;
  }
  long n = ftell(f);
  if (n < 0 || n > 4 * 1024 * 1024 || fseek(f, 0, SEEK_SET)) {
    fclose(f);
    return NULL;
  }
  char *s = malloc((size_t)n + 1);
  if (!s) {
    fclose(f);
    return NULL;
  }
  size_t got = fread(s, 1, (size_t)n, f);
  int bad = ferror(f);
  if (fclose(f) || got != (size_t)n || bad) {
    free(s);
    return NULL;
  }
  s[n] = 0;
  *size = (size_t)n;
  return s;
}
/* Complete bytes are written into a fresh sibling file, then published without
 * replacing an existing destination. Only this process's own temporary is
 * removed. Output PE bytes are produced by Nova and the shared writer. */
static int publish(const char *output, const unsigned char *bytes,
                   size_t length) {
  size_t n = strlen(output);
  if (n > 32000)
    return 0;
  char *temporary = malloc(n + 80);
  if (!temporary)
    return 0;
#ifdef _WIN32
  unsigned long pid = GetCurrentProcessId();
#else
  unsigned long pid = (unsigned long)getpid();
#endif
  FILE *f = NULL;
  for (unsigned attempt = 0; attempt < 100; attempt++) {
    snprintf(temporary, n + 80, "%s.nova-tmp-%lu-%u", output, pid, attempt);
    f = fopen(temporary, "wbx");
    if (f || errno != EEXIST)
      break;
  }
  if (!f) {
    free(temporary);
    return 0;
  }
  int good = fwrite(bytes, 1, length, f) == length;
  if (fflush(f))
    good = 0;
  if (fclose(f))
    good = 0;
  if (good) {
#ifdef _WIN32
    good = MoveFileA(temporary, output) != 0;
#else
    good = link(temporary, output) == 0;
#endif
  }
  remove(temporary);
  free(temporary);
  return good;
}
static int u64(const char *s, uint64_t *out) {
  if (!*s || *s == '-' || *s == '+')
    return 0;
  errno = 0;
  char *end;
  unsigned long long n =
      strtoull(s, &end, s[0] == '0' && (s[1] == 'x' || s[1] == 'X') ? 16 : 10);
  if (errno || *end)
    return 0;
  *out = (uint64_t)n;
  return 1;
}
int main(int argc, char **argv) {
  if (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "help"))) {
    puts("Nova Bootstrap ABI2 (BOOTSTRAP_EXTERNAL_C)\nnova check "
         "SOURCE...\nnova build SOURCE... -o NEW.efi\nnova run SOURCE... [-- "
         "--u64 N | --text TEXT | --buffer BYTES ...]\nImport modules must be "
         "supplied as additional SOURCE files. Run returns JSON; buffer "
         "arguments use checked handles.");
    return 0;
  }
  if (argc < 3)
    return report(2, "usage: nova check|build|run SOURCE...; use --help");
  int check = !strcmp(argv[1], "check"), build = !strcmp(argv[1], "build"),
      run = !strcmp(argv[1], "run");
  if (!check && !build && !run)
    return report(2, "unknown Nova command");
  NtFInput inputs[64];
  char *owned[64] = {0};
  size_t count = 0;
  const char *output = NULL;
  int runtime_start = argc;
  int status = 1;
  NtArtifact artifact = {0};
  for (int i = 2; i < argc; i++) {
    if (!strcmp(argv[i], "--")) {
      runtime_start = i + 1;
      break;
    }
    if (!strcmp(argv[i], "-o")) {
      if (!build || output || i + 1 >= argc) {
        report(2, "-o requires one build output path");
        goto finish;
      }
      output = argv[++i];
      continue;
    }
    if (count == 64) {
      report(201, "at most 64 source files");
      goto finish;
    }
    size_t length;
    char *source = read_source(argv[i], &length);
    if (!source) {
      report(2, "cannot read source, or source exceeds 4 MiB");
      goto finish;
    }
    owned[count] = source;
    inputs[count] = (NtFInput){argv[i], source, length};
    count++;
  }
  if (!count || (!run && runtime_start != argc) || (build && !output)) {
    report(2, "invalid command arguments");
    goto finish;
  }
  if (check) {
    NtDiagnostic d;
    if (!nova_check_many_v2(inputs, count, &d)) {
      diagnostic(&d, inputs, count);
      goto finish;
    }
    printf("{\"ok\":true,\"checked\":%zu,\"abi\":2}\n", count);
    status = 0;
    goto finish;
  }
  if (!nova_compile_many_v2(inputs, count, &artifact)) {
    diagnostic(&artifact.error, inputs, count);
    goto finish;
  }
  if (build) {
    if (!nt_make_pe(&artifact)) {
      report(700, "PE packaging failed");
      goto finish;
    }
    if (!publish(output, artifact.pe.bytes, artifact.pe.size)) {
      report(2, "cannot publish PE: existing destination or I/O failure");
      goto finish;
    }
    printf("{\"ok\":true,\"bytes\":%zu,\"abi\":2,\"producer\":\"BOOTSTRAP_"
           "EXTERNAL_C\"}\n",
           artifact.pe.size);
    status = 0;
    goto finish;
  }
  NovaContextV2 context;
  if (!nova_context_init_v2(&context)) {
    report(900, "runtime initialization failed");
    goto finish;
  }
  uint64_t args[64] = {0};
  unsigned slots = 0;
  struct {
    uint64_t handle, length;
  } buffers[32];
  unsigned buffer_count = 0;
  int valid = 1;
  for (int i = runtime_start; i < argc; i++) {
    const char *flag = argv[i];
    if (i + 1 >= argc) {
      valid = 0;
      break;
    }
    const char *value = argv[++i];
    uint64_t number;
    if (!strcmp(flag, "--u64")) {
      if (slots >= 64 || !u64(value, &number)) {
        valid = 0;
        break;
      }
      args[slots++] = number;
    } else if (!strcmp(flag, "--text")) {
      if (slots > 62) {
        valid = 0;
        break;
      }
      uint64_t length = strlen(value),
               h = nova_borrow_v2(&context, (void *)value, length, 0);
      args[slots++] = h;
      args[slots++] = length;
    } else if (!strcmp(flag, "--buffer")) {
      if (slots > 62 || !u64(value, &number)) {
        valid = 0;
        break;
      }
      uint64_t h = context.runtime->calls[NV2_ALLOC](&context, number, 0, 0);
      args[slots++] = h;
      args[slots++] = number;
      buffers[buffer_count].handle = h;
      buffers[buffer_count++].length = number;
    } else {
      valid = 0;
      break;
    }
  }
  if (!valid) {
    report(2, "invalid run argument; use --u64, --text or --buffer (four "
              "scalar slots maximum)");
    nova_context_destroy_v2(&context);
    goto finish;
  }
  NovaHostImage image;
  uint64_t result = 0;
  if (!context.error && !nova_host_map(&artifact, &image)) {
    context.error = NV2_RUNTIME;
  } else if (!context.error) {
    result = image.entry(&context, args, slots);
    nova_host_unmap(&image);
  }
  uint64_t execution_error = context.error, source_index = context.error_source,
           source_offset = context.error_offset;
  char *hex[32] = {0};
  static const char digits[] = "0123456789abcdef";
  for (unsigned i = 0; i < buffer_count; i++) {
    void *p = NULL;
    if (!context.error && buffers[i].length <= 4096) {
      uint64_t saved_source = context.error_source,
               saved_offset = context.error_offset;
      p = (void *)(uintptr_t)context.runtime->calls[NV2_RESOLVE](
          &context, buffers[i].handle, 0, buffers[i].length);
      context.error = 0;
      context.error_source = saved_source;
      context.error_offset = saved_offset;
    }
    if (p) {
      size_t n = (size_t)buffers[i].length;
      hex[i] = malloc(n * 2 + 1);
      if (!hex[i]) {
        if (!execution_error)
          execution_error = NV2_RUNTIME;
        continue;
      }
      for (size_t j = 0; j < n; j++) {
        unsigned byte = ((unsigned char *)p)[j];
        hex[i][j * 2] = digits[byte >> 4];
        hex[i][j * 2 + 1] = digits[byte & 15];
      }
      hex[i][n * 2] = 0;
    }
  }
  int cleanup_ok = nova_context_destroy_v2(&context);
  if (!cleanup_ok && !execution_error) {
    execution_error = NV2_IO;
    source_index = 0;
    source_offset = 0;
  }
  printf("{\"ok\":%s,\"result\":\"%llu\",\"error\":%llu,\"source\":%llu,"
         "\"offset\":%llu,\"cleanup_ok\":%s,\"buffers\":[",
         execution_error ? "false" : "true", (unsigned long long)result,
         (unsigned long long)execution_error, (unsigned long long)source_index,
         (unsigned long long)source_offset, cleanup_ok ? "true" : "false");
  for (unsigned i = 0; i < buffer_count; i++) {
    if (i)
      putchar(',');
    printf("{\"index\":%u,\"length\":%llu,\"hex\":", i,
           (unsigned long long)buffers[i].length);
    if (hex[i])
      quote(stdout, hex[i]);
    else
      fputs("null", stdout);
    putchar('}');
    free(hex[i]);
  }
  puts("]}");
  status = execution_error ? 1 : 0;
finish:
  nt_artifact_free(&artifact);
  for (size_t i = 0; i < count; i++)
    free(owned[i]);
  return status;
}
