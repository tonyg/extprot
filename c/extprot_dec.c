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

typedef struct Extprot_Decoder_State_ {
  Extprot_Pool *pool;

  void const *buffer;
  size_t input_length;
  size_t index;
} Extprot_Decoder_State;

#define BUFFER_AT(state, n)	(((uint8_t *) (state)->buffer)[(n)])
#define PEEK_BYTE(state)	(((uint8_t *) (state)->buffer)[(state)->index])
#define ADVANCE(state)		((state)->index++)
#define ADVANCE_BY(state, n)	((state)->index += n)
#define ACC(state)		((state)->pool->root)
#define SET_ACC(state, v)	((state)->pool->root = (v))

#define CHECK_LIMIT(state)			 \
  {						 \
    if ((state)->index >= (state)->input_length) \
      return Extprot_EarlyEOF;			 \
  }

#define PRE_CHECK_LIMIT(state, count)				\
  {								\
    if ((state)->index + (count) > (state)->input_length)	\
      return Extprot_EarlyEOF;					\
  }

#define CHECK(e)					\
  {							\
    Extprot_Error _err__ = (e);				\
    if (_err__ != Extprot_NoError) return _err__;	\
  }

static Extprot_Error decode(Extprot_Decoder_State *state);

static Extprot_Error read_vint_64(Extprot_Decoder_State *state, uint64_t *val) {
  uint64_t v = 0;
  int shift_by = 0;
  while (1) {
    uint8_t b;
    CHECK_LIMIT(state);
    b = PEEK_BYTE(state);
    ADVANCE(state);
    v |= ((uint64_t) (b & 0x7f)) << shift_by;
    if ((b & 0x80) == 0) break;
    shift_by += 7;
    if (shift_by > 57) {
      return Extprot_VintOverflow;
    }
  }
  *val = v;
  return Extprot_NoError;
}

#ifndef EXTPROT_NO_BIGNUMS
static Extprot_Error decode_vint(Extprot_Decoder_State *state, Extprot_Tag tag) {
  size_t byte_count = 0;
  size_t base_index = state->index;
  uint8_t b;
  do {
    CHECK_LIMIT(state);
    b = PEEK_BYTE(state);
    ADVANCE(state);
    byte_count++;
  } while (b & 0x80);
  SET_ACC(state, extprot_vint(state->pool, tag));
  mpz_import(ACC(state)->body.vint.value, byte_count, -1, 1, 0, 1, &BUFFER_AT(state, base_index));
  return Extprot_NoError;
}
#endif

static Extprot_Error read_fixed_int_64(Extprot_Decoder_State *state, int64_t *val) {
  uint64_t v = 0;
  PRE_CHECK_LIMIT(state, 8);
  v |= ((uint64_t) PEEK_BYTE(state)) << 0; ADVANCE(state);
  v |= ((uint64_t) PEEK_BYTE(state)) << 8; ADVANCE(state);
  v |= ((uint64_t) PEEK_BYTE(state)) << 16; ADVANCE(state);
  v |= ((uint64_t) PEEK_BYTE(state)) << 24; ADVANCE(state);
  v |= ((uint64_t) PEEK_BYTE(state)) << 32; ADVANCE(state);
  v |= ((uint64_t) PEEK_BYTE(state)) << 40; ADVANCE(state);
  v |= ((uint64_t) PEEK_BYTE(state)) << 48; ADVANCE(state);
  v |= ((uint64_t) PEEK_BYTE(state)) << 56; ADVANCE(state);
  *val = (int64_t) v;
  return Extprot_NoError;
}

static Extprot_Error read_n_into(Extprot_Decoder_State *state, size_t count) {
  Extprot_Object *o = ACC(state);
  size_t i;
  for (i = 0; i < count; i++) {
    CHECK(decode(state));
    o->body.tuple.vec[i] = ACC(state);
  }
  SET_ACC(state, o);
  return Extprot_NoError;
}

