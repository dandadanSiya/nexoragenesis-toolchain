#ifndef NG_NOVA_BOOTSTRAP_H
#define NG_NOVA_BOOTSTRAP_H
#include "ntasm.h"
/* BOOTSTRAP_EXTERNAL_C: Nova source is parsed/type-checked here and lowered
 * directly to the shared x64 encoder. GCC/Clang never translate Nova input.
 * Language ABI 1: u8/u64/bool values use ms_abi scalar slots. buf<u8>
 * parameters use two slots (pointer, length); at most four slots total.
 * Index violations return NOVA_BOUNDS_ERROR before any access.
 */
#define NOVA_BOUNDS_ERROR UINT64_C(0x8000000400000000)
int nova_compile(const char *source, size_t length, NtArtifact *out);
int nova_compile_many(const NtFInput *sources, size_t count, NtArtifact *out);
/* Safe execution ABI 2; legacy functions above remain for v1 proof replay. */
int nova_compile_v2(const char *source, size_t length, NtArtifact *out);
int nova_compile_many_v2(const NtFInput *sources, size_t count,
                         NtArtifact *out);
int nova_check_many_v2(const NtFInput *sources, size_t count,
                       NtDiagnostic *error);
#endif
