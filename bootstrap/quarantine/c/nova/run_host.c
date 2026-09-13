#define _GNU_SOURCE
#include "run_host.h"
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
static size_t page(size_t n) { return (n + 4095) & ~(size_t)4095; }
int nova_host_map(const NtArtifact *a, NovaHostImage *out) {
  memset(out, 0, sizeof *out);
  if (!a || a->error.code || !a->text.bytes || !a->text.size ||
      a->entry >= a->text.size || a->text.size > 16 * 1024 * 1024 ||
      a->rdata.size > 16 * 1024 * 1024 || a->data.size > 16 * 1024 * 1024)
    return 0;
  size_t ro = page(a->text.size), rw = page(ro + 8 + a->rdata.size),
         total = page(rw + (a->data.size ? a->data.size : 8));
  void *p;
#ifdef _WIN32
  p = VirtualAlloc(NULL, total, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (!p)
    return 0;
#else
  p = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1,
           0);
  if (p == MAP_FAILED)
    return 0;
#endif
  memcpy(p, a->text.bytes, a->text.size);
  uint64_t anchor = (uint64_t)(uintptr_t)((unsigned char *)p + a->entry);
  memcpy((unsigned char *)p + ro, &anchor, 8);
  if (a->rdata.size)
    memcpy((unsigned char *)p + ro + 8, a->rdata.bytes, a->rdata.size);
  if (a->data.size)
    memcpy((unsigned char *)p + rw, a->data.bytes, a->data.size);
  out->memory = p;
  out->size = total;
  out->entry = (uint64_t(NOVA_ABI *)(NovaContextV2 *, const uint64_t *,
                                     uint64_t))((unsigned char *)p + a->entry);
#ifdef _WIN32
  DWORD old;
  if (!VirtualProtect(p, ro, PAGE_EXECUTE_READ, &old) ||
      !VirtualProtect((unsigned char *)p + ro, rw - ro, PAGE_READONLY, &old) ||
      !FlushInstructionCache(GetCurrentProcess(), p, ro)) {
    nova_host_unmap(out);
    return 0;
  }
#else
  if (mprotect(p, ro, PROT_READ | PROT_EXEC) ||
      mprotect((unsigned char *)p + ro, rw - ro, PROT_READ)) {
    nova_host_unmap(out);
    return 0;
  }
#endif
  return 1;
}
void nova_host_unmap(NovaHostImage *p) {
  if (p && p->memory) {
#ifdef _WIN32
    VirtualFree(p->memory, 0, MEM_RELEASE);
#else
    munmap(p->memory, p->size);
#endif
  }
  if (p)
    memset(p, 0, sizeof *p);
}
