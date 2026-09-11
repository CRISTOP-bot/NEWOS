#ifndef LIBK_FORMAT_H
#define LIBK_FORMAT_H

#include <core/core_types.h>

/* Internal single-character output callback used by vsnformat. */
typedef void (*format_emit_fn)(char c, void *opaque);

/* Formats a printf-style string, emitting to `emit` with context `opaque`.
 * Supports: %c %s %d %i %u %x %o %p %lu %llu %lx %zu and width/precision
 * and the 0 / - / space / + flags. Returns the number of characters
 * emitted. */
size_t format_fmt(format_emit_fn emit, void *opaque,
                  const char *fmt, __builtin_va_list args);

/* Writes formatted output into a fixed buffer, NUL-terminated. Returns the
 * number of characters that would have been written (excluding NUL). */
size_t vsnformat(char *buf, size_t size, const char *fmt, __builtin_va_list args);

#endif