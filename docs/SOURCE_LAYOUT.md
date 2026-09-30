# Kernel source layout

NEWOS keeps the familiar subsystem-oriented layout used by large kernels, but
retains its own names and interfaces so it can build independently.

| NEWOS | Purpose | Linux kernel analogue |
| --- | --- | --- |
| `arch/x86_64` | Boot, CPU, interrupt, paging and context-switch code | `arch/x86` |
| `core`, `process`, `sys`, `syscall` | Kernel core, processes, synchronization and syscall boundary | `kernel`, `init` |
| `drivers`, `dev` | Hardware drivers and device support | `drivers` |
| `fs` | VFS, tmpfs, initramfs and pseudo-filesystems | `fs` |
| `include`, `abi` | Internal headers and user/kernel ABI | `include` |
| `ipc` | Pipes and inter-process communication | `ipc` |
| `lib`, `libc` | Kernel helpers and user-space C library | `lib`, `tools` libraries |
| `mm` | Physical, virtual and user memory management | `mm` |
| `net` | Network devices and protocol layers | `net` |
| `crypto` | Cryptographic primitives | `crypto` |
| `docs`, `scripts`, `tools` | Documentation, build automation and host tools | `Documentation`, `scripts`, `tools` |

Linux places private headers beside their implementation and shared kernel
interfaces under `include/linux/`; architecture-specific interfaces live under
`arch/<architecture>/include/`. NEWOS follows the same separation in a smaller
form, with cross-subsystem headers under `include/` and x86-specific headers
under `arch/x86_64/include/`.

This is a structural reference, not a vendored Linux kernel. Linux source uses
a different boot flow, kernel ABI, driver model and licensing terms, so copying
its files into NEWOS would not make them build or run here. Ported components
need to be adapted to NEWOS interfaces and kept with their own license and
attribution.
