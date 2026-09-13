#include "pe_validate.h"
static uint16_t r16(const uint8_t *p) { return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
static uint32_t r32(const uint8_t *p) { return (uint32_t)r16(p)|((uint32_t)r16(p+2)<<16); }
static int range(size_t at,size_t n,size_t bound) { return at<=bound && n<=bound-at; }
int ng_pe_validate(const uint8_t *bytes, size_t length, int mapped,
                   uint32_t *entry_rva) {
    uint32_t nt, image, headers, entry; uint16_t count,opt; size_t sections;
    unsigned i,j; int entry_ok=0;
    *entry_rva=0;
    if (!bytes || length<64) return NG_PE_SHORT;
    if (r16(bytes)!=0x5a4d) return NG_PE_SIGNATURE;
    nt=r32(bytes+60);
    if (nt<64 || !range(nt,24,length)) return NG_PE_HEADERS;
    if (r32(bytes+nt)!=0x4550) return NG_PE_SIGNATURE;
    count=r16(bytes+nt+6); opt=r16(bytes+nt+20);
    if (!count || count>96 || opt<112 || !range((size_t)nt+24,opt,length)) return NG_PE_HEADERS;
    if (r16(bytes+nt+4)!=0x8664 || r16(bytes+nt+24)!=0x20b || r16(bytes+nt+24+68)!=10) return NG_PE_MACHINE;
    sections=(size_t)nt+24+opt; image=r32(bytes+nt+24+56);
    headers=r32(bytes+nt+24+60);entry=r32(bytes+nt+24+16);
    if (!range(sections,(size_t)count*40,length) || !range(sections,(size_t)count*40,headers) || headers>length) return NG_PE_HEADERS;
    if (!image || image>64*1024*1024 || headers>image || (mapped && image>length)) return NG_PE_RANGE;
    for(i=0;i<count;i++) {
        const uint8_t *s=bytes+sections+(size_t)i*40;
        uint32_t vs=r32(s+8),va=r32(s+12),rs=r32(s+16),raw=r32(s+20),flags=r32(s+36);
        uint32_t span=vs>rs?vs:rs;
        if ((span && va<headers) || !range(va,span,image) || (!mapped && rs && !range(raw,rs,length))) return NG_PE_RANGE;
        for(j=0;j<i;j++) {
            const uint8_t *t=bytes+sections+(size_t)j*40;
            uint32_t ts=r32(t+8),tr=r32(t+16),tv=r32(t+12); if (tr>ts)ts=tr;
            if (span && ts && (uint64_t)va<(uint64_t)tv+ts && (uint64_t)tv<(uint64_t)va+span) return NG_PE_SECTION;
        }
        if(entry>=va && entry-va<vs && (flags&0xe0000000)==0x60000000 && entry-va<rs) entry_ok=1;
    }
    if(!entry_ok) return NG_PE_ENTRY;
    *entry_rva=entry; return NG_PE_OK;
}
