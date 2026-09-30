# NEWOS filesystem layout

NEWOS builds a small, Linux-style root hierarchy in tmpfs at boot. The
directories follow familiar Filesystem Hierarchy Standard roles, while their
contents are native to NEWOS and its current drivers.

| Path | Role |
| --- | --- |
| `/bin`, `/sbin` | Native command toolbox and system commands |
| `/boot` | Boot-related files and future boot configuration |
| `/dev` | Kernel device nodes such as console, serial, zero and mouse |
| `/etc` | Hostname, passwd, release metadata, shell startup and system config |
| `/home`, `/root` | User and administrator homes with Desktop, Documents, Downloads and Pictures |
| `/lib`, `/lib64` | Reserved for system libraries |
| `/media`, `/mnt` | Removable media and temporary mount points |
| `/opt` | Optional software |
| `/proc`, `/sys` | Kernel process and device information |
| `/run` | Runtime state, including lock files |
| `/srv` | Data served by system services |
| `/tmp` | Temporary files and staged packages |
| `/usr/bin`, `/usr/sbin` | Add-on and packaged commands; GNU tools install here |
| `/usr/include`, `/usr/lib`, `/usr/lib64`, `/usr/src` | Development files and libraries |
| `/usr/share` | Architecture-independent data, documentation and licenses |
| `/usr/local` | Locally installed software |
| `/var/cache`, `/var/lib`, `/var/log`, `/var/opt`, `/var/spool`, `/var/tmp` | Persistent-style cache, package state, logs and variable data |

The current VFS does not implement symbolic links or a persistent disk root.
For compatibility, NEWOS keeps its built-in commands in `/bin` and places
installable packages under `/usr/bin` instead of aliasing `/bin` to
`/usr/bin`. `/etc/os-release` identifies the distribution, and
`/usr/share/doc/newos/README` gives a brief in-system overview. The initial
root also includes `/etc/group`, `/etc/shells`, `/etc/issue`, and starter
`.profile` files under `/etc/skel`, `/home/user`, and `/root`.
