#!/usr/bin/env python3
"""Generate NEWOS test images with the standard library only.

- test.ppm: 160x100 binary P6 PPM (color bars + checker).

The boot splash is no longer generated: brand/newos-splash.bmp is the
source of truth (see the $(SPLASH_BMP) rule in the Makefile).

Usage: gen-images.py <test.ppm>
"""
import sys


def write_ppm(path, w, h):
    bars = [(255, 0, 0), (0, 255, 0), (0, 0, 255),
            (255, 255, 0), (0, 255, 255), (255, 0, 255)]
    px = bytearray()
    for y in range(h):
        for x in range(w):
            if (x // 10 + y // 10) % 2 == 0:
                px += bytes((x * 255 // (w - 1), y * 255 // (h - 1),
                             (x + y) * 255 // (w + h - 2)))
            else:
                r, g, b = bars[(x * len(bars)) // w]
                px += bytes((r, g, b))
    with open(path, 'wb') as f:
        f.write(('P6\n%d %d\n255\n' % (w, h)).encode('ascii'))
        f.write(px)


write_ppm(sys.argv[1], 160, 100)
print('image: %s' % sys.argv[1])
