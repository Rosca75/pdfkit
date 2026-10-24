# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Checks that nothing opens a network connection (CLAUDE.md section 9): no
# socket, HTTP or URL API and no URL used as a fetch target in the code under
# include/, src/ and cli/; no link to a networking library in any CMake file of
# ours; and no network module imported by the Python tools under build/,
# tests/ and dataset/tools/. The named exemptions below are developer tools
# that download test inputs and never ship; an exemption whose file is gone
# fails, unless build/paths_pending.tsv says a later phase creates it.
"""Check for network use: python build/check_network.py [--root <tree>]."""

from __future__ import annotations

import ast
import re
from pathlib import Path

import checklib

EXEMPTIONS = {
    "dataset/tools/fetch.py": "downloads the public dataset sets, pinned by hash, into the "
                              "ignored dataset cache; a developer tool that never ships",
    "tests/pdfium/fetch.py": "downloads the pinned PDFium test binary into the shared cache; "
                             "test-only, never linked or shipped",
}
C_APIS = re.compile(
    r"\b(socket|WSAStartup|getaddrinfo|gethostbyname|URLDownloadToFile\w*|InternetOpen\w*|"
    r"InternetConnect\w*|HttpOpenRequest\w*|WinHttp\w+|curl_\w+)\s*\(")
C_HEADERS = re.compile(
    r'#\s*include\s*[<"](winsock2?\.h|ws2tcpip\.h|wininet\.h|winhttp\.h|urlmon\.h|curl/[^>"]+|'
    r'sys/socket\.h|netdb\.h|netinet/[^>"]+|arpa/inet\.h)[>"]', re.IGNORECASE)
URL_LITERAL = re.compile(r'"((?:https?|ftp|file)://[^"]*)"')
# XMP and RDF namespace names are identifiers in the metadata format; they are
# compared, never fetched.
NAMESPACE_URIS = re.compile(r"^https?://(ns\.adobe\.com|www\.w3\.org|purl\.org|www\.aiim\.org|"
                            r"iptc\.org|ns\.useplus\.org|www\.npes\.org)/")
LINKED = re.compile(r"\b(ws2_32|wsock32|wininet|winhttp|urlmon|curl|libcurl)\b", re.IGNORECASE)
PY_MODULES = {"socket", "ssl", "http", "urllib", "urllib3", "requests", "httpx", "aiohttp",
              "ftplib", "smtplib", "poplib", "imaplib", "telnetlib", "xmlrpc", "websocket",
              "websockets"}


def finding(path: str, line: int, failed: str, expected: str, fix: str) -> checklib.Finding:
    return checklib.Finding(path, line, failed, expected, fix)


def check_c(path: str, text: str) -> list[checklib.Finding]:
    findings: list[checklib.Finding] = []
    code, _ = checklib.split_c_like(text)
    fix = "the library takes bytes and returns bytes; move any download into a developer tool"
    for m in C_APIS.finditer(code):
        findings.append(finding(path, checklib.line_of(code, m.start()),
                                f"calls the network API {m.group(1)}",
                                "no socket, HTTP or URL API in the library or the CLI", fix))
    for m in C_HEADERS.finditer(text):
        findings.append(finding(path, checklib.line_of(text, m.start()),
                                f"includes the network header {m.group(1)}",
                                "no networking header in the library or the CLI", fix))
    for m in URL_LITERAL.finditer(text):
        line = checklib.line_of(text, m.start())
        if code.split("\n")[line - 1].strip() == "":
            continue
        if NAMESPACE_URIS.match(m.group(1)):
            continue
        findings.append(finding(path, line, f"holds the URL {m.group(1)} in code",
                                "no URL a program could fetch; sources belong in comments",
                                "move the URL into a comment, or name the namespace in the "
                                "check's list if it is an identifier"))
    return findings


def check_cmake(path: str, text: str) -> list[checklib.Finding]:
    findings: list[checklib.Finding] = []
    commented = {line for line, _ in checklib.hash_comments(text)}
    for number, line in enumerate(text.split("\n"), start=1):
        code = line.split("#", 1)[0] if number in commented else line
        m = LINKED.search(code)
        if m:
            findings.append(finding(path, number, f"links the networking library {m.group(1)}",
                                    "no networking library linked into any target",
                                    "remove the library; pdfkit opens no connection"))
    return findings


def check_python(path: str, text: str) -> list[checklib.Finding]:
    findings: list[checklib.Finding] = []
    try:
        tree = ast.parse(text)
    except SyntaxError:
        return findings
    for node in ast.walk(tree):
        names: list[str] = []
        if isinstance(node, ast.Import):
            names = [alias.name for alias in node.names]
        elif isinstance(node, ast.ImportFrom) and node.module:
            names = [node.module]
        for name in names:
            if name.split(".")[0] in PY_MODULES:
                findings.append(finding(path, node.lineno, f"imports the network module {name}",
                                        "no network module outside the named exemptions of "
                                        "build/check_network.py",
                                        "read inputs from the shared cache, or add the tool to "
                                        "the exemptions with its reason"))
    return findings


def check(root: Path) -> list[checklib.Finding]:
    findings: list[checklib.Finding] = []
    pending = checklib.pending_paths(root)
    for path, reason in EXEMPTIONS.items():
        if not (root / path).is_file() and path not in pending:
            findings.append(finding(path, 0, f"the exempted file {path} does not exist",
                                    f"an exemption names a file that exists ({reason})",
                                    "remove the exemption, or list the path in "
                                    "build/paths_pending.tsv with the phase that creates it"))
    for path in checklib.walk(root, ("include", "src", "cli")):
        if Path(path).suffix in {".h", ".hpp", ".cpp"}:
            findings += check_c(path, checklib.read_text(root, path))
    cmake_files = [p for p in checklib.walk(root) if p.endswith(("CMakeLists.txt", ".cmake"))
                   and not p.startswith("third_party/")
                   or p.startswith("third_party/") and p.count("/") == 2
                   and p.endswith("CMakeLists.txt")]
    for path in cmake_files:
        findings += check_cmake(path, checklib.read_text(root, path))
    for path in checklib.walk(root, ("build", "tests", "dataset/tools")):
        if path.endswith(".py") and path not in EXEMPTIONS:
            findings += check_python(path, checklib.read_text(root, path))
    return findings


def main() -> int:
    args = checklib.parse_args(__doc__)
    return checklib.report("check_network", check(Path(args.root)))


if __name__ == "__main__":
    checklib.run_main(main)
