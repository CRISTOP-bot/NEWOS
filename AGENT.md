# AGENT.md — NEWOS (guía para agentes de código)

Sistema operativo x86_64 escrito desde cero (sin Linux): kernel monolítico
freestanding, shell (`nsh`), toolbox `/bin`, libc propia (`libc/`) y
capa fina de syscalls (`user/lib/nshlib`). Idioma del repo: inglés en
código/docs; el usuario habla español.

## Comandos (siempre desde la raíz `~/newos`)

```sh
make all            # ISO Limine -> build/images/newos-x86_64-limine.iso
make build/images/newos-x86_64.elf   # kernel PVH/multiboot2 (lo que arranca qemu-test)
make limine         # kernel variante Limine  -> build/images/newos-x86_64-limine.elf
make build/images/newos-x86_64-limine.iso   # ISO híbrida BIOS+UEFI (Limine)
make qemu-test      # suite automatizada PVH (14 tests, exit 1 = PASS)
make qemu-limine-test  # suite vía ISO Limine (rebuild con test_mode=1)
make qemu           # arranque interactivo con ventana (requiere DISPLAY)
make clean
```

QEMU manual (sin ventana):

```sh
# PVH directo, serie a stdio:
qemu-system-x86_64 -kernel build/images/newos-x86_64.elf -m 128M \
  -no-reboot -display none -serial stdio -device pcnet
# ISO Limine BIOS:
qemu-system-x86_64 -cdrom build/images/newos-x86_64-limine.iso -boot order=d \
  -m 128M -no-reboot -display none -serial stdio -device pcnet
# ISO Limine UEFI (OVMF de /usr/share/ovmf/x64/):
qemu-system-x86_64 -cdrom build/images/newos-x86_64-limine.iso -boot order=d \
  -serial stdio -no-reboot -m 128M -display none \
  -drive file=/usr/share/ovmf/x64/OVMF_CODE.4m.fd,if=pflash,format=raw,unit=0,readonly=on \
  -drive file=/tmp/ovmf_vars.fd,if=pflash,format=raw,unit=1 -device pcnet
```

`qemu-test` usa `-append "test_mode=1"` + `isa-debug-exit` (exit 1 = todo
PASS, >= 3 = fallo). Sin `test_mode` arranca `/init` (shell interactiva).

## Depurar con QEMU (headless)

- Consola serie es autoritativa para CI; VGA/fbcon es visual.
- Screenshot VGA/FB vía monitor unix + `screendump`:
  `-monitor unix:/tmp/mon,server,nowait`, luego
  `echo "screendump /tmp/s.ppm" | socat - UNIX-CONNECT:/tmp/mon`
  y convertir con `convert s.ppm s.png`. Leer el PNG para verificar.
- Inyectar input PS/2: `mouse_move dx dy`, `mouse_button 0|1` por monitor.
  (`info mice` lista dispositivos; solo hay PS/2, índice 1.)
- Entrada serie desde pipe: `(sleep 15; printf 'cmd\n'; sleep N) | qemu ... -serial stdio`
  (esperar ~10-15 s al boot; el tipado es lento por redibujado).
- `read` del agente SÍ lee imágenes PNG.

## Arquitectura (verificado en código)

- Boot triple: **PVH** (`-kernel`, `arch_main` detecta `start_info`),
  **multiboot2** (GRUB), **Limine** (`limine_arch_main`, `limine_entry.S`).
- Solo Limine da framebuffer; PVH/GRUB usan texto VGA + serie.
- Mapa: kernel `0xffffffff80000000`, direct map `0xfffffe0000000000`
  (4 GiB en páginas 2 MiB WB — NUNCA 1 GiB: VBox oculta PDPE1GB),
  usuario `[0x400000, 0x800000000000)`,
  stack 1 MiB bajo `0x7fffffffe000`.
- Interrupciones: IDT 256, PIC 8259 remapeado `0x20/0x28`, `int $0x80`
  para syscalls. **Sin MSI/MSI-X, sin IOAPIC/LAPIC, sin ACPI.**
- Scheduler cooperativo+tick: `sched_yield()` + handoff por
  `g_sched_next_sp` en el epílogo ASM. Leer `process/sched.c` antes de
  tocar bloqueos.
- Capas estrictas (`scripts/ci/lint_layering.sh` corre en cada build):
  `LIB < ABI < ARCH/CORE (permisivos) < DRV/MM/FS < PROC < IPC < SYSC < USER`.
  **SYSC no puede incluir `<drivers/...>`** (las decls del backend FB
  viven en `include/syscall/syscall.h` por esto). DRV sí puede usar
  ARCH/CORE/MM.

## Layout y convenciones

- Un fichero = un dominio; prefijo en el nombre (`pcnet_`, `vfs_`, ...).
- Drivers con dispositivo real: PCI (`0xCF8/0xCFC`, bus `"pci"`),
  PCnet (DWIO, anillo 4+4), serial 16550, PS/2 kbd/mouse, PIT, CMOS,
  framebuffer (`drivers/graphics/fb.c`), VGA texto (fallback).
