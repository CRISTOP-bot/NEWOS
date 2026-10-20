#!/usr/bin/env python3
"""Validate the static NEWOS website.

Checks that:
  1. every local href/src in web/*.html resolves to a real file, and
  2. every file under web/assets (and web/dl) is referenced by some page.

The site has no build step, so this is the CI gate that keeps it honest:
adding a page with a typo'd asset, or deleting an asset still linked from
HTML, fails the check instead of shipping a broken download.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.path.join(ROOT, "web")
ATTR = re.compile(r'(?:href|src)="([^"]+)"')
SKIP_PREFIXES = ("http://", "https://", "//", "mailto:", "tel:", "data:", "javascript:", "#")


def html_files():
    return [os.path.join(WEB, n) for n in sorted(os.listdir(WEB)) if n.endswith(".html")]


def main():
    errors = []
    referenced = set()

    for path in html_files():
        base = os.path.dirname(path)
        text = open(path, encoding="utf-8").read()
        for raw in ATTR.findall(text):
            if raw.startswith(SKIP_PREFIXES):
                continue
            target = raw.split("#", 1)[0].split("?", 1)[0]
            if not target:
                continue
            resolved = os.path.normpath(os.path.join(base, target))
            rel = os.path.relpath(resolved, WEB)
            referenced.add(rel)
            if not os.path.isfile(resolved):
                # web/dl holds release artifacts populated by CI before a
                # release; their absence in a source checkout is expected.
                if rel.startswith("dl" + os.sep):
                    continue
                errors.append(f"{os.path.relpath(path, ROOT)}: missing {raw}")

    for sub in ("assets", "dl"):
        d = os.path.join(WEB, sub)
        if not os.path.isdir(d):
            continue
        for dirpath, _dirs, files in os.walk(d):
            for name in files:
                rel = os.path.relpath(os.path.join(dirpath, name), WEB)
                if rel not in referenced:
                    errors.append(f"web/{rel}: not referenced by any page")

    if errors:
        print("check-web: FAIL")
        for e in errors:
            print("  " + e)
        return 1

    print(f"check-web: OK ({len(html_files())} pages, {len(referenced)} local references)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
