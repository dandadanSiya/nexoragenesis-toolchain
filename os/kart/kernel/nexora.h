/* BOOTSTRAP_C: Nexora toy kernel services shared by kernel.c and platform.c.
 * After ExitBootServices the kernel owns the framebuffer, the PS/2 keyboard,
 * the TSC clock and one memory arena; Nova programs reach them only through
 * the ABI2 runtime file calls (see platform.c for the device names). */
#ifndef NEXORA_KERNEL_H
#define NEXORA_KERNEL_H
#include <stddef.h>
#include <stdint.h>

enum { NX_SCREEN_W = 320, NX_SCREEN_H = 200, NX_KEYS = 8 };
enum {
  NX_KEY_UP = 0,
  NX_KEY_DOWN = 1,
  NX_KEY_LEFT = 2,
  NX_KEY_RIGHT = 3,
  NX_KEY_SPACE = 4,
  NX_KEY_ENTER = 5,
  NX_KEY_ESCAPE = 6,
  NX_KEY_BOOST = 7
};

typedef struct {
  char name[32];
  const uint8_t *data;
  size_t size;
} NxFile;

void nx_log(const char *text);
void nx_log_hex(uint64_t value);
uint64_t nx_millis(void);
void nx_keys_read(uint8_t *out, size_t n);
void nx_present(const uint8_t *pixels, size_t n);
void nx_palette(const uint8_t *rgb, size_t n);
void nx_tone(const uint8_t *request, size_t n);
const NxFile *nx_ramfs_find(const char *name);
void nx_heap_init(void *base, size_t size);
size_t nx_heap_used(void);
#endif
