/*
Copyright (c) 2000-2004, 2007, 2009 Tony Garnock-Jones <tonyg@kcbbs.gen.nz>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <assert.h>
#include <stdarg.h>

#include "extprot.h"

char const *extprot_version(void) {
  return EXTPROT_VERSION; // defined in makefile.
}

void init_extprot_pool(Extprot_Pool *pool, size_t pagesize) {
  pool->root = NULL;

  pool->num_blocks = 0;
  pool->blocklist = NULL;

  pool->pagesize = pagesize ? pagesize : 4096;
  pool->alloc_block = NULL;
  pool->alloc_used = 0;

#ifndef EXTPROT_NO_BIGNUMS
  pool->bignum_chain = NULL;
#endif
}

void empty_extprot_pool(Extprot_Pool *pool) {
  int i;

  pool->root = NULL;

#ifndef EXTPROT_NO_BIGNUMS
  {
    Extprot_Object *p = pool->bignum_chain;
    while (p != NULL) {
      mpz_clear(p->body.vint.value);
      p = p->body.vint.chain;
    }
  }
#endif

  for (i = 0; i < pool->num_blocks; i++) {
    free(pool->blocklist[i]);
  }
  if (pool->blocklist != NULL) {
    free(pool->blocklist);
  }
  pool->num_blocks = 0;
  pool->blocklist = NULL;

  if (pool->alloc_block != NULL) {
    free(pool->alloc_block);
  }
  pool->alloc_block = NULL;
  pool->alloc_used = 0;
}

static void record_pool_block(Extprot_Pool *pool, void *block) {
  size_t blocklistlength = sizeof(void *) * (pool->num_blocks + 1);

  if (pool->blocklist == NULL) {
    pool->blocklist = malloc(blocklistlength);
  } else {
    pool->blocklist = realloc(pool->blocklist, blocklistlength);
  }

  pool->blocklist[pool->num_blocks] = block;
  pool->num_blocks++;
}

void *extprot_pool_alloc(Extprot_Pool *pool, size_t amount) {
  if (amount == 0) {
    return NULL;
  }

  amount = (amount + 7) & (~7); /* round up to nearest 8-byte boundary */

  if (amount > (pool->pagesize >> 1)) {
    void *result = calloc(1, amount);
    record_pool_block(pool, result);
    return result;
  }

  if (pool->alloc_block != NULL) {
    assert(pool->alloc_used <= pool->pagesize);

    if (pool->alloc_used + amount <= pool->pagesize) {
      void *result = pool->alloc_block + pool->alloc_used;
      pool->alloc_used += amount;
      return result;
    }

    record_pool_block(pool, pool->alloc_block);
  }

  pool->alloc_block = calloc(1, pool->pagesize);
  pool->alloc_used = amount;
  return pool->alloc_block;
}

char const *extprot_error_message(Extprot_Error error) {
  static char err_buf[1024];
  switch (error) {
    case Extprot_NoError: return "No error";
    case Extprot_EarlyEOF: return "Unexpected EOF";
    case Extprot_VintOverflow: return "vint value overflowed 64 bits";
    case Extprot_SizeTOverflow: return "vint value overflowed size_t";
    case Extprot_InvalidTag: return "Invalid tag";
    default:
      sprintf(err_buf, "Unknown error (%d)", (int) error);
      return err_buf;
  }
}

#define ALLOCO(wire_type)						\
  Extprot_Object *o = extprot_pool_alloc(pool, sizeof(Extprot_Object)); \
  o->kind = (tag << 4) | (wire_type);

#ifndef EXTPROT_NO_BIGNUMS
Extprot_Object *extprot_vint(Extprot_Pool *pool, Extprot_Tag tag) {
  ALLOCO(EXTPROT_VINT);
  o->body.vint.chain = pool->bignum_chain;
  mpz_init(o->body.vint.value);
  pool->bignum_chain = o;
  return o;
}
#else
Extprot_Object *extprot_vint(Extprot_Pool *pool, Extprot_Tag tag, int64_t num) {
  ALLOCO(EXTPROT_VINT);
  o->body.bits64_long = num;
  return o;
}
#endif

Extprot_Object *extprot_bits8(Extprot_Pool *pool, Extprot_Tag tag, uint8_t num) {
  ALLOCO(EXTPROT_BITS8);
  o->body.bits8 = num;
  return o;
}

Extprot_Object *extprot_bits32(Extprot_Pool *pool, Extprot_Tag tag, uint32_t num) {
  ALLOCO(EXTPROT_BITS32);
  o->body.bits32 = num;
  return o;
}

Extprot_Object *extprot_bits64_long(Extprot_Pool *pool, Extprot_Tag tag, int64_t num) {
  ALLOCO(EXTPROT_BITS64_LONG);
  o->body.bits64_long = num;
  return o;
}

