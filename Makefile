CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic
CPPFLAGS += -Iinclude
PREFIX ?= /usr/local
EXEEXT ?=

.PHONY: all clean test install
all: LikeMake$(EXEEXT) LikeMakefileBuilder$(EXEEXT)

src/common.o: src/common.c include/lm_common.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
src/likemake.o: src/likemake.c include/lm_common.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
src/builder.o: src/builder.c include/lm_common.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
LikeMake$(EXEEXT): src/likemake.o src/common.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)
LikeMakefileBuilder$(EXEEXT): src/builder.o src/common.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)
test: all
	python3 tests/test_integration.py ./LikeMake$(EXEEXT) ./LikeMakefileBuilder$(EXEEXT)
install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 LikeMake$(EXEEXT) LikeMakefileBuilder$(EXEEXT) $(DESTDIR)$(PREFIX)/bin/
clean:
	rm -f src/*.o LikeMake$(EXEEXT) LikeMakefileBuilder$(EXEEXT)
