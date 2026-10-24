# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The pdfkit authors
#
# Points git at the committed hooks under build/hooks/, so the pre-push gate
# runs before every push from every worktree of this repository. Undo with
# git config --unset core.hooksPath.
"""Install the git hooks: python build/install_hooks.py."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    subprocess.run(["git", "config", "core.hooksPath", "build/hooks"], cwd=ROOT, check=True)
    print("install_hooks: core.hooksPath is build/hooks; the pre-push gate runs on every push")
    return 0


if __name__ == "__main__":
    sys.exit(main())
