#!/usr/bin/env python3
"""newpkg-build.py: host builder for NEWOS `.new` packages (spec v1).

Reads a `.newspec` definition and assembles a deterministic `.new` file:
  [ header 48 B ][ metadata ][ manifest ][ payload ]

Usage:
  python3 tools/newpkg/newpkg-build.py packages/hello-new.newspec \\
      -o build/packages/hello-new-1.0.0-x86_64.new [--root .]

The newspec format is KEY = "value" lines plus a `files:` section:
  name = "hello-new"
  version = "1.0.0"
  ...
  files:
    build/userland/hello.elf /bin/hello-new 0755

Output is byte-deterministic: metadata keys sorted, manifest sorted by
destination path, LF endings, no timestamps inside the artifact.
"""
import argparse
import binascii
import os
import struct
import sys

MAGIC = b"NEW1"
SPEC_VERSION = 1
HEADER_SIZE = 48

META_MAX = 4096
MANIFEST_MAX = 65536
PATH_MAX = 256
FILES_MAX = 1024
FILE_MAX = 8 * 1024 * 1024
PKG_MAX = 32 * 1024 * 1024

REQUIRED_KEYS = ["name", "version", "arch", "desc", "license",
                 "maintainer", "build", "newos-min"]

ALLOWED_PREFIXES = ("/bin/", "/sbin/", "/lib/", "/etc/", "/usr/", "/opt/",
                    "/var/", "/home/", "/root/", "/tmp/")

OK_CHARS = set(
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._+~@%=-/")


def fail(msg):
    print("newpkg-build: error: %s" % msg, file=sys.stderr)
    sys.exit(1)


def path_valid(path):
    if not path or len(path) < 2 or len(path) >= PATH_MAX:
        return False
    if not path.startswith("/"):
        return False
    if "//" in path or path.endswith("/"):
        return False
    if any(c not in OK_CHARS for c in path):
        return False
    parts = path[1:].split("/")
    if any(p in (".", "..", "") for p in parts):
        return False
    return any(path.startswith(p) for p in ALLOWED_PREFIXES)


def parse_newspec(text):
    meta = {}
    files = []
    section = "meta"
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line == "files:":
            section = "files"
            continue
        if section == "meta":
            if "=" not in line:
                fail("line %d: expected KEY = \"value\"" % lineno)
            key, _, val = line.partition("=")
            key = key.strip()
            val = val.strip()
            if len(val) >= 2 and val[0] == '"' and val[-1] == '"':
                val = val[1:-1]
            else:
                fail("line %d: value must be double-quoted" % lineno)
            meta[key] = val
        else:
            parts = line.split()
            if len(parts) != 3:
                fail("line %d: expected '<src> <dst> <mode>'" % lineno)
            src, dst, mode = parts
            if len(mode) not in (3, 4) or any(
                    c not in "01234567" for c in mode):
                fail("line %d: bad octal mode '%s'" % (lineno, mode))
            files.append((src, dst, mode))
    return meta, files


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("newspec")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--root", default=".")
    args = ap.parse_args()

    with open(args.newspec, "r", encoding="utf-8") as f:
        meta, files = parse_newspec(f.read())

    for k in REQUIRED_KEYS:
        if k not in meta or not meta[k]:
            fail("newspec lacks required key '%s'" % k)
    meta.setdefault("depends", "")

    name = meta["name"]
    if len(name) >= 64 or any(
            c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "0123456789-_+" for c in name):
        fail("bad package name '%s'" % name)

    payloads = []
    for src, dst, mode in files:
        if not path_valid(dst):
            fail("rejected destination path '%s'" % dst)
        full = os.path.join(args.root, src)
        if not os.path.isfile(full):
            fail("source not found: %s" % full)
        with open(full, "rb") as f:
            data = f.read()
        if len(data) > FILE_MAX:
            fail("file too large: %s (%d bytes)" % (src, len(data)))
        payloads.append((dst, int(mode, 8), data, src))

    payloads.sort(key=lambda e: e[0])
    dsts = [d for d, _, _, _ in payloads]
    if len(set(dsts)) != len(dsts):
        fail("duplicate destination paths")

    meta_lines = "".join("%s: %s\n" % (k, meta[k]) for k in sorted(meta))
    meta_bytes = meta_lines.encode("utf-8")
    if not meta_bytes or len(meta_bytes) > META_MAX:
        fail("metadata size out of bounds (%d)" % len(meta_bytes))

    man_text = ""
    for dst, mode, data, _ in payloads:
        man_text += "%04o %d %08x %s\n" % (mode, len(data),
                                          binascii.crc32(data) & 0xffffffff,
                                          dst)
    man_bytes = man_text.encode("utf-8")
    if not man_bytes or len(man_bytes) > MANIFEST_MAX:
        fail("manifest size out of bounds (%d)" % len(man_bytes))

    payload = b"".join(data for _, _, data, _ in payloads)
    if len(payload) + len(meta_bytes) + len(man_bytes) > PKG_MAX:
        fail("package too large")

    hdr = bytearray(HEADER_SIZE)
    hdr[0:4] = MAGIC
    struct.pack_into("<H", hdr, 4, SPEC_VERSION)
    struct.pack_into("<H", hdr, 6, 0)
    struct.pack_into("<I", hdr, 8, len(meta_bytes))
    struct.pack_into("<I", hdr, 12, len(man_bytes))
    struct.pack_into("<I", hdr, 16, len(payload))
    struct.pack_into("<I", hdr, 24, binascii.crc32(meta_bytes) & 0xffffffff)
    struct.pack_into("<I", hdr, 28, binascii.crc32(man_bytes) & 0xffffffff)
    struct.pack_into("<I", hdr, 32, binascii.crc32(payload) & 0xffffffff)
    struct.pack_into("<I", hdr, 36, len(payloads))
    hdr_crc = binascii.crc32(bytes(hdr[0:20]) + bytes(hdr[24:48])) & 0xffffffff
    struct.pack_into("<I", hdr, 20, hdr_crc)

    out = bytes(hdr) + meta_bytes + man_bytes + payload
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "wb") as f:
        f.write(out)
    print("newpkg-build: %s: %d files, %d bytes" % (
        args.output, len(payloads), len(out)))


if __name__ == "__main__":
    main()
