#!/usr/bin/env python3
"""pack-openrc.py: build the OpenRC configuration tree blob (ORCF).

The kernel installs /etc/{rc.conf,inittab,conf.d,init.d,runlevels} from this
single blob at rootfs build time, exactly like the other embedded assets, so
the service manager finds a normal /etc on every boot (and an image without
the blob simply has no OpenRC tree, which the tools report instead of
guessing).

Record layout, all integers little endian:

    "ORCF"                     magic, 4 bytes
    then, for each entry:
      u8   type                'F' file, 'D' directory
      u32  pathlen             length of the absolute VFS path
      u32  size                payload length (0 for 'D')
      bytes path               pathlen bytes, no NUL
      bytes payload            size bytes (files only)

Directories are listed before their contents and empty runlevel directories
are emitted too, because `openrc --list` and `rc-update` read the tree, not
the package.

  scripts/pack-openrc.py [source-dir] [outfile]
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.realpath(sys.argv[1] if len(sys.argv) > 1
                       else os.path.join(ROOT, "userland/user/rootfs/openrc"))
OUT = os.path.realpath(sys.argv[2] if len(sys.argv) > 2
                       else os.path.join(ROOT, "build/images/openrc-rootfs.orcf"))

if not os.path.isdir(os.path.join(SRC, "etc")):
    sys.exit(f"pack-openrc: no etc/ tree under {SRC}")


def walk(dirpath, entries):
    names = sorted(os.listdir(dirpath))
    dirs = [n for n in names if os.path.isdir(os.path.join(dirpath, n))]
    files = [n for n in names if os.path.isfile(os.path.join(dirpath, n))]
    for n in dirs:
        full = os.path.join(dirpath, n)
        entries.append(("D", "/" + os.path.relpath(full, SRC).replace(os.sep, "/"), b""))
        walk(full, entries)
    for n in files:
        full = os.path.join(dirpath, n)
        with open(full, "rb") as fh:
            data = fh.read()
        entries.append(("F", "/" + os.path.relpath(full, SRC).replace(os.sep, "/"), data))


entries = []
walk(os.path.join(SRC, "etc"), entries)

blob = bytearray(b"ORCF")
for kind, path, data in entries:
    pb = path.encode("ascii")
    if len(pb) >= 512:
        sys.exit(f"pack-openrc: path too long: {path}")
    blob += struct.pack("<BII", ord(kind), len(pb), len(data))
    blob += pb
    blob += data

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "wb") as fh:
    fh.write(blob)

nfiles = sum(1 for e in entries if e[0] == "F")
ndirs = sum(1 for e in entries if e[0] == "D")
print(f"pack-openrc: {OUT} ({len(blob)} bytes, {nfiles} files, {ndirs} dirs)")
