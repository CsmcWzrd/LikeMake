# LikeMake syntax and execution design

## Grammar

Each nonempty line is evaluated according to the active block. Lines ending in a colon are block headers, as is `Execution:<name>` with a nonempty name. Blank lines terminate the active block. Reserved block headers are `Defaults:`, `MainConfig:`, `OutputDir:`, `DependsOn:`, and `Config:`. Any other `<name>:` or `Execution:<name>` header denotes a named executable section. Headers are case-sensitive. Leading/trailing whitespace is ignored; nested indentation is not significant.

`Defaults:` consumes `COMPILER command` or `LINKER command`. `MainConfig:` collects reusable flags. `DependsOn:` and `Config:` are **pending state**, bound to the next execution header, regardless of unrelated execution blocks' names. `Config:` contains literal strings and optional `MainConfig` expansion references. Each `Config:` item is appended to every line in the following execution block. Expansion is performed when execution is evaluated, after the entire file has been parsed, so forward references to `MainConfig` are allowed.

The command prefix parser recognizes `#` as a comment, `~#` as explicitly verbose, and `!#` as silent. Unprefixed commands are verbose. `@clean` is a built-in operation; all other commands run through the platform shell. Compilers and linkers are substituted only when those keywords appear as complete tokens, or through `$(COMPILER)`, `${COMPILER}`, `$(LINKER)` and `${LINKER}`.

## Incrementality

Each section containing `DependsOn:` is keyed to a stable 64-bit FNV-1a digest of its name, expanded commands, compiler/linker defaults, configuration flags, OutputDir and dependencies. Each dependency contributes its name, filesystem modification time, file size and full content. Saved section state is stored under `.likemake/stamp-<name-digest>.txt`. A hash match allows the section to be skipped; `--force` executes regardless. Sections without dependencies are treated as phony and run each time.

Paths in `DependsOn:` must be regular files. Dependencies originating in `OutputDir` can also resolve through that folder. A file missing in both the current working directory and OutputDir causes a fail-fast error.

## OutputDir transaction model

At invocation start, the runner snapshots pre-existing top-level regular files, reads `.likemake/artifacts.txt`, and stages already tracked outputs back from `OutputDir` to the working directory. This ensures later compilation/link commands can reference generated object files in the normal local locations. After the requested sections complete (or if one fails), newly generated top-level files plus previously tracked staged artifacts are moved back to `OutputDir`. Original files present before the invocation are not moved unless they were previously explicitly tracked as artifacts.

The artifact manifest uses a strict leaf-filename check, disallowing separator-based directory traversal. The relocation step refuses to replace an untracked existing OutputDir file. Rename is preferred; a binary file copy-and-delete fallback handles cross-device moves. A missing tracked output invalidates guarded sections for the current invocation. `@clean` removes tracked objects and section state.

This conservative model avoids interpreting any shell text to infer target names. It is designed for single-process, sequential builds. Parallel executions against the same working directory are not supported.

## Builder rules

`LikeMakefileBuilder` creates `build` and `clean` blocks. `build` depends on the listed source filenames and compiles each into a path-hash-suffixed `.o` file (or `.obj` when `--msvc` is chosen), then links the object files into a target executable. `clean` uses the platform-independent `@clean` command. Arbitrary extra execution sections can be emitted with `--section`, `--command`, `--depends` and `--config`.

To get a cross-platform generated build, choose `--msvc` for Microsoft Visual C commands or default GCC-style flags on Unix/MinGW; toolchain-specific flags are explicitly provided by the user.

## Security and failure behavior

Commands run with the same privileges as the invoking user. Never execute untrusted build files. Failed commands stop execution; after a failure, relocation is still attempted to keep generated artifacts in the expected destination. The interpreter checks return codes and surfaces missing dependencies, unknown section names, unsafe `--file` paths, and duplicate section declarations. `@clean` is restricted to previously tracked leaf files and `.likemake`, and recursive state removal rejects symlinks/junctions.

## Why this is not yet a complete GNU Make replacement

GNU Make performs implicit rule search, target timestamp DAG analysis, variable expansion and parallel scheduling. LikeMake instead offers simple named execution blocks and a stable content-hash-based section cache. Support for targets, automatic dependency discovery, dependency graphs and parallelism would be future opt-in extensions to preserve the readable grammar.
