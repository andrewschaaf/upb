/*
 * upb - a minimalist implementation of protocol buffers.
 *
 * Copyright (c) 2011 Google Inc.  See LICENSE for details.
 * Author: Josh Haberman <jhaberman@gmail.com>
 *
 * A number of routines for varint decoding (we keep them all around to have
 * multiple approaches available for benchmarking).  All of these functions
 * require the buffer to have at least 10 bytes available; if we don't know
 * for sure that there are 10 bytes, then there is only one viable option
 * (branching on every byte).
 */

#ifndef UPB_VARINT_DECODER_H_
#define UPB_VARINT_DECODER_H_

#include "upb.h"
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// All decoding functions return this struct by value.
typedef struct {
  const char *p;  // NULL if the varint was unterminated.
  uint64_t val;
} upb_decoderet;

// A basic branch-based decoder, uses 32-bit values to get good performance
// on 32-bit architectures (but performs well on 64-bits also).
INLINE upb_decoderet upb_vdecode_branch32(const char *p) {
  upb_decoderet r = {NULL, 0};
  uint32_t low, high = 0;
  uint32_t b;
  b = *(p++); low   = (b & 0x7f)      ; if(!(b & 0x80)) goto done;
  b = *(p++); low  |= (b & 0x7f) <<  7; if(!(b & 0x80)) goto done;
  b = *(p++); low  |= (b & 0x7f) << 14; if(!(b & 0x80)) goto done;
  b = *(p++); low  |= (b & 0x7f) << 21; if(!(b & 0x80)) goto done;
  b = *(p++); low  |= (b & 0x7f) << 28;
              high  = (b & 0x7f) >>  4; if(!(b & 0x80)) goto done;
  b = *(p++); high |= (b & 0x7f) <<  3; if(!(b & 0x80)) goto done;
  b = *(p++); high |= (b & 0x7f) << 10; if(!(b & 0x80)) goto done;
  b = *(p++); high |= (b & 0x7f) << 17; if(!(b & 0x80)) goto done;
  b = *(p++); high |= (b & 0x7f) << 24; if(!(b & 0x80)) goto done;
  b = *(p++); high |= (b & 0x7f) << 31; if(!(b & 0x80)) goto done;
  return r;

done:
  r.val = ((uint64_t)high << 32) | low;
  r.p = p;
  return r;
}

// Like the previous, but uses 64-bit values.
INLINE upb_decoderet upb_vdecode_branch64(const char *p) {
  uint64_t val;
  uint64_t b;
  upb_decoderet r = {(void*)0, 0};
  b = *(p++); val  = (b & 0x7f)      ; if(!(b & 0x80)) goto done;
  b = *(p++); val |= (b & 0x7f) <<  7; if(!(b & 0x80)) goto done;
  b = *(p++); val |= (b & 0x7f) << 14; if(!(b & 0x80)) goto done;
  b = *(p++); val |= (b & 0x7f) << 21; if(!(b & 0x80)) goto done;
  b = *(p++); val |= (b & 0x7f) << 28; if(!(b & 0x80)) goto done;
  b = *(p++); val |= (b & 0x7f) << 35; if(!(b & 0x80)) goto done;
  b = *(p++); val |= (b & 0x7f) << 42; if(!(b & 0x80)) goto done;
  b = *(p++); val |= (b & 0x7f) << 49; if(!(b & 0x80)) goto done;
  b = *(p++); val |= (b & 0x7f) << 56; if(!(b & 0x80)) goto done;
  b = *(p++); val |= (b & 0x7f) << 63; if(!(b & 0x80)) goto done;
  return r;

done:
  r.val = val;
  r.p = p;
  return r;
}

// Decodes a varint of at most 8 bytes without branching (except for error).
INLINE upb_decoderet upb_vdecode_max8_wright(upb_decoderet r) {
  uint64_t b;
  memcpy(&b, r.p, sizeof(b));
  uint64_t cbits = b | 0x7f7f7f7f7f7f7f7fULL;
  uint64_t stop_bit = ~cbits & (cbits+1);
  b &= (stop_bit - 1);
  b = ((b & 0x7f007f007f007f00) >> 1) | (b & 0x007f007f007f007f);
  b = ((b & 0xffff0000ffff0000) >> 2) | (b & 0x0000ffff0000ffff);
  b = ((b & 0xffffffff00000000) >> 4) | (b & 0x00000000ffffffff);
  if (stop_bit == 0) {
    // Error: unterminated varint.
    upb_decoderet err_r = {(void*)0, 0};
    return err_r;
  }
  upb_decoderet my_r = {r.p + ((__builtin_ctzll(stop_bit) + 1) / 8),
                        r.val | (b << 14)};
  return my_r;
}

// Another implementation of the previous.
INLINE upb_decoderet upb_vdecode_max8_massimino(upb_decoderet r) {
  uint64_t b;
  memcpy(&b, r.p, sizeof(b));
  uint64_t cbits = b | 0x7f7f7f7f7f7f7f7fULL;
  uint64_t stop_bit = ~cbits & (cbits + 1);
  b =  (b & 0x7f7f7f7f7f7f7f7fULL) & (stop_bit - 1);
  b +=       b & 0x007f007f007f007fULL;
  b +=  3 * (b & 0x0000ffff0000ffffULL);
  b += 15 * (b & 0x00000000ffffffffULL);
  if (stop_bit == 0) {
    // Error: unterminated varint.
    upb_decoderet err_r = {(void*)0, 0};
    return err_r;
  }
  upb_decoderet my_r = {r.p + ((__builtin_ctzll(stop_bit) + 1) / 8),
                        r.val | (b << 7)};
  return my_r;
}

// Template for a function that checks the first two bytes with branching
// and dispatches 2-10 bytes with a separate function.
#define UPB_VARINT_DECODER_CHECK2(name, decode_max8_function)            \
INLINE upb_decoderet upb_vdecode_check2_ ## name(const char *p) {        \
  uint64_t b = 0;                                                        \
  upb_decoderet r = {p, 0};                                              \
  memcpy(&b, r.p, 2);                                                    \
  if ((b & 0x80) == 0) { r.val = (b & 0x7f); r.p = p + 1; return r; }    \
  r.val = (b & 0x7f) | ((b & 0x7f00) >> 1);                              \
  r.p = p + 2;                                                           \
  if ((b & 0x8000) == 0) return r;                                       \
  return decode_max8_function(r);                                        \
}

UPB_VARINT_DECODER_CHECK2(wright, upb_vdecode_max8_wright);
UPB_VARINT_DECODER_CHECK2(massimino, upb_vdecode_max8_massimino);
#undef UPB_VARINT_DECODER_CHECK2

// Our canonical functions for decoding varints, based on the currently
// favored best-performing implementations.
INLINE upb_decoderet upb_vdecode_fast(const char *p) {
  // Use nobranch2 on 64-bit, branch32 on 32-bit.
  if (sizeof(long) == 8)
    return upb_vdecode_check2_massimino(p);
  else
    return upb_vdecode_branch32(p);
}

INLINE upb_decoderet upb_vdecode_max8_fast(upb_decoderet r) {
  return upb_vdecode_max8_massimino(r);
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* UPB_VARINT_DECODER_H_ */
