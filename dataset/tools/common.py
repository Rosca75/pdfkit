# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Shared helpers of the dataset tools: where the shared dataset root is, how the
# manifest, taxonomy and sources tables are read and written, the SHA-256 of a
# file, the run-id format and the actionable failure every tool reports. Every
# worktree reads one copy of the downloaded and selected files, so the shared
# root is the main checkout's dataset folder, found through git's common
# directory, unless PDFKIT_DATASET_ROOT names another folder.

from __future__ import annotations

import datetime
import functools
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

MANIFEST_COLUMNS = (
    "id", "set", "source", "licence", "sha256", "bytes", "pages", "producer",
    "tags", "expect_open", "expect_text", "expect_images", "expect_redact",
    "expect_metadata", "expect_fonts", "expect_write", "expect_select", "notes",
)
TAXONOMY_COLUMNS = ("tag", "group", "minimum", "description", "detector")
SOURCES_COLUMNS = (
    "set", "url", "fetch", "pin", "licence", "licence_url", "optional", "select",
    "status", "notes",
)

# The expectation columns, each with the catalogue function it checks and the
# values it may hold (a value ending in ":" takes a free-form reason after it).
EXPECTATIONS = {
    "expect_open": ("open", ("opens", "repaired", "refused:")),
    "expect_text": ("text", ("agree", "known_gap:", "n/a")),
    "expect_images": ("images", ("decoded", "partial:", "n/a")),
    "expect_redact": ("redact", ("stable", "n/a")),
    "expect_metadata": ("metadata", ("agree", "n/a")),
    # The decoding ladder's unreadable codes, read from the text outcome; filled by
    # tests/pdfium/text_agreement.py --write-manifest.
    "expect_fonts": ("text", ("loads", "unreadable:", "n/a")),
    "expect_write": ("write", ("identical", "known_gap:", "n/a")),
    "expect_select": ("select", ("ok", "known_gap:", "n/a")),
}

# The refusal codes of docs/DESIGN.md chapter 19 an `expect_open` may name.
REFUSAL_CODES = (
    "not_pdf", "no_page", "password_required", "unsupported_security_handler",
    "limit_exceeded", "page_ceiling",
)


class ToolError(Exception):
    """A failure a tool reports with what failed, what was expected and the fix."""

    def __init__(self, failed: str, expected: str, fix: str):
        super().__init__(failed)
        self.failed = failed
        self.expected = expected
        self.fix = fix

    def as_json(self) -> str:
        return json.dumps(
            {"failed": self.failed, "expected": self.expected, "fix": self.fix},
            ensure_ascii=False,
        )


def run_tool(main) -> None:
    """Runs a tool's main function and turns a ToolError into exit code 2."""
    try:
        code = main()
    except ToolError as err:
        print(err.as_json(), file=sys.stderr)
        sys.exit(2)
    sys.exit(code or 0)


# ---------------------------------------------------------------------------
# Locations


def repo_root() -> Path:
    """The checkout holding this file (a worktree or the main checkout)."""
    return Path(__file__).resolve().parents[2]


@functools.lru_cache(maxsize=1)
def main_checkout() -> Path:
    """The main checkout: the parent of `git rev-parse --git-common-dir`."""
    root = repo_root()
    try:
        out = subprocess.run(
            ["git", "rev-parse", "--git-common-dir"], cwd=root, check=True,
            capture_output=True, text=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError) as err:
        raise ToolError(
            f"git rev-parse --git-common-dir failed in {root}: {err}",
            "a git checkout of pdfkit with git on PATH",
            "run the tool from inside a pdfkit checkout, or set PDFKIT_DATASET_ROOT",
        ) from err
    common = Path(out)
    if not common.is_absolute():
        common = (root / common).resolve()
    return common.parent


def committed_dataset_dir() -> Path:
    """The dataset folder of this checkout, holding the committed tables."""
    return repo_root() / "dataset"


def shared_dataset_root() -> Path:
    """The shared folder holding files/, cache/ and reports/."""
    override = os.environ.get("PDFKIT_DATASET_ROOT")
    if override:
        return Path(override).resolve()
    return main_checkout() / "dataset"


def files_dir() -> Path:
    return shared_dataset_root() / "files"


def cache_dir() -> Path:
    return shared_dataset_root() / "cache"


def reports_dir() -> Path:
    return shared_dataset_root() / "reports"


def pdfium_cache_dir() -> Path:
    """build/cache/pdfium of the main checkout (or beside the override root)."""
    override = os.environ.get("PDFKIT_DATASET_ROOT")
    if override:
        return Path(override).resolve().parent / "build" / "cache" / "pdfium"
    return main_checkout() / "build" / "cache" / "pdfium"


def manifest_path() -> Path:
    return committed_dataset_dir() / "manifest.tsv"


def taxonomy_path() -> Path:
    return committed_dataset_dir() / "taxonomy.tsv"


def sources_path() -> Path:
    return committed_dataset_dir() / "sources.tsv"


