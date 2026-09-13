#ifndef NG_NOVA_UEFI_PLATFORM_H
#define NG_NOVA_UEFI_PLATFORM_H
/* BOOTSTRAP_C_TEST: QEMU/OVMF adapter only, no final OS dependency. */
#include <stddef.h>
#include <stdint.h>
#define EFIAPI __attribute__((ms_abi))
typedef uint64_t EFI_STATUS;
typedef void *EFI_HANDLE;
typedef uint16_t CHAR16;
#define EFI_ERROR(s) (((s) >> 63) != 0)
#define EFI_FAILURE(n) (UINT64_C(0x8000000000000000) | (n))
typedef struct {
  uint32_t a;
  uint16_t b, c;
  uint8_t d[8];
} EFI_GUID;
typedef struct {
  uint64_t signature;
  uint32_t revision, size, crc, reserved;
} EFI_HEADER;
typedef struct EFI_FILE EFI_FILE;
struct EFI_FILE {
  uint64_t revision;
  EFI_STATUS(EFIAPI *open)(EFI_FILE *, EFI_FILE **, CHAR16 *, uint64_t,
                           uint64_t);
  EFI_STATUS(EFIAPI *close)(EFI_FILE *);
  void *remove;
  EFI_STATUS(EFIAPI *read)(EFI_FILE *, uint64_t *, void *);
  EFI_STATUS(EFIAPI *write)(EFI_FILE *, uint64_t *, const void *);
  EFI_STATUS(EFIAPI *get_position)(EFI_FILE *, uint64_t *);
  EFI_STATUS(EFIAPI *set_position)(EFI_FILE *, uint64_t);
  void *get_info, *set_info, *flush;
};
typedef struct {
  uint64_t revision;
  EFI_STATUS(EFIAPI *open_volume)(void *, EFI_FILE **);
} EFI_FS;
typedef struct {
  uint32_t revision;
  EFI_HANDLE parent;
  void *system;
  EFI_HANDLE device;
  void *path, *reserved;
  uint32_t options_size;
  void *options, *base;
  uint64_t size;
  uint32_t code_type, data_type;
  void *unload;
} EFI_LOADED;
typedef struct {
  EFI_HEADER header;
  void *raise_tpl, *restore_tpl, *allocate_pages, *free_pages, *get_memory_map;
  EFI_STATUS(EFIAPI *allocate_pool)(uint32_t, uint64_t, void **);
  EFI_STATUS(EFIAPI *free_pool)(void *);
  void *create_event, *set_timer, *wait_event, *signal_event, *close_event,
      *check_event, *install_protocol, *reinstall_protocol, *uninstall_protocol;
  EFI_STATUS(EFIAPI *handle_protocol)(EFI_HANDLE, EFI_GUID *, void **);
  void *reserved, *register_protocol_notify, *locate_handle,
      *locate_device_path, *install_config_table;
  EFI_STATUS(EFIAPI *load_image)(uint8_t, EFI_HANDLE, void *, void *, uint64_t,
                                 EFI_HANDLE *);
  void *start_image, *exit_image;
  EFI_STATUS(EFIAPI *unload_image)(EFI_HANDLE);
  void *exit_boot_services, *get_monotonic, *stall, *set_watchdog,
      *connect_controller, *disconnect_controller, *open_protocol,
      *close_protocol, *open_protocol_info, *protocols_per_handle,
      *locate_handle_buffer;
  EFI_STATUS(EFIAPI *locate_protocol)(EFI_GUID *, void *, void **);
} EFI_BS;
typedef struct {
  EFI_HEADER header;
  CHAR16 *vendor;
  uint32_t revision;
  EFI_HANDLE con_in_handle;
  void *con_in;
  EFI_HANDLE con_out_handle;
  void *con_out;
  EFI_HANDLE stderr_handle;
  void *stderr_interface;
  void *runtime;
  EFI_BS *bs;
} EFI_SYSTEM;
typedef struct {
  uint32_t revision;
  void *reset, *set_attributes, *set_control, *get_control;
  EFI_STATUS(EFIAPI *write)(void *, uint64_t *, const void *);
} EFI_SERIAL;
_Static_assert(offsetof(EFI_SYSTEM, bs) == 96, "UEFI system ABI");
_Static_assert(offsetof(EFI_BS, handle_protocol) == 152, "UEFI protocol ABI");
_Static_assert(offsetof(EFI_BS, load_image) == 200, "UEFI loader ABI");
_Static_assert(offsetof(EFI_BS, locate_protocol) == 320, "UEFI locate ABI");
typedef struct {
  EFI_FILE *handle;
  int error;
} FILE;
extern FILE *stderr;
extern uint64_t nova_uefi_platform_error, nova_uefi_pools, nova_uefi_files;
void nova_uefi_init(EFI_BS *, EFI_FILE *, EFI_SERIAL *);
void nova_uefi_log(const char *);
void nova_uefi_hex(uint64_t);
void *malloc(size_t);
void *calloc(size_t, size_t);
void free(void *);
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
void *memchr(const void *, int, size_t);
FILE *fopen(const char *, const char *);
int fclose(FILE *);
size_t fread(void *, size_t, size_t, FILE *);
size_t fwrite(const void *, size_t, size_t, FILE *);
int ferror(FILE *);
int fseek(FILE *, long, int);
long ftell(FILE *);
#define SEEK_SET 0
#define SEEK_END 2
#endif
