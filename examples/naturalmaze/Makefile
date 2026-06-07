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
	# .scc files can be run individually from the CLI (e.g. curry world.scc),
	# but multi-file programs must still use (load ...) with .scm source.
	$(CURRY) main.scm

clean:
	rm -f $(BCFILES)
