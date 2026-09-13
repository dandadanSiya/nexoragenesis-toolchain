#ifndef NG_NOVA_RUN_HOST_H
#define NG_NOVA_RUN_HOST_H
#include "../nova.h"
#include "runtime_v2.h"
typedef struct {
  void *memory;
  size_t size;
  uint64_t(NOVA_ABI *entry)(NovaContextV2 *, const uint64_t *, uint64_t);
} NovaHostImage;
int nova_host_map(const NtArtifact *, NovaHostImage *);
void nova_host_unmap(NovaHostImage *);
#endif
