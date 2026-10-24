# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Checks the architecture rules that keep a parser change from breaking the
# writer in silence (CLAUDE.md sections 3.1, 3.4 and 3.5): the folder layout;
# that every source under src/<layer>/ is in the namespace pdfkit::<layer>;
# that a header declares at most one public class, named after the file; that
# each layer includes only the layers and libraries its row of the table
# allows; that the CLI includes only the public header; and that no OS header,
# platform macro, intrinsic or export attribute appears outside src/platform/.
"""Check the layout, namespaces, classes and includes: python build/check_layout.py [--root <tree>]."""

from __future__ import annotations

import re
from pathlib import Path

import checklib

# CLAUDE.md section 3.1: what each layer may include, besides itself.
ALLOWED_LAYERS = {
    "platform": set(),
    "syntax": {"platform"},
    "fonts": {"syntax", "platform"},
    "text": {"fonts", "syntax", "platform"},
    "images": {"text", "syntax", "platform"},
    "writer": {"syntax", "platform"},
    "redact": {"writer", "images", "text", "fonts", "syntax", "platform"},
    "api": {"platform", "syntax", "fonts", "text", "images", "writer", "redact"},
}
ALLOWED_LIBRARIES = {
    "syntax": {"zlib"},
    "images": {"libjpeg-turbo", "openjpeg", "zlib"},
    "writer": {"zlib"},
    "api": {"zlib", "libjpeg-turbo", "openjpeg"},
}
# The public headers of each vendored library, as the sources include them.
LIBRARY_HEADERS = {
    "zlib.h": "zlib", "zconf.h": "zlib",
    "jpeglib.h": "libjpeg-turbo", "jerror.h": "libjpeg-turbo", "jconfig.h": "libjpeg-turbo",
    "jmorecfg.h": "libjpeg-turbo", "turbojpeg.h": "libjpeg-turbo",
    "openjpeg.h": "openjpeg", "opj_config.h": "openjpeg",
    "doctest.h": "doctest",
}
OS_HEADERS = re.compile(
    r"^(windows|winsock2?|ws2tcpip|wininet|winhttp|winbase|winnt|windef|io|fcntl|unistd|direct|"
    r"process|dlfcn|pthread|intrin|x86intrin|immintrin|emmintrin|malloc|shlwapi|shellapi|objbase|"
    r"bcrypt|wincrypt|sys/.*|netinet/.*|arpa/.*|netdb|mach/.*)\.h$",
    re.IGNORECASE)
PLATFORM_TOKENS = re.compile(
    r"\b(_WIN32|_WIN64|__linux__|__APPLE__|__unix__|__GNUC__|__clang__|_MSC_VER|__MINGW32__|"
    r"__MINGW64__|__declspec|__attribute__|__builtin_\w+|__cpuid\w*|_mm_\w+)\b")
INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.MULTILINE)
NAMESPACE = re.compile(r"\bnamespace\s+(pdfkit(?:::\w+)*)\s*\{")
CLASS = re.compile(r"\b(class|struct)\s+(\w+)\s*(?:final\s*)?([:{;])")
NAMESPACE_OPEN = re.compile(r"\bnamespace\b\s*([\w:]*)\s*(?=\{)")
# In blanked code a string literal keeps only its quotes, so extern "C" reads extern " ".
EXTERN_BLOCK = re.compile(r'\bextern\s+"\s*"\s*(?=\{)')
EXTERN_C = re.compile(r'\bextern\s+"C"')
PREPROCESSOR = re.compile(r"^\s*#.*$", re.MULTILINE)
CPP_SUFFIXES = {".h", ".hpp", ".cpp"}
REQUIRED_FOLDERS = ["include/pdfkit", "cli", "cli/verbs", "tests/unit", "docs", "build"] + [
    f"src/{layer}" for layer in checklib.LAYERS]


def camel(stem: str) -> str:
    return "".join(part[:1].upper() + part[1:] for part in stem.split("_"))


def finding(path: str, line: int, failed: str, expected: str, fix: str) -> checklib.Finding:
    return checklib.Finding(path, line, failed, expected, fix)


