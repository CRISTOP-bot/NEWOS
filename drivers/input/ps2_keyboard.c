#include <drivers/ps2.h>
#include <core/core_console.h>
#include <core/core_printk.h>
#include <x86_io.h>
#include <x86_irq.h>
#include <x86_pic.h>

/* PS/2 keyboard: scancode set 1 (the controller translates, as the BIOS
 * leaves it), Spanish layout, IRQ1. Decoded bytes feed the console input
 * queue, so anything typed in the graphical window reaches the shell.
 * Special keys emit the same ANSI sequences a serial terminal would send,
 * so the shell's line editor handles both sources with one code path. */

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

#define PS2_ST_OUT_FULL 0x01
#define PS2_ST_IN_FULL  0x02

#define PS2_CMD_READ_CFG  0x20
#define PS2_CMD_WRITE_CFG 0x60
#define PS2_CMD_ENABLE_KBD 0xAE

#define KBD_CMD_SET_LEDS 0xED

static int ps2_wait_write(void)
{
    for (int i = 0; i < 100000; i++) {
        if (!(inb(PS2_STATUS) & PS2_ST_IN_FULL))
            return 0;
    }
    return -1;
}

static int ps2_wait_read(void)
{
    for (int i = 0; i < 100000; i++) {
        if (inb(PS2_STATUS) & PS2_ST_OUT_FULL)
            return 0;
    }
    return -1;
}

/* Send a byte to the keyboard itself (port 1), waiting for ACK (0xFA). */
static int kbd_cmd(u8 cmd)
{
    if (ps2_wait_write() != 0)
        return -1;
    outb(PS2_DATA, cmd);
    if (ps2_wait_read() != 0)
        return -1;
    return inb(PS2_DATA) == 0xFA ? 0 : -1;
}

static void kbd_set_leds(u8 caps, u8 num)
{
    u8 leds = (u8)((caps ? 0x04 : 0) | (num ? 0x02 : 0));
    if (kbd_cmd(KBD_CMD_SET_LEDS) == 0) {
        if (ps2_wait_write() == 0) {
            outb(PS2_DATA, leds);
            if (ps2_wait_read() == 0)
                (void)inb(PS2_DATA);   /* ACK */
        }
    }
}

/* --- Spanish layout, scancode set 1 --------------------------------------
 * Each entry: normal byte, shifted byte. 0x00 = no output. Letters are
 * stored lowercase; caps/shift logic is applied by the decoder. Accented
 * output is UTF-8 (2 bytes, pushed as a pair). */
struct keymap_entry {
    u8 normal;
    u8 shift;
    const char *utf8_normal;   /* non-NULL: push this pair instead */
    const char *utf8_shift;
};

#define K(k, n, s) [k] = { (n), (s), 0, 0 }
#define KU(k, n, s) [k] = { 0, 0, (n), (s) }

static const struct keymap_entry es_map[128] = {
    K(0x02, '1', '!'),  K(0x03, '2', '"'),
    K(0x05, '4', '$'),  K(0x06, '5', '%'),  K(0x07, '6', '&'),
    K(0x08, '7', '/'),  K(0x09, '8', '('),  K(0x0A, '9', ')'),
    K(0x0B, '0', '='),  K(0x0C, '\'', '?'),
    K(0x0F, '\t', '\t'), K(0x1C, '\n', '\n'), K(0x39, ' ', ' '),
    K(0x10, 'q', 'Q'), K(0x11, 'w', 'W'), K(0x12, 'e', 'E'),
    K(0x13, 'r', 'R'), K(0x14, 't', 'T'), K(0x15, 'y', 'Y'),
    K(0x16, 'u', 'U'), K(0x17, 'i', 'I'), K(0x18, 'o', 'O'),
    K(0x19, 'p', 'P'),
    K(0x1A, '^', '['), K(0x1B, '*', ']'),
    K(0x1E, 'a', 'A'), K(0x1F, 's', 'S'), K(0x20, 'd', 'D'),
    K(0x21, 'f', 'F'), K(0x22, 'g', 'G'), K(0x23, 'h', 'H'),
    K(0x24, 'j', 'J'), K(0x25, 'k', 'K'), K(0x26, 'l', 'L'),
    K(0x2C, 'z', 'Z'), K(0x2D, 'x', 'X'), K(0x2E, 'c', 'C'),
    K(0x2F, 'v', 'V'), K(0x30, 'b', 'B'), K(0x31, 'n', 'N'),
    K(0x32, 'm', 'M'), K(0x33, ',', ';'), K(0x34, '.', ':'),
    K(0x35, '-', '_'),
    KU(0x04, "\xC2\xB7", "\xC2\xB7"),        /* middle dot (3) */
    KU(0x0D, "\xC2\xA1", "\xC2\xBF"),        /* inverted !/? */
    KU(0x27, "\xC3\xB1", "\xC3\x91"),        /* n-tilde */
    KU(0x2B, "\xC3\xA7", "\xC3\x87"),        /* c-cedilla */
    KU(0x29, "\xC2\xBA", "\xC2\xAA"),        /* masculine/feminine ordinal */
};

