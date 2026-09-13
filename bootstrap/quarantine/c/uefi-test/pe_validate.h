/* BOOTSTRAP_C_TEST: independent bounds checks for an untrusted PE consumer. */
#ifndef NG_PE_VALIDATE_H
#define NG_PE_VALIDATE_H
#include <stdint.h>
#include <stddef.h>
enum { NG_PE_OK=0, NG_PE_SHORT=1, NG_PE_SIGNATURE=2, NG_PE_MACHINE=3,
       NG_PE_HEADERS=4, NG_PE_RANGE=5, NG_PE_ENTRY=6, NG_PE_SECTION=7 };
int ng_pe_validate(const uint8_t *bytes, size_t length, int mapped,
                   uint32_t *entry_rva);
#endif
