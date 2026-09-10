cmake_minimum_required(VERSION 3.16)
project(NEWOS LANGUAGES C ASM)

# NEWOS alternative build system (CMake).
# Primary build system is the Makefile/Kbuild pair.

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(BUILD_DIR ${CMAKE_BINARY_DIR})

add_compile_options(
  -ffreestanding -nostdlib -fno-pic -mno-red-zone -mno-sse -mno-mmx
  -fno-builtin -fno-exceptions -mno-80387 -mgeneral-regs-only -O2 -g -Wall -Wextra
)

include_directories(
  include include/kernel lib/libk arch/x86_64/include ${CMAKE_SOURCE_DIR}
)

file(GLOB_RECURSE KERNEL_SOURCES
  kernel/*.c arch/x86_64/*.c arch/x86_64/*.S mm/*.c drivers/serial/*.c
  lib/libk/*.c lib/*.c fs/*.c fs/vfs/*.c fs/tmpfs/*.c fs/devfs/*.c
)

add_executable(newos.elf ${KERNEL_SOURCES})
target_link_options(newos.elf PRIVATE
  -n --gc-sections -T ${CMAKE_SOURCE_DIR}/arch/x86_64/linker.ld
)

add_custom_target(qemu
  COMMAND qemu-system-x86_64 -kernel ${BUILD_DIR}/newos.elf -serial stdio -no-reboot -m 128M
  DEPENDS newos.elf
)