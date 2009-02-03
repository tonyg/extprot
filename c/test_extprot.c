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
#include <errno.h>
#include <stdarg.h>

#include "extprot.h"

static void die(char const *where, Extprot_Error e) {
  fprintf(stderr, "Error: %s: %s\n", where, extprot_error_message(e));
  exit(1);
}

static void iprintf(int indent, char const *fmt, ...) {
  int i;
  va_list vl;
  for (i = 0; i < indent; i++) fputc(' ', stdout);
  va_start(vl, fmt);
  vprintf(fmt, vl);
  va_end(vl);
}

static void dump(int indent, Extprot_Object *o);

static void dump_n(int indent, Extprot_Object **v, size_t count) {
  size_t i;
  for (i = 0; i < count; i++) {
    iprintf(indent, "%d:\n", i);
    dump(indent + 2, v[i]);
  }
}

static void dump_2n(int indent, Extprot_Object **v, size_t count) {
  size_t i;
  for (i = 0; i < count; i++) {
    iprintf(indent, "%d:\n", i);
    dump(indent + 2, v[2 * i]);
    dump(indent + 2, v[2 * i + 1]);
  }
}

static void dump(int indent, Extprot_Object *o) {
  iprintf(indent, "tag 0x%02x ", o->kind >> 4);
  switch (o->kind & 0xf) {
    case EXTPROT_VINT:
      printf("vint ");
#ifndef EXTPROT_NO_BIGNUMS
      mpz_out_str(stdout, 10, o->body.vint.value);
      fputc('\n', stdout);
#else
      printf("%llu\n", o->body.vint);
#endif
      break;

    case EXTPROT_BITS8:
      printf("bits8 %u (0x%02x)\n", o->body.bits8, o->body.bits8);
      break;

    case EXTPROT_BITS32:
      printf("bits32 %u (0x%08x)\n", o->body.bits32, o->body.bits32);
      break;

    case EXTPROT_BITS64_LONG:
      printf("bits64_long %lld (0x%016llx)\n", o->body.bits64_long, o->body.bits64_long);
      break;

    case EXTPROT_BITS64_FLOAT:
      printf("bits64_float %g\n", o->body.bits64_float);
      break;

    case EXTPROT_ENUM:
      printf("enum\n");
      break;

    case EXTPROT_HTUPLE:
      fputc('h', stdout);
    case EXTPROT_TUPLE:
      printf("tuple %u\n", o->body.tuple.length);
      dump_n(indent + 2, o->body.tuple.vec, o->body.tuple.length);
      break;

    case EXTPROT_BYTES:
      printf("binary %u\n", o->body.bytes.length);
      iprintf(indent + 2, ">>>%*s<<<\n", o->body.bytes.length, o->body.bytes.vec);
      break;

    case EXTPROT_ASSOC:
      printf("assoc %u\n", o->body.tuple.length);
      dump_2n(indent + 2, o->body.tuple.vec, o->body.tuple.length);
      break;

    default:
      printf("??\n");
  }
}

static Extprot_Object *read_one(Extprot_Pool *pool, char const *testName) {
  long len;
  uint8_t *buffer;
  Extprot_Error e;
  uint32_t tag_and_type;
  size_t total_len;
  FILE *f;

  f = fopen(testName, "rb");
  if (f == NULL) {
    fprintf(stderr, "Could not open test %s\n", testName);
    exit(1);
  }

  fseek(f, 0, SEEK_END);
  len = ftell(f);
  fseek(f, 0, SEEK_SET);

  buffer = extprot_pool_alloc(pool, len);
  fread(buffer, 1, len, f);

  e = extprot_decode_header(buffer, len, &tag_and_type, &total_len);
  if (e) { die("extprot_decode_header", e); }

  e = extprot_decode(pool, buffer, len);
  if (e) { die("extprot_decode", e); }

  fclose(f);
  return pool->root;
}

static void write_one(Extprot_Object *o, char const *testName) {
  char buf[1024];
  FILE *f;
  size_t len;
  void *out_buffer;

  snprintf(buf, sizeof(buf), "%s.out", testName);
  f = fopen(buf, "wb");
  if (f == NULL) {
    fprintf(stderr, "Could not write test output %s\n", buf);
    exit(1);
  }

  len = extprot_compute_length(o);
  printf("Encoding as %u bytes...\n", len);
  out_buffer = malloc(len);
  extprot_encode(o, out_buffer);
  fwrite(out_buffer, len, 1, f);
  fclose(f);
}

int main(int argc, char *argv[]) {
  Extprot_Pool p;
  int i;

  printf("test_extprot: extprot version %s\n", extprot_version());

  for (i = 1; i < argc; i++) {
    Extprot_Object *o;

    printf("%s ...\n", argv[i]);
    init_extprot_pool(&p, 0);
    o = read_one(&p, argv[i]);
    printf("pointer is %p\n", o);
    dump(2, o);
    write_one(o, argv[i]);
    empty_extprot_pool(&p);
  }

  return 0;
}
