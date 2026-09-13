#ifndef GENESIS_NTASM_CONFORMANCE_H
#define GENESIS_NTASM_CONFORMANCE_H
#include "frontend.h"
/* Read-only function CFG proof, after name/type/instruction resolution.
 * Edges include lexical block exits, if merges, local branches and backedges.
 * Must-defined flags and locals are intersected at every merge until a fixed
 * point. Unreachable instructions never establish a fact. Arithmetic lowering
 * temporaries are transparent; actual callee flag clobbers invalidate facts.
 * Returns1 only when every reachable flag/local read is defined and no path
 * falls out of the function. Infinite loops are allowed: this is safety, not
 * a termination proof. The source function's never rule is independently
 * checked. On failure returns0 and fills one positioned diagnostic. No input
 * mutation. Analysis allocation is bounded to64MiB; overflow/budget exhaustion
 * is explicit.
 */
int nt_conformance_verify(const NtFProgram *program, NtFId function,
                          NtFDiagnostic *diagnostic);
uint64_t nt_conformance_output_registers(const NtFProgram *program,
                                         NtFId function);
/* Performs the same proof and records proven pointer types on register-valued
 * expression nodes. Failed programs must still not be emitted. */
int nt_conformance_verify_typed(NtFProgram *program, NtFId function,
                                NtFDiagnostic *diagnostic);
#endif