/* Dead keys: acute (0x28), diaeresis (shift+0x28), circumflex (0x1A '^').
 * pending_dead: 0 = none, 1 = acute, 2 = diaeresis, 3 = circumflex. */
static int pending_dead;

static const char *acute_mark = "\xC2\xB4";
static const char *diaeresis_mark = "\xC2\xA8";

static void push_str(const char *s)
{
    while (*s)
        core_console_kbd_push((u8)*s++);
}

/* Compose dead+vowel into precomposed UTF-8. Returns NULL when the pair
 * does not compose (caller emits the mark, then the base char). */
static const char *compose(int dead, u8 base, int upper)
{
    static const char *acute_lo[5] = {
        "\xC3\xA1", "\xC3\xA9", "\xC3\xAD", "\xC3\xB3", "\xC3\xBA" };
    static const char *acute_hi[5] = {
        "\xC3\x81", "\xC3\x89", "\xC3\x8D", "\xC3\x93", "\xC3\x9A" };
    static const char *circ_lo[5] = {
        "\xC3\xA2", "\xC3\xAA", "\xC3\xAE", "\xC3\xB4", "\xC3\xBB" };
    static const char *circ_hi[5] = {
        "\xC3\x82", "\xC3\x8A", "\xC3\x8E", "\xC3\x94", "\xC3\xBB" };
    int idx = -1;

    switch (base | 0x20) {
    case 'a': idx = 0; break;
    case 'e': idx = 1; break;
    case 'i': idx = 2; break;
    case 'o': idx = 3; break;
    case 'u': idx = 4; break;
    }
    if (idx < 0)
        return NULL;
    if (dead == 1)
        return upper ? acute_hi[idx] : acute_lo[idx];
    if (dead == 3)
        return upper ? circ_hi[idx] : circ_lo[idx];
    if (dead == 2 && idx == 4)
        return upper ? "\xC3\x9C" : "\xC3\xBC";
    return NULL;
}

static int g_shift;
static int g_ctrl;
static int g_alt;
static int g_caps;
static int g_num = 1;
static int g_extended;

