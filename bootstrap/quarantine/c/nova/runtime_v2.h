#ifndef NG_NOVA_RUNTIME_V2_H
#define NG_NOVA_RUNTIME_V2_H
#include "runtime.h"
typedef struct NovaContextV2 NovaContextV2;
typedef uint64_t(NOVA_ABI *NovaCallV2)(NovaContextV2 *, uint64_t, uint64_t,
                                       uint64_t);
typedef struct {
  uint64_t version, size;
  NovaCallV2 calls[12];
  void *state;
  /* Size-negotiated extension; original slots/state offset stay fixed. */
  NovaCallV2 copy_bytes;
} NovaRuntimeV2;
struct NovaContextV2 {
  uint64_t version, size, error, error_source, error_offset;
  NovaRuntimeV2 *runtime;
};
enum {
  NV2_BOUNDS = 4,
  NV2_DIVZERO = 5,
  NV2_RUNTIME = 6,
  NV2_LIFETIME = 7,
  NV2_IO = 8,
  NV2_READONLY = 9,
  NV2_ARGUMENT = 10,
  NV2_OVERFLOW = 11,
  NV2_SHIFT = 12
};
enum {
  NV2_ALLOC = 0,
  NV2_FREE = 1,
  NV2_OPEN_READ = 2,
  NV2_OPEN_NEW = 3,
  NV2_READ = 4,
  NV2_WRITE = 5,
  NV2_CLOSE = 6,
  NV2_DIAG = 7,
  NV2_RESOLVE = 8,
  NV2_SLICE = 9,
  NV2_LENGTH = 10,
  NV2_LITERAL = 11,
  NV2_COPY = 12
};
_Static_assert(sizeof(NovaContextV2) == 48, "Nova execution ABI 2");
_Static_assert(offsetof(NovaRuntimeV2, calls) == 16, "Nova callback ABI 2");
_Static_assert(offsetof(NovaRuntimeV2, state) == 112,
               "Nova ABI2 fixed state slot");
_Static_assert(offsetof(NovaRuntimeV2, copy_bytes) == 120,
               "Nova ABI2 copy extension");
/* Public compiled entry: u64 main(NovaContextV2*,const u64*flat_args,u64
 * count). R15 is private execution-context state inside generated Nova
 * functions. Buffer flat arguments are opaque generation-tagged handle,length
 * pairs. Borrowed host memory is registered explicitly and never freed by Nova.
 */
int nova_context_init_v2(NovaContextV2 *);
int nova_context_destroy_v2(NovaContextV2 *);
uint64_t nova_borrow_v2(NovaContextV2 *, void *, uint64_t, int writable);
void nova_error_clear_v2(NovaContextV2 *);
#endif
