#include <drivers/ps2.h>
#include <core/core_printk.h>
#include <drivers/fb.h>
#include <drivers/vga_console.h>
#include <x86_io.h>
#include <x86_irq.h>
#include <x86_pic.h>

/* PS/2 auxiliary mouse (port 2, IRQ12). Standard 3/4-byte packets update a
 * pixel position plus a text-cell cursor (VGA overlay), and publish one
 * event per packet on an SPSC ring consumed by /dev/mouse and
 * SYS_MOUSE_GET. */

#define PS2_DATA   0x60
#define PS2_STATUS 0x64
#define PS2_CMD    0x64

#define PS2_ST_OUT_FULL 0x01
#define PS2_ST_IN_FULL  0x02

#define PS2_CMD_WRITE_CFG 0x60
#define PS2_CMD_ENABLE_AUX 0xA8
#define PS2_CMD_WRITE_AUX 0xD4

#define MOUSE_RESET   0xFF
#define MOUSE_DEFAULT 0xF6
#define MOUSE_ENABLE  0xF4
#define MOUSE_DISABLE 0xF5
#define MOUSE_SET_RATE 0xF3
#define MOUSE_GET_ID  0xF2
#define MOUSE_SET_RES 0xE8

static int g_mouse_x = 40;     /* 80x25 text cell (VGA overlay) */
static int g_mouse_y = 12;
static int g_px = 360;         /* pixel position (device space) */
static int g_py = 200;
static int g_mouse_buttons;
static int g_wheel_last;       /* signed wheel delta of the latest packet */
static u64 g_mouse_seq;        /* one increment per packet */
static int g_mouse_dx_acc;
static int g_mouse_dy_acc;
static u8 g_packet[4];
static int g_packet_idx;
static int g_packet_size = 3;  /* 3, or 4 with a wheel mouse */

/* SPSC event ring: IRQ is the only producer, syscall/FS the only consumer
 * (same pattern as the console key ring). One event per decoded packet. */
#define MOUSE_EV_RING 64
struct mouse_ev {
    s16 x, y;
    u8 buttons;
    s8 wheel;
};
static volatile struct mouse_ev g_ev_ring[MOUSE_EV_RING];
static volatile u32 g_ev_head;
static volatile u32 g_ev_tail;

static void ev_push(s16 x, s16 y, u8 buttons, s8 wheel)
{
    u32 next = (g_ev_head + 1) & (MOUSE_EV_RING - 1);
    if (next == g_ev_tail)
        return;                    /* full: drop the oldest event window */
    g_ev_ring[g_ev_head].x = x;
    g_ev_ring[g_ev_head].y = y;
    g_ev_ring[g_ev_head].buttons = buttons;
    g_ev_ring[g_ev_head].wheel = wheel;
    g_ev_head = next;
}

int ps2_mouse_pop_event(int *x, int *y, int *buttons, int *wheel)
{
    if (g_ev_tail == g_ev_head)
        return 0;
    *x = (int)g_ev_ring[g_ev_tail].x;
    *y = (int)g_ev_ring[g_ev_tail].y;
    *buttons = (int)g_ev_ring[g_ev_tail].buttons;
    *wheel = (int)g_ev_ring[g_ev_tail].wheel;
    g_ev_tail = (g_ev_tail + 1) & (MOUSE_EV_RING - 1);
    return 1;
}

void ps2_mouse_get(u64 *seq, int *x, int *y, int *buttons, int *wheel)
{
    if (seq)
        *seq = g_mouse_seq;
    if (x)
        *x = g_px;
    if (y)
        *y = g_py;
    if (buttons)
        *buttons = g_mouse_buttons;
    if (wheel)
        *wheel = g_wheel_last;
}

/* Pixel bounds: the active panel, or the VGA text area (720x400) when
 * no framebuffer exists. */
static void mouse_bounds(int *w, int *h)
{
    u64 fw, fh, pitch;
    u32 bpp;

    if (fb_present()) {
        fb_geometry(&fw, &fh, &pitch, &bpp);
        *w = (int)fw;
        *h = (int)fh;
    } else {
        *w = 720;
        *h = 400;
    }
}

/* Drain any stale output bytes (old replies, keystrokes) so the next
 * command's ACK cannot be confused with leftover data. */
static void aux_drain(void)
{
    for (int i = 0; i < 256; i++) {
        if (!(inb(PS2_STATUS) & PS2_ST_OUT_FULL))
            break;
        (void)inb(PS2_DATA);
    }
}

static int aux_wait_write(void)
{
    for (int i = 0; i < 100000; i++) {
        if (!(inb(PS2_STATUS) & PS2_ST_IN_FULL))
            return 0;
    }
    return -1;
}