static void kbd_emit_make(u8 code, int ext)
{
    /* Modifier make codes. */
    if (!ext) {
        switch (code) {
        case 0x2A: case 0x36: g_shift = 1; return;
        case 0x1D: g_ctrl = 1; return;
        case 0x38: g_alt = 1; return;
        case 0x3A:
            g_caps = !g_caps;
            kbd_set_leds((u8)g_caps, (u8)g_num);
            return;
        case 0x45:
            g_num = !g_num;
            kbd_set_leds((u8)g_caps, (u8)g_num);
            return;
        case 0x0E:
            core_console_kbd_push(0x7F);   /* DEL: shell deletes back */
            return;
        }
    } else {
        switch (code) {
        case 0x1D: g_ctrl = 1; return;     /* right ctrl */
        case 0x38: g_alt = 1; return;      /* alt-gr */
        case 0x48: push_str("\x1B[A"); return;
        case 0x50: push_str("\x1B[B"); return;
        case 0x4B: push_str("\x1B[D"); return;
        case 0x4D: push_str("\x1B[C"); return;
        case 0x47: push_str("\x1B[H"); return;
        case 0x4F: push_str("\x1B[F"); return;
        case 0x52: push_str("\x1B[2~"); return;
        case 0x53: push_str("\x1B[3~"); return;
        case 0x49: push_str("\x1B[5~"); return;
        case 0x51: push_str("\x1B[6~"); return;
        }
    }

    if (code >= 128)
        return;
    const struct keymap_entry *e = &es_map[code];

    /* Dead-key make codes (never shifted output on their own). */
    if (!ext && (code == 0x28 || code == 0x1A)) {
        if (code == 0x1A && g_shift)
            ;   /* shifted ^[ is '[', handled by the map below */
        else {
            pending_dead = (code == 0x28) ? (g_shift ? 2 : 1) : 3;
            return;
        }
    }

    int upper = (g_shift != 0) ^ (g_caps != 0);
    u8 out = 0;
    const char *utf8 = NULL;

    if (e->utf8_normal) {
        utf8 = (g_shift && e->utf8_shift) ? e->utf8_shift : e->utf8_normal;
    } else {
        out = g_shift ? e->shift : e->normal;
        if (out >= 'a' && out <= 'z') {
            if (upper)
                out = (u8)(out - ('a' - 'A'));
        } else if (out >= 'A' && out <= 'Z') {
            if (!upper)
                out = (u8)(out + ('a' - 'A'));
        }
    }

    /* Dead-key composition: dead+vowel merges, anything else emits the
     * mark followed by the base character (real terminal behavior). */
    if (pending_dead) {
        int dead = pending_dead;
        pending_dead = 0;
        if (!utf8 && out &&
            (((out | 0x20) >= 'a' && (out | 0x20) <= 'z'))) {
            int upper = !(out >= 'a' && out <= 'z');
            const char *c = compose(dead, (u8)(out | 0x20), upper);
            if (c) {
                push_str(c);
                return;
            }
        }
        push_str(dead == 2 ? diaeresis_mark : acute_mark);
        if (!utf8 && !out)
            return;
    }

    if (utf8) {
        push_str(utf8);
    } else if (out) {
        if (g_ctrl && ((out | 0x20) >= 'a' && (out | 0x20) <= 'z'))
            core_console_kbd_push((u8)((out | 0x20) - 'a' + 1));
        else
            core_console_kbd_push(out);
    }
}

static void kbd_emit_break(u8 code, int ext)
{
    if (!ext) {
        switch (code) {
        case 0x2A: case 0x36: g_shift = 0; break;
        case 0x1D: g_ctrl = 0; break;
        case 0x38: g_alt = 0; break;
        }
    } else {
        switch (code) {
        case 0x1D: g_ctrl = 0; break;
        case 0x38: g_alt = 0; break;
        }
    }
}

static void ps2_kbd_irq(void *arg)
{
    (void)arg;

    for (;;) {
        u8 st = inb(PS2_STATUS);
        if (!(st & PS2_ST_OUT_FULL))
            break;
        /* Aux (mouse) bytes surface here on some controllers when both
         * ports share the output buffer: the mouse IRQ owns them, so
         * leave them for it instead of decoding clicks as keys. */
        if (st & 0x20)
            break;
        u8 data = inb(PS2_DATA);

        if (data == 0xE0) {
            g_extended = 1;
            continue;
        }
        if (data == 0xE1) {
            /* Pause/Break 6-byte sequence: swallow it whole. */
            for (int i = 0; i < 5; i++) {
                if (ps2_wait_read() != 0)
                    break;
                (void)inb(PS2_DATA);
            }
            continue;
        }

        int ext = g_extended;
        g_extended = 0;

        if (data & 0x80)
            kbd_emit_break((u8)(data & 0x7F), ext);
        else
            kbd_emit_make(data, ext);
    }
}

void ps2_keyboard_init(void)
{
    /* Flush any stale output, then enable IRQ1 in the controller config
     * (keep the BIOS translation to set 1 and the aux port untouched). */
    while (inb(PS2_STATUS) & PS2_ST_OUT_FULL)
        (void)inb(PS2_DATA);

    if (ps2_wait_write() == 0) {
        outb(PS2_CMD, PS2_CMD_READ_CFG);
        if (ps2_wait_read() == 0) {
            u8 cfg = inb(PS2_DATA);
            cfg |= 0x01;   /* keyboard interrupt */
            cfg |= 0x40;   /* keep set-1 translation */
            if (ps2_wait_write() == 0) {
                outb(PS2_CMD, PS2_CMD_WRITE_CFG);
                if (ps2_wait_write() == 0)
                    outb(PS2_DATA, cfg);
            }
        }
    }

    if (ps2_wait_write() == 0)
        outb(PS2_CMD, PS2_CMD_ENABLE_KBD);

    x86_irq_register(X86_IRQ_KEYBOARD, ps2_kbd_irq, NULL);
    x64_pic_set_mask(X86_IRQ_KEYBOARD, 0);
    printk("ps2: keyboard ready (ES layout, IRQ1)\n");
}
