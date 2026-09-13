#ifndef NG_NOVA_RUNTIME_V1_H
#define NG_NOVA_RUNTIME_V1_H
#include <stddef.h>
#include <stdint.h>
#if defined(__GNUC__) && defined(__x86_64__)
#define NOVA_ABI __attribute__((ms_abi))
#else
#define NOVA_ABI
#endif
/* BOOTSTRAP_EXTERNAL_C host adapter. The compiled Nova code computes its own
 * results; these callbacks only allocate, transport files, and report bytes.
 * All callbacks have four ms_abi slots: context,a,b,c. Value UINT64_MAX means
 * runtime failure, except allocation returns 0. File handles are generation
 * tagged. Creation never overwrites an existing path. No callback owns a
 * borrowed argument. Runtime destroy releases only resources it owns. */
typedef uint64_t(NOVA_ABI *NovaRuntimeCall)(void *, uint64_t, uint64_t,
                                            uint64_t);
typedef struct {
  uint64_t version, size;
  void *context;
  NovaRuntimeCall allocate, release, open_read, open_new, read, write, close,
      diagnostic;
} NovaRuntimeV1;
_Static_assert(offsetof(NovaRuntimeV1, allocate) == 24, "Nova runtime layout");
_Static_assert(sizeof(NovaRuntimeV1) == 88, "Nova runtime v1 size");
int nova_runtime_init(NovaRuntimeV1 *runtime);
int nova_runtime_destroy(NovaRuntimeV1 *runtime);
#endif
