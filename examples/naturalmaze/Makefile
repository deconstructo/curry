CURRY   ?= ../../build-release/curry
MODULES  = world player shaders renderer ui
BCFILES  = $(MODULES:%=%.scc)

.PHONY: all run clean

all: $(BCFILES)

%.scc: %.scm
	$(CURRY) -c $< -o $@

run:
	$(CURRY) main.scm

run-compiled: all
	$(CURRY) main.scm

clean:
	rm -f $(BCFILES)
