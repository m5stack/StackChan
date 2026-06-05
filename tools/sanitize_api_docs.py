#!/usr/bin/env python3
"""Fail if public docs/API fixtures contain live-looking identifiers.

This is a guardrail for generated docs and captured fixtures. It is not a
general secret scanner; it focuses on values our capture/doc tooling is likely
to leak.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SCAN_PATHS = [
    REPO_ROOT / "README.md",
    REPO_ROOT / "docs",
    REPO_ROOT / "tools/generate_api_docs.py",
    REPO_ROOT / "tools/capture_device_api.py",
    REPO_ROOT / "tools/sanitize_api_docs.py",
    REPO_ROOT / "pre-push",
]


@dataclass(frozen=True)
class Rule:
    name: str
    pattern: re.Pattern[str]


RULES = [
    Rule("colon_mac", re.compile(r"\b[0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5}\b")),
    Rule("compact_stackchan_device_id", re.compile(r"\bstackchan-[0-9a-fA-F]{12}\b")),
    Rule(
        "uuid",
        re.compile(
            r"\b[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\b"
        ),
    ),
    Rule("long_hex_secret", re.compile(r"\b[0-9a-fA-F]{32,}\b")),
    Rule("hardcoded_dev_token", re.compile(r"\bstackchan-local-dev\b")),
]


ALLOWED_LITERAL_FILES = {
    "docs/local-control-websocket.md",
    "docs/generated/local-control-websocket.md",
    "docs/api/local-control.methods.json",
    "docs/generated/api-index.json",
}

ALLOWED_DEV_TOKEN_FILES = {
    "README.md",
    "docs/local-control-websocket.md",
    "docs/sdcard-settings.md",
    "docs/generated/local-control-websocket.md",
}


def iter_files(paths: list[Path]) -> list[Path]:
    files: list[Path] = []
    for path in paths:
        if not path.exists():
            continue
        if path.is_file():
            files.append(path)
            continue
        files.extend(child for child in path.rglob("*") if child.is_file())
    return sorted(set(files))


def line_number(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def is_allowed(path: Path, rule_name: str, value: str, line: str) -> bool:
    relative = path.relative_to(REPO_ROOT).as_posix()

    if "<" in value or ">" in value:
        return True

    if "\\b" in line or "re.compile" in line or "pattern" in line:
        return True

    if rule_name == "hardcoded_dev_token":
        return relative in ALLOWED_DEV_TOKEN_FILES

    if rule_name == "long_hex_secret" and relative in ALLOWED_LITERAL_FILES:
        return "hmac-sha256" in line.lower() or "HMAC-SHA256" in line

    if rule_name == "long_hex_secret" and relative == "README.md":
        return "cdn.shopify.com" in line and (".png" in line or ".webp" in line)

    return False


def scan_file(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8")
    findings: list[str] = []
    lines = text.splitlines()
    for rule in RULES:
        for match in rule.pattern.finditer(text):
            lineno = line_number(text, match.start())
            line = lines[lineno - 1] if lineno - 1 < len(lines) else ""
            value = match.group(0)
            if is_allowed(path, rule.name, value, line):
                continue
            relative = path.relative_to(REPO_ROOT)
            findings.append(f"{relative}:{lineno}: {rule.name}: {value}")
    return findings


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="scan docs and fail on leak-looking values")
    args = parser.parse_args()

    findings: list[str] = []
    for path in iter_files(SCAN_PATHS):
        findings.extend(scan_file(path))

    if findings:
        print("doc sanitization failed:", file=sys.stderr)
        for finding in findings:
            print(f"  {finding}", file=sys.stderr)
        return 1

    if not args.check:
        print("doc sanitization passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
