#!/usr/bin/env python3
"""Normalize ISO metadata for reproducible builds.

grub-mkrescue stamps wall-clock data in several places, which makes two
otherwise identical builds differ:

  * grub/xorriso ".uuid" volume marker file name (ASCII + Joliet UTF-16LE)
  * the FAT volume serial inside the UEFI efi.img boot sector
  * the GPT disk/partition GUIDs and their CRC32s (isohybrid images)
  * ISO9660 primary/secondary volume dates and every directory-record /
    Rock Ridge TF timestamp

This script rewrites all of them to fixed constants, in place, so a clean
rebuild is byte-identical.  Input: an ISO path.  Output: same path.
"""

import struct
import sys
import zlib

# 1970-01-01 00:00:00 GMT (years since 1900, month, day, h, m, s, tz).
FIXED7 = bytes([70, 1, 1, 0, 0, 0, 0])
FIXED17 = b"1970010100000000\x00"
EPOCH = b"1970-01-01-00-00-00-00"  # 24 chars, same length as the generated name
FAT_SERIAL = b"\x6e\x65\x77\x6f"  # "nowo"
# Stable fake GUID used for the GPT disk and partition entries.
GPT_DISK_GUID = bytes.fromhex("4e45574f532d47505400000000000001")
GPT_PART_GUID = bytes.fromhex("4e45574f532d47505400000000000002")

ENAM = len(EPOCH)  # 22  (the generated name is 22 chars)
SECTOR = 2048

le32 = lambda b, o: struct.unpack_from("<I", b, o)[0]
le64 = lambda b, o: struct.unpack_from("<Q", b, o)[0]


def fix_uuid_marker(b: bytes) -> bytes:
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
    return b


def _fat_entry(b: bytearray, fat_start: int, cluster: int) -> int:
    off = fat_start + cluster + cluster // 2
    if off + 2 > len(b):
        return 0xFFFFFFFF
    v = struct.unpack_from("<H", b, off)[0]
    return (v >> 4) if (cluster & 1) else (v & 0x0FFF)


def _fat_dir(b: bytearray, start: int, size: int, spc: int, data_start: int,
             fat_start: int, seen: set) -> None:
    pos = 0
    while pos + 32 <= size:
        e = start + pos
        first = b[e]
        attr = b[e + 11]
        if first == 0x00:
            break
        if first != 0xE5 and attr != 0x0F:
            # Creation date/time, access date, write date/time -> 1980-01-01.
            struct.pack_into("<H", b, e + 14, 0)
            struct.pack_into("<H", b, e + 16, 0x0021)
            struct.pack_into("<H", b, e + 18, 0x0021)
            struct.pack_into("<H", b, e + 20, 0)
            struct.pack_into("<H", b, e + 22, 0x0021)
            if attr & 0x10:
                name = bytes(b[e : e + 11])
                if name not in (b".          ", b"..         "):
                    cluster = struct.unpack_from("<H", b, e + 26)[0]
                    if cluster >= 2 and cluster not in seen:
                        seen.add(cluster)
                        cstart = data_start + (cluster - 2) * spc * 512
                        _fat_dir(b, cstart, spc * 512, spc, data_start,
                                 fat_start, seen)
        pos += 32


def fix_fat(b: bytearray) -> bytearray:
    p = b.find(b"NO NAME    FAT12")
    if p < 0x2B:
        raise SystemExit("fixup_iso: FAT labels not found")
    boot = p - 0x2B
    b[p - 4 : p] = FAT_SERIAL

    bps = struct.unpack_from("<H", b, boot + 0x0B)[0]
    spc = b[boot + 0x0D]
    reserved = struct.unpack_from("<H", b, boot + 0x0E)[0]
    nfats = b[boot + 0x10]
    root_entries = struct.unpack_from("<H", b, boot + 0x11)[0]
    if bps == 0 or spc == 0:
        raise SystemExit("fixup_iso: invalid FAT BPB")
    fat_size = struct.unpack_from("<H", b, boot + 0x16)[0]
    if fat_size == 0:
        fat_size = struct.unpack_from("<I", b, boot + 0x24)[0]

    fat_start = boot + reserved * bps
    root_start = (reserved + nfats * fat_size) * bps + boot
    data_start = root_start + root_entries * 32
    _fat_dir(b, root_start, root_entries * 32, spc, data_start, fat_start, set())
    return b


