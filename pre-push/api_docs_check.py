#!/usr/bin/env python3
"""Pre-push API documentation drift check."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    checks = [
        [sys.executable, str(REPO_ROOT / "tools/generate_api_docs.py"), "--check"],
        [sys.executable, str(REPO_ROOT / "tools/sanitize_api_docs.py"), "--check"],
    ]
    for command in checks:
        result = subprocess.call(command, cwd=REPO_ROOT)
        if result != 0:
            return result
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
