/* BOOTSTRAP_C_TEST: consumer of actual compiled Nova ABI2 PE, QEMU only. */
#include "../uefi-test/pe_validate.h"
#include "runtime_v2.h"
#include "uefi_platform.h"
static EFI_GUID loaded_guid = {
    0x5b1b31a1, 0x9562, 0x11d2, {0x8e, 0x3f, 0, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static EFI_GUID fs_guid = {
    0x964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static EFI_GUID serial_guid = {
    0xbb25cf6f, 0xf1d4, 0x11d2, {0x9a, 0x0c, 0, 0x90, 0x27, 0x3f, 0xc1, 0xfd}};
static void *read_file(const char *name, size_t limit, size_t *out) {
  FILE *f = fopen(name, "rb");
  if (!f)
    return NULL;
  void *p = NULL;
  if (fseek(f, 0, SEEK_END))
    goto done;
  long n = ftell(f);
  if (n <= 0 || (size_t)n > limit || fseek(f, 0, SEEK_SET))
    goto done;
  p = malloc((size_t)n);
  if (p && fread(p, 1, (size_t)n, f) == (size_t)n && !ferror(f))
    *out = (size_t)n;
  else {
    free(p);
    p = NULL;
  }
done:
  if (fclose(f)) {
    free(p);
    p = NULL;
  }
  return p;
}
static uint64_t le64(const unsigned char *p) {
  uint64_t n = 0;
  for (unsigned i = 0; i < 8; i++)
    n |= (uint64_t)p[i] << (8 * i);
  return n;
}
EFI_STATUS EFIAPI efi_main(EFI_HANDLE self, EFI_SYSTEM *system) {
  EFI_BS *bs = system->bs;
  EFI_SERIAL *serial = NULL;
  EFI_LOADED *own = NULL, *loaded = NULL;
  EFI_FS *fs = NULL;
  EFI_FILE *root = NULL;
  EFI_HANDLE child = NULL;
  void *image = NULL, *oracle = NULL;
  size_t image_size = 0, oracle_size = 0;
  unsigned code = 0;
  uint32_t entry = 0;
  uint64_t result = 0, error = 0, expected = 0, expected_error = 0;
  NovaContextV2 context = {0};
  EFI_STATUS st = bs->locate_protocol(&serial_guid, NULL, (void **)&serial);
  if (EFI_ERROR(st) || !serial)
    return EFI_FAILURE(3);
  nova_uefi_init(bs, NULL, serial);
  nova_uefi_log("BOOTSTRAP_C_TEST NOVA_RUNTIME_ABI2\r\n");
  st = bs->handle_protocol(self, &loaded_guid, (void **)&own);
  if (EFI_ERROR(st) || !own) {
    code = 1;
    goto done;
  }
  st = bs->handle_protocol(own->device, &fs_guid, (void **)&fs);
  if (EFI_ERROR(st) || !fs) {
    code = 2;
    goto done;
  }
  st = fs->open_volume(fs, &root);
  if (EFI_ERROR(st) || !root) {
    code = 3;
    goto done;
  }
  nova_uefi_init(bs, root, serial);
  oracle = read_file("EXPECT.BIN", 16, &oracle_size);
  if (!oracle || oracle_size != 16) {
    code = 10;
    goto done;
  }
  expected = le64(oracle);
  expected_error = le64((unsigned char *)oracle + 8);
  image = read_file("NTRESULT.EFI", 16 * 1024 * 1024, &image_size);
  if (!image) {
    code = 11;
    goto done;
  }
  code = (unsigned)ng_pe_validate(image, image_size, 0, &entry);
  if (code) {
    code += 20;
    goto done;
  }
  st = bs->load_image(0, self, NULL, image, image_size, &child);
  if (EFI_ERROR(st) || !child) {
    code = 30;
    goto done;
  }
  st = bs->handle_protocol(child, &loaded_guid, (void **)&loaded);
  if (EFI_ERROR(st) || !loaded || !loaded->base ||
      loaded->size > 64 * 1024 * 1024) {
    code = 31;
    goto done;
  }
  code =
      (unsigned)ng_pe_validate(loaded->base, (size_t)loaded->size, 1, &entry);
  if (code) {
    code += 40;
    goto done;
  }
  if (!nova_context_init_v2(&context)) {
    code = 48;
    goto done;
  }
  nova_uefi_log("NG_NOVA_V2_LOADED base=");
  nova_uefi_hex((uint64_t)(uintptr_t)loaded->base);
  nova_uefi_log(" entry=");
  nova_uefi_hex(entry);
  nova_uefi_log("\r\n");
  result = ((uint64_t(EFIAPI *)(NovaContextV2 *, const uint64_t *, uint64_t))(
      (unsigned char *)loaded->base + entry))(&context, NULL, 0);
  error = context.error;
  nova_uefi_log("NG_NOVA_V2_RETURN value=");
  nova_uefi_hex(result);
  nova_uefi_log(" error=");
  nova_uefi_hex(error);
  nova_uefi_log("\r\n");
  if (result != expected)
    code = 50;
  else if (error != expected_error)
    code = 51;
done:
  if (context.runtime && !nova_context_destroy_v2(&context))
    code = 60;
  if (child && EFI_ERROR(bs->unload_image(child)))
    code = 60;
  free(image);
  free(oracle);
  if (root && EFI_ERROR(root->close(root)))
    code = 60;
  if (nova_uefi_platform_error || nova_uefi_pools || nova_uefi_files)
    code = 60;
  nova_uefi_log("NG_NOVA_V2_CLEANUP pools=");
  nova_uefi_hex(nova_uefi_pools);
  nova_uefi_log(" files=");
  nova_uefi_hex(nova_uefi_files);
  nova_uefi_log("\r\n");
  if (code) {
    nova_uefi_log("NG_NOVA_V2_RESULT_BAD code=");
    nova_uefi_hex(code);
    nova_uefi_log("\r\n");
    return EFI_FAILURE(1);
  }
  nova_uefi_log("NG_NOVA_V2_RESULT_OK\r\n");
  return 0;
}
