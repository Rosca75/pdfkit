# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Fetches, builds and verifies the reference panel's members, each pinned in
# tests/panel/members.tsv, one row per member and platform. Nothing here is linked, vendored or shipped: every member runs as a
# separate process started by tests/ (CLAUDE.md section 6), from
# build/cache/panel/ of the main checkout or, on a Linux host, from the
# distribution's packages.
#
# Per member and platform:
# - PDFium: the PDFium tier's pin (tests/pdfium/fetch.py) fetches it; this tool
#   checks the library's SHA-256 against its row.
# - Poppler, MuPDF and QPDF on Linux: the Ubuntu packages, never downloaded
#   here. Each package's version (dpkg-query) and each binary's SHA-256 must
#   equal the row; otherwise the member is refused by name with the apt
#   command that supplies it, and the others go on.
# - Poppler, MuPDF and QPDF on Windows: a release archive downloaded by URL
#   into build/cache/panel/<member>/<version>/, checked against its pinned
#   SHA-256 and extracted. A row whose version or digest says "to record on
#   the laptop" is refused unless --record is given, which downloads it, writes
#   the archive's and the executables' digests into the row for review and
#   commit, and extracts it.
# - The Rust members (oxide, extract, lopdf): built from tests/panel/rust/ by
#   `cargo build --release --locked` with CARGO_HOME and CARGO_TARGET_DIR under
#   build/cache/panel/, every crate pinned by checksum in the committed
#   Cargo.lock. On Linux cargo is the host's rustup toolchain at RUST_LINUX; on
#   Windows the x86_64-pc-windows-gnu toolchain archive of RUST_WINDOWS is
#   unpacked into build/cache/panel/rust/<version>/ first (each component's
#   manifest.in names its files) and links with the pinned WinLibs GCC.
#
# --verify re-hashes every binary and every archive present (the members'
# Windows archives, PDFium's archive and the Windows Rust toolchain's, each
# skipped by name when absent) and compares them with their pins, downloading
# and building nothing. --record writes the digests
# found on disk into this platform's rows (the first fetch of a pin), after
# which the rows are reviewed and committed. This is the one file of
# tests/panel/ that downloads, a named exemption of build/check_network.py.
#
# Sources: the MuPDF 1.28.5 Windows archive's URL and SHA-256 were read on
# https://mupdf.com/releases on 2026-11-07; the Rust 1.99.1
# x86_64-pc-windows-gnu archive's URL and SHA-256 (xz_hash) were read in
# https://static.rust-lang.org/dist/channel-rust-stable.toml (dated 2026-10-15)
# on 2026-11-07; the poppler-windows and QPDF release pages answer 403 through
# the Linux host's proxy, so their versions and digests are recorded on the
# laptop. The component layout of a Rust dist archive (components, each with a
# manifest.in of `file:` and `dir:` lines) is recalled from rust-installer.

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import io
import json
import os
import subprocess
import sys
import tarfile
import urllib.request
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(REPO / "dataset" / "tools"))

import common  # noqa: E402
import readers  # noqa: E402

RUST_DIR = HERE / "rust"
RUST_LINUX = "1.97.0"
RUST_WINDOWS = {
    "version": "1.99.1",
    "url": "https://static.rust-lang.org/dist/2026-10-15/rust-1.99.1-x86_64-pc-windows-gnu.tar.xz",
    "sha256": "0210624b2eb2bdf4ec32c2a6caf5c3bd2ab84e5df66c7530023e37f02add1084",
}
RUST_MEMBERS = {"oxide": "panel-oxide", "extract": "panel-extract", "lopdf": "panel-lopdf"}
PACKAGED = ("poppler", "mupdf", "qpdf")
# The executables each archive member keeps, by file name.
EXECUTABLES = {"poppler": ("pdftotext", "pdfinfo", "pdfimages", "pdftoppm"), "mupdf": ("mutool",),
               "qpdf": ("qpdf",)}
MAX_BYTES = 512 * 1024 * 1024


def exe(name: str) -> str:
    return name + (".exe" if readers.platform() == "windows" else "")


def cargo_env() -> dict:
    env = dict(os.environ)
    env["CARGO_HOME"] = str(readers.cache_dir() / "cargo-home")
    env["CARGO_TARGET_DIR"] = str(readers.cache_dir() / "cargo-target")
    if readers.platform() == "windows":
        toolchain = readers.cache_dir() / "rust" / RUST_WINDOWS["version"]
        env["PATH"] = str(toolchain / "bin") + os.pathsep + env.get("PATH", "")
    return env


def rust_binary(member: str) -> Path:
    return readers.cache_dir() / "cargo-target" / "release" / exe(RUST_MEMBERS[member])


