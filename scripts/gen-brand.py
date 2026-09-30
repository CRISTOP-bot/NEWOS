#!/usr/bin/env python3
"""Generate the NEWOS brand SVGs.

Everything in brand/ is emitted from this file so the family stays coherent.
The visual language is borrowed from the sibling BoredOS project: sharp
geometry, one 135-degree indigo gradient, white marks on the plate, a single
amber accent, and a 45-degree chamfer on the bottom-right corner.
"""

import os

ROOT = os.path.dirname(os.path.abspath(__file__))
BRAND = os.path.join(ROOT, "..", "brand")
ICONS = os.path.join(BRAND, "app-icons")
CONCEPTS = os.path.join(BRAND, "concepts")

# palette (sampled from the BoredOS branding sheet)
BRIGHT = "#4A4DC8"
INDIGO = "#37417E"
DEEP = "#29305D"
MID = "#666C92"
PERI = "#A1A5BB"
LIGHT = "#D7D9E5"
CARBON = "#12131C"
INK = "#0B0B0B"
WHITE = "#FFFFFF"
AMBER = "#DC8621"

PLATE = "M150 150H850V680L680 850H150Z"      # square, bottom-right chamfered
CHEVRON = "M330 370l170 130-170 130"
CURSOR = "M540 595h150v95H540Z"
PROMPT = CHEVRON + " M540 595h150v95H540Z"


def head(w=1000, h=1000, label="NEWOS"):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}"'
            f' width="{w}" height="{h}" role="img" aria-label="{label}">\n'
            f'<title>{label}</title>\n')


def grad(gid, a, b, angle=135):
    vec = {"135": ("0%", "0%", "100%", "100%"),
           "90": ("0%", "0%", "0%", "100%"),
           "45": ("0%", "100%", "100%", "0%")}[str(angle)]
    return (f'<linearGradient id="{gid}" x1="{vec[0]}" y1="{vec[1]}"'
            f' x2="{vec[2]}" y2="{vec[3]}">'
            f'<stop offset="0" stop-color="{a}"/>'
            f'<stop offset="1" stop-color="{b}"/></linearGradient>')


def stroke(d, color, w=95):
    return (f'<path d="{d}" fill="none" stroke="{color}"'
            f' stroke-width="{w}" stroke-linecap="square" stroke-linejoin="miter"/>')


def write(path, body):
    with open(path, "w") as f:
        f.write(body)
    print(os.path.relpath(path, ROOT))


def master():
    return head() + (
        f'<defs>{grad("g", BRIGHT, DEEP)}</defs>\n'
        f'<path d="{PLATE}" fill="url(#g)"/>\n'
        f'{stroke(CHEVRON, WHITE)}\n'
        f'<path d="{CURSOR}" fill="{AMBER}"/>\n</svg>\n')


def mono():
    return head(label="NEWOS (mono)") + (
        f'<defs><mask id="m"><rect width="1000" height="1000" fill="{WHITE}"/>'
        f'<path d="{PROMPT}" fill="{INK}" stroke="{INK}" stroke-width="95"'
        f' stroke-linejoin="miter"/></mask></defs>\n'
        f'<path d="{PLATE}" fill="currentColor" mask="url(#m)"/>\n</svg>\n')


def boot():
    """For the black boot framebuffer: pale plate, dark mark."""
    return head(label="NEWOS (boot)") + (
        f'<defs>{grad("g", LIGHT, PERI)}</defs>\n'
        f'<path d="{PLATE}" fill="url(#g)"/>\n'
        f'{stroke(CHEVRON, INK)}\n'
        f'<path d="{CURSOR}" fill="{AMBER}"/>\n</svg>\n')


# --- app icons: shared plate, one white glyph each, one amber accent -------

def icon(name, glyph, plate=INDIGO):
    return head(label=name) + (
        f'<defs>{grad("g", BRIGHT, DEEP)}</defs>\n'
        f'<path d="{PLATE}" fill="url(#g)"/>\n'
        f'{glyph}\n</svg>\n')


S = stroke
F = lambda d, c: f'<path d="{d}" fill="{c}"/>'

