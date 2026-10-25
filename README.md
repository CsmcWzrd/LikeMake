# LikeMake 1.0.0

**LikeMake** is a small, readable build-command runner written in ISO C11 for Windows, Linux, Unix-like systems and macOS. **LikeMakefileBuilder** is a separate command-line/interactive C program that generates `LikeMakefile` recipes from a list of C source files. LikeMake reads build instructions from **the current working directory**; a subdirectory is visited only with an explicit `-c SUBFOLDER` option.

Unlike GNU Make, LikeMake uses blocks with blank lines and human-readable section names rather than targets containing tab-indented shell recipes. It is intentionally **not yet a drop-in GNU Make replacement**: no parallel execution, Make variable language, implicit-rule resolution or automatic dependency graph generation is claimed.

## Build LikeMake and LikeMakefileBuilder

### Linux and Unix

```sh
make
make test
sudo make install PREFIX=/usr/local
```

Choose `make -f Makefile.linux`, `make -f Makefile.unix`, or `make CC=clang`. Supported `make` is GNU Make or a compatible implementation; the provided platform-specific files use GNU Make's `include` directive.

### macOS

```sh
make -f Makefile.macos
make -f Makefile.macos test
```

Requires Xcode command-line tools or another compatible C11 toolchain.

### CMake (Windows, Linux, macOS)

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

### GNU Autotools (Linux, Unix, macOS)

```sh
autoreconf -fi
./configure
make
make install
```

The repository includes `configure.ac` and `Makefile.am`. Run `autoreconf` to generate `configure` and the platform-specific generated `Makefile.in`; these generated files are intentionally not shipped in the source archive.

### Visual Studio 2022 on Windows 10/11

Open `vs2022/LikeMake.sln` in Visual Studio 2022, select `Debug` or `Release` and `x64` or `Win32`, and choose **Build Solution**. Both `LikeMake.exe` and `LikeMakefileBuilder.exe` use the Visual Studio C11 compiler and Platform Toolset v143, with no third-party dependencies. Visual Studio builds the *tools themselves* without requiring GCC.

Alternatively, from an x64 Developer Command Prompt:

```bat
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64
cmake --build build-vs --config Release
```

When generating *other* programs to compile with MSVC, choose `LikeMakefileBuilder --msvc` and run LikeMake from a Visual Studio Developer Command Prompt where `cl.exe` and `link.exe` are available. On other Windows toolchains you can use `gcc` or `clang` as appropriate.

**Verification status:** GNU Make (GCC and Clang), CMake/CTest and GNU Autotools have been compiled/tested on Linux. The VS2022 solution and Windows/macOS portability have been authored but were not executed in native Windows/macOS environments here.

## Generate a project

```sh
LikeMakefileBuilder --project hello --target hello \
  --source main.c --source helper.c \
  --cflag -O2 --cflag -Wall --ldflag -lm \
  --output-dir build-output
LikeMake build
LikeMake build           # unchanged source: up-to-date
LikeMake clean           # deletes tracked outputs and dependency state
LikeMake clean build     # executes named sections in requested order
```

Windows (MSVC):

```bat
LikeMakefileBuilder.exe --project hello --source main.c --msvc
LikeMake.exe build
```

Run `LikeMakefileBuilder --interactive` for a guided prompt, or `LikeMakefileBuilder --help` to see all options. Builder can also add named sections:

```sh
LikeMakefileBuilder --project hello --source main.c \
  --section verify --command "echo verification" \
  --section child --command "LikeMake -c subproject build"
```

The builder refuses to overwrite an existing LikeMakefile unless `--force` is given. It generates `build` first and `clean` second and uses object filenames containing a short path-derived hash, avoiding collisions between identically named sources in different folders.

## Example LikeMakefile

```text
Defaults:
COMPILER gcc
LINKER gcc

MainConfig:
-O2
-I.

OutputDir:
build-output

DependsOn:
main.c
helper.c

Config:
MainConfig
-lm

Execution:build
~# COMPILER -c main.c -o main.o
!# COMPILER -c helper.c -o helper.o
LINKER main.o helper.o -o hello

clean-all:
@clean
```

Run `LikeMake build`, `LikeMake clean-all`, `LikeMake --list`, or simply `LikeMake`. With **no section names, LikeMake runs only the first execution section** (as `make` normally builds its first/default target). With multiple section names, LikeMake runs them in the order given on the command line.

The alternative spelling `Likemakefile` is recognized if `LikeMakefile` is absent, and can be generated using `LikeMakefileBuilder --output Likemakefile`.

