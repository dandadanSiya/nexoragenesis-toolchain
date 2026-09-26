/* BOOTSTRAP_C: libc subset used by the Nova ABI2 runtime inside the Nexora
 * toy kernel. Memory comes from one arena; files come from the RAM filesystem
 * loaded before ExitBootServices, plus device files:
 *   SCREEN  write 320x200 palette indices, presented at once
 *   PALETTE write 256 RGB triplets
 *   KEYS    read 8 bytes: bit0 held, bit1 pressed since the previous read
 *   CLOCK   read 8 bytes: milliseconds since boot, little endian
 *   TONE    write 8 bytes: PC speaker frequency in Hz (0 = silence)
 *   LOG     write text to the serial port */
#include "nexora.h"
#include "uefi_platform.h"

enum { NX_DEV_NONE, NX_DEV_FILE, NX_DEV_SCREEN, NX_DEV_PALETTE, NX_DEV_KEYS,
       NX_DEV_CLOCK, NX_DEV_TONE, NX_DEV_LOG };

typedef struct {
  int kind, writable;
  const NxFile *file;
  size_t position;
} Node;

static Node log_node = {NX_DEV_LOG, 1, NULL, 0};
static FILE log_file = {(EFI_FILE *)&log_node, 0};
FILE *stderr = &log_file;
uint64_t nova_uefi_platform_error, nova_uefi_pools, nova_uefi_files;

void *memcpy(void *d, const void *s, size_t n) {
  unsigned char *out = d;
  const unsigned char *in = s;
  for (size_t i = 0; i < n; i++)
    out[i] = in[i];
  return d;
}
void *memset(void *d, int b, size_t n) {
  unsigned char *out = d;
  for (size_t i = 0; i < n; i++)
    out[i] = (unsigned char)b;
  return d;
}
void *memchr(const void *p, int b, size_t n) {
  const unsigned char *s = p;
  for (size_t i = 0; i < n; i++)
    if (s[i] == (unsigned char)b)
      return (void *)(s + i);
  return NULL;
}

/* Address-ordered first-fit allocator with coalescing. Each block starts with
 * a 16-byte header holding its total size and whether it is free. */
typedef struct Block {
  size_t size;
  struct Block *next_free;
} Block;
static Block *free_list;
static size_t heap_used;

void nx_heap_init(void *base, size_t size) {
  uintptr_t start = ((uintptr_t)base + 15) & ~(uintptr_t)15;
  size -= start - (uintptr_t)base;
  free_list = (Block *)start;
  free_list->size = size & ~(size_t)15;
  free_list->next_free = NULL;
  heap_used = 0;
}
size_t nx_heap_used(void) { return heap_used; }

void *malloc(size_t n) {
  if (n > ((size_t)1 << 32))
    return NULL;
  size_t need = ((n + 15) & ~(size_t)15) + sizeof(Block);
  for (Block **link = &free_list; *link; link = &(*link)->next_free) {
    Block *b = *link;
    if (b->size < need)
      continue;
    if (b->size - need >= 64) {
      Block *rest = (Block *)((unsigned char *)b + need);
      rest->size = b->size - need;
      rest->next_free = b->next_free;
      b->size = need;
      *link = rest;
    } else
      *link = b->next_free;
    b->next_free = (Block *)1;
    heap_used += b->size;
    return b + 1;
  }
  nx_log("NEXORA heap exhausted\r\n");
  return NULL;
}
void *calloc(size_t count, size_t size) {
  if (size && count > ((size_t)1 << 32) / size)
    return NULL;
  void *p = malloc(count * size);
  if (p)
    memset(p, 0, count * size);
  return p;
}
void free(void *p) {
  if (!p)
    return;
  Block *b = (Block *)p - 1;
  heap_used -= b->size;
  Block **link = &free_list;
  while (*link && *link < b)
    link = &(*link)->next_free;
  b->next_free = *link;
  *link = b;
  if (b->next_free && (unsigned char *)b + b->size == (unsigned char *)b->next_free) {
    b->size += b->next_free->size;
    b->next_free = b->next_free->next_free;
  }
  if (link != &free_list) {
    Block *previous = (Block *)((unsigned char *)link - offsetof(Block, next_free));
    if ((unsigned char *)previous + previous->size == (unsigned char *)b) {
      previous->size += b->size;
      previous->next_free = b->next_free;
    }
  }
}

