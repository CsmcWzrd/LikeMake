AUTOMAKE_OPTIONS = foreign subdir-objects
bin_PROGRAMS = LikeMake LikeMakefileBuilder
LikeMake_SOURCES = src/likemake.c src/common.c include/lm_common.h
LikeMakefileBuilder_SOURCES = src/builder.c src/common.c include/lm_common.h
AM_CPPFLAGS = -I$(srcdir)/include
AM_CFLAGS = -std=c11 -Wall -Wextra -Wpedantic
EXTRA_DIST = README.md DESIGN.md LICENSE LikeMakefile Likemakefile \
  Makefile.linux Makefile.macos Makefile.unix Makefile.windows \
  tests/test_integration.py \
  examples/simple/hello.c examples/simple/LikeMakefile \
  examples/multisection/file0.c examples/multisection/main.c \
  examples/multisection/LikeMakefile \
  vs2022/LikeMake.sln vs2022/LikeMake.vcxproj \
  vs2022/LikeMakefileBuilder.vcxproj .github/workflows/build.yml
check-local:
	python3 $(srcdir)/tests/test_integration.py "$(abs_builddir)/LikeMake" "$(abs_builddir)/LikeMakefileBuilder"