Extprot_Object *extprot_bits64_float(Extprot_Pool *pool, Extprot_Tag tag, double num) {
  ALLOCO(EXTPROT_BITS64_FLOAT);
  o->body.bits64_float = num;
  return o;
}

Extprot_Object *extprot_enum(Extprot_Pool *pool, Extprot_Tag tag) {
  ALLOCO(EXTPROT_ENUM);
  return o;
}

Extprot_Object *extprot_tuple_init(Extprot_Pool *pool, Extprot_Tag tag, size_t len, ...) {
  va_list vl;
  size_t i;
  Extprot_Object *o = extprot_pool_alloc(pool, sizeof(Extprot_Object) + len * sizeof(Extprot_Object *));
  o->kind = (tag << 4) | EXTPROT_TUPLE;
  o->body.tuple.length = len;

  va_start(vl, len);
  for (i = 0; i < len; i++) {
    o->body.tuple.vec[i] = va_arg(vl, Extprot_Object *);
  }
  va_end(vl);

  return o;
}

Extprot_Object *extprot_tuple(Extprot_Pool *pool, Extprot_Tag tag, size_t len) {
  Extprot_Object *o = extprot_pool_alloc(pool, sizeof(Extprot_Object) + len * sizeof(Extprot_Object *));
  o->kind = (tag << 4) | EXTPROT_TUPLE;
  o->body.tuple.length = len;
  return o;
}

Extprot_Object *extprot_htuple_init(Extprot_Pool *pool, Extprot_Tag tag, size_t len, ...) {
  va_list vl;
  size_t i;
  Extprot_Object *o = extprot_pool_alloc(pool, sizeof(Extprot_Object) + len * sizeof(Extprot_Object *));
  o->kind = (tag << 4) | EXTPROT_HTUPLE;
  o->body.tuple.length = len;

  va_start(vl, len);
  for (i = 0; i < len; i++) {
    o->body.tuple.vec[i] = va_arg(vl, Extprot_Object *);
  }
  va_end(vl);

  return o;
}

Extprot_Object *extprot_htuple(Extprot_Pool *pool, Extprot_Tag tag, size_t len) {
  Extprot_Object *o = extprot_pool_alloc(pool, sizeof(Extprot_Object) + len * sizeof(Extprot_Object *));
  o->kind = (tag << 4) | EXTPROT_HTUPLE;
  o->body.tuple.length = len;
  return o;
}

Extprot_Object *extprot_cstring(Extprot_Pool *pool, Extprot_Tag tag, char const *str) {
  size_t len = strlen(str);
  Extprot_Object *o = extprot_pool_alloc(pool, sizeof(Extprot_Object) + len + 1);
  o->kind = (tag << 4) | EXTPROT_BYTES;
  o->body.bytes.length = len;
  memcpy(o->body.bytes.vec, str, len);
  o->body.bytes.vec[len] = '\0';
  return o;
}

Extprot_Object *extprot_bytes(Extprot_Pool *pool, Extprot_Tag tag, void const *bin, size_t len) {
  Extprot_Object *o = extprot_pool_alloc(pool, sizeof(Extprot_Object) + len + 1);
  o->kind = (tag << 4) | EXTPROT_BYTES;
  o->body.bytes.length = len;
  memcpy(o->body.bytes.vec, bin, len);
  return o;
}

Extprot_Object *extprot_bytes_nocopy(Extprot_Pool *pool, Extprot_Tag tag, size_t len) {
  Extprot_Object *o = extprot_pool_alloc(pool, sizeof(Extprot_Object) + len + 1);
  o->kind = (tag << 4) | EXTPROT_BYTES;
  o->body.bytes.length = len;
  o->body.bytes.vec[len] = '\0';
  return o;
}

Extprot_Object *extprot_assoc_init(Extprot_Pool *pool, Extprot_Tag tag, size_t len, ...) {
  va_list vl;
  size_t i;
  Extprot_Object *o = extprot_pool_alloc(pool, sizeof(Extprot_Object) + 2 * len * sizeof(Extprot_Object *));
  o->kind = (tag << 4) | EXTPROT_ASSOC;
  o->body.tuple.length = len;

  va_start(vl, len);
  for (i = 0; i < len; i++) {
    o->body.tuple.vec[2 * i] = va_arg(vl, Extprot_Object *);
    o->body.tuple.vec[2 * i + 1] = va_arg(vl, Extprot_Object *);
  }
  va_end(vl);

  return o;
}

Extprot_Object *extprot_assoc(Extprot_Pool *pool, Extprot_Tag tag, size_t len) {
  Extprot_Object *o = extprot_pool_alloc(pool, sizeof(Extprot_Object) + 2 * len * sizeof(Extprot_Object *));
  o->kind = (tag << 4) | EXTPROT_ASSOC;
  o->body.tuple.length = len;
  return o;
}