## Command and configuration behavior

- `Defaults:` accepts `COMPILER <command>` and `LINKER <command>`; both default to `gcc`. You can use `COMPILER`/`LINKER` as complete words or the `${COMPILER}`, `$(COMPILER)`, `${LINKER}`, `$(LINKER)` spellings in commands and config lines.
- `MainConfig:` is a reusable list of command-line options. Put the literal `MainConfig` in a `Config:` block to include it in that section. Additional lines in `Config:` append further options. The resulting configuration string is appended **to every command** in the *next* execution block. For shell commands such as `echo`, avoid a `Config:` block if extra options are not desired.
- `DependsOn:` lists existing *files*, one per line, for the next execution block. LikeMake hashes dependency names, contents, modification times and sizes alongside the expanded execution commands and configuration. It skips that execution if the saved signature is unchanged. An execution block without dependencies runs every time. A missing dependency is an error.
- `Execution:NAME` and `NAME:` both introduce execution blocks. Names can be anything not reserved by the interpreter; examples are `build`, `clean-all`, `Execution2` and `deploy-staging`.
- `# comment` is ignored. `~# command` prints the command and its output. `!# command` suppresses both the command echo and command output. A command with no prefix prints the command and its output. Failed commands stop the run and propagate a failure status.
- An **empty line ends the active block**, unless it is the end of the file. A new header may immediately follow a preceding block without a blank line, but blank lines must not appear inside multi-line blocks.
- `OutputDir:` names a single destination folder, relative to the working directory or absolute. The runner records top-level files created by the build and moves them there after commands finish. **Pre-existing source and project files are left in place**. On later runs it stages previously tracked outputs back into the work directory before executing commands, then moves them to `OutputDir` again. This preserves incremental object-file linking and avoids moving or overwriting pre-existing inputs.
- `@clean` is a built-in command that removes tracked build outputs and stored dependency state without touching original source files. It can be used in any named execution section, typically `clean` or `clean-all`.
- LikeMake runs shell commands using the platform shell (`/bin/sh` on Unix-like systems, `cmd.exe` on Windows). Recipes must use shell syntax available on their target OS. For a cross-platform build, the generated compiler/linker commands avoid Unix-specific shell utilities.

## Runner options

```text
LikeMake [ -c SUBFOLDER ] [ --file FILENAME ] [ --list ]
         [ --dry-run | -n ] [ --force ] [ SECTION ... ]
```

`-c` intentionally permits changing working folders, to support `LikeMake -c subproject build` within another execution section. `--file` accepts a filename in the *current* folder only; it does not allow `/` or `\\` paths. `--dry-run` does not execute commands, move artifacts or write dependency state. `--force` reruns dependency-guarded sections. `--list` prints available section names.

## Layout

```text
include/lm_common.h             shared public-internal declarations
src/common.c                    portable filesystem, list, hash utilities
src/likemake.c                  interpreter and execution logic
src/builder.c                   LikeMakefileBuilder generator
examples/simple/               one-file runnable C example
examples/multisection/         two-stage incremental compile/link example
tests/test_integration.py       end-to-end regression suite
vs2022/LikeMake.sln              Visual Studio 2022 solution
vs2022/*.vcxproj                MSVC projects
CMakeLists.txt                  multi-platform CMake
Makefile                        GNU-style Makefile
Makefile.{linux,macos,unix,windows}
Makefile.am, configure.ac       GNU Automake and Autoconf input
LikeMakefile                    self-hosting build recipe
DESIGN.md                       format and trade-offs
```

## Known limitations

- Build order between sections is explicit, not inferred from output files. Use `LikeMake section1 section2` when multiple stages must run.
- Only regular files created in the current directory are relocated; files created in nested directories are left there. Existing files in the current directory are never automatically relocated. Tracked paths stored in `.likemake/artifacts.txt` must be simple top-level filenames.
- Output names are not declared in `DependsOn`; deletion of output files triggers broad invalidation only when the runner can detect previously tracked files are missing.
- No concurrency, built-in wildcard expansion, Make-style recipe language, cycle detection, or remote building is provided.
- Windows filesystem functions currently use the system's native ANSI API, so Unicode filenames outside the active Windows code page are not guaranteed to work. ASCII filenames work on all supported OS toolchains.
- Running shell commands from an untrusted LikeMakefile is unsafe by design, as with any build script.

## Testing

```sh
make test
# or
ctest --test-dir build --output-on-failure
```

The Python integration suite is used **only for testing**, not as a runtime dependency. The programs themselves are implemented entirely in C.