def public_classes(code: str) -> list[tuple[int, str]]:
    """Classes and structs defined at namespace scope, outside anonymous namespaces."""
    found: list[tuple[int, str]] = []
    scopes: list[str] = []
    i = 0
    pending = "other"
    while i < len(code):
        ch = code[i]
        if ch == "{":
            scopes.append(pending)
            pending = "other"
        elif ch == "}":
            if scopes:
                scopes.pop()
        elif ch == ";":
            pending = "other"
        else:
            m = NAMESPACE_OPEN.match(code, i)
            if m:
                pending = "namespace" if m.group(1) else "anonymous"
                i = m.end()
                continue
            m = EXTERN_BLOCK.match(code, i)
            if m:
                pending = "namespace"
                i = m.end()
                continue
            m = CLASS.match(code, i)
            if m and (i == 0 or not (code[i - 1].isalnum() or code[i - 1] == "_")):
                before = code[max(0, i - 6):i]
                at_namespace_scope = all(scope == "namespace" for scope in scopes)
                if m.group(3) != ";" and "enum" not in before and at_namespace_scope:
                    found.append((i, m.group(2)))
        i += 1
    return found


def check_namespace(path: str, layer_path: list[str], code: str, text: str) -> list[checklib.Finding]:
    expected = "pdfkit::" + "::".join(layer_path)
    names = [m.group(1) for m in NAMESPACE.finditer(code)]
    findings: list[checklib.Finding] = []
    if not names:
        # The C ABI's definitions live in the global namespace by C's rules,
        # and a header of macros only declares nothing a namespace could hold.
        only_c_abi = layer_path[0] == "api" and EXTERN_C.search(text) is not None
        only_macros = not PREPROCESSOR.sub("", code).strip()
        if not only_c_abi and not only_macros:
            findings.append(finding(path, 1, "the file declares no namespace",
                                    f"namespace {expected}",
                                    f"wrap the file's declarations in namespace {expected} {{ }}"))
    for name in names:
        if name != expected:
            findings.append(finding(path, 1, f"the file declares namespace {name}",
                                    f"namespace {expected}, the namespace of its folder",
                                    "move the file to the folder of its namespace or rename the "
                                    "namespace"))
    return findings


def check_includes(path: str, text: str, kind: str, layer: str | None) -> list[checklib.Finding]:
    findings: list[checklib.Finding] = []
    for m in INCLUDE.finditer(text):
        bracket, target = m.group(1), m.group(2)
        line = checklib.line_of(text, m.start())
        first = target.split("/", 1)[0]
        name = target.rsplit("/", 1)[-1]
        if OS_HEADERS.match(target) and layer != "platform":
            findings.append(finding(path, line, f"includes the OS header <{target}>",
                                    "OS headers in src/platform/ only (CLAUDE.md section 3.4)",
                                    "move the OS-specific code into src/platform/ behind a "
                                    "portable function"))
            continue
        if kind == "cli":
            if bracket == '"' and target != "pdfkit/pdfkit.h" and (
                    first in checklib.LAYERS or target.startswith(("../", "src/"))):
                findings.append(finding(path, line, f'includes "{target}" from the library',
                                        'only "pdfkit/pdfkit.h" from the library; the CLI calls '
                                        "the C ABI and nothing beneath it",
                                        "call the C ABI instead, or move the helper into cli/"))
            if bracket == "<" and name in LIBRARY_HEADERS:
                findings.append(finding(path, line, f"includes the library header <{target}>",
                                        "no vendored library in the CLI",
                                        "call the C ABI instead"))
            continue
        if kind != "src" or layer is None:
            continue
        if bracket == '"' and target == "pdfkit/pdfkit.h" and layer != "api":
            findings.append(finding(path, line, "includes the public C header",
                                    "the C header in src/api/ only", "move the code into src/api/"))
        elif bracket == '"' and first in checklib.LAYERS and first != layer \
                and first not in ALLOWED_LAYERS[layer]:
            allowed = ", ".join(sorted(ALLOWED_LAYERS[layer])) or "no other layer"
            findings.append(finding(path, line, f"the {layer} layer includes the {first} layer",
                                    f"the {layer} layer includes {allowed} (CLAUDE.md section 3.1)",
                                    "move the code to a layer allowed to depend on both, or "
                                    "change the table in CLAUDE.md first"))
        elif bracket == "<" and name in LIBRARY_HEADERS:
            library = LIBRARY_HEADERS[name]
            if library not in ALLOWED_LIBRARIES.get(layer, set()):
                findings.append(finding(path, line, f"the {layer} layer includes {library}",
                                        f"{library} only in the layers CLAUDE.md section 3.1 "
                                        "lists for it", "move the code into an allowed layer"))
    return findings