def fix_volume_dates(b: bytearray) -> None:
    """Clear the four 17-byte date fields of the PVD and SVD."""
    for sector in range(16, 64):
        off = sector * SECTOR
        if b[off + 1 : off + 6] != b"CD001":
            break
        vtype = b[off]
        if vtype in (1, 2):
            b[off + 813 : off + 881] = b"\x00" * 68
        if vtype == 255:
            break


def fix_susp_area(b: bytearray, start: int, end: int, seen: set) -> None:
    """Rewrite TF timestamps in a SUSP area, following CE continuations."""
    i = start
    while i + 4 <= end:
        sig = bytes(b[i : i + 2])
        size = b[i + 2]
        if sig == b"ST" or size < 4 or i + size > end:
            break
        if sig == b"TF":
            flags = b[i + 4]
            p = i + 5
            # Bits 0,4,5,6 are long (17-byte ASCII); 1,2,3 are 7-byte.
            for bit in range(7):
                if not (flags & (1 << bit)):
                    continue
                if bit in (0, 4, 5, 6):
                    if p + 17 <= i + size:
                        b[p : p + 17] = FIXED17
                    p += 17
                else:
                    if p + 7 <= i + size:
                        b[p : p + 7] = FIXED7
                    p += 7
        elif sig == b"CE" and size >= 28:
            block = le32(b, i + 4)
            offset = le32(b, i + 8)
            length = le32(b, i + 12)
            target = block * SECTOR + offset
            if target not in seen and target + length <= len(b):
                seen.add(target)
                fix_susp_area(b, target, target + length, seen)
        i += size


def fix_dir_record(b: bytearray, pos: int, length: int) -> None:
    b[pos + 18 : pos + 25] = FIXED7
    name_len = b[pos + 32]
    su = pos + 33 + name_len
    if name_len % 2 == 0:
        su += 1  # padding keeps the record even
    if su < pos + length:
        fix_susp_area(b, su, pos + length, set())


def fix_directory_tree(b: bytearray, extent: int, size: int, seen: set) -> None:
    if extent in seen or size == 0:
        return
    seen.add(extent)
    start = extent * SECTOR
    end = min(start + size, len(b))
    pos = 0
    while pos < size and start + pos < end:
        length = b[start + pos]
        if length == 0:
            pos = (pos // SECTOR + 1) * SECTOR
            continue
        rec = start + pos
        if length < 34:
            break
        fix_dir_record(b, rec, length)

        flags = b[rec + 25]
        name_len = b[rec + 32]
        name = bytes(b[rec + 33 : rec + 33 + name_len])
        if (flags & 0x02) and name not in (b"\x00", b"\x01"):
            child_extent = le32(b, rec + 2)
            child_size = le32(b, rec + 10)
            fix_directory_tree(b, child_extent, child_size, seen)
        pos += length


def fix_iso_tree(b: bytearray) -> None:
    pvd = 16 * SECTOR
    if b[pvd + 1 : pvd + 6] != b"CD001":
        raise SystemExit("fixup_iso: volume descriptor not found")
    root = pvd + 156
    extent = le32(b, root + 2)
    size = le32(b, root + 10)
    fix_directory_tree(b, extent, size, set())


def fix_gpt(b: bytearray) -> None:
    total = len(b) // 512
    for lba in (1, total - 1):
        hoff = lba * 512
        if b[hoff : hoff + 8] != b"EFI PART":
            continue
        hsize = le32(b, hoff + 12)
        entries_lba = le64(b, hoff + 72)
        count = le32(b, hoff + 80)
        esize = le32(b, hoff + 84)

        b[hoff + 56 : hoff + 72] = GPT_DISK_GUID

        eoff = entries_lba * 512
        for n in range(count):
            e = eoff + n * esize
            if e + esize > len(b):
                break
            if b[e : e + 16] == b"\x00" * 16:
                continue
            # Keep partitions distinct but deterministic.
            b[e + 16 : e + 32] = GPT_PART_GUID[:-1] + bytes([n & 0xFF])

        entries_crc = zlib.crc32(b[eoff : eoff + count * esize]) & 0xFFFFFFFF
        struct.pack_into("<I", b, hoff + 88, entries_crc)

        struct.pack_into("<I", b, hoff + 16, 0)
        header_crc = zlib.crc32(b[hoff : hoff + hsize]) & 0xFFFFFFFF
        struct.pack_into("<I", b, hoff + 16, header_crc)


def fix(b: bytes) -> bytes:
    b = bytearray(fix_uuid_marker(b))
    b = fix_fat(b)
    fix_gpt(b)
    fix_volume_dates(b)
    fix_iso_tree(b)
    return bytes(b)


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
