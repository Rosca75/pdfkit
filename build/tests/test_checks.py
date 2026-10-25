# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Tests of the check scripts: each runs over a temporary copy of the
# repository, first clean (it must pass) and then with one planted violation
# (it must fail and name the file). A check that passes a planted violation
# guards nothing, so every check has at least one planted case here.
"""Test the checks: python build/tests/test_checks.py (or python -m pytest build/tests)."""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
IGNORED = shutil.ignore_patterns(".git", ".claude", "out", "dist", "cache", "__pycache__", "bin",
                                 "obj", ".vs", ".vscode", ".idea")


def make_tree() -> Path:
    folder = Path(tempfile.mkdtemp(prefix="pdfkit-checks-"))
    tree = folder / "repo"
    shutil.copytree(REPO, tree, ignore=IGNORED)
    subprocess.run(["git", "init", "-q"], cwd=tree, check=True)
    subprocess.run(["git", "-c", "core.autocrlf=false", "add", "-A"], cwd=tree, check=True,
                   capture_output=True)
    return tree


def run_check(tree: Path, script: str, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run([sys.executable, str(tree / "build" / script), "--root", str(tree), *args],
                          cwd=tree, capture_output=True, text=True, check=False)


def append(path: Path, text: str) -> None:
    with path.open("a", encoding="utf-8", newline="\n") as handle:
        handle.write(text)


def replace(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    if old not in text:
        raise AssertionError(f"{old!r} is not in {path}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


class CheckTest(unittest.TestCase):
    tree: Path

    def setUp(self) -> None:
        self.tree = make_tree()

    def tearDown(self) -> None:
        shutil.rmtree(self.tree.parent, ignore_errors=True)

    def assert_fails(self, script: str, *needles: str, args: tuple[str, ...] = ()) -> None:
        result = run_check(self.tree, script, *args)
        self.assertEqual(result.returncode, 1, f"{script} passed a planted violation:\n"
                                               f"{result.stdout}{result.stderr}")
        for needle in needles:
            self.assertIn(needle, result.stdout, f"{script} did not name {needle!r}:\n"
                                                 f"{result.stdout}")


class CleanTreeTest(CheckTest):
    def test_every_file_check_passes_the_clean_tree(self) -> None:
        for script in ("check_layout.py", "check_headers.py", "check_comments.py",
                       "check_catalogue.py", "check_third_party.py", "check_network.py",
                       "check_repo.py", "check_paths.py", "check_generated.py"):
            with self.subTest(script=script):
                result = run_check(self.tree, script)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class LayoutTest(CheckTest):
    def test_a_layer_including_a_layer_it_may_not(self) -> None:
        append(self.tree / "src/syntax/syntax_placeholder.cpp", '#include "writer/writer.hpp"\n')
        self.assert_fails("check_layout.py", "src/syntax/syntax_placeholder.cpp",
                          "includes the writer layer")

    def test_a_namespace_that_does_not_match_its_folder(self) -> None:
        replace(self.tree / "src/fonts/ligatures.cpp", "namespace pdfkit::fonts",
                "namespace pdfkit::text")
        self.assert_fails("check_layout.py", "src/fonts/ligatures.cpp", "pdfkit::text")

    def test_a_header_with_two_public_classes(self) -> None:
        replace(self.tree / "src/platform/budget.hpp", "class Budget {",
                "class Extra {};\n\nclass Budget {")
        self.assert_fails("check_layout.py", "src/platform/budget.hpp", "2 public classes")

    def test_an_os_header_in_the_cli(self) -> None:
        replace(self.tree / "cli/main.cpp", "#include <array>", "#include <windows.h>\n#include <array>")
        self.assert_fails("check_layout.py", "cli/main.cpp", "windows.h")

    def test_a_platform_macro_outside_the_platform_layer(self) -> None:
        append(self.tree / "src/text/text_placeholder.cpp", "#ifdef _WIN32\n#endif\n")
        self.assert_fails("check_layout.py", "src/text/text_placeholder.cpp", "_WIN32")

    def test_the_cli_including_a_library_layer(self) -> None:
        replace(self.tree / "cli/main.cpp", "#include <array>",
                '#include "platform/error.hpp"\n#include <array>')
        self.assert_fails("check_layout.py", "cli/main.cpp", "platform/error.hpp")


class HeadersTest(CheckTest):
    def test_a_source_without_its_licence_line(self) -> None:
        replace(self.tree / "src/platform/error.cpp", "// SPDX-License-Identifier: Apache-2.0\n", "")
        self.assert_fails("check_headers.py", "src/platform/error.cpp", "SPDX")

    def test_a_dataset_tool_without_its_purpose_header(self) -> None:
        tool = self.tree / "tests/dataset/tool.py"
        tool.parent.mkdir(parents=True, exist_ok=True)
        tool.write_text("# SPDX-License-Identifier: Apache-2.0\n"
                        "# Copyright 2026 The pdfkit authors\n\nprint(1)\n", encoding="utf-8")
        self.assert_fails("check_headers.py", "tests/dataset/tool.py", "purpose header")


class CommentsTest(CheckTest):
    def test_a_comment_naming_a_build_phase(self) -> None:
        append(self.tree / "src/platform/error.cpp", "// Filled in by phase 3.\n")
        self.assert_fails("check_comments.py", "src/platform/error.cpp", "names a build phase")

    def test_a_todo_without_its_question(self) -> None:
        append(self.tree / "build/check_repo.py", "# TODO tidy this\n")
        self.assert_fails("check_comments.py", "build/check_repo.py", "TODO")

    def test_delta_words(self) -> None:
        append(self.tree / "src/platform/error.cpp", "// This no longer throws.\n")
        self.assert_fails("check_comments.py", "src/platform/error.cpp", "no longer")


class CatalogueTest(CheckTest):
    def test_a_registry_row_removed(self) -> None:
        catalogue = self.tree / "src/api/catalogue.cpp"
        lines = catalogue.read_text(encoding="utf-8").split("\n")
        lines = [line for line in lines if '"pdfkit_text"' not in line]
        catalogue.write_text("\n".join(lines), encoding="utf-8", newline="\n")
        self.assert_fails("check_catalogue.py", "src/api/catalogue.cpp")

    def test_a_verb_file_removed(self) -> None:
        (self.tree / "cli/verbs/check.cpp").unlink()
        self.assert_fails("check_catalogue.py", "cli/verbs/check.cpp")

    def test_a_symbol_missing_from_the_header(self) -> None:
        replace(self.tree / "include/pdfkit/pdfkit.h", "PDFKIT_API pdfkit_status pdfkit_metadata(",
                "pdfkit_status pdfkit_metadata(")
        self.assert_fails("check_catalogue.py", "include/pdfkit/pdfkit.h", "pdfkit_metadata")


class ThirdPartyTest(CheckTest):
    def test_an_edited_vendored_file(self) -> None:
        append(self.tree / "third_party/doctest/doctest.h", "// local edit\n")
        self.assert_fails("check_third_party.py", "third_party/inventory.tsv", "tree hash")

    def test_a_licence_outside_the_list(self) -> None:
        replace(self.tree / "third_party/inventory.tsv", "\tMIT\t", "\tGPL-3.0-only\t")
        self.assert_fails("check_third_party.py", "third_party/inventory.tsv", "GPL-3.0-only")

    def test_a_folder_without_a_row(self) -> None:
        folder = self.tree / "third_party/zlib"
        folder.mkdir()
        (folder / "LICENSE").write_text("zlib licence\n", encoding="utf-8")
        self.assert_fails("check_third_party.py", "third_party/zlib")


class NetworkTest(CheckTest):
    def test_a_socket_header_in_the_library(self) -> None:
        append(self.tree / "src/syntax/syntax_placeholder.cpp", "#include <winsock2.h>\n")
        self.assert_fails("check_network.py", "src/syntax/syntax_placeholder.cpp", "winsock2.h")

    def test_a_url_in_code(self) -> None:
        append(self.tree / "src/syntax/syntax_placeholder.cpp",
               'namespace pdfkit::syntax { const char* where = "https://example.com/x.pdf"; }\n')
        self.assert_fails("check_network.py", "src/syntax/syntax_placeholder.cpp", "example.com")

    def test_a_python_tool_importing_urllib(self) -> None:
        (self.tree / "build/tool_fetch.py").write_text("import urllib.request\n", encoding="utf-8")
        self.assert_fails("check_network.py", "build/tool_fetch.py", "urllib.request")

    def test_a_networking_library_linked(self) -> None:
        append(self.tree / "cli/CMakeLists.txt", "target_link_libraries(pdfkit_cli PRIVATE ws2_32)\n")
        self.assert_fails("check_network.py", "cli/CMakeLists.txt", "ws2_32")

    def test_an_exemption_whose_file_is_gone_and_not_pending(self) -> None:
        pending = self.tree / "build/paths_pending.tsv"
        lines = pending.read_text(encoding="utf-8").split("\n")
        lines = [line for line in lines if not line.startswith("tests/pdfium/fetch.py\t")]
        pending.write_text("\n".join(lines), encoding="utf-8", newline="\n")
        self.assert_fails("check_network.py", "tests/pdfium/fetch.py")


class RepoTest(CheckTest):
    def test_a_committed_pdf(self) -> None:
        (self.tree / "fixture.bin").write_bytes(b"%PDF-1.7\n%%EOF\n")
        subprocess.run(["git", "add", "-f", "fixture.bin"], cwd=self.tree, check=True)
        self.assert_fails("check_repo.py", "fixture.bin", "%PDF")


class PathsTest(CheckTest):
    def test_a_dead_cited_path(self) -> None:
        append(self.tree / "docs/DESIGN.md", "\nSee `src/nowhere/gone.cpp`.\n")
        self.assert_fails("check_paths.py", "docs/DESIGN.md", "src/nowhere/gone.cpp")

    def test_a_pending_path_that_now_exists(self) -> None:
        append(self.tree / "build/paths_pending.tsv", "src/writer/planted_pending.cpp\t7\ta planted row\n")
        (self.tree / "src/writer/planted_pending.cpp").write_text("// x\n", encoding="utf-8")
        self.assert_fails("check_paths.py", "build/paths_pending.tsv", "src/writer/planted_pending.cpp")


class GeneratedTest(CheckTest):
    def test_a_generated_table_edited_by_hand(self) -> None:
        replace(self.tree / "src/fonts/generated/core14_table.cpp", '{"A", 65, 667}',
                '{"A", 65, 668}')
        self.assert_fails("check_generated.py", "src/fonts/generated/core14_table.cpp",
                          "differs from its generator's output")


class FormatTest(CheckTest):
    def test_a_badly_formatted_line(self) -> None:
        sys.path.insert(0, str(REPO / "build"))
        import check_format  # noqa: PLC0415

        if check_format.find_tool("clang-format") is None:
            self.skipTest("clang-format is absent; install it with python -m pip install --user "
                          "--require-hashes -r build/requirements-dev.txt")
        append(self.tree / "src/platform/error.cpp",
               "namespace pdfkit::platform {  int   badly_formatted ( ) ; }\n")
        self.assert_fails("check_format.py", "src/platform/error.cpp", args=("--no-tidy",))


class ArtefactTest(CheckTest):
    def test_a_dll_that_imports_the_compiler_runtime(self) -> None:
        compiler = shutil.which("g++")
        if compiler is None or shutil.which("objdump") is None:
            self.skipTest("g++ or objdump is absent; put the WinLibs bin folder of "
                          "build/toolchain.md on PATH")
        folder = self.tree / "out" / "planted"
        folder.mkdir(parents=True)
        source = folder / "probe.cpp"
        source.write_text('#include <string>\nextern "C" __declspec(dllexport) int '
                          "pdfkit_abi_version() { return static_cast<int>(std::to_string(1)"
                          ".size()); }\n", encoding="utf-8")
        subprocess.run([compiler, "-shared", str(source), "-o", str(folder / "pdfkit.dll")],
                       check=True)
        self.assert_fails("check_artefact.py", "pdfkit.dll imports libstdc++-6.dll",
                          "pdfkit.exe is missing", "pdfkit_text is not exported",
                          args=(str(folder),))


if __name__ == "__main__":
    unittest.main(verbosity=2)
