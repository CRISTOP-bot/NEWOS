#!/usr/bin/env python3
"""newpkg-repo.py: build a Debian-like package repository for NEWOS `.new`.

The repository is a plain directory served by any static HTTP server. It is
deliberately shaped like a Debian archive so the on-board `newpkg` client
uses the same mental model (Release -> Packages -> pool):

    <repo>/
      Release                         archive metadata + index checksums
      dists/stable/Release             (mirror of the top-level Release)
      dists/stable/main/binary-x86_64/Packages    Debian-style index
      dists/stable/main/binary-x86_64/Packages.gz (with --gzip)
      pool/main/<name>/<file>.new      exact bytes from the builder

`Packages` is UTF-8 LF text: blank-line-separated stanzas with Debian-style
fields plus NEWOS-specific ones:

    Package: hello-new
    Version: 1.0.0
    Architecture: x86_64
    Description: ...
    Maintainer: ...
    License: ...
    Depends: ...
    Section: main
    Filename: pool/main/hello-new/hello-new-1.0.0-x86_64.new
    Size: 1359
    SHA256: <hex>
    NEWOS-CRC32: <hex8>      whole .new file CRC32 (what NEWOS checks)
    NEWOS-Min: 0.2.0

`Release` carries MD5Sum / SHA256 / CRC32 of the Packages index; the on-board
client verifies the CRC32 (the only digest it can compute today).

Usage:
  python3 tools/newpkg/newpkg-repo.py build <packages-dir> <repo-dir> \
      [--suite stable] [--codename newos] [--arch x86_64] [--gzip]
  python3 tools/newpkg/newpkg-repo.py verify <repo-dir> [--suite stable] \
      [--arch x86_64]

Deterministic: stanzas sorted by Package/Version, LF, no timestamps in the
package bytes (the builder guarantees that); Release carries a Date like
Debian does.
"""
import argparse
import binascii
import gzip
import hashlib
import os
import struct
import sys
from datetime import datetime, timezone

REQUIRED_KEYS = ("name", "version", "arch", "desc", "license",
                 "maintainer", "build", "newos-min")


def fail(msg):
    print("newpkg-repo: error: %s" % msg, file=sys.stderr)
    sys.exit(1)


def crc32_file(path):
    c = 0
    with open(path, "rb") as f:
        while True:
            b = f.read(65536)
            if not b:
                break
            c = binascii.crc32(b, c)
    return c & 0xFFFFFFFF


