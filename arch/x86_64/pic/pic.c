#include <arch/x86_64/io.h>

/* Legacy 8259 Programmable Interrupt Controller.
 *
 * The CPU boots with the PIC wired so that IRQ0-15 map to vectors 0x08-0x0F,
 * colliding with CPU exception vectors. This driver remaps the two cascaded
 * PICS onto vector offsets 0x20-0x2F, so hard IRQs no longer overlap with
 * exceptions. All lines are masked by default; the timer/device drivers
 * unmask what they use.
 */

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define PIC_ICW1_INIT     0x11
#define PIC_ICW4_8086     0x01
#define ICW1_ICW4         0x01
#define ICW1_SINGLE       0x02
#define ICW1_INTERVAL4    0x04
#define ICW1_LEVEL        0x08

#define PIC1_INTERRUPT_BASE 0x20
#define PIC2_INTERRUPT_BASE 0x28
#define PIC_CASCADE_IRQ     2
#define PIC1_CASCADE_PIC2   0x04
#define PIC2_CASCADE_PIC1   0x02

#define PIC_EOI 0x20

static inline void pic_wait_ready(void)
{
    outb(PIC2_CMD, 0x00);
    outb(PIC2_CMD, 0x00);
}

void x64_pic_init(void)
{
    u8 mask1 = inb(PIC1_DATA);
    u8 mask2 = inb(PIC2_DATA);

    /* Initialization command words. */
    outb(PIC1_CMD, PIC_ICW1_INIT | ICW1_ICW4);
    pic_wait_ready();
    outb(PIC2_CMD, PIC_ICW1_INIT | ICW1_ICW4);
    pic_wait_ready();

    outb(PIC1_DATA, PIC1_INTERRUPT_BASE);    /* ICW2: vector offset */
    pic_wait_ready();
    outb(PIC2_DATA, PIC2_INTERRUPT_BASE);
    pic_wait_ready();

    outb(PIC1_DATA, PIC1_CASCADE_PIC2);      /* ICW3: cascade wiring   */
    pic_wait_ready();
    outb(PIC2_DATA, PIC2_CASCADE_PIC1);
    pic_wait_ready();

    outb(PIC1_DATA, PIC_ICW4_8086);          /* ICW4: 8086 mode, no AEOI */
    pic_wait_ready();
    outb(PIC2_DATA, PIC_ICW4_8086);
    pic_wait_ready();

    /* Mask every line; drivers unmask only what they service. */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    mask1 = mask1;
    mask2 = mask2;
}

void x64_pic_send_eoi(int irq)
{
    if (irq >= 8)
        outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}

void x64_pic_set_mask(int irq, int masked)
{
    u16 port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    u8 bit = (u8)(1u << (irq & 7));
    u8 mask = inb(port);

    if (irq >= 8 && (inb(PIC1_DATA) & (1u << PIC_CASCADE_IRQ))) {
        /* Cascade line still masked - unmask it so secondary IRQs flow. */
        outb(PIC1_DATA, (u8)(inb(PIC1_DATA) & ~(u8)(1u << PIC_CASCADE_IRQ)));
    }

    if (masked)
        outb(port, (u8)(mask | bit));
    else
        outb(port, (u8)(mask & ~bit));
}