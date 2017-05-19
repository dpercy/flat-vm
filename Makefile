
CC = clang

# standard warnings and optimization
flags = -Wall -O3 -g
# trying to eliminate those pesky memory operations on the stack pointers
##flags += -mcmodel=medium

## ??
## ld: warning: PIE disabled. Absolute addressing (perhaps -mdynamic-no-pic)
## not allowed in code signed PIE, but used in _assert_eq_ty from
## /var/folders/d1/_38mp30s0x9_bcdzk156fl1w0000gp/T/main-4489f9.o. To
## fix this warning, don't compile with -mdynamic-no-pic or link with
## -Wl,-no_pie
link_flags = -Wl,-no_pie

default: bench

bench: bench.c
	$(CC) bench.c $(flags) $(link_flags) -o bench

%.generated.c: %.ss
	racket compile.rkt <$< >$@
%.exe: %.generated.c
	$(CC) $< $(flags) $(link_flags) -o $@


clean:
	rm -rf bench.s bench bench.dSYM
