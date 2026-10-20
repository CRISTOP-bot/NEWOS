#!/usr/bin/env python3
"""Fix the whitespace errors reported by ``git diff --check``.

The CI workflow rejects added lines with trailing whitespace and added blank
lines at end of file.  This helper rewrites only the lines that git flagged;
it never reformats an entire file.  It is intentionally limited to text files
and leaves merge conflicts, binary files and submodules alone.

Usage:
    python3 scripts/ci/fix_whitespace.py --base origin/main

Exit status is 0 even when there is nothing to fix.  The caller decides
whether the resulting working-tree changes should be committed.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ERROR_RE = re.compile(
    r"^(?P<path>.+):(?P<line>[0-9]+): "
    r"(?P<kind>trailing whitespace\.|new blank line at EOF\.|"
    r"space before tab in indent\.)$"
)
MAX_BYTES = 5 * 1024 * 1024


def run_git(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", *args],
        check=False,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def collect_errors(base: str) -> list[tuple[str, int, str]]:
    """Return ``(path, line, kind)`` tuples for the diff against *base*."""
    result = run_git("diff", "--check", "--no-color", f"{base}...HEAD")
    if result.returncode == 0:
        return []
    if not result.stdout.strip():
        # A non-zero status without diagnostics means git itself failed
        # (for example an unknown base).  Do not guess at a repair.
        sys.stderr.write(result.stderr)
        return []

    errors: list[tuple[str, int, str]] = []
    seen: set[tuple[str, int, str]] = set()
    for raw in result.stdout.splitlines():
        match = ERROR_RE.match(raw)
        if not match:
            continue
        item = (
            match.group("path"),
            int(match.group("line")),
            match.group("kind"),
        )
        if item not in seen:
            seen.add(item)
            errors.append(item)
    return errors


def strip_trailing_space(line: bytes) -> bytes:
    """Strip spaces and tabs immediately before the line ending."""
    if line.endswith(b"\r\n"):
        return line[:-2].rstrip(b" \t") + b"\r\n"
    if line.endswith(b"\n"):
        return line[:-1].rstrip(b" \t") + b"\n"
    if line.endswith(b"\r"):
        return line[:-1].rstrip(b" \t") + b"\r"
    return line.rstrip(b" \t")


def trim_blank_lines_at_eof(lines: list[bytes]) -> list[bytes]:
    while lines and lines[-1].strip(b" \t\r\n") == b"":
        lines.pop()
    if lines and not lines[-1].endswith((b"\n", b"\r")):
        lines[-1] += b"\n"
    return lines


def fix_file(path: Path, errors: list[tuple[int, str]]) -> bool:
    if path.is_symlink() or not path.is_file():
        return False
    data = path.read_bytes()
    if len(data) > MAX_BYTES or b"\0" in data[:8192]:
        return False

    lines = data.splitlines(keepends=True)
    changed = False

    if any(kind == "new blank line at EOF." for _, kind in errors):
        new_lines = trim_blank_lines_at_eof(list(lines))
        if new_lines != lines:
            lines = new_lines
            changed = True

    for line_number, kind in errors:
        if kind not in {"trailing whitespace.", "space before tab in indent."}:
            continue
        index = line_number - 1
        if index < 0 or index >= len(lines):
            continue
        fixed = strip_trailing_space(lines[index])
        if fixed != lines[index]:
            lines[index] = fixed
            changed = True

    if changed:
        path.write_bytes(b"".join(lines))
    return changed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--base",
        default="origin/main",
        help="git revision to compare against (default: origin/main)",
    )
    args = parser.parse_args()

    errors = collect_errors(args.base)
    if not errors:
        print("No whitespace errors found.")
        return 0

    by_file: dict[str, list[tuple[int, str]]] = {}
    for path, line, kind in errors:
        by_file.setdefault(path, []).append((line, kind))

    fixed: list[str] = []
    skipped: list[str] = []
    for name, file_errors in sorted(by_file.items()):
        path = Path(name)
        if path.is_symlink() or not path.is_file():
            skipped.append(name)
            continue
        if fix_file(path, file_errors):
            fixed.append(name)

    for name in fixed:
        print(f"fixed whitespace: {name}")
    for name in skipped:
        print(f"skipped non-regular file: {name}", file=sys.stderr)
    if not fixed:
        print("Flagged files could not be repaired automatically.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
