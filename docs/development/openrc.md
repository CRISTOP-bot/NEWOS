# OpenRC on NEWOS

NEWOS ships a real OpenRC service manager: a dependency-solving runlevel
transaction engine, a PID 1 that follows inittab, the `rc-*` operator tools,
state files under `/run/openrc`, and init scripts that actually do work. It
is a native NEWOS implementation of upstream OpenRC's data model and
semantics, not a stub and not a wrapper around another init.

Everything lives in `userland/user/programs/openrc/` (six programs sharing
`librc/`), `userland/user/rootfs/openrc/etc/` (the configuration tree), and
`kernel/fs/initramfs/initramfs.c` (which installs both).

## What runs at boot

`kernel/core/core_init.c` starts PID 1 from a chain
`/sbin/openrc-init` → `/bin/sh` → `/init`. Only when every binary in that
chain fails does the kernel give up on userspace, so a broken OpenRC still
leaves a shell instead of a dead box.

`openrc-init` (PID 1) does four things, in order:

1. builds the runtime tree (`/run/openrc`, `/run/lock`, `/var/log`) and loads
   `/etc/rc.conf`;
2. runs every `sysinit` entry of `/etc/inittab`, serially;
3. spawns `/sbin/openrc` — with no level argument, which makes the manager
   walk `rc_bootlevel` and then `rc_defaultlevel` from `/etc/rc.conf`;
4. supervises the console: every `wait`/`once`/`askfirst` entry runs first (in
   file order, waited for), then the `respawn` entry is restarted whenever it
   exits. If inittab has no usable console entry at all, PID 1 drops to a
   plain `/bin/sh` loop.

`/run/openrc/current` is owned by `/sbin/openrc`: it writes the level as it
enters it. Nothing else may touch it.

## On-disk layout

