# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Fetches the public sets of dataset/sources.tsv into the shared cache, pinned,
# and materialises the files the manifest names under dataset/files/<set>/.
# A git set is fetched at one commit, shallow, without blobs outside its sparse
# folders; an archive set is downloaded streamed with a size limit. The first
# fetch records the pin (commit or SHA-256) in sources.tsv and every later fetch
# refuses a different one, because a dataset that changes under us is not a
# regression suite. --verify re-hashes every manifest file. A source the proxy
# refuses is reported and skipped, never worked around. This developer tool is a
# named exemption of the network check: it downloads test data and never ships.

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import common  # noqa: E402

ARCHIVE_LIMIT = 4 * 1024 ** 3
USER_AGENT = "pdfkit-dataset-fetch"


def git_command(*args: str) -> list[str]:
    """git with TLS checked against the Windows certificate store on Windows.

    The laptop's proxy re-signs HTTPS with a certificate the Windows store
    trusts and git's bundled CA file does not, so git uses schannel there.
    """
    base = ["git"]
    if os.name == "nt":
        base += ["-c", "http.sslBackend=schannel"]
    return base + list(args)


def run_git(*args: str, cwd: Path | None = None) -> str:
    proc = subprocess.run(git_command(*args), cwd=cwd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise common.ToolError(
            f"git {' '.join(args)} failed: {proc.stderr.strip()[-400:]}",
            "the repository reachable over HTTPS and the pinned commit present",
            "check the proxy and the URL in sources.tsv; a source the proxy refuses is "
            "marked refused_by_proxy and skipped",
        )
    return proc.stdout


def parse_fetch(spec: str) -> tuple[str, list[str]]:
    """`git:<path>;<path>` (or `git:*`) gives the kind and the sparse folders."""
    kind, _, rest = spec.partition(":")
    paths = [p for p in rest.split(";") if p and p != "*"]
    return kind, paths


def set_cache(set_name: str) -> Path:
    return common.cache_dir() / set_name


def protect_bytes(repo: Path) -> None:
    """Checks every file out byte for byte, whatever core.autocrlf or .gitattributes say.

    A PDF without NUL bytes looks like text to git, and an end-of-line conversion
    on checkout moves every offset after the first line.
    """
    run_git("config", "core.autocrlf", "false", cwd=repo)
    run_git("config", "core.eol", "lf", cwd=repo)
    info = repo / ".git" / "info"
    info.mkdir(parents=True, exist_ok=True)
    (info / "attributes").write_text("* -text -diff -merge\n", encoding="ascii")


def fetch_git(source: dict, offline: bool) -> str:
    """Fetches a git set at its pin (or the remote HEAD on a first fetch)."""
    _, paths = parse_fetch(source["fetch"])
    target = set_cache(source["set"]) / "repo"
    pin = source["pin"]
    if target.exists() and (target / ".git").exists():
        head = run_git("rev-parse", "HEAD", cwd=target).strip()
        if pin and head != pin:
            raise common.ToolError(
                f"{target} is at {head}",
                f"the pinned commit {pin}",
                f"delete {target} and fetch again, or change the pin in sources.tsv deliberately",
            )
        if not (target / ".git" / "info" / "attributes").exists():
            # A checkout made with end-of-line conversion is written again byte for byte.
            protect_bytes(target)
            for entry in target.iterdir():
                if entry.name != ".git":
                    shutil.rmtree(entry) if entry.is_dir() else entry.unlink()
            run_git("checkout", "-q", "-f", "HEAD", cwd=target)
        return head
    if offline:
        raise common.ToolError(
            f"--offline and {target} holds no git checkout",
            "a clone placed by hand at that path",
            "clone the repository there at the pinned commit, then rerun with --offline",
        )
    if not pin:
        line = run_git("ls-remote", source["url"], "HEAD").split()
        if not line:
            raise common.ToolError(
                f"git ls-remote {source['url']} HEAD printed nothing",
                "the remote's HEAD commit", "check the URL in sources.tsv")
        pin = line[0]
    target.mkdir(parents=True, exist_ok=True)
    run_git("init", "-q", cwd=target)
    run_git("remote", "add", "origin", source["url"], cwd=target)
    run_git("config", "core.longpaths", "true", cwd=target)
    protect_bytes(target)
    if paths:
        # A folder is taken whole; a leading "!" excludes a sub-folder by name.
        patterns = [("!" if p.startswith("!") else "") + "/" + p.lstrip("!").strip("/") + "/"
                    for p in paths]
        run_git("sparse-checkout", "set", "--no-cone", *patterns, cwd=target)
    run_git("fetch", "-q", "--depth", "1", "--filter=blob:none", "origin", pin, cwd=target)
    run_git("checkout", "-q", "--detach", "FETCH_HEAD", cwd=target)
    head = run_git("rev-parse", "HEAD", cwd=target).strip()
    if head != pin:
        raise common.ToolError(f"checked out {head}", f"the pin {pin}", "fetch again")
    return head


def archive_name(source: dict) -> str:
    return source["url"].rstrip("/").rsplit("/", 1)[-1]


def fetch_archive(source: dict, offline: bool) -> str:
    """Downloads an archive set and checks or records its SHA-256."""
    target = set_cache(source["set"]) / archive_name(source)
    if not target.exists():
        if offline:
            raise common.ToolError(
                f"--offline and {target} is missing", "the archive placed by hand there",
                f"download {source['url']} on a network that allows it and place it at {target}")
        target.parent.mkdir(parents=True, exist_ok=True)
        part = target.with_suffix(target.suffix + ".part")
        digest = hashlib.sha256()
        size = 0
        request = urllib.request.Request(source["url"], headers={"User-Agent": USER_AGENT})
        try:
            with urllib.request.urlopen(request, timeout=120) as response, open(part, "wb") as out:
                while True:
                    block = response.read(1 << 20)
                    if not block:
                        break
                    size += len(block)
                    if size > ARCHIVE_LIMIT:
                        raise common.ToolError(
                            f"{source['url']} is larger than {ARCHIVE_LIMIT} bytes",
                            "an archive under the limit", "pick a smaller archive of the set")
                    digest.update(block)
                    out.write(block)
        except OSError as err:
            part.unlink(missing_ok=True)
            raise common.ToolError(
                f"downloading {source['url']} failed: {err}",
                "the archive reachable over HTTPS",
                "check the proxy; a source the proxy refuses is marked refused_by_proxy") from err
        os.replace(part, target)
        found = digest.hexdigest()
    else:
        found = common.sha256_file(target)
    if source["pin"] and source["pin"] != found:
        raise common.ToolError(
            f"{target} has SHA-256 {found}",
            f"the pinned SHA-256 {source['pin']}",
            f"delete {target} and fetch again, or change the pin deliberately",
        )
    return found


def fetch_set(source: dict, offline: bool) -> str:
    kind, _ = parse_fetch(source["fetch"])
    if kind == "git":
        return fetch_git(source, offline)
    if kind == "archive":
        return fetch_archive(source, offline)
    raise common.ToolError(
        f"set {source['set']} has fetch '{source['fetch']}'",
        "git:<folders> or archive", "fix the set's row in sources.tsv")


# ---------------------------------------------------------------------------
# From the cache to dataset/files/


def cached_member(source_url: str, sources: dict[str, dict], set_name: str) -> tuple[Path, str | None]:
    """The cache path of a row's file: a repository path, or an archive and its member."""
    if "#" in source_url:
        url, member = source_url.split("#", 1)
        return set_cache(set_name) / url.rstrip("/").rsplit("/", 1)[-1], member
    pin = sources[set_name]["pin"]
    match = re.search(re.escape(pin) + r"/(.+)$", source_url)
    if not match:
        raise common.ToolError(
            f"source {source_url} does not name the pinned commit {pin}",
            "a URL of the form <repository>/blob/<pin>/<path>",
            "re-register the file with add.py after fetching the set at its pin")
    from urllib.parse import unquote

    return set_cache(set_name) / "repo" / unquote(match.group(1)), None


def materialise(rows: list[dict], sources: dict[str, dict]) -> tuple[int, list[str]]:
    copied, missing = 0, []
    for row in rows:
        target = common.file_path(row)
        if target.exists():
            continue
        source = sources.get(row["set"])
        if source is None or not source["url"]:
            missing.append(f"{row['id']}: generated file absent; regenerate it ({row['source']})")
            continue
        path, member = cached_member(row["source"], sources, row["set"])
        if not path.exists():
            missing.append(f"{row['id']}: {path} absent; fetch the set {row['set']}")
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        if member is None:
            shutil.copyfile(path, target)
        else:
            with zipfile.ZipFile(path) as archive, archive.open(member) as src, open(target, "wb") as out:
                shutil.copyfileobj(src, out)
        copied += 1
    return copied, missing


def verify(rows: list[dict]) -> list[str]:
    problems = []
    for row in rows:
        path = common.file_path(row)
        if not path.exists():
            problems.append(f"{row['id']}: {path} is missing")
            continue
        digest = common.sha256_file(path)
        if digest != row["sha256"]:
            problems.append(f"{row['id']}: SHA-256 {digest}, manifest {row['sha256']}")
        elif str(path.stat().st_size) != row["bytes"]:
            problems.append(f"{row['id']}: {path.stat().st_size} bytes, manifest {row['bytes']}")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description="Fetch the dataset's public sets, pinned.")
    parser.add_argument("--set", action="append", help="fetch only these sets")
    parser.add_argument("--verify", action="store_true", help="re-hash every manifest file")
    parser.add_argument("--offline", action="store_true", help="register sets placed by hand")
    parser.add_argument("--no-materialise", action="store_true", help="only fetch the cache")
    args = parser.parse_args()

    sources = common.read_sources()
    by_set = {s["set"]: s for s in sources}
    rows = common.read_manifest()

    if args.verify:
        problems = verify(rows)
        for problem in problems:
            print(problem)
        print(json.dumps({"rows": len(rows), "verified": len(rows) - len(problems),
                          "problems": len(problems)}))
        if problems:
            print("FAIL: run `python dataset/tools/fetch.py` to restore files from the cache, "
                  "or regenerate the synthesised and mutated ones", file=sys.stderr)
            return 1
        return 0

    report = []
    changed = False
    for source in sources:
        if args.set and source["set"] not in args.set:
            continue
        if source["status"] != "admitted" or source["optional"] == "yes" or not source["url"]:
            continue
        try:
            pin = fetch_set(source, args.offline)
        except common.ToolError as err:
            report.append({"set": source["set"], "status": "failed", "failed": err.failed})
            print(err.as_json(), file=sys.stderr)
            continue
        if source["pin"] != pin:
            source["pin"] = pin
            changed = True
        report.append({"set": source["set"], "status": "fetched", "pin": pin})
    if changed:
        common.write_sources(sources)
    if not args.no_materialise:
        wanted = [r for r in rows if not args.set or r["set"] in args.set]
        copied, missing = materialise(wanted, by_set)
        report.append({"materialised": copied, "missing": missing})
    print(json.dumps(report, indent=1))
    return 1 if any(r.get("status") == "failed" for r in report) else 0


if __name__ == "__main__":
    common.run_tool(main)
