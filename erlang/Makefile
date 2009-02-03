TARGETS=extproc_generic.beam

all: $(TARGETS)

clean:
	rm -f $(TARGETS)

%.beam: %.erl
	erlc $<
