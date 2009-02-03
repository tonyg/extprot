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

#ifndef libextprot__extprot_h
#define libextprot__extprot_h

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#ifndef EXTPROT_NO_BIGNUMS
#include <gmp.h>
#endif

typedef uint32_t Extprot_Tag;

typedef enum Extprot_ObjectKind_ {
  EXTPROT_VINT = 0,
  EXTPROT_BITS8 = 2,
  EXTPROT_BITS32 = 4,
  EXTPROT_BITS64_LONG = 6,
  EXTPROT_BITS64_FLOAT = 8,
  EXTPROT_ENUM = 10,

  EXTPROT_TUPLE = 1,
  EXTPROT_BYTES = 3,
  EXTPROT_HTUPLE = 5,
  EXTPROT_ASSOC = 7
} Extprot_ObjectKind;

typedef struct Extprot_Object_ {
  uint32_t kind;
  union {
#ifndef EXTPROT_NO_BIGNUMS
    struct {
      struct Extprot_Object_ *chain;
      mpz_t value;
    } vint;
#else
    uint64_t vint;
#endif
    uint8_t bits8;
    uint32_t bits32;
    int64_t bits64_long;
    double bits64_float;

    struct {
      size_t length;
      struct Extprot_Object_ *vec[1];
    } tuple;
    struct {
      size_t length;
      uint8_t vec[1];
    } bytes;
  } body;
} Extprot_Object;

typedef struct Extprot_Pool_ {
  Extprot_Object *root;

  int num_blocks;
  void **blocklist;

  size_t pagesize;
  char *alloc_block;
  size_t alloc_used;

#ifndef EXTPROT_NO_BIGNUMS
  Extprot_Object *bignum_chain;
#endif
} Extprot_Pool;

typedef enum Extprot_Error_ {
  Extprot_NoError = 0,
  Extprot_EarlyEOF,
  Extprot_VintOverflow,
  Extprot_SizeTOverflow,
  Extprot_InvalidTag,

  Extprot_Error_MAX
} Extprot_Error;

extern char const *extprot_version(void);

extern void init_extprot_pool(Extprot_Pool *pool, size_t pagesize);
extern void empty_extprot_pool(Extprot_Pool *pool);

extern void *extprot_pool_alloc(Extprot_Pool *pool, size_t amount);

extern char const *extprot_error_message(Extprot_Error error);

extern Extprot_Error extprot_decode_header(void const *buffer,
					   size_t in_len,
					   uint32_t *tag_and_type,
					   size_t *total_len);
extern Extprot_Error extprot_decode(Extprot_Pool *pool,
				    void const *buffer,
				    size_t len);

extern size_t extprot_compute_length(Extprot_Object const *o);
extern void extprot_encode(Extprot_Object const *o, void *buffer);

#ifndef EXTPROT_NO_BIGNUMS
extern Extprot_Object *extprot_vint(Extprot_Pool *pool, Extprot_Tag tag);
#else
extern Extprot_Object *extprot_vint(Extprot_Pool *pool, Extprot_Tag tag, uint64_t num);
#endif
extern Extprot_Object *extprot_bits8(Extprot_Pool *pool, Extprot_Tag tag, uint8_t num);
extern Extprot_Object *extprot_bits32(Extprot_Pool *pool, Extprot_Tag tag, uint32_t num);
extern Extprot_Object *extprot_bits64_long(Extprot_Pool *pool, Extprot_Tag tag, int64_t num);
extern Extprot_Object *extprot_bits64_float(Extprot_Pool *pool, Extprot_Tag tag, double num);
extern Extprot_Object *extprot_enum(Extprot_Pool *pool, Extprot_Tag tag);

extern Extprot_Object *extprot_tuple_init(Extprot_Pool *pool, Extprot_Tag tag, size_t len, ...);
extern Extprot_Object *extprot_tuple(Extprot_Pool *pool, Extprot_Tag tag, size_t len);
extern Extprot_Object *extprot_htuple_init(Extprot_Pool *pool, Extprot_Tag tag, size_t len, ...);
extern Extprot_Object *extprot_htuple(Extprot_Pool *pool, Extprot_Tag tag, size_t len);
extern Extprot_Object *extprot_cstring(Extprot_Pool *pool, Extprot_Tag tag, char const *str);
extern Extprot_Object *extprot_bytes(Extprot_Pool *pool, Extprot_Tag tag,
				     void const *bin, size_t len);
extern Extprot_Object *extprot_bytes_nocopy(Extprot_Pool *pool, Extprot_Tag tag, size_t len);
extern Extprot_Object *extprot_assoc_init(Extprot_Pool *pool, Extprot_Tag tag, size_t len, ...);
extern Extprot_Object *extprot_assoc(Extprot_Pool *pool, Extprot_Tag tag, size_t len);

#ifdef __cplusplus
}
#endif

#endif
