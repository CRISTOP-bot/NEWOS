#ifndef ARCH_X86_64_IO_H
#define ARCH_X86_64_IO_H

#include <kernel/types.h>

static inline u8  inb(u16 port) { u8 v; __asm__ volatile("inb %w1, %b0" : "=a"(v) : "Nd"(port)); return v; }
static inline u16 inw(u16 port){ u16 v; __asm__ volatile("inw %w1, %w0" : "=a"(v) : "Nd"(port)); return v; }
static inline u32 inl(u16 port){ u32 v; __asm__ volatile("inl %w1, %0" : "=a"(v) : "Nd"(port)); return v; }
static inline void outb(u16 port, u8 value)  { __asm__ volatile("outb %b0, %w1" : : "a"(value), "Nd"(port)); }
static inline void outw(u16 port, u16 value){ __asm__ volatile("outw %w0, %w1" : : "a"(value), "Nd"(port)); }
static inline void outl(u16 port, u32 value){ __asm__ volatile("outl %0, %w1" : : "a"(value), "Nd"(port)); }
static inline void io_wait(void) { outb(0x80, 0); }

#endif