def file_path(row: dict) -> Path:
    """Where a manifest row's file lives under the shared root."""
    return files_dir() / row["set"] / f"{row['id']}.pdf"


# ---------------------------------------------------------------------------
# Tables


def _clean(value) -> str:
    text = "" if value is None else str(value)
    return text.replace("\t", " ").replace("\r", " ").replace("\n", " ")


def read_tsv(path: Path, columns: tuple[str, ...]) -> list[dict]:
    """Reads a tab-separated table whose header must equal `columns`."""
    if not path.exists():
        return []
    # A checkout with core.autocrlf may hand the table over with CR LF line ends.
    lines = path.read_text(encoding="utf-8").replace("\r\n", "\n").split("\n")
    if not lines or not lines[0]:
        return []
    header = tuple(lines[0].split("\t"))
    if header != columns:
        raise ToolError(
            f"{path} has header {list(header)}",
            f"the header {list(columns)}",
            "restore the header line from dataset/README.md",
        )
    rows = []
    for number, line in enumerate(lines[1:], start=2):
        if not line:
            continue
        cells = line.split("\t")
        if len(cells) != len(columns):
            raise ToolError(
                f"{path}:{number} has {len(cells)} cells",
                f"{len(columns)} tab-separated cells",
                "fix the line so that it has one cell per column",
            )
        rows.append(dict(zip(columns, cells)))
    return rows


def write_tsv(path: Path, columns: tuple[str, ...], rows: list[dict], key: str | None) -> None:
    """Writes a table in UTF-8 with LF endings, stably sorted by `key`."""
    ordered = sorted(rows, key=lambda r: r[key]) if key else list(rows)
    out = ["\t".join(columns)]
    for row in ordered:
        out.append("\t".join(_clean(row.get(c, "")) for c in columns))
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_bytes(("\n".join(out) + "\n").encode("utf-8"))
    os.replace(tmp, path)


def read_manifest() -> list[dict]:
    return read_tsv(manifest_path(), MANIFEST_COLUMNS)


def write_manifest(rows: list[dict]) -> None:
    write_tsv(manifest_path(), MANIFEST_COLUMNS, rows, "id")


def read_taxonomy() -> list[dict]:
    return read_tsv(taxonomy_path(), TAXONOMY_COLUMNS)


def read_sources() -> list[dict]:
    return read_tsv(sources_path(), SOURCES_COLUMNS)


def write_sources(rows: list[dict]) -> None:
    write_tsv(sources_path(), SOURCES_COLUMNS, rows, None)


def split_tags(text: str) -> list[str]:
    return [t for t in text.split(",") if t]


def join_tags(tags) -> str:
    return ",".join(sorted(set(tags)))


def next_id(rows: list[dict], set_name: str) -> str:
    """The next free `<set>-<nnnn>` id of a set."""
    prefix = f"{set_name}-"
    used = [int(r["id"][len(prefix):]) for r in rows
            if r["id"].startswith(prefix) and r["id"][len(prefix):].isdigit()]
    return f"{set_name}-{(max(used) + 1 if used else 1):04d}"


def default_expect_write(expect_open: str) -> str:
    """`identical` for a file the open expectation lets pdfkit read, else `n/a`."""
    return "identical" if expect_open in ("opens", "repaired") else "n/a"


def default_expect_select(expect_open: str, pages: str) -> str:
    """`ok` for a readable file of two or more pages (PDFium's count), else `n/a`."""
    count = int(pages) if pages.isdigit() else 0
    return "ok" if expect_open in ("opens", "repaired") and count >= 2 else "n/a"


def check_expectation(column: str, value: str) -> bool:
    """True when `value` is one of the values `column` may hold."""
    allowed = EXPECTATIONS[column][1]
    for option in allowed:
        if option.endswith(":"):
            if value.startswith(option) and len(value) > len(option):
                if column == "expect_open":
                    return value[len(option):] in REFUSAL_CODES
                return True
        elif value == option:
            return True
    return False


# ---------------------------------------------------------------------------
# Hashes and run ids


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def git_short_hash() -> str:
    try:
        return subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"], cwd=repo_root(), check=True,
            capture_output=True, text=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "nogit"


def run_id(now: datetime.datetime | None = None) -> str:
    """`YYYYMMDD-HHMMSS-<git short hash>`, the name of a run's report folder."""
    moment = now or datetime.datetime.now()
    return f"{moment:%Y%m%d-%H%M%S}-{git_short_hash()}"


def print_table(headers: list[str], rows: list[list]) -> None:
    """Prints a plain left-aligned text table."""
    widths = [len(h) for h in headers]
    for row in rows:
        for i, cell in enumerate(row):
            widths[i] = max(widths[i], len(str(cell)))
    fmt = "  ".join("{:<%d}" % w for w in widths)
    print(fmt.format(*headers))
    print(fmt.format(*["-" * w for w in widths]))
    for row in rows:
        print(fmt.format(*[str(c) for c in row]))