static Extprot_Error decode1(Extprot_Decoder_State *state,
			     Extprot_Tag tag,
			     int wire_type,
			     size_t len)
{
  switch (wire_type) {
    case EXTPROT_VINT:
#ifndef EXTPROT_NO_BIGNUMS
      return decode_vint(state, tag);
#else
      {
	uint64_t value;
	CHECK(read_vint_64(state, &value));
	SET_ACC(state, extprot_vint(state->pool, tag, value));
	return Extprot_NoError;
      }
#endif

    case EXTPROT_BITS8:
      CHECK_LIMIT(state);
      SET_ACC(state, extprot_bits8(state->pool, tag, PEEK_BYTE(state)));
      ADVANCE(state);
      return Extprot_NoError;

    case EXTPROT_BITS32:
      {
	uint32_t val = 0;
	PRE_CHECK_LIMIT(state, 4);
	val |= PEEK_BYTE(state) << 0; ADVANCE(state);
	val |= PEEK_BYTE(state) << 8; ADVANCE(state);
	val |= PEEK_BYTE(state) << 16; ADVANCE(state);
	val |= PEEK_BYTE(state) << 24; ADVANCE(state);
	SET_ACC(state, extprot_bits32(state->pool, tag, val));
	return Extprot_NoError;
      }

    case EXTPROT_BITS64_LONG:
      {
	int64_t val;
	CHECK(read_fixed_int_64(state, &val));
	SET_ACC(state, extprot_bits64_long(state->pool, tag, val));
	return Extprot_NoError;
      }

    case EXTPROT_BITS64_FLOAT:
      {
	int64_t val;
	CHECK(read_fixed_int_64(state, &val));
	SET_ACC(state, extprot_bits64_float(state->pool, tag, * (double *) &val));
	return Extprot_NoError;
      }

    case EXTPROT_ENUM:
      SET_ACC(state, extprot_enum(state->pool, tag));
      return Extprot_NoError;

    case EXTPROT_TUPLE:
      {
	uint64_t n_elems;
	CHECK(read_vint_64(state, &n_elems));
	SET_ACC(state, extprot_tuple(state->pool, tag, n_elems));
	CHECK(read_n_into(state, n_elems));
	return Extprot_NoError;
      }

    case EXTPROT_HTUPLE:
      {
	uint64_t n_elems;
	CHECK(read_vint_64(state, &n_elems));
	SET_ACC(state, extprot_htuple(state->pool, tag, n_elems));
	CHECK(read_n_into(state, n_elems));
	return Extprot_NoError;
      }

    case EXTPROT_BYTES:
      SET_ACC(state, extprot_bytes(state->pool, tag, &BUFFER_AT(state, state->index), len));
      ADVANCE_BY(state, len);
      return Extprot_NoError;

    case EXTPROT_ASSOC:
      {
	uint64_t n_pairs;
	CHECK(read_vint_64(state, &n_pairs));
	SET_ACC(state, extprot_assoc(state->pool, tag, n_pairs));
	CHECK(read_n_into(state, n_pairs * 2));
	return Extprot_NoError;
      }

    default:
      return Extprot_InvalidTag;
  }
}

Extprot_Error extprot_decode_header(void const *buffer,
				    size_t in_len,
				    uint32_t *tag_and_type,
				    size_t *total_len)
{
  Extprot_Decoder_State stateRecord;
  stateRecord.pool = NULL;
  stateRecord.buffer = buffer;
  stateRecord.input_length = in_len;
  stateRecord.index = 0;

  uint64_t v;
  CHECK(read_vint_64(&stateRecord, &v));
  *tag_and_type = (uint32_t) v;

  switch (v & 0xf) {
    case EXTPROT_BITS8:
      *total_len = stateRecord.index + 1;
      return Extprot_NoError;

    case EXTPROT_BITS32:
      *total_len = stateRecord.index + 4;
      return Extprot_NoError;

    case EXTPROT_BITS64_LONG:
    case EXTPROT_BITS64_FLOAT:
      *total_len = stateRecord.index + 8;
      return Extprot_NoError;

    case EXTPROT_ENUM:
      *total_len = stateRecord.index;
      return Extprot_NoError;

    case EXTPROT_TUPLE:
    case EXTPROT_HTUPLE:
    case EXTPROT_BYTES:
    case EXTPROT_ASSOC:
      CHECK(read_vint_64(&stateRecord, &v));
      *total_len = stateRecord.index + v;
      return Extprot_NoError;

    case EXTPROT_VINT:
    default:
      return Extprot_InvalidTag;
  }
}

static Extprot_Error decode(Extprot_Decoder_State *state) {
  uint64_t tag_and_type;
  CHECK(read_vint_64(state, &tag_and_type));
  size_t len = 0;
  if (tag_and_type & 1) {
    uint64_t tmp_len;
    CHECK(read_vint_64(state, &tmp_len));
    len = (size_t) tmp_len;
    if (len != tmp_len) {
      return Extprot_SizeTOverflow;
    }
    PRE_CHECK_LIMIT(state, len);
  }
  CHECK(decode1(state, (Extprot_Tag) (tag_and_type >> 4), (int) (tag_and_type & 0xf), len));
  ACC(state)->kind |= ((uint32_t) tag_and_type) & ~0xf;
  return Extprot_NoError;
}

Extprot_Error extprot_decode(Extprot_Pool *pool,
			     void const *buffer,
			     size_t len)
{
  Extprot_Decoder_State stateRecord;
  stateRecord.pool = pool;
  stateRecord.buffer = buffer;
  stateRecord.input_length = len;
  stateRecord.index = 0;

  return decode(&stateRecord);
}

