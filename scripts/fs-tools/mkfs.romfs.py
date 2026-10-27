#!/usr/bin/env python3
"""NEWOS mkfs.romfs -- builds a romfs v1 image from a directory tree.

romfs 1.0 on-disk format (mirrors kernel/fs/romfs/romfs.c and the format
Linux ships in include/uapi/linux/romfs_fs.h):

  superblock (16 bytes): '-rom1fs-' | size BE32 | checksum BE32, the
  volume name starts right after at offset 16 (NUL-terminated); the root
  directory inode header sits at (16 + namelen + 1 + 15) & ~15.

  inode: 16-byte header + NUL-padded name:
      BE32 next -- byte offset of the next sibling header (0 ends the
                    chain); low nibble carries type (+ exec bit 0x8):
                    0 hardlink, 1 dir, 2 reg, 3 sym, 4 blk, 5 chr,
                    6 sock, 7 fifo
      BE32 spec -- dir: header offset of the first child (chain head)
                   reg: data offset (header_pos + padded name length)
                   hardlink: header offset of the link target
                   sym: data offset (the link text)
      BE32 size -- data size (files, syms); dirs store 0
      BE32 csum -- per-inode checksum; 0 is fine (consumers skip it)

  The superblock checksum zeroes the additive 32-bit sum of the first
  min(size, 512) bytes read as BE32 words.

Usage:
  python3 mkfs.romfs.py -o out.img --root ./tree [--label NEWOS]
      [--link SRCFILE=LINKNAME] [--symlink TARGET@NAME]

--link HELLO.TXT=LINK.TXT: LINK.TXT appears in the parent directory of
SRCFILE as a hardlink. --symlink ./HELLO.TXT@SYM.TXT appends a symlink
entry (incompatible with the NEWOS mount, valid for genromfs readers).
"""
import argparse
import struct
from pathlib import Path

HDR = 16
TYPE_HRD, TYPE_DIR, TYPE_REG, TYPE_SYM = 0, 1, 2, 3


def words_sum(data: bytes) -> int:
    pad = (4 - len(data) % 4) % 4
    data = data + b"\x00" * pad
    total = 0
    for i in range(0, len(data), 4):
        total = (total + struct.unpack(">I", data[i:i + 4])[0]) & 0xFFFFFFFF
    return total


class Node:
    def __init__(self, rel, name, kind, data=b""):
        self.rel = rel              # normalized path inside the tree
        self.name = name            # "" for the root
        self.kind = kind
        self.data = data
        self.children = []          # ordered child Nodes
        self.parent = None
        self.header_pos = 0         # patched during layout
        self.data_off = 0           # files/syms

    def full_kind(self):
        return self.kind            # exec bits unsupported by this tool


def collect(dpath: Path, node: Node, rel: str):
    for item in sorted(dpath.iterdir(), key=lambda p: p.name):
        child_rel = f"{rel}/{item.name}" if rel else item.name
        if item.is_dir():
            child = Node(child_rel, item.name, TYPE_DIR)
            child.parent = node
            node.children.append(child)
            collect(item, child, child_rel)
        else:
            child = Node(child_rel, item.name, TYPE_REG,
                         item.read_bytes())
            child.parent = node
            node.children.append(child)


def find(root: Node, rel: str) -> Node:
    parts = rel.split("/")
    cur = root
    for part in parts:
        nxt = None
        for ch in cur.children:
            if ch.name == part:
                nxt = ch
                break
        if nxt is None:
            raise SystemExit(f"reference not found in tree: {rel}")
        cur = nxt
    return cur


def layout(root: Node, label: str):
    """Second pass: place every inode/data blob, then patch the headers."""
    out = bytearray()

    # Superblock + label; the root header lands after the padded label.
    namelen = len(label.encode())
    if namelen > 127:
        raise SystemExit("label too long")
    root_off = (HDR + namelen + 1 + 15) & ~15
    out.extend(b"-rom1fs-")
    out.extend(b"\x00" * 8)          # size + checksum, patched last
    out.extend(label.encode())
    out.extend(b"\x00")
    while len(out) < root_off:
        out.extend(b"\x00")

    # Allocate every inode (DFS), parents before children.
    stack = [root]
    while stack:
        n = stack.pop(0)
        n.header_pos = len(out)
        out.extend(b"\x00" * HDR)   # patched once children are known
        out.extend(n.name.encode())
        out.extend(b"\x00")
        while len(out) % 16:
            out.extend(b"\x00")
        if n.kind in (TYPE_REG, TYPE_SYM):
            n.data_off = len(out)
            out.extend(n.data)
            # Pad data to 16 like genromfs: every header address must
            # stay 16-byte aligned so the type nibble in the low bits
            # stays unambiguous for consumers that mask & ~15.
            while len(out) % 16:
                out.extend(b"\x00")
        stack.extend(n.children)

    # Patch headers: sibling chains, dir specs, regs/syms, hardlinks.
    for n in all_nodes(root):
        kind = n.full_kind()
        siblings = n.parent.children if n.parent else [root]
        idx = siblings.index(n)
        next_pos = siblings[idx + 1].header_pos \
            if idx + 1 < len(siblings) else 0
        next_word = next_pos | kind
        if kind == TYPE_DIR:
            spec = n.children[0].header_pos if n.children else 0
            size = 0
        elif kind == TYPE_REG:
            spec = n.data_off
            size = len(n.data)
        elif kind == TYPE_SYM:
            spec = n.data_off
            size = len(n.data)
        else:                        # TYPE_HRD: resolved below
            spec = 0
            size = 0
        out[n.header_pos:n.header_pos + 12] = struct.pack(
            ">III", next_word, spec, size)
    for n in all_nodes(root):
        if n.kind == TYPE_HRD:
            target = find(root, n.rel_target)
            out[n.header_pos + 4:n.header_pos + 8] = struct.pack(
                ">I", target.header_pos)

    image = bytearray(out)
    size = len(image)
    while size % 512:
        image.extend(b"\x00")
        size = len(image)
    image[8:12] = struct.pack(">I", size)
    checksum = words_sum(bytes(image[:512]))
    image[12:16] = struct.pack(">I", (0 - checksum) & 0xFFFFFFFF)
    return bytes(image)


