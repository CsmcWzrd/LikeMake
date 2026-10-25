#!/usr/bin/env python3
"""Integration tests; invoked by make test or ctest. Python is test-only."""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

if len(sys.argv) < 3:
    raise SystemExit("Usage: test_integration.py PATH_TO_LIKEMAKE PATH_TO_BUILDER")
MAKE = str(pathlib.Path(sys.argv.pop(1)).resolve())
BUILDER = str(pathlib.Path(sys.argv.pop(1)).resolve())
HAS_CC = shutil.which("gcc") is not None


class LikeMakeTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="likemake-test-")
        self.root = pathlib.Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        return path

    def run_make(self, *args, cwd=None, ok=True):
        proc = subprocess.run([MAKE, *args], cwd=cwd or self.root,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              text=True, encoding="utf-8", errors="replace")
        if ok and proc.returncode != 0:
            self.fail(f"LikeMake returned {proc.returncode}\nstdout:\n{proc.stdout}\nstderr:\n{proc.stderr}")
        return proc

    def run_builder(self, *args, ok=True):
        proc = subprocess.run([BUILDER, *args], cwd=self.root,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              text=True, encoding="utf-8", errors="replace")
        if ok and proc.returncode != 0:
            self.fail(f"builder returned {proc.returncode}: {proc.stderr}")
        return proc

    def test_default_is_first_section_only_and_named_selection(self):
        self.write("LikeMakefile", """Execution:first
 echo first > first.out

clean-all:
 echo second > second.out
""")
        self.run_make()
        self.assertTrue((self.root / "first.out").exists())
        self.assertFalse((self.root / "second.out").exists())
        self.run_make("clean-all")
        self.assertTrue((self.root / "second.out").exists())
        out = self.run_make("--list").stdout
        self.assertEqual(out.splitlines(), ["first", "clean-all"])

    def test_selection_follows_argument_order(self):
        self.write("LikeMakefile", """Execution:one
echo 1 >> log

Execution:two
echo 2 >> log
""")
        self.run_make("two", "one")
        self.assertEqual((self.root / "log").read_text().split(), ["2", "1"])

    def test_commands_print_vs_silent(self):
        self.write("LikeMakefile", """run:
# comment
~# echo public message
!# echo SECRET_SHOULD_NOT_BE_VISIBLE
""")
        result = self.run_make("run")
        self.assertIn("public message", result.stdout)
        self.assertNotIn("SECRET_SHOULD_NOT_BE_VISIBLE", result.stdout)
        self.assertNotIn("SECRET_SHOULD_NOT_BE_VISIBLE", result.stderr)

    def test_main_config_and_compiler_expansion(self):
        self.write("LikeMakefile", """Defaults:
COMPILER echo
LINKER echo

MainConfig:
ONE
TWO

Config:
MainConfig
THREE

Execution:run
~# COMPILER selected
~# ${LINKER} linked
""")
        result = self.run_make()
        self.assertIn("echo selected ONE TWO THREE", result.stdout)
        self.assertIn("echo linked ONE TWO THREE", result.stdout)

    def test_dependency_incremental_content_and_mtime(self):
        dep = self.write("input.txt", "first\n")
        self.write("LikeMakefile", """DependsOn:
input.txt

build:
echo build >> count.log
""")
        self.run_make()
        self.run_make()
        self.assertEqual((self.root / "count.log").read_text().splitlines(), ["build"])
        dep.write_text("second\n", encoding="utf-8")
        self.run_make()
        self.assertEqual(len((self.root / "count.log").read_text().splitlines()), 2)
        self.run_make("--force")
        self.assertEqual(len((self.root / "count.log").read_text().splitlines()), 3)
        old = dep.stat().st_mtime
        os.utime(dep, (old + 5, old + 5))
        self.run_make()
        self.assertEqual(len((self.root / "count.log").read_text().splitlines()), 4)

    def test_missing_dependency_fails(self):
        self.write("LikeMakefile", """DependsOn:
missing.source

build:
echo NOT_RUN > bad.out
""")
        p = self.run_make(ok=False)
        self.assertNotEqual(p.returncode, 0)
        self.assertIn("missing dependency", p.stderr)
        self.assertFalse((self.root / "bad.out").exists())

    def test_output_dir_preserves_original_files(self):
        self.write("existing.txt", "do not move")
        self.write("LikeMakefile", """OutputDir:
artifacts

Execution:run
echo data > generated.txt
""")
        self.run_make()
        self.assertTrue((self.root / "existing.txt").is_file())
        self.assertTrue((self.root / "LikeMakefile").is_file())
        self.assertFalse((self.root / "generated.txt").exists())
        self.assertTrue((self.root / "artifacts/generated.txt").exists())
        self.run_make()
        self.assertTrue((self.root / "artifacts/generated.txt").exists())

    def test_output_dir_restore_and_rebuild_dependent_sections(self):
        source = self.write("input.txt", "alpha\n")
        command1 = "copy /Y input.txt obj.txt" if os.name == "nt" else "cp input.txt obj.txt"
        command2 = "copy /Y obj.txt result.txt" if os.name == "nt" else "cp obj.txt result.txt"
        self.write("LikeMakefile", f"""OutputDir:
out

DependsOn:
input.txt

Execution:compile
{command1}

DependsOn:
obj.txt

Execution:link
{command2}
""")
        self.run_make("compile", "link")
        self.assertEqual((self.root / "out/result.txt").read_text(), "alpha\n")
        self.run_make("compile", "link")
        source.write_text("beta\n", encoding="utf-8")
        self.run_make("compile", "link")
        self.assertEqual((self.root / "out/result.txt").read_text(), "beta\n")
        self.assertFalse((self.root / "obj.txt").exists())

    def test_clean_removes_artifacts_and_states(self):
        self.write("input.txt", "x")
        self.write("LikeMakefile", """OutputDir:
out

DependsOn:
input.txt

build:
echo output > generated.txt

clean:
@clean
""")
        self.run_make("build")
        self.assertTrue((self.root / "out/generated.txt").exists())
        self.run_make("clean")
        self.assertFalse((self.root / "out/generated.txt").exists())
        self.run_make("build")
        self.assertTrue((self.root / "out/generated.txt").exists())

    def test_clean_build_together_restores_dependency_state(self):
        self.write("input.txt", "x")
        self.write("LikeMakefile", """OutputDir:
out

DependsOn:
input.txt

build:
echo output > generated.txt

clean:
@clean
""")
        self.run_make("build")
        self.run_make("clean", "build")
        out = self.run_make("build").stdout
        self.assertIn("up to date", out)

    def test_child_directory_requires_explicit_c(self):
        self.write("LikeMakefile", """parent:
echo parent > parent.out
""")
        self.write("child/LikeMakefile", """Execution:child-task
echo child > child.out
""")
        self.run_make("-c", "child", "child-task")
        self.assertTrue((self.root / "child/child.out").exists())
        self.assertFalse((self.root / "parent.out").exists())

    def test_alias_filename_and_disallowed_paths(self):
        self.write("Likemakefile", """hi:
echo hi > done
""")
        self.run_make()
        self.assertTrue((self.root / "done").exists())
        p = self.run_make("--file", "../Likemakefile", ok=False)
        self.assertNotEqual(p.returncode, 0)

    def test_unknown_section_and_duplicate_sections_fail(self):
        self.write("LikeMakefile", """build:
echo okay
""")
        self.assertNotEqual(self.run_make("unknown", ok=False).returncode, 0)
        self.write("LikeMakefile", """build:
echo okay

build:
echo twice
""")
        self.assertNotEqual(self.run_make(ok=False).returncode, 0)

    def test_dry_run_does_not_write_files(self):
        self.write("dep.txt", "x")
        self.write("LikeMakefile", """OutputDir:
out

DependsOn:
dep.txt

build:
echo x > output.txt
""")
        self.run_make("--dry-run")
        self.assertFalse((self.root / "output.txt").exists())
        self.assertFalse((self.root / ".likemake").exists())

    def test_output_dir_collision_does_not_overwrite_untracked_file(self):
        self.write("output/generated.txt", "original\n")
        self.write("LikeMakefile", """OutputDir:
output

build:
echo replacement > generated.txt
""")
        p = self.run_make(ok=False)
        self.assertNotEqual(p.returncode, 0)
        self.assertEqual((self.root / "output/generated.txt").read_text(), "original\n")
        self.assertTrue((self.root / "generated.txt").exists())

    def test_generator_supports_alternate_filename(self):
        self.write("main.c", "int main(void){return 0;}\n")
        self.run_builder("--project", "alternate", "--source", "main.c",
                         "--output", "Likemakefile")
        self.assertFalse((self.root / "LikeMakefile").exists())
        self.assertTrue((self.root / "Likemakefile").exists())
        self.assertIn("build", self.run_make("--list").stdout)

    def test_builder_refuses_overwrite(self):
        self.write("main.c", "int main(void){return 0;}\n")
        self.run_builder("--project", "test", "--source", "main.c")
        p = self.run_builder("--project", "test", "--source", "main.c", ok=False)
        self.assertNotEqual(p.returncode, 0)
        self.run_builder("--project", "test", "--source", "main.c", "--force")

    @unittest.skipUnless(HAS_CC, "requires gcc to compile the example")
    def test_builder_generates_compilable_executable_and_clean(self):
        self.write("hello.c", '#include <stdio.h>\nint main(void) { puts("HELLO"); return 0; }\n')
        self.run_builder("--project", "hello", "--source", "hello.c", "--output-dir", "out",
                         "--cflag", "-O2", "--section", "verify", "--command", "echo verification")
        self.assertIn("Execution:verify", (self.root / "LikeMakefile").read_text())
        self.run_make("build")
        exe = self.root / ("out/hello.exe" if os.name == "nt" else "out/hello")
        self.assertTrue(exe.is_file())
        self.assertEqual(subprocess.run([str(exe)], capture_output=True, text=True).stdout.strip(), "HELLO")
        self.assertIn("up to date", self.run_make("build").stdout)
        self.run_make("clean")
        self.assertFalse(exe.exists())

    def test_builder_msvc_command_generation(self):
        self.write("main.c", "int main(void) {return 0;}")
        self.run_builder("--project", "hello", "--source", "main.c", "--msvc",
                         "--compiler", "cl", "--linker", "link")
        text = (self.root / "LikeMakefile").read_text()
        self.assertIn("COMPILER /nologo /c", text)
        self.assertIn("LINKER", text)
        self.assertIn(" /OUT:", text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
