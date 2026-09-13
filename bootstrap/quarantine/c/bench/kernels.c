/* BOOTSTRAP_C_TEST: same algorithms as examples/nova-bootstrap/benchmarks.
 * Unsigned overflow is intentionally modulo 2^64 in both languages.
 * No LTO, calls live in a separate translation unit, results are consumed.
 * BENCH_CASE builds a single freestanding Windows PE entry for build timings.
 */
#include <stdint.h>
#ifndef BENCH_SUFFIX
#define BENCH_SUFFIX _o2
#endif
#define CAT_(a,b) a##b
#define CAT(a,b) CAT_(a,b)
#ifdef BENCH_CASE
#define NAME(n) bench_entry
#else
#define NAME(n) CAT(n,BENCH_SUFFIX)
#endif
#if !defined(BENCH_CASE) || BENCH_CASE == 1
uint64_t NAME(xorshift)(uint64_t n,uint64_t seed,const uint8_t *data,uint64_t len) {
  (void)data;(void)len;
  uint64_t x=seed;
  for(uint64_t i=0;i<n;i++){x^=x<<13;x^=x>>7;x^=x<<17;}
  return x;
}
#endif
#if !defined(BENCH_CASE) || BENCH_CASE == 2
uint64_t NAME(buffer_hash)(uint64_t n,uint64_t seed,const uint8_t *data,uint64_t len) {
  uint64_t sum=seed;
  for(uint64_t pass=0;pass<n;pass++)for(uint64_t i=0;i<len;i++)sum=(sum^data[i])*UINT64_C(1099511628211);
  return sum;
}
#endif
#if !defined(BENCH_CASE) || BENCH_CASE == 3
uint64_t NAME(decimal)(uint64_t n,uint64_t seed,const uint8_t *data,uint64_t len) {
  if(len<5120)return UINT64_MAX;
  uint64_t sum=seed;
  for(uint64_t row=0;row<n;row++){
    uint64_t offset=(row&255)*20,value=0;
    for(uint64_t j=0;j<20;j++){
      uint8_t ch=data[offset+j];
      if(ch<48 || ch>57)return UINT64_MAX;
      uint64_t digit=(uint64_t)ch-48;
      if(value>UINT64_C(1844674407370955161) || (value==UINT64_C(1844674407370955161) && digit>5))return UINT64_MAX-1;
      value=value*10+digit;
    }
    sum^=value+row;
  }
  return sum;
}
#endif
