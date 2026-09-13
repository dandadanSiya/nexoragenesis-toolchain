/* BOOTSTRAP_C_TEST, QEMU/OVMF ONLY.
 * Installs a copied IDT with only vector6 redirected to an NTASM-produced
 * same-CPL/no-error raw stub. No inline assembly or foreign assembler source.
 */
#include <stddef.h>
#include <stdint.h>
typedef uint64_t U64, STATUS, UINTN;
typedef uint16_t CHAR16;
typedef void *HANDLE;
#define EFIAPI __attribute__((ms_abi))
#define ERROR(s) (((s) >> 63) != 0)
#define EFI_ERROR(n) (UINT64_C(0x8000000000000000) | (n))
typedef struct {
  uint32_t a;
  uint16_t b, c;
  uint8_t d[8];
} GUID;
typedef struct {
  U64 sig;
  uint32_t rev, size, crc, res;
} HEADER;
typedef struct FILE FILE;
struct FILE {
  U64 rev;
  STATUS(EFIAPI *open)(FILE *, FILE **, CHAR16 *, U64, U64);
  STATUS(EFIAPI *close)(FILE *);
  void *del;
  STATUS(EFIAPI *read)(FILE *, UINTN *, void *);
  void *write, *get_position;
  STATUS(EFIAPI *set_position)(FILE *, U64);
  void *get_info, *set_info, *flush;
};
typedef struct {
  U64 rev;
  STATUS(EFIAPI *open_volume)(void *, FILE **);
} FS;
typedef struct {
  uint32_t rev;
  HANDLE parent;
  void *system;
  HANDLE device;
  void *path, *reserved;
  uint32_t options_size;
  void *options;
  void *base;
  U64 size;
  uint32_t code_type, data_type;
  void *unload;
} LOADED;
typedef struct {
  HEADER hdr;
  void *raise_tpl, *restore_tpl, *allocate_pages, *free_pages, *get_memory_map;
  STATUS(EFIAPI *allocate_pool)(uint32_t, UINTN, void **);
  STATUS(EFIAPI *free_pool)(void *);
  void *create_event, *set_timer, *wait_event, *signal_event, *close_event,
      *check_event;
  void *install_protocol, *reinstall_protocol, *uninstall_protocol;
  STATUS(EFIAPI *handle_protocol)(HANDLE, GUID *, void **);
  void *reserved, *register_protocol_notify, *locate_handle,
      *locate_device_path, *install_config_table;
  STATUS(EFIAPI *load_image)(uint8_t, HANDLE, void *, void *, UINTN, HANDLE *);
  void *start_image, *exit_image;
  STATUS(EFIAPI *unload_image)(HANDLE);
  void *exit_boot_services, *get_monotonic, *stall, *set_watchdog,
      *connect_controller, *disconnect_controller;
  void *open_protocol, *close_protocol, *open_protocol_info,
      *protocols_per_handle, *locate_handle_buffer;
  STATUS(EFIAPI *locate_protocol)(GUID *, void *, void **);
} BS;
typedef struct {
  HEADER hdr;
  CHAR16 *vendor;
  uint32_t revision;
  HANDLE cinh;
  void *cin;
  HANDLE couth;
  void *cout;
  HANDLE errh;
  void *err;
  void *runtime;
  BS *bs;
} SYSTEM;
typedef struct {
  uint32_t revision;
  void *reset, *set_attributes, *set_control, *get_control;
  STATUS(EFIAPI *write)(void *, UINTN *, void *);
} SERIAL;
typedef struct __attribute__((packed)) {
  uint16_t limit;
  uint64_t base;
} IDTR;
typedef struct __attribute__((packed)) {
  uint16_t offset_low, selector;
  uint8_t ist, type;
  uint16_t offset_mid;
  uint32_t offset_high, zero;
} GATE;
_Static_assert(sizeof(IDTR) == 10, "IDTR size");
_Static_assert(sizeof(GATE) == 16, "IDT gate size");
static GUID loaded_guid = {0x5b1b31a1,
                           0x9562,
                           0x11d2,
                           {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static GUID fs_guid = {0x964e5b22,
                       0x6459,
                       0x11d2,
                       {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static GUID serial_guid = {0xbb25cf6f,
                           0xf1d4,
                           0x11d2,
                           {0x9a, 0x0c, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0xfd}};
static SERIAL *uart;
static CHAR16 image_path[] = {'N', 'T', 'R', 'E', 'S', 'U', 'L',
                              'T', '.', 'E', 'F', 'I', 0};
static CHAR16 offset_path[] = {'S', 'T', 'U', 'B', 'O', 'F',
                               'F', '.', 'B', 'I', 'N', 0};
void *memset(void *destination, int value, size_t count) {
  uint8_t *bytes = destination;
  while (count--)
    *bytes++ = (uint8_t)value;
  return destination;
}
void *memcpy(void *destination, const void *source, size_t count) {
  uint8_t *d = destination;
  const uint8_t *s = source;
  while (count--)
    *d++ = *s++;
  return destination;
}
static void bytes_copy(void *destination, const void *source, UINTN count) {
  uint8_t *d = destination;
  const uint8_t *s = source;
  while (count--)
    *d++ = *s++;
}
static void serial(const char *text) {
  UINTN length = 0;
  while (text[length])
    length++;
  while (length) {
    UINTN written = length;
    if (ERROR(uart->write(uart, &written, (void *)text)) || !written ||
        written > length)
      return;
    text += written;
    length -= written;
  }
}
static void hex(U64 value) {
  char text[19];
  text[0] = '0';
  text[1] = 'x';
  for (unsigned i = 0; i < 16; i++)
    text[2 + i] = "0123456789abcdef"[(value >> ((15 - i) * 4)) & 15];
  text[18] = 0;
  serial(text);
}
/* Dedicated executable/read-only COFF section. These are test control helpers,
 * not NTASM product bytes and not source assembler. */
#ifdef _MSC_VER
#pragma section(".xctl", read, execute)
#define CONTROL __declspec(allocate(".xctl")) __declspec(align(16))
#else
#define CONTROL __attribute__((aligned(16), used))
#endif
static const uint8_t CONTROL sidt_code[] = {0x0f, 0x01, 0x09, 0xc3};
static const uint8_t CONTROL lidt_code[] = {0x0f, 0x01, 0x19, 0xc3};
static const uint8_t CONTROL cs_code[] = {0x31, 0xc0, 0x66, 0x8c, 0xc8, 0xc3};
static const uint8_t CONTROL flags_code[] = {0x9c, 0x58, 0xc3};
/* Save flags in RDX, execute real UD2, capture flags after IRETQ in RAX,
 * return XOR. Zero proves IRETQ restored the interrupted flags. */
static const uint8_t CONTROL ud2_code[] = {0x9c, 0x5a, 0x0f, 0x0b, 0x9c,
                                           0x58, 0x48, 0x31, 0xd0, 0xc3};
typedef void(EFIAPI *DESC_FN)(IDTR *);
typedef U64(EFIAPI *READ_FN)(void);
static STATUS read_file(BS *bs, FILE *root, CHAR16 *path, UINTN limit,
                        void **data, UINTN *size) {
  FILE *file = 0;
  void *buffer = 0;
  STATUS status;
  U64 length = 0;
  typedef STATUS(EFIAPI * GET_POSITION)(FILE *, U64 *);
  *data = 0;
  *size = 0;
  status = root->open(root, &file, path, 1, 0);
  if (ERROR(status) || !file)
    return status;
  status = file->set_position(file, UINT64_MAX);
  if (ERROR(status))
    goto finish;
  status = ((GET_POSITION)file->get_position)(file, &length);
  if (ERROR(status) || !length || length > limit) {
    status = EFI_ERROR(4);
    goto finish;
  }
  status = file->set_position(file, 0);
  if (ERROR(status))
    goto finish;
  status = bs->allocate_pool(2, (UINTN)length, &buffer);
  if (ERROR(status) || !buffer) {
    status = EFI_ERROR(9);
    goto finish;
  }
  *size = (UINTN)length;
  status = file->read(file, size, buffer);
  if (!ERROR(status) && *size == length) {
    *data = buffer;
    buffer = 0;
  } else if (!ERROR(status))
    status = EFI_ERROR(7);
finish:
  if (buffer)
    bs->free_pool(buffer);
  file->close(file);
  return status;
}
static void make_gate(GATE *gate, U64 handler, uint16_t selector) {
  gate->offset_low = (uint16_t)handler;
  gate->selector = selector;
  gate->ist = 0;
  gate->type = 0x8e;
  gate->offset_mid = (uint16_t)(handler >> 16);
  gate->offset_high = (uint32_t)(handler >> 32);
  gate->zero = 0;
}
static int same_idtr(const IDTR *a, const IDTR *b) {
  return a->limit == b->limit && a->base == b->base;
}
STATUS EFIAPI efi_main(HANDLE self, SYSTEM *system) {
  BS *bs = system->bs;
  LOADED *own = 0, *loaded = 0;
  FS *fs = 0;
  FILE *root = 0;
  HANDLE child = 0;
  void *image = 0, *offset_data = 0, *idt_copy = 0;
  UINTN image_size = 0, offset_size = 0;
  STATUS status = 0;
  unsigned error = 0;
  IDTR original = {0}, temporary = {0}, observed = {0};
  int idt_installed = 0;
  int cleanup_bad = 0;
  status = bs->locate_protocol(&serial_guid, 0, (void **)&uart);
  if (ERROR(status) || !uart)
    return EFI_ERROR(3);
  serial("BOOTSTRAP_C_TEST A6_UD2_IRETQ\r\n");
  status = bs->handle_protocol(self, &loaded_guid, (void **)&own);
  if (ERROR(status) || !own) {
    error = 1;
    goto finish;
  }
  status = bs->handle_protocol(own->device, &fs_guid, (void **)&fs);
  if (ERROR(status) || !fs) {
    error = 2;
    goto finish;
  }
  status = fs->open_volume(fs, &root);
  if (ERROR(status) || !root) {
    error = 3;
    goto finish;
  }
  status =
      read_file(bs, root, image_path, 16 * 1024 * 1024, &image, &image_size);
  if (ERROR(status)) {
    error = 4;
    goto finish;
  }
  status = read_file(bs, root, offset_path, 8, &offset_data, &offset_size);
  if (ERROR(status) || offset_size != 8) {
    error = 5;
    goto finish;
  }
  U64 stub_rva = 0;
  for (unsigned i = 0; i < 8; i++)
    stub_rva |= (U64)((uint8_t *)offset_data)[i] << (8 * i);
  status = bs->load_image(0, self, 0, image, image_size, &child);
  if (ERROR(status) || !child) {
    error = 6;
    goto finish;
  }
  status = bs->handle_protocol(child, &loaded_guid, (void **)&loaded);
  if (ERROR(status) || !loaded || !loaded->base || stub_rva >= loaded->size) {
    error = 7;
    goto finish;
  }
  ((DESC_FN)(uintptr_t)sidt_code)(&original);
  U64 table_size = (U64)original.limit + 1;
  if (!original.base || table_size < 7 * sizeof(GATE) ||
      table_size > 1024 * 1024) {
    error = 8;
    goto finish;
  }
  status = bs->allocate_pool(2, (UINTN)table_size, &idt_copy);
  if (ERROR(status) || !idt_copy) {
    error = 9;
    goto finish;
  }
  bytes_copy(idt_copy, (const void *)(uintptr_t)original.base,
             (UINTN)table_size);
  U64 handler = (U64)(uintptr_t)loaded->base + stub_rva;
  uint16_t selector = (uint16_t)((READ_FN)(uintptr_t)cs_code)();
  make_gate(&((GATE *)idt_copy)[6], handler, selector);
  temporary.limit = original.limit;
  temporary.base = (U64)(uintptr_t)idt_copy;
  U64 flags_before = ((READ_FN)(uintptr_t)flags_code)();
  ((DESC_FN)(uintptr_t)lidt_code)(&temporary);
  idt_installed = 1;
  ((DESC_FN)(uintptr_t)sidt_code)(&observed);
  if (!same_idtr(&observed, &temporary)) {
    error = 10;
    goto finish;
  }
  U64 flags_delta = ((READ_FN)(uintptr_t)ud2_code)();
  ((DESC_FN)(uintptr_t)lidt_code)(&original);
  idt_installed = 0;
  ((DESC_FN)(uintptr_t)sidt_code)(&observed);
  if (!same_idtr(&observed, &original)) {
    error = 11;
    goto finish;
  }
  U64 flags_after = ((READ_FN)(uintptr_t)flags_code)();
  serial("NG_A6_UD2 frame_handler=");
  hex(handler);
  serial(" flags_before=");
  hex(flags_before);
  serial(" flags_after=");
  hex(flags_after);
  serial("\r\n");
  if (flags_delta || ((flags_before ^ flags_after) & UINT64_C(0x200))) {
    error = 12;
    goto finish;
  }
finish:
  if (idt_installed) {
    ((DESC_FN)(uintptr_t)lidt_code)(&original);
    ((DESC_FN)(uintptr_t)sidt_code)(&observed);
    if (!same_idtr(&observed, &original))
      cleanup_bad = 1;
  }
  if (idt_copy && ERROR(bs->free_pool(idt_copy)))
    cleanup_bad = 1;
  if (child && ERROR(bs->unload_image(child)))
    cleanup_bad = 1;
  if (offset_data && ERROR(bs->free_pool(offset_data)))
    cleanup_bad = 1;
  if (image && ERROR(bs->free_pool(image)))
    cleanup_bad = 1;
  if (root && ERROR(root->close(root)))
    cleanup_bad = 1;
  if (cleanup_bad && !error)
    error = 20;
  if (error) {
    serial("NG_A6_UD2_IRETQ_BAD code=");
    hex(error);
    serial(" status=");
    hex(status);
    serial("\r\n");
    return EFI_ERROR(1);
  }
  serial("NG_A6_UD2_IRETQ_OK\r\n");
  return 0;
}
