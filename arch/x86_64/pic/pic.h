#ifndef ARCH_X86_64_PIC_H
#define ARCH_X86_64_PIC_H

void x64_pic_init(void);
void x64_pic_send_eoi(int irq);
void x64_pic_set_mask(int irq, int masked);

#endif