def all_nodes(root: Node):
    ordered = [root]
    i = 0
    while i < len(ordered) and i < 100000:
        n = ordered[i]
        i += 1
        ordered.extend(n.children)
    if i >= 100000:
        raise SystemExit("tree absurdly large")
    return ordered


def verify(image: bytes, root: Node):
    """Re-walk the produced image and assert it would parse back."""
    assert image[:8] == b"-rom1fs-", "superblock magic"
    size = struct.unpack(">I", image[8:12])[0]
    assert size == len(image), f"size field {size} != {len(image)}"
    span = image[:min(size, 512)]
    pad = (4 - len(span) % 4) % 4
    total = 0
    for i in range(0, len(span) + pad - pad, 4):
        total = (total + struct.unpack(">I", image[i:i + 4])[0]) & 0xFFFFFFFF
    assert total == 0, "superblock checksum does not zero"

    def hdr(pos):
        assert pos % 16 == 0, f"header at {pos:#x} not 16-aligned"
        nxt, spec, vsize = struct.unpack(">III", image[pos:pos + 12])
        return nxt, spec, vsize

    def name_of(pos):
        p = pos + 16
        while image[p] != 0:
            p += 1
        return image[pos + 16:p].decode()

    label_len = image[16:].index(0)
    root_off = (HDR + label_len + 1 + 15) & ~15
    root_next = struct.unpack(">I", image[root_off:root_off + 4])[0]
    assert root_next & 7 == TYPE_DIR, "root inode is not a dir"

    def walk_chain(start: int, expected: list, ctx: str):
        got = []
        o = start
        steps = 0
        while o:
            assert steps < 100000, f"{ctx}: chain runs away"
            steps += 1
            nxt, spec, vsize = hdr(o)
            kind = nxt & 7
            nm = name_of(o)
            node = expected.pop(0) if expected else None
            assert node is not None, f"{ctx}: unexpected entry {nm!r} at {o:#x}"
            assert nm == node.name, f"{ctx}: name {nm!r} != {node.name!r}"
            assert kind == node.kind, \
                f"{ctx}: {nm!r} kind {kind} != {node.kind}"
            if kind in (TYPE_REG, TYPE_SYM):
                assert image[spec:spec + vsize] == node.data, \
                    f"{ctx}: {nm!r} data mismatch"
                meta = (16 + len(nm.encode()) + 1 + 15) & ~15
                assert spec == o + meta, \
                    f"{ctx}: {nm!r} spec off formula"
            elif kind == TYPE_HRD:
                target_name = node.rel_target.rsplit("/", 1)[-1]
                assert name_of(spec) == target_name, \
                    f"{ctx}: {nm!r} -> {name_of(spec)!r} not {target_name!r}"
            elif kind == TYPE_DIR:
                walk_chain(spec, list(node.children), ctx + "/" + nm)
            got.append(nm)
            o = nxt & ~15
        assert not expected, \
            f"{ctx}: missing entries {[n.name for n in expected]}"

    root_spec = struct.unpack(">I", image[root_off + 4:root_off + 8])[0]
    walk_chain(root_spec & ~15, list(root.children), "/")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--root", required=True)
    ap.add_argument("--label", default="NEWOS")
    ap.add_argument("--link", action="append", default=[],
                    help="SRCFILE=LINKNAME hardlink")
    ap.add_argument("--symlink", action="append", default=[],
                    help="TARGET@NAME symlink entry")
    args = ap.parse_args()

    root = Node("", "", TYPE_DIR)
    collect(Path(args.root), root, "")

    for spec in args.link or []:
        src, _, dst = spec.partition("=")
        src_node = find(root, src)
        if src_node.kind != TYPE_REG:
            raise SystemExit(f"--link: {src} is not a regular file")
        parent = src_node.parent
        if any(ch.name == dst for ch in parent.children):
            raise SystemExit(f"--link: {dst} already exists")
        link = Node(src_node.rel + "#" + dst, dst, TYPE_HRD)
        link.rel_target = src_node.rel
        link.parent = parent
        parent.children.append(link)

    for spec in args.symlink or []:
        target, _, nm = spec.partition("@")
        sym = Node("@" + nm, nm, TYPE_SYM, target.encode())
        sym.parent = root
        root.children.append(sym)

    image = layout(root, args.label)
    verify(image, root)
    Path(args.out).write_bytes(image)
    print(f"built {args.out}: {len(image)} bytes, label {args.label!r}, "
          f"{len(all_nodes(root))} inodes")


if __name__ == "__main__":
    main()