| Path | Meaning |
| --- | --- |
| `/etc/init.d/<svc>` | the service script |
| `/etc/runlevels/<level>/<svc>` | runlevel membership |
| `/etc/conf.d/<svc>` | `KEY=VALUE` variables for one service |
| `/etc/rc.conf` | manager configuration (`rc_bootlevel`, `rc_defaultlevel`, `rc_runlevel`) |
| `/etc/inittab` | `<id>:<runlevels>:<action>:<process>` console/sysinit table |
| `/run/openrc/<level>/<svc>` | per-level state record |
| `/run/openrc/started/<svc>` | "this service is running" (upstream's marker) |
| `/run/openrc/current` | current runlevel |
| `/run/<svc>.pid` | pid of a detached service |

**The one deliberate deviation from upstream:** the NEWOS VFS has no
symlinks, so a runlevel entry is not a symlink but an *indirection file*
whose body is the target path (`/etc/init.d/desktop`). `rc_resolve_target()`
resolves it, so `rc-update add/del/show`, `rc-status` and `openrc` keep the
upstream contract and the entries stay readable with `cat`.

A state record is one line of `key=value` fields:

```
pid=21 runlevel=default started=2026-10-30 3:25:2
```

`pid=` is written only for a service that really detached (a trailing `&` in
its `start()` records `/run/<svc>.pid`). A synchronous service such as
`bootmisc` has no daemon, so naming the pid of the already-exited script
would be a lie; its record simply omits the field.

## The manager: `/sbin/openrc`

```
openrc [runlevel]            start a runlevel (writes /run/openrc/current)
openrc --stop [runlevel]     stop one; with no level, stops the current
openrc --status [runlevel]   list what that level holds as started
openrc --list                list installed runlevels
openrc <svc> start|stop|restart|status|zap
openrc --version | --help
```

A transaction is: collect the level's members → `pull_needs()` → topological
order → run each action through `/bin/sh`.

* **Solver.** `depend()` vocabulary is upstream's: `need`, `use`, `before`,
  `after`, `provide`, `keyword`. `edge_needed()` answers "must a start
  before b" from both sides of the pair, and `need`/`use` point the opposite
  way from `before`: a dependency listed there starts *first*. Contradictory
  declarations leave both nodes unplaceable, which is reported as
  `openrc: dependency cycle involving X` and then started in runlevel order
  so a cycle warns instead of stranding boot.
* **`need` across runlevels.** `pull_needs()` pulls a `need` target that is
  not a member of the level into the transaction (two passes, so a pulled
  service's own `need` is covered). A `need` that names no installed script
  fails the *dependent* service and says so; it never silently succeeds.
* **Idempotence.** A service already recorded in `/run/openrc/started` prints
  ` * already started <svc>` and is not started twice, which is what keeps
  `sysinit`'s `localmount` from being re-run when `boot` pulls it in.
* **Keywords.** `keyword gui` gates a service on the environment: `gui` is
  true only when a framebuffer exists *and* the cmdline asked for a
  graphical session; any other name is matched against `/proc/cmdline`, and a
  leading `-` inverts. A service whose keywords do not apply is skipped with
  an explicit message, not silently dropped.
* **Failures.** A non-zero exit from `start()`/`stop()` is reported, dependents
  are marked failed, and the process exit code counts the failures (which is
  what `openrc-init` prints as `openrc reported N service failure(s)`). A
  failed `stop` keeps the state record: the service is still running from the
  manager's point of view.
* **Per-service actions** resolve the owning runlevel from the state record,
  so `rc-service bootmisc restart` with `default` current updates `boot`'s
  record instead of moving it.

## Init scripts: the syntax nsh actually implements

`/bin/sh` runs a service script when invoked as `sh /etc/init.d/<svc> <action>`
(`userland/user/programs/init/shell_main.c`, `run_service_script()`). That is
a real, small Bourne-ish interpreter, not an approximation of one:

* `name() {` … `}` blocks, closing brace alone on its line;
* `$NAME` and `${NAME}` expansion, empty when unset;
* `SVCNAME` is set automatically, then `/etc/conf.d/<svc>` is loaded, so
  `$session_program` in a script comes from the conf.d file;
* one command per line, `|` pipelines, `>`/`>>`/`<` redirection;
* a trailing `&` detaches the command and writes `/run/<svc>.pid` (NEWOS has
  no `daemon(8)`/`start-stop-daemon`, so this is the supervision primitive);
* the script's exit status is the first non-zero command status, which is
  what makes `openrc`'s failure accounting real.

`kill @/run/<svc>.pid` (the `@path` form in `/bin/kill`) is how a service
stops its own detached process.

## The operator tools

* `rc-status [-a|--all] [runlevel]` — members of the reported level, then
  `Boot Runlevel` for records owned by `boot`, then `Dynamic Runlevel: manual`
  for everything else the manager is holding. Detached services show
  `(pid N)`.
* `rc-update add|del|show [<level>]` — writes/removes indirection files;
  `show` prints upstream's `          boot | bootmisc` layout.
* `rc-service <svc> <action>` — a thin frontend that forwards to
  `/sbin/openrc <svc> <action>` so state and solver live in exactly one place.

## How the tree gets into the image

`scripts/pack-openrc.py` walks `userland/user/rootfs/openrc/etc` and emits one
ORCF blob (`build/images/openrc-rootfs.orcf`: `"ORCF"`, then per record
`u8 type`, `u32 pathlen`, `u32 size`, path, payload). The **kernel** installs
it during initramfs setup (`install_rc_tree()` in
`kernel/fs/initramfs/initramfs.c`), next to the `/bin` toolbox, together with
the five `/sbin` binaries. Materialising `/etc` in the kernel is deliberate:
`rc-status`, `openrc` and `rc-service` then work even when PID 1 is the plain
nsh fallback, because nothing depends on a userspace unpack step having run.

To add a service: drop `etc/init.d/<name>` (plus `etc/conf.d/<name>` if it
needs variables) under `userland/user/rootfs/openrc/`, then
`rc-update add <name> default`. The packer picks the file up on the next
build; no C change is involved.

## Honest limits

* No `daemon(8)`, no `start-stop-daemon`, no `supervise-daemon`: detachment is
  the `&` + pidfile convention above, and a dead daemon is not restarted
  (there is no per-service respawn).
* No `/proc/kmsg`, so upstream's `dmesg`-on-start (`crashlog`) has no data to
  save; no `sethostname` syscall, so the `hostname` service does not exist.
* No `SIGHUP`-based reload conventions, no cgroups, no virtual runlevels
  (`nonetwork`, etc.), no `rc.conf` service-state (`rc_hotplug`,
  `rc_dlignore`), and no dependency on `/etc/conf.d/rc`.
* Inittab supports `sysinit`, `respawn`, `wait`, `once`, `askfirst` only: no
  single-user transition, no power actions, no ctrl allocator. `wait`, `once`
  and `askfirst` are all "run once, waited for", and exactly one `respawn`
  entry can be supervised, because `SYS_WAITPID` takes an exact pid and that
  blocking wait is what keeps the console alive.
* `openrc --stop` stops a level's services in reverse dependency order but
  does not switch runlevels the way `init 0` would, because there is no
  shutdown path that unmounts the tmpfs root.