static int aux_wait_read(void)
{
    for (int i = 0; i < 100000; i++) {
        if (inb(PS2_STATUS) & PS2_ST_OUT_FULL)
            return 0;
    }
    return -1;
}

static int aux_read_byte(u8 *out)
{
    if (aux_wait_read() != 0)
        return -1;
    *out = inb(PS2_DATA);
    return 0;
}

/* Send a command byte to the aux port, expecting ACK (0xFA). */
static int mouse_cmd(u8 cmd)
{
    u8 ack;

    if (aux_wait_write() != 0)
        return -1;
    outb(PS2_CMD, PS2_CMD_WRITE_AUX);
    if (aux_wait_write() != 0)
        return -1;
    outb(PS2_DATA, cmd);
    if (aux_read_byte(&ack) != 0 || ack != 0xFA)
        return -1;
    return 0;
}

/* Send a command byte followed by one parameter byte (e.g. F3 rate,
 * E8 resolution), expecting ACK after each. */
static int mouse_cmd_param(u8 cmd, u8 param)
{
    u8 ack;

    if (mouse_cmd(cmd) != 0)
        return -1;
    if (aux_wait_write() != 0)
        return -1;
    outb(PS2_CMD, PS2_CMD_WRITE_AUX);
    if (aux_wait_write() != 0)
        return -1;
    outb(PS2_DATA, param);
    if (aux_read_byte(&ack) != 0 || ack != 0xFA)
        return -1;
    return 0;
}

/* Text cells are coarse: accumulate sub-cell motion so the pointer glides
 * instead of jumping (4 counts per cell at the default resolution). */
#define MOUSE_COUNTS_PER_CELL 4

static void mouse_packet(void)
{
    u8 b0 = g_packet[0];
    int dx = (int)(s8)g_packet[1];
    /* PS/2 reports +Y as motion UP the screen (toward smaller text rows),
     * so the row accumulator runs opposite to the raw delta. */
    int dy = -(int)(s8)g_packet[2];
    int wheel = 0;

    if (b0 & 0x40)
        dx = (dx < 0) ? -255 : 255;    /* X overflow: clamp the step */
    if (b0 & 0x80)
        dy = (dy < 0) ? -255 : 255;    /* Y overflow */

    /* Fourth byte (wheel mice): signed Z steps, +ack = wheel up. On
     * 5-button mice bits 4/5 carry buttons 4/5. */
    int extra_buttons = 0;
    if (g_packet_size == 4) {
        wheel = (int)(s8)g_packet[3];
        extra_buttons = (g_packet[3] & 0x30) >> 1;  /* b4/b5 -> bits 3/4 */
    }
    g_wheel_last = wheel;

    /* Pixel position: counts map 1:1 to device pixels. */
    {
        int bw, bh;
        mouse_bounds(&bw, &bh);
        g_px += dx;
        g_py += dy;
        if (g_px < 0)
            g_px = 0;
        if (g_py < 0)
            g_py = 0;
        if (g_px > bw - 1)
            g_px = bw - 1;
        if (g_py > bh - 1)
            g_py = bh - 1;
    }

    g_mouse_dx_acc += dx;
    g_mouse_dy_acc += dy;

    while (g_mouse_dx_acc >= MOUSE_COUNTS_PER_CELL) {
        if (g_mouse_x < 79)
            g_mouse_x++;
        g_mouse_dx_acc -= MOUSE_COUNTS_PER_CELL;
    }
    while (g_mouse_dx_acc <= -MOUSE_COUNTS_PER_CELL) {
        if (g_mouse_x > 0)
            g_mouse_x--;
        g_mouse_dx_acc += MOUSE_COUNTS_PER_CELL;
    }
    while (g_mouse_dy_acc >= MOUSE_COUNTS_PER_CELL) {
        if (g_mouse_y < 24)
            g_mouse_y++;
        g_mouse_dy_acc -= MOUSE_COUNTS_PER_CELL;
    }
    while (g_mouse_dy_acc <= -MOUSE_COUNTS_PER_CELL) {
        if (g_mouse_y > 0)
            g_mouse_y--;
        g_mouse_dy_acc += MOUSE_COUNTS_PER_CELL;
    }

    g_mouse_buttons = (b0 & 0x07) | extra_buttons;
    g_mouse_seq++;
    ev_push((s16)g_px, (s16)g_py, (u8)g_mouse_buttons, (s8)wheel);
    if (fb_present())
        fb_mouse_place(g_px, g_py, g_mouse_buttons);
    else
        vga_mouse_place(g_mouse_x, g_mouse_y, g_mouse_buttons);
}