def parse_new(path):
    """Parse a .new header+metadata; return (meta dict, file size)."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        fail("cannot read %s: %s" % (path, e))
    if len(data) < 48 or data[0:4] != b"NEW1":
        fail("not a .new package: %s" % path)
    ver, flags = struct.unpack_from("<HH", data, 4)
    if ver != 1 or flags != 0:
        fail("unsupported .new version/flags in %s" % path)
    meta_len, man_len, pay_len = struct.unpack_from("<III", data, 8)
    if len(data) != 48 + meta_len + man_len + pay_len:
        fail("length mismatch in %s" % path)
    hdr_crc = struct.unpack_from("<I", data, 20)[0]
    if (binascii.crc32(data[0:20] + data[24:48]) & 0xFFFFFFFF) != hdr_crc:
        fail("header CRC mismatch in %s" % path)
    meta_bytes = data[48:48 + meta_len]
    if (binascii.crc32(meta_bytes) & 0xFFFFFFFF) != struct.unpack_from("<I", data, 24)[0]:
        fail("metadata CRC mismatch in %s" % path)
    meta = {}
    for line in meta_bytes.decode("utf-8").splitlines():
        if ": " in line:
            k, v = line.split(": ", 1)
            meta[k] = v
    for k in REQUIRED_KEYS:
        if k not in meta:
            fail("package %s missing metadata key %s" % (path, k))
    return meta, len(data)


def make_stanza(meta, rel, size, crc, blob):
    """Debian-style stanza with NEWOS extensions."""
    fields = [
        ("Package", meta["name"]),
        ("Version", meta["version"]),
        ("Architecture", meta["arch"]),
        ("Description", meta.get("desc", "")),
        ("Maintainer", meta.get("maintainer", "")),
        ("License", meta.get("license", "")),
        ("Depends", meta.get("depends", "")),
        ("Section", meta.get("section", "main")),
        ("Filename", rel),
        ("Size", str(size)),
        ("SHA256", hashlib.sha256(blob).hexdigest()),
        ("NEWOS-CRC32", "%08x" % crc),
        ("NEWOS-Min", meta.get("newos-min", "")),
    ]
    return "".join("%s: %s\n" % (k, v) for k, v in fields) + "\n"


def build_repo(args):
    src = os.path.abspath(args.packages_dir)
    repo = os.path.abspath(args.repo_dir)
    if not os.path.isdir(src):
        fail("packages directory not found: %s" % src)
    arch = args.arch
    suite = args.suite
    comp = "main"
    bin_dir = os.path.join(repo, "dists", suite, comp, "binary-%s" % arch)
    pool_dir = os.path.join(repo, "pool", comp)
    os.makedirs(bin_dir, exist_ok=True)
    os.makedirs(pool_dir, exist_ok=True)

    packages = []
    for fn in sorted(os.listdir(src)):
        if not fn.endswith(".new"):
            continue
        full = os.path.join(src, fn)
        meta, size = parse_new(full)
        if meta["arch"] not in (arch, "any"):
            continue
        # pool/main/<name>/<filename>.new
        pool_sub = os.path.join(pool_dir, meta["name"])
        os.makedirs(pool_sub, exist_ok=True)
        dst = os.path.join(pool_sub, fn)
        if os.path.abspath(dst) != os.path.abspath(full):
            with open(full, "rb") as fi, open(dst, "wb") as fo:
                fo.write(fi.read())
        rel = os.path.relpath(dst, repo).replace(os.sep, "/")
        with open(dst, "rb") as f:
            blob = f.read()
        crc = binascii.crc32(blob) & 0xFFFFFFFF
        if size != len(blob):
            fail("size mismatch for %s" % fn)
        stanza = make_stanza(meta, rel, size, crc, blob)
        packages.append((meta["name"], meta["version"], stanza))

    packages.sort(key=lambda p: (p[0], p[1]))
    idx = "".join(p[2] for p in packages)
    idx_path = os.path.join(bin_dir, "Packages")
    with open(idx_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(idx)
    if args.gzip:
        with open(idx_path + ".gz", "wb") as f:
            f.write(gzip.compress(idx.encode("utf-8"), mtime=0))

    idx_bytes = idx.encode("utf-8")
    rel_path_d = os.path.relpath(idx_path, repo).replace(os.sep, "/")
    crc = binascii.crc32(idx_bytes) & 0xFFFFFFFF
    now = datetime.now(timezone.utc).strftime("%a, %d %b %Y %H:%M:%S UTC")
    release = (
        "Origin: NEWOS\n"
        "Label: NEWOS\n"
        "Suite: %s\n"
        "Codename: %s\n"
        "Architectures: %s\n"
        "Components: %s\n"
        "Description: NEWOS native package repository\n"
        "Date: %s\n"
        "MD5Sum: %-32d %s %s\n"
        "SHA256: %-64d %s %s\n"
        "CRC32: %08x %d %s\n"
    ) % (suite, args.codename, arch, comp, now,
         len(idx_bytes), hashlib.md5(idx_bytes).hexdigest(), rel_path_d,
         len(idx_bytes), hashlib.sha256(idx_bytes).hexdigest(), rel_path_d,
         crc, len(idx_bytes), rel_path_d)
    for name in ("Release", "dists/%s/Release" % suite):
        rp = os.path.join(repo, name)
        os.makedirs(os.path.dirname(rp), exist_ok=True)
        with open(rp, "w", encoding="utf-8", newline="\n") as f:
            f.write(release)

    print("newpkg-repo: built %s (%d packages, %s/%s)"
          % (repo, len(packages), suite, arch))


def verify_repo(args):
    repo = os.path.abspath(args.repo_dir)
    idx_path = os.path.join(repo, "dists", args.suite, "main",
                            "binary-%s" % args.arch, "Packages")
    if not os.path.isfile(idx_path):
        fail("Packages index not found: %s" % idx_path)
    data = open(idx_path, "rb").read()
    rel = os.path.relpath(idx_path, repo).replace(os.sep, "/")
    rel_text = open(os.path.join(repo, "Release"), encoding="utf-8").read()
    line = next((l for l in rel_text.splitlines() if l.startswith("CRC32:")), "")
    parts = line.split()
    if len(parts) != 4:
        fail("malformed Release CRC32 line")
    want_crc, want_size, want_rel = parts[1], parts[2], parts[3]
    got = "%08x" % (binascii.crc32(data) & 0xFFFFFFFF)
    if got != want_crc or str(len(data)) != want_size or rel != want_rel:
        fail("Release CRC32/size/path mismatch for Packages")
    count = 0
    for block in data.decode("utf-8").split("\n\n"):
        if not block.strip():
            continue
        fields = {}
        for l in block.splitlines():
            if ": " in l:
                k, v = l.split(": ", 1)
                fields[k] = v
        fp = os.path.join(repo, fields["Filename"])
        if not os.path.isfile(fp):
            fail("missing pool package: %s" % fields["Filename"])
        crc = "%08x" % crc32_file(fp)
        if crc != fields.get("NEWOS-CRC32", ""):
            fail("CRC mismatch for %s" % fields["Filename"])
        if os.path.getsize(fp) != int(fields["Size"]):
            fail("size mismatch for %s" % fields["Filename"])
        count += 1
    print("newpkg-repo: OK (%d packages, index CRC %s)" % (count, got))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("build", help="build a repository from .new files")
    b.add_argument("packages_dir")
    b.add_argument("repo_dir")
    b.add_argument("--suite", default="stable")
    b.add_argument("--codename", default="newos")
    b.add_argument("--arch", default="x86_64")
    b.add_argument("--gzip", action="store_true")
    b.set_defaults(fn=build_repo)
    v = sub.add_parser("verify", help="verify a repository index and packages")
    v.add_argument("repo_dir")
    v.add_argument("--suite", default="stable")
    v.add_argument("--arch", default="x86_64")
    v.set_defaults(fn=verify_repo)
    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
