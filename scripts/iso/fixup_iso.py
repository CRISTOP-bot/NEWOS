#!/usr/bin/env python3
"""Normalize ISO metadata for reproducible builds.

grub-mkrescue stamps volume-creation markers and a FAT volume serial that
embed the wall-clock time, which makes the ISO non-reproducible.  This
script rewrites those bytes to fixed constants:

  * grub/xorriso ".uuid" volume marker file name (ASCII + Joliet UTF-16LE)
  * the FAT volume serial inside the UEFI efi.img boot sector

Input: an ISO path.  Output: same path, in place.
"""
import sys

EPOCH = b"1970-01-01-00-00-00-00"  # 24 chars, same length as the generated name
FAT_SERIAL = b"\x6e\x65\x77\x6f"  # "nowo"


ENAM = len(EPOCH)  # 22  (the generated name is 22 chars)


def fix(b: bytes) -> bytes:
    i = 0
    while True:
        j = b.find(b".uuid", i)
        if j < 0:
            break
        if j >= ENAM:
            b = b[: j - ENAM] + EPOCH + b[j:]
        i = j + 1

    needle = b".\x00u\x00u\x00i\x00d\x00"
    i = 0
    while True:
        j = b.find(needle, i)
        if j < 0:
            break
        if j >= ENAM * 2:
            enc = b"".join(bytes([c]) + b"\x00" for c in bytes(EPOCH))
            b = b[: j - ENAM * 2] + enc + b[j:]
        i = j + 1

    p = b.find(b"NO NAME    FAT12")
    if p < 0x2B:
        raise SystemExit("fixup_iso: FAT labels not found")
    b = b[: p - 4] + FAT_SERIAL + b[p:]
    return b


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: fixup_iso.py <image.iso>")
    path = sys.argv[1]
    with open(path, "rb") as f:
        data = f.read()
    with open(path, "wb") as f:
        f.write(fix(data))


if __name__ == "__main__":
    main()