LIBEXTPROT_TARGET=libextprot.la
LIBEXTPROT_SOURCES=extprot_enc.c extprot_dec.c extprot_mem.c
LIBEXTPROT_OBJECTS=$(patsubst %.c, %.lo, $(LIBEXTPROT_SOURCES))
LIBEXTPROT_HEADERS=extprot.h

ifeq ($(shell uname -s),Darwin)
LIBTOOL=glibtool --tag=CC
else
LIBTOOL=libtool --tag=CC
endif

ifeq ($(NO_BIGNUMS),)
EXTRA_LIBS += -lgmp
else
CFLAGS += -DEXTPROT_NO_BIGNUMS
endif

CFLAGS += -Wall -D_XOPEN_SOURCE=500 -DEXTPROT_VERSION='"0.0.1"' -g

all: $(LIBEXTPROT_TARGET) test_extprot

clean:
	rm -f *.extprot.out
	$(LIBTOOL) --mode=clean rm -f $(LIBEXTPROT_TARGET) $(LIBEXTPROT_OBJECTS) test_extprot
	rm -rf .libs test_extprot.dSYM

install: all

$(LIBEXTPROT_TARGET): $(LIBEXTPROT_OBJECTS) $(LIBEXTPROT_HEADERS)
	$(LIBTOOL) --mode=link $(CC) $(CFLAGS) -o $@ $(LIBEXTPROT_OBJECTS)

%.lo: %.c $(LIBEXTPROT_HEADERS)
	$(LIBTOOL) --mode=compile $(CC) $(CFLAGS) -c $<

test_extprot: test_extprot.c $(LIBEXTPROT_TARGET)
	$(LIBTOOL) --mode=link $(CC) $(CFLAGS) -o $@ $< -lextprot $(EXTRA_LIBS)

test: test_extprot
	./test_extprot *.extprot
	for d in *.extprot; do echo $$d > t1; cp t1 t2; xxd $$d >> t1; xxd $$d.out >> t2; diff -u t1 t2; done
	rm -f t1 t2
