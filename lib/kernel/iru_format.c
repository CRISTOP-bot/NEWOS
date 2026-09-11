#include <iru_format.h>

struct buf_sink {
    char *buf;
    size_t size;
    size_t len;
};

static void buf_write(char c, void *opaque)
{
    struct buf_sink *sink = (struct buf_sink *)opaque;
    if (sink->len + 1 < sink->size)
        sink->buf[sink->len] = c;
    sink->len++;
}

static void emit_repeat(format_emit_fn emit, void *opaque, char c, size_t n)
{
    for (size_t i = 0; i < n; i++)
        emit(c, opaque);
}

static void emit_str(format_emit_fn emit, void *opaque, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        emit(s[i], opaque);
}

/* Emits an unsigned magnitude with sign/padding rules.
 * flags: bit0 = left-align, bit1 = zero-pad, bit2 = '+',
 *        bit3 = space, bit4 = 0x prefix, bit5 = uppercase */
static void emit_uint(format_emit_fn emit, void *opaque, u64 value,
                      int base, int width, int prec, int flags,
                      char forced_sign)
{
    static const char lower[] = "0123456789abcdef";
    static const char upper[] = "0123456789ABCDEF";
    const char *table = (flags & (1 << 5)) ? upper : lower;

    char digits[66];
    int i = 0;
    if (value == 0) {
        digits[i++] = '0';
    } else {
        while (value && i < 64) {
            digits[i++] = table[value % base];
            value /= base;
        }
    }

    int ndigits = i;
    char sign = 0;
    if (forced_sign)
        sign = (char)forced_sign;
    else if (flags & (1 << 2))
        sign = '+';
    else if (flags & (1 << 3))
        sign = ' ';

    int prefix_len = 0;
    if (flags & (1 << 4))
        prefix_len = (base == 16) ? 2 : 1;

    int total_digits = (ndigits > prec) ? ndigits : prec;
    int total = total_digits + prefix_len + (sign ? 1 : 0);
    int pad = (width > total) ? width - total : 0;

    int left = flags & (1 << 0);
    int zero_pad = (flags & (1 << 1)) && !left && (prec < 0);

    if (!left && !zero_pad)
        emit_repeat(emit, opaque, ' ', pad);

    if (sign)
        emit(sign, opaque);

    if (flags & (1 << 4)) {
        if (base == 16) {
            emit('0', opaque);
            emit((flags & (1 << 5)) ? 'X' : 'x', opaque);
        } else if (base == 8) {
            emit('0', opaque);
        }
    }

    if (zero_pad)
        emit_repeat(emit, opaque, '0', pad);

    for (int p = 0; p < prec - ndigits; p++)
        emit('0', opaque);

    for (int j = ndigits - 1; j >= 0; j--)
        emit(digits[j], opaque);

    if (left)
        emit_repeat(emit, opaque, ' ', pad);
}

size_t format_fmt(format_emit_fn emit, void *opaque,
                  const char *fmt, __builtin_va_list args)
{
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            emit(*p, opaque);
            continue;
        }

        int flags = 0;
        const char *q = p + 1;
        for (;; q++) {
            if (*q == '-')       flags |= (1 << 0);
            else if (*q == '0')  flags |= (1 << 1);
            else if (*q == '+')  flags |= (1 << 2);
            else if (*q == ' ')  flags |= (1 << 3);
            else if (*q == '#')  flags |= (1 << 4);
            else break;
        }

        int width = -1;
        if (*q == '*') {
            width = __builtin_va_arg(args, int);
            if (width < 0) {
                flags |= (1 << 0);
                width = -width;
            }
            q++;
        } else if (*q >= '0' && *q <= '9') {
            width = 0;
            while (*q >= '0' && *q <= '9') {
                width = width * 10 + (*q - '0');
                q++;
            }
        }

        int prec = -1;
        if (*q == '.') {
            q++;
            if (*q == '*') {
                prec = __builtin_va_arg(args, int);
                q++;
            } else {
                prec = 0;
                while (*q >= '0' && *q <= '9') {
                    prec = prec * 10 + (*q - '0');
                    q++;
                }
            }
        }

        while (*q == 'l' || *q == 'z' || *q == 'h')
            q++;

        const char *conv = q;

        switch (*conv) {
        case '%':
            emit('%', opaque);
            break;
        case 'c': {
            int c = __builtin_va_arg(args, int);
            emit((char)c, opaque);
            break;
        }
        case 's': {
            const char *s = __builtin_va_arg(args, const char *);
            if (!s)
                s = "(null)";
            size_t len = 0;
            while (s[len])
                len++;
            if (prec >= 0 && (size_t)prec < len)
                len = (size_t)prec;
            if (width > (int)len && !(flags & (1 << 0)))
                emit_repeat(emit, opaque, ' ', width - (int)len);
            emit_str(emit, opaque, s, len);
            if (width > (int)len && (flags & (1 << 0)))
                emit_repeat(emit, opaque, ' ', width - (int)len);
            break;
        }
        case 'd':
        case 'i': {
            s64 v = __builtin_va_arg(args, s64);
            if (v < 0)
                emit_uint(emit, opaque, (u64)(-(v + 1)) + 1, 10,
                          width, prec, flags, '-');
            else
                emit_uint(emit, opaque, (u64)v, 10, width, prec, flags, 0);
            break;
        }
        case 'u':
            emit_uint(emit, opaque, __builtin_va_arg(args, u64), 10,
                      width, prec, flags, 0);
            break;
        case 'o':
            emit_uint(emit, opaque, __builtin_va_arg(args, u64), 8,
                      width, prec, flags, 0);
            break;
        case 'x':
        case 'X':
            emit_uint(emit, opaque, __builtin_va_arg(args, u64), 16,
                      width, prec, (conv[0] == 'X') ? (flags | (1 << 5))
                                                   : flags,
                      0);
            break;
        case 'p':
            emit_uint(emit, opaque, __builtin_va_arg(args, u64),
                      (sizeof(void *) == 8) ? 16 : 8,
                      (width < 0) ? (int)(2 * sizeof(void *)) : width, prec,
                      flags | (1 << 4), 0);
            break;
        default:
            emit('%', opaque);
            emit(*conv, opaque);
            break;
        }

        p = conv;
    }

    return 0; /* explicit length accounting is done by the sink */
}

size_t vsnformat(char *buf, size_t size, const char *fmt, __builtin_va_list args)
{
    struct buf_sink sink = { buf, size, 0 };
    format_fmt(buf_write, &sink, fmt, args);
    if (sink.len < size)
        buf[sink.len] = '\0';
    else if (size > 0)
        buf[size - 1] = '\0';
    return sink.len;
}