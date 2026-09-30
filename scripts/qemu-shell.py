#!/usr/bin/env python3
"""Boot NEWOS in QEMU and drive nsh over the serial console.

Reads commands from a file (one per line, '#' starts a comment) and prints a
transcript. Each command is typed a byte at a time while the read side keeps
being drained, and the harness waits for the prompt instead of sleeping, so
what comes back is the guest's answer to that exact command.

  scripts/qemu-shell.py build/images/newos-x86_64.elf cmds.txt [--mem 512] [-- extra qemu args]

Exit status is non-zero when any command did not return a prompt.
"""

import os
import pty
import re
import select
import subprocess
import sys
import termios
import time

PROMPT = re.compile(rb"(?:nsh>|root@newos:[^#]*#)\s*$")
TIMEOUT = 30
BOOT_TIMEOUT = 90
PACE = 0.02


def raw(slave):
    """Host line discipline off: no local echo, no canonical buffering."""
    attrs = termios.tcgetattr(slave)
    attrs[0] = attrs[1] = attrs[3] = 0
    attrs[6][termios.VMIN] = 1
    attrs[6][termios.VTIME] = 0
    termios.tcsetattr(slave, termios.TCSANOW, attrs)


def run(cmds, kernel, mem="512", extra=()):
    """Yield (label, bytes | None); None means the prompt never came back."""
    master, slave = pty.openpty()
    raw(slave)
    argv = [
        os.environ.get("QEMU", "qemu-system-x86_64"),
        "-kernel", kernel, "-m", mem, "-no-reboot", "-display", "none",
        "-serial", "stdio",
    ] + list(extra)
    proc = subprocess.Popen(argv, stdin=slave, stdout=slave,
                            stderr=sys.stderr, close_fds=False)
    os.close(slave)
    buf = bytearray()

    def drain(until, pattern=PROMPT):
        """Read until `pattern` matches the tail, or `until` passes."""
        while time.time() < until:
            if not select.select([master], [], [], 0.05)[0]:
                continue
            try:
                buf.extend(os.read(master, 4096))
            except OSError:
                return False
            if pattern.search(bytes(buf)):
                return True
        return False

    def type_line(text):
        """One byte per PACE seconds, draining as we go.

        QEMU's stdio chardev drives host stdin and the guest's serial output
        from one loop: while nobody reads the write side of the pty, the
        device model stops servicing RX too and keystrokes vanish silently.
        """
        data = text.encode() + b"\n"
        now = time.time()
        for i, b in enumerate(data):
            at = now + i * PACE
            while time.time() < at:
                if select.select([master], [], [], min(0.005, at - time.time()))[0]:
                    buf.extend(os.read(master, 4096))
            os.write(master, bytes([b]))
        return drain(time.time() + TIMEOUT)

    try:
        ok = drain(time.time() + BOOT_TIMEOUT)
        yield "<boot>", bytes(buf)
        buf.clear()
        if not ok:
            return
        for c in cmds:
            hit = type_line(c)
            out = bytes(buf)
            buf.clear()
            if proc.poll() is not None:
                yield c, None
                print(f"!!! QEMU exited (status {proc.returncode}) during `{c}`")
                return
            yield c, out if hit else None
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
        os.close(master)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    kernel, cmdfile = sys.argv[1], sys.argv[2]
    mem, extra, args = "512", [], sys.argv[3:]
    i = 0
    while i < len(args):
        if args[i] == "--mem":
            mem, i = args[i + 1], i + 2
        else:
            extra.append(args[i])
            i += 1

    with open(cmdfile) as f:
        cmds = [l.split("#")[0].strip() for l in f]
        cmds = [c for c in cmds if c]

    timeouts = 0
    for cmd, out in run(cmds, kernel, mem, extra):
        text = (out or b"").decode("utf-8", "replace")
        if cmd == "<boot>":
            print("=== boot ===")
            print(text[-2500:])
            continue
        print(f"\n$ {cmd}")
        if out is None:
            timeouts += 1
            print(f"!!! no prompt within {TIMEOUT}s; tail: {text[-400:]}")
            continue
        nl = text.find("\n")
        if nl != -1 and cmd in text[:nl + 1]:
            text = text[nl + 1:]
        sys.stdout.write(text)

    print(f"\n=== {len(cmds)} commands, {timeouts} timeouts ===")
    return 1 if timeouts else 0


if __name__ == "__main__":
    sys.exit(main())