static int same_name(const char *a, const char *b) {
  for (;; a++, b++) {
    char x = *a >= 'a' && *a <= 'z' ? (char)(*a - 32) : *a;
    char y = *b >= 'a' && *b <= 'z' ? (char)(*b - 32) : *b;
    if (x != y)
      return 0;
    if (!x)
      return 1;
  }
}

FILE *fopen(const char *name, const char *mode) {
  static const struct {
    const char *name;
    int kind, writable;
  } devices[] = {{"SCREEN", NX_DEV_SCREEN, 1}, {"PALETTE", NX_DEV_PALETTE, 1},
                 {"KEYS", NX_DEV_KEYS, 0},     {"CLOCK", NX_DEV_CLOCK, 0},
                 {"TONE", NX_DEV_TONE, 1},     {"LOG", NX_DEV_LOG, 1}};
  int writable = mode[0] == 'w';
  Node node = {NX_DEV_NONE, writable, NULL, 0};
  for (size_t i = 0; i < sizeof devices / sizeof devices[0]; i++)
    if (same_name(name, devices[i].name)) {
      if (writable != devices[i].writable)
        return NULL;
      node.kind = devices[i].kind;
    }
  if (node.kind == NX_DEV_NONE) {
    if (writable)
      return NULL; /* the RAM filesystem is read-only */
    node.file = nx_ramfs_find(name);
    if (!node.file)
      return NULL;
    node.kind = NX_DEV_FILE;
  }
  Node *owned = malloc(sizeof *owned);
  FILE *f = malloc(sizeof *f);
  if (!owned || !f) {
    free(owned);
    free(f);
    return NULL;
  }
  *owned = node;
  f->handle = (EFI_FILE *)owned;
  f->error = 0;
  return f;
}
int fclose(FILE *f) {
  if (!f || f == stderr)
    return 0;
  free(f->handle);
  free(f);
  return 0;
}
size_t fread(void *p, size_t size, size_t count, FILE *f) {
  Node *node = (Node *)f->handle;
  size_t n = size * count;
  if (node->kind == NX_DEV_FILE) {
    size_t left = node->file->size - node->position;
    if (n > left)
      n = left;
    memcpy(p, node->file->data + node->position, n);
    node->position += n;
    return size ? n / size : 0;
  }
  if (node->kind == NX_DEV_KEYS) {
    if (n > NX_KEYS)
      n = NX_KEYS;
    nx_keys_read(p, n);
    return size ? n / size : 0;
  }
  if (node->kind == NX_DEV_CLOCK) {
    uint64_t now = nx_millis();
    if (n > 8)
      n = 8;
    for (size_t i = 0; i < n; i++)
      ((unsigned char *)p)[i] = (unsigned char)(now >> (8 * i));
    return size ? n / size : 0;
  }
  f->error = 1;
  return 0;
}
size_t fwrite(const void *p, size_t size, size_t count, FILE *f) {
  Node *node = (Node *)f->handle;
  size_t n = size * count;
  switch (node->kind) {
  case NX_DEV_SCREEN:
    nx_present(p, n);
    break;
  case NX_DEV_PALETTE:
    nx_palette(p, n);
    break;
  case NX_DEV_TONE:
    nx_tone(p, n);
    break;
  case NX_DEV_LOG: {
    char text[129];
    const char *s = p;
    for (size_t done = 0; done < n;) {
      size_t chunk = n - done > 128 ? 128 : n - done;
      memcpy(text, s + done, chunk);
      text[chunk] = 0;
      nx_log(text);
      done += chunk;
    }
    break;
  }
  default:
    f->error = 1;
    return 0;
  }
  return count;
}
int ferror(FILE *f) { return f->error; }
int fseek(FILE *f, long offset, int whence) {
  Node *node = (Node *)f->handle;
  if (node->kind != NX_DEV_FILE)
    return -1;
  size_t base = whence == SEEK_END ? node->file->size : 0;
  if (offset < 0 || base + (size_t)offset > node->file->size)
    return -1;
  node->position = base + (size_t)offset;
  return 0;
}
long ftell(FILE *f) {
  Node *node = (Node *)f->handle;
  return node->kind == NX_DEV_FILE ? (long)node->position : -1;
}