static void ps2_mouse_irq(void *arg)
{
    (void)arg;

    for (;;) {
        u8 st = inb(PS2_STATUS);
        if (!(st & PS2_ST_OUT_FULL))
            break;
        /* Aux data arrives with the AUX bit set. A keyboard byte here
         * (spurious IRQ12) must be left in the output buffer: reading it
         * would swallow a scancode. */
        if (!(st & 0x20))
            break;
        u8 data = inb(PS2_DATA);

        if (g_packet_idx == 0 && !(data & 0x08))
            continue;                  /* resync: first byte has bit3 set */
        if (g_packet_idx < (int)sizeof(g_packet))
            g_packet[g_packet_idx++] = data;
        else
            g_packet_idx = 0;          /* overrun: restart the packet */
        if (g_packet_idx == g_packet_size) {
            g_packet_idx = 0;
            mouse_packet();
        }
    }
}

void ps2_mouse_init(void)
{
    u8 reply;
    int step = 0;

    /* Controller replies (e.g. the 0x20 config byte) travel on the
     * keyboard queue and raise IRQ1, where the keyboard handler would
     * eat them as scancodes. Mask IRQ1 and drain stale bytes so every
     * ACK below is really ours (IRQ12 stays masked until the end, so
     * aux bytes cannot be stolen either). */
    x64_pic_set_mask(X86_IRQ_KEYBOARD, 1);
    aux_drain();

    step = 1;
    if (aux_wait_write() != 0)
        goto fail;
    outb(PS2_CMD, PS2_CMD_ENABLE_AUX);

    /* Enable the aux interrupt in the controller config. */
    step = 2;
    if (aux_wait_write() != 0)
        goto fail;
    outb(PS2_CMD, 0x20);
    if (aux_read_byte(&reply) != 0)
        goto fail;
    reply |= 0x02;
    if (aux_wait_write() != 0)
        goto fail;
    outb(PS2_CMD, PS2_CMD_WRITE_CFG);
    if (aux_wait_write() != 0)
        goto fail;
    outb(PS2_DATA, reply);

    /* Reset, defaults. */
    step = 3;
    if (mouse_cmd(MOUSE_RESET) != 0)
        goto fail;
    if (aux_read_byte(&reply) != 0 || reply != 0xAA)
        goto fail;
    if (aux_read_byte(&reply) != 0 || reply != 0x00)
        goto fail;
    step = 4;
    if (mouse_cmd(MOUSE_DEFAULT) != 0)
        goto fail;

    /* IntelliMouse magic: sample rates 200/100/80, then Get ID. An ID
     * of 0x03 (wheel) or 0x04 (wheel + buttons 4/5) switches the
     * packet assembler to 4 bytes; anything else keeps classic 3-byte
     * packets. A failed magic is harmless: the mouse simply stays
     * 3-byte (QEMU's PS/2 mouse answers 0x00). */
    g_packet_size = 3;
    step = 5;
    if (mouse_cmd_param(MOUSE_SET_RATE, 200) == 0 &&
        mouse_cmd_param(MOUSE_SET_RATE, 100) == 0 &&
        mouse_cmd_param(MOUSE_SET_RATE, 80) == 0 &&
        mouse_cmd(MOUSE_GET_ID) == 0 &&
        aux_read_byte(&reply) == 0 && (reply == 0x03 || reply == 0x04)) {
        g_packet_size = 4;
    } else {
        /* Not a wheel mouse (or no answer): restore the default rate.
         * Result ignored, reporting stays off until ENABLE below. */
        mouse_cmd_param(MOUSE_SET_RATE, 100);
    }
    aux_drain();

    /* Enable reporting last, after all negotiation. */
    step = 6;
    if (mouse_cmd(MOUSE_ENABLE) != 0)
        goto fail;

    g_packet_idx = 0;
    x86_irq_register(X86_IRQ_MOUSE, ps2_mouse_irq, NULL);
    x64_pic_set_mask(X86_IRQ_MOUSE, 0);
    x64_pic_set_mask(X86_IRQ_KEYBOARD, 0);
    if (fb_present())
        fb_mouse_place(g_px, g_py, 0);
    else
        vga_mouse_place(g_mouse_x, g_mouse_y, 0);
    printk("ps2: mouse ready (IRQ12, %d-byte packets, cursor @ %d,%d)\n",
           g_packet_size, g_mouse_x, g_mouse_y);
    return;

fail:
    x64_pic_set_mask(X86_IRQ_KEYBOARD, 0);
    printk("ps2: no mouse detected (step %d), cursor disabled\n", step);
}