def download(url: str, target: Path, pinned: str | None) -> bytes:
    """The archive at `url`, from `target` when present; refused when it differs from `pinned`."""
    if target.exists():
        data = target.read_bytes()
    else:
        request = urllib.request.Request(url, headers={"User-Agent": "pdfkit-panel-fetch"})
        try:
            with urllib.request.urlopen(request, timeout=300) as response:
                data = response.read(MAX_BYTES + 1)
        except OSError as err:
            raise common.ToolError(f"downloading {url} failed: {err}", "the archive reachable over HTTPS",
                                   f"check the proxy, or place the archive by hand at {target}") from err
        if len(data) > MAX_BYTES:
            raise common.ToolError(f"{url} is larger than {MAX_BYTES} bytes", "a release archive",
                                   "check the pinned URL")
    digest = hashlib.sha256(data).hexdigest()
    if pinned is not None and digest != pinned:
        raise common.ToolError(f"{url} has SHA-256 {digest}", f"the pinned SHA-256 {pinned}",
                               f"delete {target} and fetch again; if the release changed, pin it deliberately")
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists():
        target.write_bytes(data)
    return data


def safe_target(root: Path, name: str) -> Path | None:
    target = (root / name).resolve()
    return target if root.resolve() in target.parents else None


def extract_zip(data: bytes, into: Path) -> None:
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        for info in archive.infolist():
            target = safe_target(into, info.filename)
            if target is None or info.is_dir():
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(archive.read(info))


def install_rust(data: bytes, into: Path) -> None:
    """Merges every component of a Rust dist archive into one sysroot."""
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:xz") as archive:
        members = {m.name: m for m in archive.getmembers()}
        for name, member in members.items():
            parts = name.split("/")
            if len(parts) < 3 or not member.isfile() or parts[-1] in ("manifest.in", "components"):
                continue
            target = safe_target(into, "/".join(parts[2:]))
            source = archive.extractfile(member)
            if target is not None and source is not None:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(source.read())


def rust_version() -> str | None:
    try:
        out = subprocess.run(["rustc", "--version"], capture_output=True, text=True, env=cargo_env()).stdout
    except OSError:
        return None
    return out.split()[1] if out.startswith("rustc ") else None


def build_rust() -> None:
    """Builds the three Rust adapters from the committed Cargo.lock."""
    if readers.platform() == "windows":
        folder = readers.cache_dir() / "rust" / RUST_WINDOWS["version"]
        if not (folder / "bin" / "cargo.exe").exists():
            archive = readers.cache_dir() / "rust" / Path(RUST_WINDOWS["url"]).name
            install_rust(download(RUST_WINDOWS["url"], archive, RUST_WINDOWS["sha256"]), folder)
    wanted = RUST_WINDOWS["version"] if readers.platform() == "windows" else RUST_LINUX
    found = rust_version()
    if found != wanted:
        raise common.ToolError(
            f"rustc is {found or 'absent'}", f"rustc {wanted}",
            "install it with `rustup toolchain install " + wanted + "` and make it the default"
            if readers.platform() == "linux" else "delete build/cache/panel/rust/ and fetch again")
    proc = subprocess.run(["cargo", "build", "--release", "--locked"], cwd=RUST_DIR, env=cargo_env())
    if proc.returncode != 0:
        raise common.ToolError("cargo build --release --locked failed", "the three panel adapters built",
                               "read cargo's message above; the crates are pinned by tests/panel/rust/Cargo.lock")


def fetch_archive(row: dict, record: bool) -> None:
    """A Windows release archive of Poppler, MuPDF or QPDF, extracted into its cache folder."""
    member, version = row["member"], row["version"]
    if version == readers.NOT_RECORDED:
        raise common.ToolError(f"{member} has no pinned Windows version", "a version read on its release page",
                               "read the latest stable release on the laptop and write it in members.tsv")
    url = row["upstream"].replace("{version}", version)
    pinned = None if record else row["archive_sha256"]
    if pinned is not None and len(pinned) != 64:
        raise common.ToolError(f"{member} {version} has no recorded archive SHA-256",
                               "a digest in tests/panel/members.tsv",
                               "run `python tests/panel/fetch.py --member " + member + " --record` on the laptop, "
                               "check the digest against the release page and commit it")
    folder = readers.cache_dir() / member / answers_safe(version)
    data = download(url, folder / Path(url).name, pinned)
    row["archive_sha256"] = hashlib.sha256(data).hexdigest()
    extract_zip(data, folder / "files")
    if record:
        found = []
        for name in EXECUTABLES[member]:
            matches = sorted((folder / "files").rglob(exe(name)))
            if matches:
                found.append(matches[0])
        row["binaries"] = ";".join(f"{p.relative_to(readers.shared_cache()).as_posix()}:{readers.sha256_of(p)}"
                                   for p in found)


