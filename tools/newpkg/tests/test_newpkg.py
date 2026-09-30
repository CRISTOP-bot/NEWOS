#!/usr/bin/env python3
"""test_newpkg.py: end-to-end tests for the native `.new` package system.

What runs here is REAL code, not simulations:
- packages are assembled by tools/newpkg/newpkg-build.py (the shiped builder),
- all install/verify/remove/list/files/conflict/dependency decisions are made
  by user/programs/newpkg/newpkg_main.c + newpkg_format.c, compiled for the
  host against tools/newpkg/posix-shim.c (a syscall jail, like QEMU
  virtualizes hardware: only read/write/open/close/mkdir/unlink/readdir and
  fixed uname/gettime answers).

Covers: parsing, metadata, checksums, creation, extraction, installation,
removal, conflicts, dependencies (missing/incompatible/circular),
corrupt packages, wrong architecture. Run:
  python3 tools/newpkg/tests/test_newpkg.py
(or: make check-newpkg, which also runs the C unit driver + a full build).
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

# tools/newpkg/tests/test_newpkg.py -> repo root (3 dirs up from this file).
REPO = os.path.dirname(
    os.path.dirname(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
BUILDER = os.path.join(REPO, "tools", "newpkg", "newpkg-build.py")
HOST_BIN = os.environ.get("NEWPKG_HOST_BIN", "/tmp/newpkg-host")


def run_builder(newspec, out, root):
    return subprocess.run(
        [sys.executable, BUILDER, newspec, "-o", out, "--root", root],
        capture_output=True, text=True)


def parse_pkg(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[0:4] == b"NEW1", "bad magic"
    ver, flags = struct.unpack("<HH", data[4:8])
    assert (ver, flags) == (1, 0)
    ml, manl, payl = struct.unpack("<III", data[8:20])
    hc, mc, manc, payc, fc = struct.unpack("<IIIII", data[20:40])
    assert data[40:48] == b"\x00" * 8, "reserved must be zero"
    assert len(data) == 48 + ml + manl + payl, "length mismatch"
    import binascii
    meta = data[48:48 + ml]
    man = data[48 + ml:48 + ml + manl]
    pay = data[48 + ml + manl:]
    assert binascii.crc32(meta) & 0xffffffff == mc, "meta crc"
    assert binascii.crc32(man) & 0xffffffff == manc, "manifest crc"
    assert binascii.crc32(pay) & 0xffffffff == payc, "payload crc"
    lines = man.decode().splitlines()
    assert len(lines) == fc, "file count"
    assert man.endswith(b"\n"), "manifest must end with newline"
    paths = []
    total = 0
    for ln in lines:
        mode, size, crc, p = ln.split(" ", 3)
        assert p.startswith("/") and ".." not in p.split("/")
        total += int(size)
        paths.append(p)
        assert 0 <= int(size) <= 2 * 1024 * 1024
    assert paths == sorted(paths), "manifest must be sorted"
    assert total == payl, "payload length"
    return meta.decode(), lines, pay


class NewpkgTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="newpkg-test-")
        self.root = os.path.join(self.tmp, "root")
        os.makedirs(os.path.join(self.root, "tmp"))
        self.env = dict(os.environ, NEWPKG_HOST_ROOT=self.root)
        self.n = 0

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    # -- helpers ---------------------------------------------------------
    def make_pkg(self, name, version, files, arch="x86_64", depends="",
                 newos_min="0.2.0", desc="test package"):
        """files: [(dst, mode, bytes)]. Returns host path of the .new."""
        self.n += 1
        work = os.path.join(self.tmp, "pkg%d" % self.n)
        os.makedirs(os.path.join(work, "src"))
        spec = [
            'name = "%s"' % name,
            'version = "%s"' % version,
            'arch = "%s"' % arch,
            'desc = "%s"' % desc,
            'license = "MIT"',
            'maintainer = "test"',
            'build = "test"',
            'newos-min = "%s"' % newos_min,
            'depends = "%s"' % depends,
            'files:',
        ]
        for i, (dst, mode, content) in enumerate(files):
            src = "src/f%d" % i
            with open(os.path.join(work, src), "wb") as f:
                f.write(content)
            spec.append("  %s %s %s" % (src, dst, mode))
        newspec = os.path.join(work, "p.newspec")
        with open(newspec, "w") as f:
            f.write("\n".join(spec) + "\n")
        out = os.path.join(work, "out.new")
        r = run_builder(newspec, out, work)
        self.assertEqual(r.returncode, 0, "builder failed: " + r.stderr)
        return out

    def stage(self, pkg):
        """Copy a .new into the fake root's /tmp and return its VFS path."""
        self.n += 1
        vfs = "/tmp/stage%d.new" % self.n
        shutil.copy(pkg,
                    os.path.join(self.root, "tmp",
                                 os.path.basename(vfs)))
        return vfs

    def newpkg(self, *args):
        r = subprocess.run([HOST_BIN] + list(args), capture_output=True,
                           text=True, env=self.env)
        return r.returncode, r.stdout + r.stderr

    def root_file(self, vfs_path):
        return os.path.join(self.root, vfs_path.lstrip("/"))

    # -- format / creation ------------------------------------------------
    def test_build_deterministic(self):
        a = self.make_pkg("det", "1.0.0",
                          [("/bin/det", "0755", b"bytes")])
        with open(a, "rb") as f:
            first = f.read()
        work = os.path.join(self.tmp, "rebuild")
        shutil.copytree(os.path.join(self.tmp, "pkg1"), work)
        out = os.path.join(work, "out2.new")
        r = run_builder(os.path.join(work, "p.newspec"), out, work)
        self.assertEqual(r.returncode, 0, r.stderr)
        with open(out, "rb") as f:
            self.assertEqual(f.read(), first)

    def test_structure_valid(self):
        p = self.make_pkg("struct", "2.1.0",
                          [("/bin/b2", "0755", b"B" * 100),
                           ("/etc/a.conf", "0644", b"cfg")])
        meta, lines, pay = parse_pkg(p)
        for k in ("name", "version", "arch", "desc", "license",
                  "maintainer", "build", "newos-min", "depends"):
            self.assertIn(k + ":", meta)
        self.assertEqual(len(lines), 2)
        self.assertTrue(lines[0].endswith("/bin/b2"))
        self.assertTrue(lines[1].endswith("/etc/a.conf"))

    def test_sample_package_valid(self):
        sample = os.path.join(REPO, "build", "sample.new")
        if not os.path.isfile(sample):
            self.skipTest("build/sample.new not built yet")
        meta, lines, pay = parse_pkg(sample)
        self.assertIn("name: hello-new", meta)
        with open(os.path.join(REPO, "build", "userland", "hello.elf"),
                  "rb") as f:
            elf = f.read()
        self.assertEqual(pay, elf)
        self.assertEqual(elf[0:4], b"\x7fELF")

    # -- info / verify ------------------------------------------------------
    def test_info_verify_ok(self):
        p = self.make_pkg("hello-new", "1.0.0",
                          [("/bin/hello-new", "0755", b"ELF...")])
        vfs = self.stage(p)
        rc, out = self.newpkg("info", vfs)
        self.assertEqual(rc, 0, out)
        self.assertIn("name: hello-new", out)
        self.assertIn("integrity: OK", out)
        rc, out = self.newpkg("verify", vfs)
        self.assertEqual(rc, 0, out)
        self.assertIn("OK (1 files)", out)

    # -- install / list / files / remove ------------------------------------
    def test_install_list_files_remove(self):
        payload = b"P" * 700  # > 512: exercises chunked streaming
        p = self.make_pkg("hello-new", "1.0.0",
                          [("/bin/hello-new", "0755", payload)])
        vfs = self.stage(p)
        rc, out = self.newpkg("install", vfs)
        self.assertEqual(rc, 0, out)
        with open(self.root_file("/bin/hello-new"), "rb") as f:
            self.assertEqual(f.read(), payload)
        rc, out = self.newpkg("list")
        self.assertEqual(rc, 0, out)
        self.assertIn("hello-new 1.0.0", out)
        rc, out = self.newpkg("files", "hello-new")
        self.assertEqual(rc, 0, out)
        self.assertIn("/bin/hello-new", out)
        rc, out = self.newpkg("remove", "hello-new")
        self.assertEqual(rc, 0, out)
        self.assertFalse(os.path.exists(self.root_file("/bin/hello-new")))
        self.assertFalse(
            os.path.exists(self.root_file("/var/lib/newpkg/hello-new.info")))
        self.assertFalse(os.path.exists(
            self.root_file("/var/lib/newpkg/hello-new.files")))
        rc, out = self.newpkg("list")
        self.assertEqual(rc, 0, out)
        self.assertIn("no packages installed", out)

    def test_reinstall_same_version_ok(self):
        p = self.make_pkg("re", "1.0.0", [("/bin/re", "0755", b"v1")])
        vfs = self.stage(p)
        self.assertEqual(self.newpkg("install", vfs)[0], 0)
        rc, out = self.newpkg("install", vfs)
        self.assertEqual(rc, 0, out)  # own files: upgrade path

    def test_remove_and_files_missing_fail(self):
        self.assertNotEqual(self.newpkg("remove", "ghost")[0], 0)
        self.assertNotEqual(self.newpkg("files", "ghost")[0], 0)

    # -- corruption -----------------------------------------------------------
    def _corrupt_each_section(self, pkg):
        with open(pkg, "rb") as f:
            data = bytearray(f.read())
        ml, manl, payl = struct.unpack("<III", data[8:20])
        offs = {
            "magic": 0,
            "header": 10,
            "meta": 48,
            "manifest": 48 + ml,
            "payload": 48 + ml + manl,
        }
        self.assertTrue(payl > 0)
        for name, off in offs.items():
            bad = bytearray(data)
            bad[off] ^= 0xFF
            yield name, bytes(bad)

    def test_corrupt_rejected_and_no_trace(self):
        p = self.make_pkg("corr", "1.0.0",
                          [("/bin/corr", "0755", b"D" * 600)])
        for name, bad in self._corrupt_each_section(p):
            with self.subTest(section=name):
                self.n += 1
                vp = "/tmp/bad%d.new" % self.n
                with open(os.path.join(self.root, "tmp",
                                       os.path.basename(vp)), "wb") as f:
                    f.write(bad)
                rc, out = self.newpkg("verify", vp)
                self.assertNotEqual(rc, 0, name + ": " + out)
                rc, out = self.newpkg("install", vp)
                self.assertNotEqual(rc, 0, name + ": " + out)
                self.assertFalse(
                    os.path.exists(self.root_file("/bin/corr")),
                    name + " left a partial file")
                self.assertFalse(os.path.exists(self.root_file(
                    "/var/lib/newpkg/corr.info")), name + " left DB trace")

    def test_truncated_rejected(self):
        p = self.make_pkg("trunc", "1.0.0",
                          [("/bin/trunc", "0755", b"D" * 100)])
        with open(p, "rb") as f:
            data = f.read()
        self.n += 1
        vp = "/tmp/trunc.new"
        with open(os.path.join(self.root, "tmp", "trunc.new"),
                  "wb") as f:
            f.write(data[:len(data) // 2])
        rc, out = self.newpkg("verify", vp)
        self.assertNotEqual(rc, 0, out)
        rc, out = self.newpkg("install", vp)
        self.assertNotEqual(rc, 0, out)
        self.assertFalse(os.path.exists(self.root_file("/bin/trunc")))

    # -- architecture / OS version gates ---------------------------------------
    def test_arch_mismatch_rejected(self):
        p = self.make_pkg("arch", "1.0.0", [("/bin/arch", "0755", b"x")],
                          arch="aarch64")
        vfs = self.stage(p)
        rc, out = self.newpkg("verify", vfs)
        self.assertEqual(rc, 0, out)  # container itself is sound
        rc, out = self.newpkg("install", vfs)
        self.assertNotEqual(rc, 0, out)
        self.assertIn("architecture", out)

    def test_newos_min_future_rejected(self):
        p = self.make_pkg("fut", "1.0.0", [("/bin/fut", "0755", b"x")],
                          newos_min="9.9.9")
        vfs = self.stage(p)
        rc, out = self.newpkg("install", vfs)
        self.assertNotEqual(rc, 0, out)
        self.assertIn("NEWOS", out)

    # -- dependencies ------------------------------------------------------------
    def test_missing_dep_rejected(self):
        p = self.make_pkg("need", "1.0.0", [("/bin/need", "0755", b"x")],
                          depends="libfoo (>= 1.0)")
        vfs = self.stage(p)
        rc, out = self.newpkg("install", vfs)
        self.assertNotEqual(rc, 0, out)
        self.assertIn("missing dependency", out)

    def test_dep_version_gates(self):
        lib = self.make_pkg("libfoo", "1.2.0",
                            [("/etc/libfoo.conf", "0644", b"cfg")])
        self.assertEqual(self.newpkg("install", self.stage(lib))[0], 0)
        bad = self.make_pkg("bad", "1.0.0", [("/bin/bad", "0755", b"x")],
                            depends="libfoo (>= 2.0)")
        rc, out = self.newpkg("install", self.stage(bad))
        self.assertNotEqual(rc, 0, out)
        self.assertIn("incompatible", out)
        good = self.make_pkg("good", "1.0.0", [("/bin/good", "0755", b"x")],
                             depends="libfoo (>= 1.0), libfoo (< 2.0)")
        rc, out = self.newpkg("install", self.stage(good))
        self.assertEqual(rc, 0, out)
        # Chained: good -> libfoo; top depends on good.
        top = self.make_pkg("top", "3.0.0", [("/bin/top", "0755", b"x")],
                            depends="good (= 1.0.0)")
        rc, out = self.newpkg("install", self.stage(top))
        self.assertEqual(rc, 0, out)

    def test_self_dep_circular(self):
        p = self.make_pkg("selfish", "1.0.0",
                          [("/bin/selfish", "0755", b"x")],
                          depends="selfish")
        rc, out = self.newpkg("install", self.stage(p))
        self.assertNotEqual(rc, 0, out)
        self.assertIn("circular", out)

    def test_indirect_circular(self):
        a1 = self.make_pkg("cyc", "1.0.0", [("/bin/cyc", "0755", b"v1")])
        self.assertEqual(self.newpkg("install", self.stage(a1))[0], 0)
        b = self.make_pkg("cycb", "1.0.0", [("/bin/cycb", "0755", b"b")],
                          depends="cyc")
        self.assertEqual(self.newpkg("install", self.stage(b))[0], 0)
        # Upgrade cyc to 2.0 depending on cycb: cyc -> cycb -> cyc.
        a2 = self.make_pkg("cyc", "2.0.0", [("/bin/cyc", "0755", b"v2")],
                           depends="cycb")
        rc, out = self.newpkg("install", self.stage(a2))
        self.assertNotEqual(rc, 0, out)
        self.assertIn("circular", out)
        # Failed upgrade must not clobber the old install.
        with open(self.root_file("/bin/cyc"), "rb") as f:
            self.assertEqual(f.read(), b"v1")
        rc, out = self.newpkg("list")
        self.assertIn("cyc 1.0.0", out)

    # -- conflicts ------------------------------------------------------------------
    def test_conflict_owned_needs_force(self):
        a = self.make_pkg("ownera", "1.0.0",
                          [("/bin/shared", "0755", b"A")])
        self.assertEqual(self.newpkg("install", self.stage(a))[0], 0)
        b = self.make_pkg("ownerb", "1.0.0",
                          [("/bin/shared", "0755", b"B")])
        vb = self.stage(b)
        rc, out = self.newpkg("install", vb)
        self.assertNotEqual(rc, 0, out)
        self.assertIn("conflict", out)
        with open(self.root_file("/bin/shared"), "rb") as f:
            self.assertEqual(f.read(), b"A")  # untouched
        rc, out = self.newpkg("install", "--force", vb)
        self.assertEqual(rc, 0, out)
        with open(self.root_file("/bin/shared"), "rb") as f:
            self.assertEqual(f.read(), b"B")
        # Ownership transferred to ownerb.
        rc, out = self.newpkg("files", "ownerb")
        self.assertIn("/bin/shared", out)
        self.assertEqual(self.newpkg("remove", "ownerb")[0], 0)
        self.assertFalse(os.path.exists(self.root_file("/bin/shared")))

    def test_conflict_unowned_needs_force(self):
        os.makedirs(os.path.join(self.root, "bin"), exist_ok=True)
        with open(self.root_file("/bin/pre"), "wb") as f:
            f.write(b"PRE")
        p = self.make_pkg("pre", "1.0.0", [("/bin/pre", "0755", b"PKG")])
        vfs = self.stage(p)
        rc, out = self.newpkg("install", vfs)
        self.assertNotEqual(rc, 0, out)
        self.assertIn("conflict", out)
        with open(self.root_file("/bin/pre"), "rb") as f:
            self.assertEqual(f.read(), b"PRE")
        self.assertEqual(self.newpkg("install", "--force", vfs)[0], 0)
        with open(self.root_file("/bin/pre"), "rb") as f:
            self.assertEqual(f.read(), b"PKG")

    # -- builder security ----------------------------------------------------------
    def test_builder_rejects_bad_paths(self):
        bad_dsts = ["../escape", "/dev/null", "/proc/x", "/sys/y", "/init",
                    "/bin//double", "/bin/trailing/", "/bin/../escape",
                    "relative/path", "/nope/x", "/bin/bad char"]
        for dst in bad_dsts:
            with self.subTest(dst=dst):
                self.n += 1
                work = os.path.join(self.tmp, "bad%d" % self.n)
                os.makedirs(os.path.join(work, "src"))
                with open(os.path.join(work, "src", "f"), "wb") as f:
                    f.write(b"x")
                spec = "\n".join([
                    'name = "bad"', 'version = "1.0.0"',
                    'arch = "x86_64"', 'desc = "d"', 'license = "MIT"',
                    'maintainer = "m"', 'build = "b"',
                    'newos-min = "0.2.0"', 'depends = ""', "files:",
                    "  src/f %s 0755" % dst]) + "\n"
                ns = os.path.join(work, "p.newspec")
                with open(ns, "w") as f:
                    f.write(spec)
                r = run_builder(ns, os.path.join(work, "o.new"), work)
                self.assertNotEqual(r.returncode, 0, dst)

    def test_builder_rejects_empty_files(self):
        self.n += 1
        work = os.path.join(self.tmp, "empty%d" % self.n)
        os.makedirs(work)
        ns = os.path.join(work, "p.newspec")
        with open(ns, "w") as f:
            f.write('name = "e"\nversion = "1.0.0"\narch = "x86_64"\n'
                    'desc = "d"\nlicense = "MIT"\nmaintainer = "m"\n'
                    'build = "b"\nnewos-min = "0.2.0"\ndepends = ""\n'
                    'files:\n')
        r = run_builder(ns, os.path.join(work, "o.new"), work)
        self.assertNotEqual(r.returncode, 0, r.stdout)


if __name__ == "__main__":
    if not os.path.isfile(HOST_BIN):
        print("newpkg host binary missing: %s\nbuild it with:\n"
              "  gcc -DNEWPKG_HOST -Wall -Wextra -Werror -o %s \\\n"
              "      user/programs/newpkg/newpkg_format.c \\\n"
              "      user/programs/newpkg/newpkg_main.c \\\n"
              "      tools/newpkg/posix-shim.c" % (HOST_BIN, HOST_BIN),
              file=sys.stderr)
        sys.exit(2)
    unittest.main(verbosity=2)
