#include "uefi_platform.h"
static EFI_BS *services;
static EFI_FILE *root;
static EFI_SERIAL *serial;
static FILE serial_file;
FILE *stderr = &serial_file;
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
void nova_uefi_init(EFI_BS *b, EFI_FILE *r, EFI_SERIAL *s) {
  services = b;
  root = r;
  serial = s;
}
void nova_uefi_log(const char *s) {
  size_t n = 0;
  while (s[n])
    n++;
  uint64_t remaining = n;
  while (remaining) {
    uint64_t wrote = remaining;
    EFI_STATUS st = serial->write(serial, &wrote, s);
    if (EFI_ERROR(st) || !wrote || wrote > remaining) {
      nova_uefi_platform_error = 1;
      return;
    }
    s += wrote;
    remaining -= wrote;
  }
}
void nova_uefi_hex(uint64_t n) {
  char text[19] = {'0', 'x'};
  for (unsigned i = 0; i < 16; i++)
    text[i + 2] = "0123456789abcdef"[(n >> (60 - i * 4)) & 15];
  text[18] = 0;
  nova_uefi_log(text);
}
void *malloc(size_t n) {
  if (n > 128 * 1024 * 1024 - 16)
    return NULL;
  uint64_t *header = NULL;
  EFI_STATUS st = services->allocate_pool(2, n + 16, (void **)&header);
  if (EFI_ERROR(st) || !header)
    return NULL;
  header[0] = UINT64_C(0x4e564d454d303032);
  header[1] = n;
  nova_uefi_pools++;
  return header + 2;
}
void *calloc(size_t n, size_t size) {
  if (size && n > SIZE_MAX / size)
    return NULL;
  void *p = malloc(n * size);
  if (p)
    memset(p, 0, n * size);
  return p;
}
void free(void *p) {
  if (!p)
    return;
  uint64_t *header = (uint64_t *)p - 2;
  if (header[0] != UINT64_C(0x4e564d454d303032)) {
    nova_uefi_platform_error = 1;
    return;
  }
  header[0] = 0;
  if (EFI_ERROR(services->free_pool(header)))
    nova_uefi_platform_error = 1;
  else
    nova_uefi_pools--;
}
FILE *fopen(const char *path, const char *mode) {
  size_t length = 0;
  while (path[length]) {
    if (length == 4096 || (unsigned char)path[length] > 127)
      return NULL;
    length++;
  }
  CHAR16*name=malloc((length+1)*sizeof(CHAR16));if(!name)return NULL;
  for(size_t i=0;i<length;i++)name[i]=(unsigned char)path[i];
  name[length] = 0;
  int write = mode[0] == 'w';
  EFI_FILE *handle = NULL;
  if (write) {
    EFI_STATUS existing = root->open(root, &handle, name, 1, 0);
    if (!EFI_ERROR(existing)) {
      if (handle && EFI_ERROR(handle->close(handle)))
        nova_uefi_platform_error = 1;
      free(name);
      return NULL;
    }
    handle = NULL;
  }
  EFI_STATUS st = root->open(root, &handle, name,
                             write ? UINT64_C(0x8000000000000003) : 1, 0);
  free(name);
  if (EFI_ERROR(st) || !handle)
    return NULL;
  FILE *f = malloc(sizeof *f);
  if (!f) {
    if (EFI_ERROR(handle->close(handle)))
      nova_uefi_platform_error = 1;
    return NULL;
  }
  *f = (FILE){handle, 0};
  nova_uefi_files++;
  return f;
}
int fclose(FILE *f) {
  if (!f || f == stderr)
    return -1;
  EFI_STATUS st = f->handle->close(f->handle);
  if (EFI_ERROR(st))
    nova_uefi_platform_error = 1;
  else
    nova_uefi_files--;
  free(f);
  return EFI_ERROR(st) ? -1 : 0;
}
size_t fread(void *p, size_t size, size_t count, FILE *f) {
  if (!f || f == stderr || !size || !count)
    return 0;
  if (count > SIZE_MAX / size) {
    f->error = 1;
    return 0;
  }
  uint64_t n = size * count;
  EFI_STATUS st = f->handle->read(f->handle, &n, p);
  if (EFI_ERROR(st) || n > size * count) {
    f->error = 1;
    return 0;
  }
  return (size_t)n / size;
}
size_t fwrite(const void *p, size_t size, size_t count, FILE *f) {
  if (!f || !size || !count)
    return 0;
  if (count > SIZE_MAX / size) {
    f->error = 1;
    return 0;
  }
  uint64_t n = size * count;
  EFI_STATUS st = f == stderr ? serial->write(serial, &n, p)
                              : f->handle->write(f->handle, &n, p);
  if (EFI_ERROR(st) || n > size * count) {
    f->error = 1;
    return 0;
  }
  return (size_t)n / size;
}
int ferror(FILE *f) { return !f || f->error; }
int fseek(FILE *f, long at, int kind) {
  if (!f || f == stderr || (kind != SEEK_SET && kind != SEEK_END) ||
      (kind == SEEK_END && at) || at < 0)
    return -1;
  EFI_STATUS st = f->handle->set_position(
      f->handle, kind == SEEK_END ? UINT64_MAX : (uint64_t)at);
  if (EFI_ERROR(st)) {
    f->error = 1;
    return -1;
  }
  return 0;
}
long ftell(FILE *f) {
  uint64_t at = 0;
  if (!f || f == stderr || EFI_ERROR(f->handle->get_position(f->handle, &at)) ||
      at > 0x7fffffff)
    return -1;
  return (long)at;
}