GLYPHS = {
    "shell": (F("M250 360h340v60H250Zm0 180h200v60H250Z", WHITE)
              + F("M500 540h150v100H500Z", AMBER)),
    "newfetch": (f'<path d="M283 717a307 307 0 1 1 434 0" fill="none"'
                 f' stroke="{WHITE}" stroke-width="110"/>'
                 + F("M440 440h120v120H440Z", AMBER)),
    "about": F("M455 250h90v90h-90Z", AMBER) + F("M455 390h90v360h-90Z", WHITE),
    "desktop": (F("M250 250h500v500H250Z", WHITE)
                + F("M250 250h500v110H250Z", AMBER)
                + F("M320 430h360v60H320Zm0 130h240v60H320Z", INDIGO)),
    "free": (f'<circle cx="500" cy="500" r="215" fill="none" stroke="{WHITE}"'
             f' stroke-width="130"/>'
             + f'<path d="M500 285a215 215 0 0 1 215 215" fill="none"'
             f' stroke="{AMBER}" stroke-width="130"/>'),
    "ps": F("M270 260h120v120H270Zm0 180h120v120H270Zm0 180h120v120H270Z", WHITE)
        + F("M450 300h280v40H450Zm0 180h200v40H450Z", WHITE)
        + F("M450 480h280v40H450Zm0 180h140v40H450Z", AMBER),
    "img": (F("M250 700l200-260 140 180 110-140 50 50V700Z", WHITE)
            + F("M620 280h120v120H620Z", AMBER)),
    "vid": (F("M400 300l280 200-280 200Z", WHITE)
            + F("M230 230h190v55H285v140h-55Z", AMBER)),
    "kilo": F("M280 260h440v50H280Zm0 130h320v50H280Zm0 130h400v50H280Z", WHITE)
        + F("M700 500h70v150H700Z", AMBER),
    "lua": (F("M560 200a300 300 0 1 0 0 600 340 340 0 0 1 0-600Z", WHITE)
            + F("M660 320h110v110H660Z", AMBER)),
    "newpkg": (F("M500 200l300 150v300l-300 150-300-150V350Z", WHITE)
               + F("M500 200l300 150-300 150-300-150Z", MID)
               + F("M470 350h60v400h-60Z", AMBER)),
    "tinygl": (f'<path d="M500 230L790 730H210Z" fill="none" stroke="{WHITE}"'
               f' stroke-width="80" stroke-linejoin="miter"/>'
               + F("M500 470h110v110H500Z", AMBER)),
}

# --- secondary marks -------------------------------------------------------

CONCEPT = {
    "n1-split": (f'<defs><clipPath id="c"><path d="{PLATE}"/></clipPath>'
                 f'<mask id="cut"><rect width="1000" height="1000" fill="{WHITE}"/>'
                 f'<path d="M880 190L190 880" stroke="{INK}" stroke-width="46"/></mask></defs>'
                 f'<g clip-path="url(#c)" mask="url(#cut)">'
                 f'<path d="M150 150h700L150 850Z" fill="url(#g)"/>'
                 f'<path d="M850 150v700H150Z" fill="{PERI}"/></g>'),
    "n2-frames": (f'<path d="M150 150h320v320H150Z" fill="url(#g)"/>'
                  f'<path d="M530 150h320v320H530Z" fill="{PERI}"/>'
                  f'<path d="M150 530h320v320H150Z" fill="{PERI}"/>'
                  f'<path d="M610 610h240v240H610Z" fill="{AMBER}"/>'),
    "n3-stack": (f'<path d="M150 200h700v130H150Z" fill="url(#g)"/>'
                 f'<path d="M150 400h470v130H150Z" fill="{MID}"/>'
                 f'<path d="M150 600h130v130H150Z" fill="{AMBER}"/>'),
    "n4-aperture": ('<path d="M500 190a310 310 0 1 0 310 310h-130a180 180 0 1 1-180-180Z"'
                    ' fill="url(#g)" transform="rotate(90 500 500)"/>'
                    f'<path d="M690 690h190v190H690Z" fill="{AMBER}"/>'),
    "n5-notch": (f'<defs><mask id="h"><rect width="1000" height="1000" fill="{WHITE}"/>'
                 f'<path d="M430 150h150v270H430Z" fill="{INK}"/></mask></defs>'
                 f'<path d="{PLATE}" fill="url(#g)" mask="url(#h)"/>'
                 f'<path d="M690 690h160v160H690Z" fill="{AMBER}"/>'),
    "n6-rings": (f'<path d="{PLATE}" fill="url(#g)"/>'
                 f'<path d="M310 310h380v380H310Z" fill="{DEEP}"/>'
                 f'<path d="M435 435h130v130H435Z" fill="{PERI}"/>'),
    "n7-column": (f'<path d="M200 150h150v640H200Z" fill="url(#g)"/>'
                  f'<path d="M350 400h180a220 220 0 0 1 220 220v170H550V550H350Z" fill="{PERI}"/>'
                  f'<path d="M200 850h600v70H200Z" fill="{AMBER}"/>'),
}


def main():
    for d in (BRAND, ICONS, CONCEPTS):
        os.makedirs(d, exist_ok=True)
    write(os.path.join(BRAND, "newos.svg"), master())
    write(os.path.join(BRAND, "newos-mono.svg"), mono())
    write(os.path.join(BRAND, "newos-boot.svg"), boot())
    for name, glyph in GLYPHS.items():
        write(os.path.join(ICONS, name + ".svg"), icon(name, glyph))
    g = f'<defs>{grad("g", BRIGHT, DEEP)}</defs>'
    for name, body in CONCEPT.items():
        write(os.path.join(CONCEPTS, name + ".svg"),
              head(label=name) + g + body + "\n</svg>\n")


if __name__ == "__main__":
    main()
