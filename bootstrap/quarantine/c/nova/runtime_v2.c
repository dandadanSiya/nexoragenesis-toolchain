#include "runtime_v2.h"
#ifdef NOVA_UEFI
#include "uefi_platform.h"
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif
enum { BUFFER_MAX = 65536, FILE_MAX = 128, BYTES_MAX = 64 * 1024 * 1024 };
typedef struct {
  unsigned char *base;
  uint64_t length, offset, generation, parent;
  int live, owned, writable;
} Buffer;
typedef struct {
  FILE *file;
  uint64_t generation;
  int writable;
} File;
typedef struct {
  Buffer buffers[BUFFER_MAX];
  File files[FILE_MAX];
  uint64_t generation, allocated;
  unsigned next_buffer;
} State;
static uint64_t error(NovaContextV2 *c, uint64_t code) {
  if (!c->error)
    c->error = code;
  return 0;
}
static State *state(NovaContextV2 *c) {
  return c && c->runtime ? (State *)c->runtime->state : NULL;
}
static uint64_t handle(unsigned slot, uint64_t generation) {
  return (generation << 32) | (slot + 1);
}
static Buffer *buffer(NovaContextV2 *c, uint64_t id) {
  State *s = state(c);
  unsigned slot = (unsigned)(id & UINT32_MAX);
  if (!s || !slot || slot > BUFFER_MAX) {
    error(c, NV2_LIFETIME);
    return NULL;
  }
  Buffer *b = &s->buffers[slot - 1];
  if (!b->live || b->generation != (id >> 32)) {
    error(c, NV2_LIFETIME);
    return NULL;
  }
  return b;
}
static unsigned char *resolve(NovaContextV2 *c, uint64_t id, uint64_t at,
                              uint64_t n, int write) {
  for (unsigned depth = 0; depth < BUFFER_MAX; depth++) {
    Buffer *b = buffer(c, id);
    if (!b)
      return NULL;
    if (at > b->length || n > b->length - at) {
      error(c, NV2_BOUNDS);
      return NULL;
    }
    if (write && !b->writable) {
      error(c, NV2_READONLY);
      return NULL;
    }
    if (!b->parent)
      return b->base ? b->base + (size_t)at : NULL;
    if (at > UINT64_MAX - b->offset) {
      error(c, NV2_BOUNDS);
      return NULL;
    }
    at += b->offset;
    id = b->parent;
  }
  error(c, NV2_LIFETIME);
  return NULL;
}
static uint64_t add_buffer(NovaContextV2 *c, Buffer value) {
  State *s = state(c);
  if (s->generation == UINT32_MAX)
    return error(c, NV2_RUNTIME);
  for (unsigned n = 0; n < BUFFER_MAX; n++) {
    unsigned i = (s->next_buffer + n) % BUFFER_MAX;
    if (!s->buffers[i].live) {
      value.live = 1;
      value.generation = ++s->generation;
      s->buffers[i] = value;
      s->next_buffer = (i + 1) % BUFFER_MAX;
      return handle(i, value.generation);
    }
  }
  return error(c, NV2_RUNTIME);
}
uint64_t nova_borrow_v2(NovaContextV2 *c, void *p, uint64_t n, int writable) {
  if (!c || c->error)
    return 0;
  if (!p && n)
    return error(c, NV2_ARGUMENT);
  if (n > BYTES_MAX)
    return error(c, NV2_BOUNDS);
  return add_buffer(c, (Buffer){.base = p, .length = n, .writable = writable});
}
static uint64_t NOVA_ABI allocate_v2(NovaContextV2 *c, uint64_t n, uint64_t b,
                                     uint64_t d) {
  (void)b;
  (void)d;
  if (c->error)
    return 0;
  State *s = state(c);
  if (n > BYTES_MAX || s->allocated > 4 * BYTES_MAX - n)
    return error(c, NV2_BOUNDS);
  void *p = calloc(1, (size_t)(n ? n : 1));
  if (!p)
    return error(c, NV2_RUNTIME);
  uint64_t h = add_buffer(
      c, (Buffer){.base = p, .length = n, .owned = 1, .writable = 1});
  if (!h)
    free(p);
  else
    s->allocated += n;
  return h;
}
static uint64_t NOVA_ABI release_v2(NovaContextV2 *c, uint64_t h, uint64_t n,
                                    uint64_t d) {
  (void)d;
  if (c->error)
    return 0;
  Buffer *b = buffer(c, h);
  if (!b)
    return 0;
  if (n != b->length)
    return error(c, NV2_BOUNDS);
  if (b->owned) {
    state(c)->allocated -= b->length;
    free(b->base);
  }
  b->live = 0;
  b->base = NULL;
  return 0;
}
static uint64_t NOVA_ABI resolve_v2(NovaContextV2 *c, uint64_t h, uint64_t at,
                                    uint64_t spec) {
  if (c->error)
    return 0;
  return (uint64_t)(uintptr_t)resolve(c, h, at, spec & ~(UINT64_C(1) << 63),
                                      (spec >> 63) != 0);
}
static uint64_t NOVA_ABI slice_v2(NovaContextV2 *c, uint64_t h, uint64_t at,
                                  uint64_t n) {
  if (c->error)
    return 0;
  Buffer *b = buffer(c, h);
  if (!b)
    return 0;
  if (at > b->length || n > b->length - at)
    return error(c, NV2_BOUNDS);
  if (!resolve(c, h, at, n, 0) && n)
    return 0;
  return add_buffer(
      c, (Buffer){
             .parent = h, .offset = at, .length = n, .writable = b->writable});
}
static uint64_t NOVA_ABI length_v2(NovaContextV2 *c, uint64_t h, uint64_t a,
                                   uint64_t b) {
  (void)a;
  (void)b;
  if (c->error)
    return 0;
  Buffer *v = buffer(c, h);
  if (!v)
    return 0;
  if (!resolve(c, h, 0, 0, 0) && v->length)
    return 0;
  return v->length;
}
static uint64_t NOVA_ABI literal_v2(NovaContextV2 *c, uint64_t p, uint64_t n,
                                    uint64_t unused) {
  (void)unused;
  if (c->error)
    return 0;
  if (!p && n)
    return error(c, NV2_ARGUMENT);
  uint64_t h = allocate_v2(c, n, 0, 0);
  if (h) {
    Buffer *b = buffer(c, h);
    if (n)
      memcpy(b->base, (void *)(uintptr_t)p, (size_t)n);
    b->writable = unused != 0;
  }
  return h;
}
static char *path(NovaContextV2 *c, uint64_t h, uint64_t n) {
  if (!n || n > 4096) {
    error(c, NV2_ARGUMENT);
    return NULL;
  }
  unsigned char *p = resolve(c, h, 0, n, 0);
  if (!p)
    return NULL;
  if (memchr(p, 0, (size_t)n)) {
    error(c, NV2_ARGUMENT);
    return NULL;
  }
  char *s = malloc((size_t)n + 1);
  if (!s) {
    error(c, NV2_RUNTIME);
    return NULL;
  }
  memcpy(s, p, (size_t)n);
  s[n] = 0;
  return s;
}
static File *file(NovaContextV2 *c, uint64_t h) {
  unsigned slot = (unsigned)(h & UINT32_MAX);
  if (!slot || slot > FILE_MAX) {
    error(c, NV2_LIFETIME);
    return NULL;
  }
  File *f = &state(c)->files[slot - 1];
  if (!f->file || f->generation != (h >> 32)) {
    error(c, NV2_LIFETIME);
    return NULL;
  }
  return f;
}
static uint64_t open_file(NovaContextV2 *c, uint64_t h, uint64_t n,
                          int writable) {
  if (c->error)
    return 0;
  State *s = state(c);
  if (s->generation == UINT32_MAX)
    return error(c, NV2_RUNTIME);
  unsigned slot = FILE_MAX;
  for (unsigned i = 0; i < FILE_MAX; i++)
    if (!s->files[i].file) {
      slot = i;
      break;
    }
  if (slot == FILE_MAX)
    return error(c, NV2_RUNTIME);
  char *name = path(c, h, n);
  if (!name)
    return 0;
  FILE *f = fopen(name, writable ? "wbx" : "rb");
  free(name);
  if (!f)
    return error(c, NV2_IO);
  s->files[slot] = (File){f, ++s->generation, writable};
  return handle(slot, s->generation);
}
static uint64_t NOVA_ABI open_read_v2(NovaContextV2 *c, uint64_t h, uint64_t n,
                                      uint64_t d) {
  (void)d;
  return open_file(c, h, n, 0);
}
static uint64_t NOVA_ABI open_new_v2(NovaContextV2 *c, uint64_t h, uint64_t n,
                                     uint64_t d) {
  (void)d;
  return open_file(c, h, n, 1);
}
static uint64_t NOVA_ABI read_v2(NovaContextV2 *c, uint64_t h, uint64_t buf,
                                 uint64_t n) {
  if (c->error)
    return 0;
  File *f = file(c, h);
  if (!f)
    return 0;
  if (f->writable)
    return error(c, NV2_IO);
  void *p = resolve(c, buf, 0, n, 1);
  if (!p && n)
    return 0;
  size_t size = fread(p, 1, (size_t)n, f->file);
  if (ferror(f->file))
    return error(c, NV2_IO);
  return size;
}
static uint64_t NOVA_ABI write_v2(NovaContextV2 *c, uint64_t h, uint64_t buf,
                                  uint64_t n) {
  if (c->error)
    return 0;
  File *f = file(c, h);
  if (!f)
    return 0;
  if (!f->writable)
    return error(c, NV2_IO);
  void *p = resolve(c, buf, 0, n, 0);
  if (!p && n)
    return 0;
  size_t size = fwrite(p, 1, (size_t)n, f->file);
  if (size != n || ferror(f->file))
    return error(c, NV2_IO);
  return size;
}
static uint64_t NOVA_ABI close_v2(NovaContextV2 *c, uint64_t h, uint64_t a,
                                  uint64_t b) {
  (void)a;
  (void)b;
  if (c->error)
    return 0;
  File *f = file(c, h);
  if (!f)
    return 0;
  FILE *p = f->file;
  f->file = NULL;
  if (fclose(p))
    return error(c, NV2_IO);
  return 0;
}
static uint64_t NOVA_ABI copy_bytes_v2(NovaContextV2 *c, uint64_t destination,
                                       uint64_t source, uint64_t n) {
  if (c->error)
    return 0;
  unsigned char *d = resolve(c, destination, 0, n, 1);
  if (c->error)
    return 0;
  unsigned char *s = resolve(c, source, 0, n, 0);
  if (c->error)
    return 0;
  if ((uintptr_t)d > (uintptr_t)s && (uintptr_t)d - (uintptr_t)s < n) {
    for (uint64_t i = n; i; i--)
      d[i - 1] = s[i - 1];
  } else
    for (uint64_t i = 0; i < n; i++)
      d[i] = s[i];
  return n;
}
static uint64_t NOVA_ABI diag_v2(NovaContextV2 *c, uint64_t h, uint64_t n,
                                 uint64_t unused) {
  (void)unused;
  if (c->error)
    return 0;
  void *p = resolve(c, h, 0, n, 0);
  if (!p && n)
    return 0;
  if (fwrite(p, 1, (size_t)n, stderr) != n)
    return error(c, NV2_IO);
  return 0;
}
int nova_context_init_v2(NovaContextV2 *c) {
  if (!c)
    return 0;
  memset(c, 0, sizeof *c);
  NovaRuntimeV2 *r = calloc(1, sizeof *r);
  State *s = calloc(1, sizeof *s);
  if (!r || !s) {
    free(r);
    free(s);
    return 0;
  }
  *r = (NovaRuntimeV2){2,
                       sizeof *r,
                       {allocate_v2, release_v2, open_read_v2, open_new_v2,
                        read_v2, write_v2, close_v2, diag_v2, resolve_v2,
                        slice_v2, length_v2, literal_v2},
                       s,
                       copy_bytes_v2};
  *c = (NovaContextV2){2, sizeof *c, 0, 0, 0, r};
  return 1;
}
void nova_error_clear_v2(NovaContextV2 *c) {
  if (c) {
    c->error = 0;
    c->error_source = 0;
    c->error_offset = 0;
  }
}
int nova_context_destroy_v2(NovaContextV2 *c) {
  if (!c)
    return 0;
  int ok = 1;
  if (c->runtime) {
    State *s = state(c);
    if (s) {
      for (unsigned i = 0; i < BUFFER_MAX; i++)
        if (s->buffers[i].live && s->buffers[i].owned)
          free(s->buffers[i].base);
      for (unsigned i = 0; i < FILE_MAX; i++)
        if (s->files[i].file && fclose(s->files[i].file))
          ok = 0;
      free(s);
    }
    free(c->runtime);
  }
  memset(c, 0, sizeof *c);
  return ok;
}
