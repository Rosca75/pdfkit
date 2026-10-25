# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# A stand-in for the `pdfkit dataset` verb before the parser exists: it writes,
# for one file, the outcome of tests/dataset/outcome.md in which every catalogue
# function answers PDFKIT_E_NOT_IMPLEMENTED, exactly as the stubbed C ABI does.
# It lets the harness be measured and tested end to end (process per file,
# timeouts, determinism, summaries) with `run.py --pdfkit tests/dataset/stub_pdfkit.py`.

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

FUNCTIONS = ("open", "inspect", "text", "images", "metadata", "channels", "redact", "write", "select")


def main() -> int:
    parser = argparse.ArgumentParser(prog="pdfkit")
    parser.add_argument("verb")
    parser.add_argument("--file", required=True)
    parser.add_argument("--id", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    if args.verb != "dataset":
        print(json.dumps({"code": "PDFKIT_E_NOT_IMPLEMENTED", "failed": args.verb,
                          "expected": "the dataset verb", "fix": "use the dataset verb"}))
        return 4
    data = Path(args.file).read_bytes()
    outcome = {"id": args.id, "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}
    for name in FUNCTIONS:
        outcome[name] = {"status": "not_implemented", "code": "PDFKIT_E_NOT_IMPLEMENTED"}
    target = Path(args.out) / "outcomes" / f"{args.id}.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes((json.dumps(outcome, indent=2) + "\n").encode("utf-8"))
    print(json.dumps({"id": args.id, "outcome": str(target)}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
