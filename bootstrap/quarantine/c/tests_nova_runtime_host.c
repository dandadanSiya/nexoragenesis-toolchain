#include "nova/runtime.h"
#include <stdio.h>
#include <stdlib.h>
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
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  NovaRuntimeV1 r;
  CHECK(nova_runtime_init(&r), "initialize runtime");
  if (!r.context)
    return 1;
  uint64_t pointer = r.allocate(r.context, 16, 0, 0);
  CHECK(pointer != 0, "allocate bytes");
  unsigned char *b = (unsigned char *)(uintptr_t)pointer;
  for (unsigned i = 0; i < 16; i++)
    CHECK(b[i] == 0, "allocation initially zeroed");
  memcpy(b, "42", 2);
  uint64_t path = (uint64_t)(uintptr_t)argv[1], length = strlen(argv[1]);
  uint64_t write = r.open_new(r.context, path, length, 0);
  CHECK(write != UINT64_MAX, "new file handle");
  CHECK(r.write(r.context, write, pointer, 2) == 2, "write exact bytes");
  CHECK(r.read(r.context, write, pointer, 2) == UINT64_MAX,
        "write handle not readable");
  CHECK(r.write(r.context, write, pointer + 15, 2) == UINT64_MAX,
        "range beyond owned allocation rejected");
  CHECK(r.close(r.context, write, 0, 0) == 0, "close writer");
  CHECK(r.close(r.context, write, 0, 0) == UINT64_MAX,
        "stale closed handle rejected");
  CHECK(r.open_new(r.context, path, length, 0) == UINT64_MAX,
        "existing file never overwritten");
  uint64_t read = r.open_read(r.context, path, length, 0);
  CHECK(read != UINT64_MAX && read != write, "reader has fresh generation");
  CHECK(r.read(r.context, write, pointer, 2) == UINT64_MAX,
        "old generation cannot access reused slot");
  memset(b, 0, 16);
  CHECK(r.read(r.context, read, pointer, 16) == 2 && b[0] == '4' && b[1] == '2',
        "read preserved file contents");
  CHECK(r.read(r.context, read, pointer, 16) == 0, "EOF yields zero length");
  CHECK(r.write(r.context, read, pointer, 2) == UINT64_MAX,
        "read handle not writable");
  CHECK(r.release(r.context, pointer, 15, 0) == UINT64_MAX,
        "wrong allocation size cannot release");
  CHECK(r.release(r.context, pointer, 16, 0) == 0, "owned allocation release");
  CHECK(r.release(r.context, pointer, 16, 0) == UINT64_MAX,
        "double release rejected");
  CHECK(r.read(r.context, read, pointer, 2) == UINT64_MAX,
        "released buffer invalid for I/O");
  CHECK(r.allocate(r.context, UINT64_MAX, 0, 0) == 0,
        "oversize allocation rejected");
  CHECK(r.allocate(r.context, 0, 0, 0) == 0, "zero allocation rejected");
  char invalid[] = {'a', 0, 'b'};
  CHECK(r.open_read(r.context, (uint64_t)(uintptr_t)invalid, 3, 0) ==
            UINT64_MAX,
        "embedded NUL path rejected");
  CHECK(r.open_read(r.context, 0, 1, 0) == UINT64_MAX, "null path rejected");
  CHECK(r.diagnostic(r.context, 0, 1, 0) == UINT64_MAX,
        "null diagnostic rejected");
  CHECK(r.allocate(r.context, 1024, 0, 0) != 0,
        "owned allocation deliberately left to destroy");
  CHECK(nova_runtime_destroy(&r),
        "destroy closes reader and releases remaining allocation");
  CHECK(!r.context && !r.version, "destroy invalidates descriptor");
  CHECK(nova_runtime_destroy(&r), "destroy empty descriptor is idempotent");
  printf("NOVA_RUNTIME_HOST_TESTS checks=%u failures=%u\n", checks, failures);
  return failures ? 1 : 0;
}
