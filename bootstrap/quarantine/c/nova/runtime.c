#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define LIMIT (16u * 1024u * 1024u)
typedef struct {
  void *pointer;
  uint64_t size;
} Allocation;
typedef struct {
  FILE *file;
  uint64_t generation;
  int writable;
} FileSlot;
typedef struct {
  Allocation memory[128];
  FileSlot files[128];
  uint64_t allocated, generation;
} Context;
static uint64_t NOVA_ABI allocate(void *opaque, uint64_t size, uint64_t b,
                                  uint64_t d) {
  (void)b;
  (void)d;
  Context *c = opaque;
  if (!size || size > LIMIT || c->allocated > 4 * LIMIT - size)
    return 0;
  for (unsigned i = 0; i < 128; i++)
    if (!c->memory[i].pointer) {
      void *p = calloc(1, (size_t)size);
      if (!p)
        return 0;
      c->memory[i] = (Allocation){p, size};
      c->allocated += size;
      return (uint64_t)(uintptr_t)p;
    }
  return 0;
}
static uint64_t NOVA_ABI release(void *opaque, uint64_t pointer, uint64_t size,
                                 uint64_t d) {
  (void)d;
  Context *c = opaque;
  for (unsigned i = 0; i < 128; i++)
    if (c->memory[i].pointer &&
        (uint64_t)(uintptr_t)c->memory[i].pointer == pointer &&
        c->memory[i].size == size) {
      c->allocated -= size;
      free(c->memory[i].pointer);
      c->memory[i] = (Allocation){0};
      return 0;
    }
  return UINT64_MAX;
}
static char *path_copy(uint64_t ptr, uint64_t length) {
  if (!ptr || !length || length > 4096)
    return NULL;
  const char *p = (const char *)(uintptr_t)ptr;
  if (memchr(p, 0, (size_t)length))
    return NULL;
  char *copy = malloc((size_t)length + 1);
  if (!copy)
    return NULL;
  memcpy(copy, p, (size_t)length);
  copy[length] = 0;
  return copy;
}
static uint64_t open_file(Context *c, uint64_t pointer, uint64_t length,
                          int write) {
  if (c->generation > UINT64_MAX / 128 - 1)
    return UINT64_MAX;
  unsigned slot = 128;
  for (unsigned i = 0; i < 128; i++)
    if (!c->files[i].file) {
      slot = i;
      break;
    }
  if (slot == 128)
    return UINT64_MAX;
  char *path = path_copy(pointer, length);
  if (!path)
    return UINT64_MAX;
  FILE *f = fopen(path, write ? "wbx" : "rb");
  free(path);
  if (!f)
    return UINT64_MAX;
  c->files[slot] = (FileSlot){f, ++c->generation, write};
  return c->generation * 128 + slot;
}
static uint64_t NOVA_ABI open_read(void *c, uint64_t p, uint64_t n,
                                   uint64_t unused) {
  (void)unused;
  return open_file(c, p, n, 0);
}
static uint64_t NOVA_ABI open_new(void *c, uint64_t p, uint64_t n,
                                  uint64_t unused) {
  (void)unused;
  return open_file(c, p, n, 1);
}
static FileSlot *file(Context *c, uint64_t handle) {
  unsigned slot = (unsigned)(handle % 128);
  return c->files[slot].file && c->files[slot].generation == handle / 128
             ? &c->files[slot]
             : NULL;
}
static int owned_span(Context *c, uint64_t pointer, uint64_t size) {
  if (!pointer || size > LIMIT)
    return 0;
  for (unsigned i = 0; i < 128; i++) {
    uint64_t base = (uint64_t)(uintptr_t)c->memory[i].pointer;
    if (base && pointer >= base && pointer - base <= c->memory[i].size &&
        size <= c->memory[i].size - (pointer - base))
      return 1;
  }
  return 0;
}
static uint64_t NOVA_ABI read_file(void *opaque, uint64_t handle,
                                   uint64_t pointer, uint64_t size) {
  Context *c = opaque;
  FileSlot *f = file(c, handle);
  if (!f || f->writable || !owned_span(c, pointer, size))
    return UINT64_MAX;
  size_t n = fread((void *)(uintptr_t)pointer, 1, (size_t)size, f->file);
  return ferror(f->file) ? UINT64_MAX : n;
}
static uint64_t NOVA_ABI write_file(void *opaque, uint64_t handle,
                                    uint64_t pointer, uint64_t size) {
  Context *c = opaque;
  FileSlot *f = file(c, handle);
  if (!f || !f->writable || !owned_span(c, pointer, size))
    return UINT64_MAX;
  size_t n = fwrite((const void *)(uintptr_t)pointer, 1, (size_t)size, f->file);
  return n == size && !ferror(f->file) ? n : UINT64_MAX;
}
static uint64_t NOVA_ABI close_file(void *opaque, uint64_t handle,
                                    uint64_t unused, uint64_t unused2) {
  (void)unused;
  (void)unused2;
  Context *c = opaque;
  FileSlot *f = file(c, handle);
  if (!f)
    return UINT64_MAX;
  FILE *stream = f->file;
  f->file = NULL;
  return fclose(stream) == 0 ? 0 : UINT64_MAX;
}
static uint64_t NOVA_ABI diagnostic(void *opaque, uint64_t pointer,
                                    uint64_t size, uint64_t unused) {
  (void)opaque;
  (void)unused;
  if (!pointer || size > 1048576)
    return UINT64_MAX;
  return fwrite((const void *)(uintptr_t)pointer, 1, (size_t)size, stderr) ==
                 size
             ? 0
             : UINT64_MAX;
}
int nova_runtime_init(NovaRuntimeV1 *r) {
  if (!r)
    return 0;
  memset(r, 0, sizeof *r);
  Context *c = calloc(1, sizeof *c);
  if (!c)
    return 0;
  *r = (NovaRuntimeV1){1,          sizeof *r,  c,         allocate,
                       release,    open_read,  open_new,  read_file,
                       write_file, close_file, diagnostic};
  return 1;
}
int nova_runtime_destroy(NovaRuntimeV1 *r) {
  if (!r)
    return 0;
  int ok = 1;
  Context *c = r->context;
  if (c) {
    for (unsigned i = 0; i < 128; i++) {
      if (c->files[i].file && fclose(c->files[i].file))
        ok = 0;
      free(c->memory[i].pointer);
    }
    free(c);
  }
  memset(r, 0, sizeof *r);
  return ok;
}