- Consola multiplexada (`tty_console`): serie + VGA texto + fbcon.
- Syscalls `0-21` en `abi/syscall_abi.h` (`FBINFO=20`, `FBWRITE=21`);
  wrappers en `user/lib/nshlib.{h,c}` (macros `SYSC0-3`, solo 3 args).
- Userland: `user/programs/<cmd>/<cmd>_main.c` + registrar en
  `PROG_NAMES` (Makefile) y `bin_table` (`fs/initramfs/initramfs.c`).
  `libc/` (string/stdlib/stdio/malloc, arena 256 KiB, sin FPU/threads;
  clase lint `LIBC`, solo auto+ABI) + `libc.a`; `ltest` la ejercita.
  `-mgeneral-regs-only` (sin SSE/FPU) en kernel Y userland.
- Toolchain cruzada opcional: `toolchain/build.sh` → `toolchain/out`
  (binutils+GCC x86_64-elf); usar con `make CROSS_PREFIX=x86_64-elf-`.
- Recursos generados en `build/` (font, imágenes): reglas Makefile con
  `scripts/fetch-*.sh` / `scripts/gen-images.py`. OJO Makefile:
  `OBJS :=` expande **inmediato**: definir `*_EMBED` ANTES de `OBJS`.

## Subsistemas recientes (no romper)

- **Scheduler/yield** (`process/sched.c`): tras publicar un handoff hay
  que volver al epílogo YA (`if (sched_yield(f)) return 0;`). Seguir
  girando corrompe `rip` (doble-rewind) y el slot ajeno. Guardias
  permanentes: `YIELD-REWIND` (verifica `cd 80`), `SLOT-MISMATCH`.
  `SLEEP` usa deadline persistente (`thread.sleep_due/sleeping`).
- **copy_user_string**: copiar byte a byte (el bloque argv termina con
  padding bajo `USER_STACK_TOP`; chunks fijos sobre-leen a páginas sin
  mapear).
- **Framebuffer** (`drivers/graphics/fb.c`): ventana UC 1 GiB en
  `PML4[507]` (`0xFFFFFD8000000000`), páginas 2 MiB **con bit HUGE/PS**,
  PCD|PWT. Dirección Limine = virtual HHDM (restar `hhdm->offset`).
  Fuente `font8x8_basic.h` (CC0) es **LSB-first**. Solo RGB 24/32bpp.
- **Mouse PS/2**: el init enmascara IRQ1 (si no, el handler de teclado
  roba el byte de config `0x20` → "no mouse detected"). Soporta rueda
  (magia Intellimouse, paquetes 4 bytes). Eje Y del dispositivo = arriba.
- **`/dev/mouse`**: `X x Y y B b W w`, EOF tras una lectura (offset>0).
- **Direct map sin páginas 1 GiB**: 4 PDs × 2 MiB (`x64_pd_direct`
  en `.early_bss`, ambas rutas). VirtualBox oculta PDPE1GB → 1 GiB
  faultaba con #PF reserved-bit y triple-fault (guru meditation).
- **img/vid**: BMP 24/32-bit sin compresión + PPM P6 (decoders propios);
  JPEG/PNG se rechazan con mensaje (necesitan heap userspace + FPU).
  Imágenes de prueba en `/etc/splash.bmp`, `/etc/test.ppm`.
- Reloj: bajo QEMU-TCG los ticks PIT de 1000 Hz se coalescan (~20x
  lento: `uptime`/`sleep`/`vid` funcionan pero estirados). En HW real
  el rate programado (divisor verificado) es correcto. No tocar PIT
  por esto.
- Atajos shell: `Ctrl+A/E/U/W/K/Y/V` + clipboard interno, `Ctrl+C/D/L`.
- `whoami`/`id` parsean `/etc/passwd` real; `hostname` lee/escribe
  `/etc/hostname`; `about`/`newfetch` usan uname+sysinfo+fbinfo+`/bin`
  (todo datos vivos, nada canned excepto el logo ASCII).
- `chipset_probe` lee COMMAND/STATUS reales por función PCI.

## Alcance honesto (no prometer)

- Sin ffmpeg ni stb_image (requieren threads/FPU userspace; libc ya
  aporta heap+stdio para ese camino).
- Sin USB/AHCI/NVMe/HDA (se enumeran por PCI pero sin driver).
- IRQ PS/2 y serie funcionan; MSI/APIC/ACPI pendientes.
- `vid` es animación procedural (player real de codecs = roadmap).
- Red: solo loopback PCnet probado; sin stack IP.

## Flujo de trabajo esperado

1. Auditar antes de tocar (leer código, no asumir APIs).
2. Cambios pequeños, compilar (`-Werror`), `lint-layers` en verde.
3. `make qemu-test` 14/14 + boot ISO BIOS y UEFI + screenshot si hay
   salida visual.
4. No commits salvo pedido explícito. No `TODO`s como implementación.