def check_platform_tokens(path: str, code: str) -> list[checklib.Finding]:
    findings: list[checklib.Finding] = []
    for m in PLATFORM_TOKENS.finditer(code):
        findings.append(finding(path, checklib.line_of(code, m.start()),
                                f"uses the platform-specific token {m.group(1)}",
                                "compiler and OS specifics in src/platform/ only "
                                "(CLAUDE.md section 3.4)",
                                "move the code into src/platform/ behind a portable function or "
                                "macro"))
    return findings


def check_tree(root: Path) -> list[checklib.Finding]:
    findings: list[checklib.Finding] = []
    for folder in REQUIRED_FOLDERS:
        if not (root / folder).is_dir():
            findings.append(finding(folder, 0, f"the folder {folder}/ is missing",
                                    "the repository layout of CLAUDE.md section 3.5",
                                    f"create {folder}/ as the charter lays out"))
    for path in checklib.walk(root, ("include",)):
        if path != "include/pdfkit/pdfkit.h":
            findings.append(finding(path, 0, "a file other than the public header under include/",
                                    "include/pdfkit/pdfkit.h and nothing else",
                                    "move internal headers into src/<layer>/"))
    for path in checklib.walk(root, ("src",)):
        parts = path.split("/")
        if len(parts) == 2:
            if parts[1] != "CMakeLists.txt":
                findings.append(finding(path, 0, "a file directly under src/",
                                        "every source in src/<layer>/", "move it into its layer"))
            continue
        if parts[1] not in checklib.LAYERS:
            findings.append(finding(path, 0, f"src/{parts[1]}/ is not a layer",
                                    "one of " + ", ".join(checklib.LAYERS),
                                    "move the file into its layer's folder"))
    for path in checklib.walk(root, ("src", "cli", "include", "tests")):
        suffix = Path(path).suffix
        if suffix not in CPP_SUFFIXES:
            continue
        text = checklib.read_text(root, path)
        code, _ = checklib.split_c_like(text)
        top = path.split("/", 1)[0]
        parts = path.split("/")
        layer = parts[1] if top == "src" and len(parts) > 2 and parts[1] in checklib.LAYERS else None
        if top == "src" and layer is not None:
            findings += check_namespace(path, parts[1:-1], code, text)
        if top in ("src", "cli") and suffix in {".h", ".hpp"}:
            classes = public_classes(code)
            classes = [(at, name) for at, name in classes if not name.startswith("pdfkit_")]
            if len(classes) > 1:
                findings.append(finding(path, checklib.line_of(code, classes[1][0]),
                                        f"the header declares {len(classes)} public classes ("
                                        + ", ".join(name for _, name in classes) + ")",
                                        "one public class per header, named after the file",
                                        "move each further class into a header named after it"))
            elif classes and classes[0][1] != camel(Path(path).stem):
                findings.append(finding(path, checklib.line_of(code, classes[0][0]),
                                        f"the header declares {classes[0][1]}",
                                        f"the class named after the file, {camel(Path(path).stem)}",
                                        "rename the file or the class so they match"))
        findings += check_includes(path, _include_lines(text), top, layer)
        if layer != "platform":
            findings += check_platform_tokens(path, code)
    return findings


def _include_lines(text: str) -> str:
    """The text with comments removed but string literals kept, so quoted
    include targets stay readable."""
    out = []
    for line in text.split("\n"):
        stripped = line.lstrip()
        out.append(line if stripped.startswith("#") else "")
    return "\n".join(out)


def main() -> int:
    args = checklib.parse_args(__doc__)
    return checklib.report("check_layout", check_tree(Path(args.root)))


if __name__ == "__main__":
    checklib.run_main(main)
