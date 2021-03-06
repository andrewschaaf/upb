/*
 * upb - a minimalist implementation of protocol buffers.
 *
 * Copyright (c) 2010 Google Inc.  See LICENSE for details.
 * Author: Josh Haberman <jhaberman@gmail.com>
 */

#include "upb_string.h"

#include <stdlib.h>
#ifdef __GLIBC__
#include <malloc.h>
#elif defined(__APPLE__)
#include <malloc/malloc.h>
#endif

static uint32_t upb_round_up_pow2(uint32_t v) {
  // http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  v++;
  return v;
}

upb_string *upb_string_new() {
  upb_string *str = malloc(sizeof(*str));
  str->ptr = NULL;
  str->cached_mem = NULL;
  str->len = 0;
#ifndef UPB_HAVE_MSIZE
  str->size = 0;
#endif
  str->src = NULL;
  upb_atomic_refcount_init(&str->refcount, 1);
  return str;
}

uint32_t upb_string_size(upb_string *str) {
#ifdef __GLIBC__
  return malloc_usable_size(str->cached_mem);
#elif defined(__APPLE__)
  return malloc_size(str->cached_mem);
#else
  return str->size;
#endif
}

void _upb_string_free(upb_string *str) {
  free(str->cached_mem);
  _upb_string_release(str);
  free(str);
}

char *upb_string_getrwbuf(upb_string *str, upb_strlen_t len) {
  // assert(str->ptr == NULL);
  upb_strlen_t size = upb_string_size(str);
  if (size < len) {
    size = upb_round_up_pow2(len);
    str->cached_mem = realloc(str->cached_mem, size);
#ifndef UPB_HAVE_MSIZE
    str->size = size;
#endif
  }
  str->len = len;
  str->ptr = str->cached_mem;
  return str->cached_mem;
}

void upb_string_substr(upb_string *str, upb_string *target_str,
                       upb_strlen_t start, upb_strlen_t len) {
  assert(str->ptr == NULL);
  assert(start + len <= upb_string_len(target_str));
  if (target_str->src) {
    start += (target_str->ptr - target_str->src->ptr);
    target_str = target_str->src;
  }
  str->src = upb_string_getref(target_str);
  str->ptr = upb_string_getrobuf(target_str) + start;
  str->len = len;
}

size_t upb_string_vprintf_at(upb_string *str, size_t offset, const char *format,
                             va_list args) {
  // Try once without reallocating.  We have to va_copy because we might have
  // to call vsnprintf again.
  uint32_t size = UPB_MAX(upb_string_size(str) - offset, 16);
  char *buf = upb_string_getrwbuf(str, offset + size) + offset;
  va_list args_copy;
  va_copy(args_copy, args);
  uint32_t true_size = vsnprintf(buf, size, format, args_copy);
  va_end(args_copy);

  // Resize to be the correct size.
  if (true_size >= size) {
    // Need to print again, because some characters were truncated.  vsnprintf
    // has weird behavior (and contrary IMO to what the standard says): it will
    // not write the entire string unless you give it space to store the NULL
    // terminator also.  So we can't give it space for the string itself and
    // let NULL get truncated (after all, we don't care about it): we *must*
    // give it space for NULL.
    buf = upb_string_getrwbuf(str, offset + true_size + 1) + offset;
    vsnprintf(buf, true_size + 1, format, args);
  }
  str->len = offset + true_size;
  return true_size;
}

upb_string *upb_string_asprintf(const char *format, ...) {
  upb_string *str = upb_string_new();
  va_list args;
  va_start(args, format);
  upb_string_vprintf(str, format, args);
  va_end(args);
  return str;
}

upb_string *upb_strdup(upb_string *s) {
  upb_string *str = upb_string_new();
  upb_strcpy(str, s);
  return str;
}

void upb_strcat(upb_string *s, upb_string *append) {
  uint32_t old_size = upb_string_len(s);
  uint32_t append_size = upb_string_len(append);
  uint32_t new_size = old_size + append_size;
  char *buf = upb_string_getrwbuf(s, new_size);
  memcpy(buf + old_size, upb_string_getrobuf(append), append_size);
}

upb_string *upb_strreadfile(const char *filename) {
  FILE *f = fopen(filename, "rb");
  if(!f) return NULL;
  if(fseek(f, 0, SEEK_END) != 0) goto error;
  long size = ftell(f);
  if(size < 0) goto error;
  if(fseek(f, 0, SEEK_SET) != 0) goto error;
  upb_string *s = upb_string_new();
  char *buf = upb_string_getrwbuf(s, size);
  if(fread(buf, size, 1, f) != 1) goto error;
  fclose(f);
  return s;

error:
  fclose(f);
  return NULL;
}

upb_string *upb_emptystring() {
  static upb_string empty = UPB_STATIC_STRING("");
  return &empty;
}

char *upb_string_newcstr(upb_string *str) {
  upb_strlen_t len = upb_string_len(str);
  char *ret = malloc(len+1);
  memcpy(ret, upb_string_getrobuf(str), len);
  ret[len] = '\0';
  return ret;
}