def answers_safe(version: str) -> str:
    return "".join(c if c.isalnum() or c in "._+" else "-" for c in version)


def record_binaries(row: dict) -> None:
    """Writes the digests of the row's binaries as they are on disk."""
    listed = []
    for path, _ in readers.binaries(row):
        if not path.is_file():
            raise common.ToolError(f"{path} is missing", "the binary built or installed",
                                   "fetch or build the member before recording it")
        name = path.relative_to(readers.shared_cache()).as_posix() if readers.shared_cache() in path.parents \
            else str(path)
        listed.append(f"{name}:{readers.sha256_of(path)}")
    row["binaries"] = ";".join(listed)


def verify(row: dict) -> dict:
    """One row's state: every binary and archive present and equal to its pin, or why not."""
    member = row["member"]
    readers.pin_problem.cache_clear()
    problem = readers.pin_problem(member)
    state = {"member": member, "platform": row["platform"], "version": row["version"],
             "binaries": [str(p) for p, _ in readers.binaries(row)]}
    if problem is None:
        digest = readers.live_digest(row)
        if digest != row["binary_digest"]:
            problem = (f"{member}: the binaries' combined digest is {digest}, members.tsv says "
                       f"{row['binary_digest']}; record it with --record")
    if problem is None and row["platform"] == "windows" and member in PACKAGED:
        folder = readers.cache_dir() / member / answers_safe(row["version"])
        archive = folder / Path(row["upstream"].replace("{version}", row["version"])).name
        if not archive.exists() or common.sha256_file(archive) != row["archive_sha256"]:
            problem = f"{member}: {archive} is missing or differs from its pinned SHA-256"
    state["ok"] = problem is None
    if problem:
        state["problem"] = problem
    else:
        state["digest"] = row["binary_digest"]
    return state


def verify_archive(name: str, path: Path, pinned: str) -> dict:
    """An archive re-hashed against its pin; one that is absent is skipped, named."""
    state = {"archive": name, "path": str(path)}
    if not path.exists():
        return dict(state, ok=True, skipped=f"absent: {path} is not on this host")
    digest = common.sha256_file(path)
    if digest != pinned:
        return dict(state, ok=False, problem=f"{name}: {path} has SHA-256 {digest}, pinned {pinned}")
    return dict(state, ok=True, digest=digest)


def archives() -> list[dict]:
    """PDFium's archive and the Windows Rust toolchain's, re-hashed where present."""
    spec = importlib.util.spec_from_file_location("pdfium_fetch", REPO / "tests" / "pdfium" / "fetch.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    rust = readers.cache_dir() / "rust" / Path(RUST_WINDOWS["url"]).name
    return [verify_archive("pdfium archive", module.tag_dir() / module.ASSET, module.SHA256),
            verify_archive("rust toolchain archive", rust, RUST_WINDOWS["sha256"])]


def main() -> int:
    parser = argparse.ArgumentParser(description="Fetch, build and verify the reference panel's members.")
    parser.add_argument("--member", action="append", help="only this member (repeatable); default every one")
    parser.add_argument("--verify", action="store_true", help="re-hash every archive and binary; fetch nothing")
    parser.add_argument("--record", action="store_true",
                        help="write the digests found on disk into this platform's rows of members.tsv")
    args = parser.parse_args()
    host = readers.platform()
    rows = readers.read_members()
    chosen = [r for r in rows if r["platform"] == host and (not args.member or r["member"] in args.member)]
    if not chosen:
        raise common.ToolError(f"no member row for {host} matches {args.member}", "a member of members.tsv",
                               "name one of " + ", ".join(sorted({r['member'] for r in rows})))
    if not args.verify:
        if any(r["member"] in RUST_MEMBERS for r in chosen):
            build_rust()
        for row in chosen:
            if row["member"] == "pdfium":
                spec = importlib.util.spec_from_file_location("pdfium_fetch", REPO / "tests" / "pdfium" / "fetch.py")
                module = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(module)
                module.fetch()
            elif host == "windows" and row["member"] in PACKAGED:
                fetch_archive(row, args.record)
    if args.record:
        for row in chosen:
            record_binaries(row)
            row["binary_digest"] = readers.combined_digest([d for _, d in readers.binaries(row)])
        common.write_tsv(readers.MEMBERS_TSV, readers.MEMBER_COLUMNS, rows, key=None)
        readers.pin.cache_clear()
    states = [verify(row) for row in chosen]
    if not args.member:
        states += archives()
    for state in states:
        print(json.dumps(state, ensure_ascii=False))
    refused = [s for s in states if not s["ok"]]
    if refused:
        print(f"{len(refused)} of {len(states)} members refused; the others answer", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    common.run_tool(main)
