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

#include "extprot.h"

static size_t length_of_vint_64(uint64_t val) {
  if (val < 128) return 1;
  if (val < 16384) return 2;
  if (val < 2097152) return 3;
  if (val < 268435456) return 4;
  if (val < 34359738368LL) return 5;
  if (val < 4398046511104LL) return 6;
  if (val < 562949953421312LL) return 7;
  if (val < 72057594037927936LL) return 8;
  if (val < 9223372036854775808ULL) return 9;
  return 10;
}

static size_t sum_of_lengths(Extprot_Object * const *v, size_t count) {
  size_t i;
  size_t sum = 0;
  for (i = 0; i < count; i++) {
    sum += extprot_compute_length(v[i]);
  }
  return sum;
}

static size_t length_of_body(Extprot_Object const *o) {
  switch (o->kind & 0xf) {
    case EXTPROT_VINT:
#ifndef EXTPROT_NO_BIGNUMS
      return (mpz_sizeinbase(o->body.vint.value, 2) + 6) / 7;
#else
      return length_of_vint_64(o->body.vint);
#endif
    case EXTPROT_BITS8:
      return 1;
    case EXTPROT_BITS32:
      return 4;
    case EXTPROT_BITS64_LONG:
    case EXTPROT_BITS64_FLOAT:
      return 8;
    case EXTPROT_ENUM:
      return 0;
    case EXTPROT_TUPLE:
    case EXTPROT_HTUPLE:
      return
	length_of_vint_64(o->body.tuple.length) +
	sum_of_lengths(o->body.tuple.vec, o->body.tuple.length);
    case EXTPROT_BYTES:
      return o->body.bytes.length;
    case EXTPROT_ASSOC:
      return
	length_of_vint_64(o->body.tuple.length) +
	sum_of_lengths(o->body.tuple.vec, o->body.tuple.length * 2);
    default:
      return 0;
  }
}

size_t extprot_compute_length(Extprot_Object const *o) {
  size_t bodylen = length_of_body(o);
  return
    length_of_vint_64(o->kind) +
    ((o->kind & 1) ? length_of_vint_64(bodylen) : 0) +
    bodylen;
}

static void encode(Extprot_Object const *o, void **buffer);

#define BUFP_TO_BYTEP(bufp)	(*((uint8_t **) (bufp)))
#define PLACE_BYTE(bufp, b)	(*(BUFP_TO_BYTEP(bufp)++) = (uint8_t) (b))
#define BUFFER_AT(bufp, n)	(BUFP_TO_BYTEP(bufp)[(n)])
#define ADVANCE_BY(bufp, n)	(BUFP_TO_BYTEP(bufp) += (n))

static void encode_vint_64(uint64_t value, void **buffer) {
  do {
    uint8_t v = value & 0x7f;
    value >>= 7;
    if (value != 0) v |= 0x80;
    PLACE_BYTE(buffer, v);
  } while (value > 0);
}

static void encode_fixed_int_64(uint64_t value, void **buffer) {
  PLACE_BYTE(buffer, value >> 0);
  PLACE_BYTE(buffer, value >> 8);
  PLACE_BYTE(buffer, value >> 16);
  PLACE_BYTE(buffer, value >> 24);
  PLACE_BYTE(buffer, value >> 32);
  PLACE_BYTE(buffer, value >> 40);
  PLACE_BYTE(buffer, value >> 48);
  PLACE_BYTE(buffer, value >> 56);
}

static void save_n(Extprot_Object * const *v, size_t count, void **buffer) {
  size_t i;
  for (i = 0; i < count; i++) {
    encode(v[i], buffer);
  }
}

static void encode(Extprot_Object const *o, void **buffer) {
  encode_vint_64(o->kind, buffer);
  if (o->kind & 1) {
    encode_vint_64(length_of_body(o), buffer);
  }
  switch (o->kind & 0xf) {
    case EXTPROT_VINT:
#ifndef EXTPROT_NO_BIGNUMS
      {
	size_t i;
	size_t count = (mpz_sizeinbase(o->body.vint.value, 2) + 6) / 7;
	mpz_export(*buffer, NULL, -1, 1, 0, 1, o->body.vint.value);
	for (i = 0; i < count - 1; i++) {
	  ((uint8_t *) (*buffer))[i] |= 0x80;
	}
	ADVANCE_BY(buffer, count);
      }
      break;
#else
      encode_vint_64(o->body.vint, buffer);
      break;
#endif

    case EXTPROT_BITS8:
      PLACE_BYTE(buffer, o->body.bits8);
      break;

    case EXTPROT_BITS32:
      PLACE_BYTE(buffer, o->body.bits32 >> 0);
      PLACE_BYTE(buffer, o->body.bits32 >> 8);
      PLACE_BYTE(buffer, o->body.bits32 >> 16);
      PLACE_BYTE(buffer, o->body.bits32 >> 24);
      break;

    case EXTPROT_BITS64_LONG:
      encode_fixed_int_64(o->body.bits64_long, buffer);
      break;

    case EXTPROT_BITS64_FLOAT:
      encode_fixed_int_64(* (uint64_t *) &o->body.bits64_float, buffer);
      break;

    case EXTPROT_ENUM:
      break;

    case EXTPROT_TUPLE:
    case EXTPROT_HTUPLE:
      encode_vint_64(o->body.tuple.length, buffer);
      save_n(o->body.tuple.vec, o->body.tuple.length, buffer);
      break;

    case EXTPROT_BYTES:
      memcpy(&BUFFER_AT(buffer, 0), o->body.bytes.vec, o->body.bytes.length);
      ADVANCE_BY(buffer, o->body.bytes.length);
      break;

    case EXTPROT_ASSOC:
      encode_vint_64(o->body.tuple.length, buffer);
      save_n(o->body.tuple.vec, o->body.tuple.length * 2, buffer);
      break;

    default:
      break;
  }
}

void extprot_encode(Extprot_Object const *o, void *buffer) {
  encode(o, &buffer);
}
