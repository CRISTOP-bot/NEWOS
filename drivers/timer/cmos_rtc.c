#include <drivers/cmos_rtc.h>
#include <core/core_printk.h>
#include <x86_io.h>

/* PC CMOS RTC. The update-in-progress flag (register 0x0A, bit 7) gates
 * every read so seconds/minutes never tear mid-tick. Handles both BCD and
 * binary mode plus 12/24h (status register B) like real hardware. */

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

#define CMOS_REG_SEC    0x00
#define CMOS_REG_MIN    0x02
#define CMOS_REG_HOUR   0x04
#define CMOS_REG_DAY    0x07
#define CMOS_REG_MON    0x08
#define CMOS_REG_YEAR   0x09
#define CMOS_REG_STATUS_A 0x0A
#define CMOS_REG_STATUS_B 0x0B

static u8 cmos_read(u8 reg)
{
    outb(CMOS_ADDR, (u8)(reg | 0x80));   /* bit7: keep NMI disabled */
    io_wait();
    return inb(CMOS_DATA);
}

static u8 bcd_to_bin(u8 v)
{
    return (u8)(((v >> 4) * 10) + (v & 0x0F));
}

int cmos_rtc_get(u16 *year, u8 *mon, u8 *day,
                 u8 *hour, u8 *min, u8 *sec)
{
    u8 s, mi, h, d, mo, y, reg_b;

    if (!year || !mon || !day || !hour || !min || !sec)
        return -1;

    /* Spin while an update is in progress (at most ~2ms on real time). */
    for (int i = 0; i < 1000000; i++) {
        if (!(cmos_read(CMOS_REG_STATUS_A) & 0x80))
            break;
    }

    s = cmos_read(CMOS_REG_SEC);
    mi = cmos_read(CMOS_REG_MIN);
    h = cmos_read(CMOS_REG_HOUR);
    d = cmos_read(CMOS_REG_DAY);
    mo = cmos_read(CMOS_REG_MON);
    y = cmos_read(CMOS_REG_YEAR);
    reg_b = cmos_read(CMOS_REG_STATUS_B);

    if (!(reg_b & 0x04)) {               /* BCD mode: convert */
        s = bcd_to_bin(s);
        mi = bcd_to_bin(mi);
        h = bcd_to_bin((u8)(h & 0x7F)) | (u8)(h & 0x80);
        d = bcd_to_bin(d);
        mo = bcd_to_bin(mo);
        y = bcd_to_bin(y);
    }

    if (!(reg_b & 0x02)) {               /* 12h mode: fold to 24h */
        int pm = (h & 0x80) != 0;
        h = (u8)(((u8)(h & 0x7F)) % 12);
        if (pm)
            h = (u8)(h + 12);
    }

    if (mo < 1 || mo > 12 || d < 1 || d > 31 ||
        h > 23 || mi > 59 || s > 60)
        return -1;

    *year = (u16)(2000 + y);
    *mon = mo;
    *day = d;
    *hour = h;
    *min = mi;
    *sec = s;
    return 0;
}

void cmos_rtc_init(void)
{
    u16 year;
    u8 mon, day, hour, min, sec;
    if (cmos_rtc_get(&year, &mon, &day, &hour, &min, &sec) == 0)
        printk("cmos: RTC %u-%02u-%02u %02u:%02u:%02u\n",
               year, mon, day, hour, min, sec);
    else
        printk("cmos: RTC unreadable, wall clock unavailable\n");
}
