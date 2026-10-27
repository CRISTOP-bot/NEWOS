#!/usr/bin/env python3
"""Generate the NEWOS brand family.

The visual language is Windows 95 meets BoredOS: a four-pane waving flag in
the BoredOS rainbow, chunky beveled icon tiles, and a dark navy boot/wallpaper
plate with angular shards. Every SVG under brand/ is emitted from this file so
the desktop, website and boot assets stay in sync.
"""

import os
import shutil
import subprocess

ROOT = os.path.dirname(os.path.abspath(__file__))
BRAND = os.path.join(ROOT, "..", "brand")
ICONS = os.path.join(BRAND, "app-icons")
CONCEPTS = os.path.join(BRAND, "concepts")
WEB = os.path.join(ROOT, "..", "web")

# Windows 95 face colours plus the BoredOS rainbow.
BLACK = "#000000"
DARK = "#404040"
SHADOW = "#808080"
FACE = "#C0C0C0"
LIGHT = "#FFFFFF"
WHITE = "#F4F5FA"
NAVY = "#29305D"
DEEP = "#14162E"
BLUE = "#4A4DC8"
CYAN = "#35B6C8"
GREEN = "#35A853"
LIME = "#7BD45A"
YELLOW = "#FFD23F"
GOLD = "#DC8621"
ORANGE = "#FF7A3C"
RED = "#C42B1C"
SKY = "#5AA9E6"
DARKBLUE = "#102040"
PERI = "#A1A5BB"

# Waving four-pane flag, 512x512 coordinates.
PANES = {
    "tl": ("96,160 232,128 232,252 96,284", ORANGE, "#FFB45A"),
    "tr": ("248,124 392,92 392,216 248,248", LIME, "#D7FF8A"),
    "bl": ("96,300 232,268 232,392 96,424", CYAN, "#8AF0FF"),
    "br": ("248,264 392,232 392,356 248,388", BLUE, "#8F93FF"),
}


def write(path, body):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(body)
    print(os.path.relpath(path, ROOT))


def head(w, h, label, crisp=False):
    extra = ' shape-rendering="crispEdges"' if crisp else ""
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}"'
            f' width="{w}" height="{h}" role="img" aria-label="{label}"'
            f'{extra}>\n<title>{label}</title>\n')


def flag_group(mono=False, shadow=False):
    out = []
    if shadow:
        out.append('<g fill="#000" opacity=".35" transform="translate(10,12)">')
        for points, _, _ in PANES.values():
            out.append(f'<polygon points="{points}"/>')
        out.append('</g>')
    out.append('<g>')
    for i, (key, (points, color, light)) in enumerate(PANES.items()):
        fill = "currentColor" if mono else color
        opacity = (1.0, 0.82, 0.64, 0.46)[i] if mono else 1.0
        out.append(f'<polygon points="{points}" fill="{fill}"'
                   f' opacity="{opacity}" stroke="{BLACK}" stroke-width="6"'
                   ' stroke-linejoin="round"/>')
        if not mono:
            # a single chunky highlight along the top edge
            x0, y0 = (int(v) for v in points.split()[0].split(","))
            x2, y2 = (int(v) for v in points.split()[1].split(","))
            out.append(f'<polyline points="{x0},{y0} {x2},{y2}" fill="none"'
                       f' stroke="{light}" stroke-width="8" opacity=".85"/>')
    out.append('</g>')
    return "\n".join(out)


def master():
    body = head(512, 512, "NEWOS") + (
        '  <defs>\n'
        '    <linearGradient id="tile" x1="0" y1="0" x2="1" y2="1">\n'
        f'      <stop stop-color="{NAVY}"/><stop offset="1" stop-color="{DEEP}"/>\n'
        '    </linearGradient>\n'
        '    <linearGradient id="rainbow" x1="0" y1="0" x2="1" y2="0">\n'
        f'      <stop stop-color="{ORANGE}"/><stop offset=".33" stop-color="{LIME}"/>'
        f'<stop offset=".66" stop-color="{CYAN}"/><stop offset="1" stop-color="{BLUE}"/>\n'
        '    </linearGradient>\n'
        '  </defs>\n'
        '  <rect x="16" y="16" width="480" height="480" rx="96" fill="#080A18"/>\n'
        '  <rect x="22" y="22" width="468" height="468" rx="90" fill="url(#tile)"/>\n'
        '  <path d="M24 96h464" stroke="url(#rainbow)" stroke-width="6" opacity=".85"/>\n'
        '  <path d="M76 420 436 76" stroke="#FFFFFF" stroke-width="26" opacity=".05"/>\n'
        + flag_group(shadow=True) +
        '\n</svg>\n')
    return body


def mono():
    body = head(512, 512, "NEWOS monochrome mark") + flag_group(mono=True) + "\n</svg>\n"
    return body


def boot():
    body = head(512, 512, "NEWOS boot mark") + (
        '  <defs><linearGradient id="b" x1="0" y1="0" x2="1" y2="1">\n'
        f'    <stop stop-color="#1B1D36"/><stop offset="1" stop-color="#070812"/>\n'
        '  </linearGradient></defs>\n'
        '  <rect x="16" y="16" width="480" height="480" rx="96" fill="#05060E"/>\n'
        '  <rect x="24" y="24" width="464" height="464" rx="88" fill="url(#b)"/>\n'
        + flag_group(shadow=True) +
        '\n</svg>\n')
    return body


# --- Windows 95 style app icons -------------------------------------------

def R(x, y, w, h, fill, stroke=BLACK):
    s = f' stroke="{stroke}"' if stroke else ""
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{fill}"{s}/>'


def P(points, fill, stroke=BLACK):
    s = f' stroke="{stroke}"' if stroke else ""
    return f'<polygon points="{points}" fill="{fill}"{s}/>'


def L(x1, y1, x2, y2, color, width=1):
    return (f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}"'
            f' stroke="{color}" stroke-width="{width}"/>')


def icon_tile(glyph):
    """A 32x32 Win95 beveled tile with a transparent outside."""
    plate = (
        R(1, 1, 30, 30, FACE) +
        '<path d="M2 30V2h28" fill="none" stroke="%s" stroke-width="1"/>' % LIGHT +
        '<path d="M3 30h27V3" fill="none" stroke="%s" stroke-width="1"/>' % SHADOW +
        '<path d="M4 29h25V4" fill="none" stroke="%s" stroke-width="1"/>' % DARK
    )
    return head(32, 32, "NEWOS app icon", crisp=True) + plate + glyph + "\n</svg>\n"


def icon_shell():
    g = (R(4, 5, 24, 17, FACE) + R(6, 7, 20, 11, DARKBLUE) +
         L(8, 9, 12, 12, LIME, 2) + L(12, 12, 8, 15, LIME, 2) +
         R(15, 14, 7, 2, YELLOW) +
         R(13, 22, 6, 3, FACE) + R(9, 26, 14, 2, FACE))
    return icon_tile(g)


def icon_desktop():
    g = (R(4, 4, 24, 24, FACE) + R(5, 5, 22, 6, BLUE) +
         R(6, 6, 5, 3, ORANGE) + R(12, 6, 5, 3, LIME) +
         R(6, 10, 5, 2, CYAN) + R(12, 10, 5, 2, BLUE) +
         R(5, 12, 22, 15, DARKBLUE) +
         R(8, 15, 10, 2, FACE) + R(8, 19, 16, 2, SHADOW) +
         R(8, 23, 12, 2, SHADOW))
    return icon_tile(g)


def icon_about():
    g = (R(4, 4, 24, 19, FACE) + R(6, 6, 20, 14, DARKBLUE) +
         R(13, 8, 6, 2, WHITE) + R(17, 10, 2, 2, WHITE) +
         R(14, 12, 4, 2, WHITE) + R(14, 14, 2, 3, WHITE) +
         R(14, 18, 2, 2, WHITE) +
         R(13, 23, 6, 3, FACE) + R(9, 26, 14, 2, FACE))
    return icon_tile(g)


def icon_free():
    g = (R(6, 8, 20, 16, FACE) + R(9, 11, 14, 10, BLUE) +
         R(12, 14, 8, 4, CYAN))
    for x in (8, 13, 18, 23):
        g += R(x, 5, 2, 3, SHADOW) + R(x, 24, 2, 3, SHADOW)
    return icon_tile(g)


def icon_ps():
    g = (R(4, 4, 20, 22, FACE) + R(5, 5, 18, 5, BLUE) +
         R(7, 12, 10, 2, SHADOW) + R(7, 16, 14, 2, SHADOW) +
         R(7, 20, 8, 2, SHADOW) +
         R(10, 8, 20, 22, FACE) + R(11, 9, 18, 5, CYAN) +
         R(13, 16, 10, 2, SHADOW) + R(13, 20, 14, 2, SHADOW) +
         R(13, 24, 8, 2, SHADOW))
    return icon_tile(g)


def icon_img():
    g = (R(4, 4, 24, 24, FACE) + R(6, 6, 20, 20, SKY) +
         P("6,26 13,14 18,20 22,12 26,18 26,26", GREEN) +
         R(20, 8, 4, 4, YELLOW))
    return icon_tile(g)


def icon_vid():
    g = R(3, 8, 26, 16, DARK)
    for y in (9, 13, 17, 21):
        g += R(4, y, 3, 2, FACE) + R(25, y, 3, 2, FACE)
    g += P("12,11 22,16 12,21", WHITE)
    return icon_tile(g)


def icon_kilo():
    g = R(7, 3, 18, 26, WHITE)
    for y in (7, 11, 15, 19):
        g += R(9, y, 14, 1, BLUE)
    g += R(21, 5, 2, 20, GOLD) + P("21,25 25,21 25,27", ORANGE)
    return icon_tile(g)


def icon_newpkg():
    g = (P("4,12 16,6 28,12 16,18", GOLD) +
         P("4,12 16,18 16,28 4,22", FACE) +
         P("28,12 16,18 16,28 28,22", SHADOW) +
         R(14, 17, 4, 11, YELLOW))
    return icon_tile(g)


def icon_newfetch():
    g = (R(4, 4, 24, 19, FACE) + R(6, 6, 20, 14, DARKBLUE) +
         R(8, 8, 5, 5, ORANGE) + R(14, 8, 5, 5, LIME) +
         R(8, 14, 5, 4, CYAN) + R(14, 14, 5, 4, BLUE) +
         R(13, 23, 6, 3, FACE) + R(9, 26, 14, 2, FACE))
    return icon_tile(g)


def icon_tinygl():
    g = (R(4, 4, 24, 24, DARKBLUE) +
         '<polygon points="16,7 26,24 6,24" fill="none" stroke="%s" stroke-width="2"/>' % WHITE +
         L(16, 7, 16, 18, CYAN, 2) + L(16, 18, 9, 24, CYAN, 2) +
         L(16, 18, 23, 24, CYAN, 2) + R(14, 16, 4, 4, YELLOW))
    return icon_tile(g)


ICON_BUILDERS = {
    "shell": icon_shell,
    "desktop": icon_desktop,
    "about": icon_about,
    "free": icon_free,
    "ps": icon_ps,
    "img": icon_img,
    "vid": icon_vid,
    "kilo": icon_kilo,
    "newpkg": icon_newpkg,
    "newfetch": icon_newfetch,
    "tinygl": icon_tinygl,
}


def favicon():
    body = head(64, 64, "NEWOS") + (
        '<rect x="2" y="2" width="60" height="60" rx="14" fill="#0B0B1A"/>'
        '<rect x="5" y="5" width="54" height="54" rx="11" fill="#29305D"/>'
        '<g stroke="#000" stroke-width="1.5" stroke-linejoin="round">'
        f'<polygon points="12,21 31,17 31,31 12,35" fill="{ORANGE}"/>'
        f'<polygon points="34,16 52,12 52,26 34,30" fill="{LIME}"/>'
        f'<polygon points="12,38 31,34 31,48 12,52" fill="{CYAN}"/>'
        f'<polygon points="34,33 52,29 52,43 34,47" fill="{BLUE}"/>'
        '</g>\n</svg>\n')
    return body


# --- Limine wallpaper and splash ------------------------------------------

def wallpaper():
    body = head(1920, 1080, "NEWOS boot wallpaper") + (
        '  <defs>\n'
        '    <linearGradient id="bg" x1="0" y1="0" x2="1" y2="1">\n'
        f'      <stop stop-color="#080A18"/><stop offset=".55" stop-color="{NAVY}"/>'
        f'<stop offset="1" stop-color="#0B0B1A"/>\n'
        '    </linearGradient>\n'
        '    <linearGradient id="rainbow" x1="0" y1="0" x2="1" y2="0">\n'
        f'      <stop stop-color="{ORANGE}"/><stop offset=".33" stop-color="{LIME}"/>'
        f'<stop offset=".66" stop-color="{CYAN}"/><stop offset="1" stop-color="{BLUE}"/>\n'
        '    </linearGradient>\n'
        '  </defs>\n'
        '  <rect width="1920" height="1080" fill="url(#bg)"/>\n'
        '  <g opacity=".22">\n'
        f'    <polygon points="0,0 520,0 160,620 0,780" fill="{BLUE}"/>\n'
        f'    <polygon points="1920,0 1920,420 1420,180 1680,0" fill="{CYAN}"/>\n'
        f'    <polygon points="1920,1080 1220,1080 1560,700 1920,620" fill="{ORANGE}"/>\n'
        f'    <polygon points="0,1080 0,720 420,1080" fill="{LIME}"/>\n'
        '  </g>\n'
        '  <g opacity=".35">\n'
        f'    <polygon points="760,0 980,0 1260,1080 1080,1080" fill="{NAVY}"/>\n'
        f'    <polygon points="1190,0 1280,0 1600,1080 1500,1080" fill="{DEEP}"/>\n'
        '  </g>\n'
        '  <g transform="translate(170,290) scale(.72)">\n'
        + flag_group(shadow=True) +
        '  </g>\n'
        '  <text x="760" y="470" font-family="DejaVu Sans Mono, monospace"'
        ' font-weight="bold" font-size="150" letter-spacing="14"'
        f' fill="{WHITE}">NEWOS</text>\n'
        '  <text x="766" y="536" font-family="DejaVu Sans Mono, monospace"'
        ' font-size="30" letter-spacing="5"'
        f' fill="{PERI}">A SMALL SYSTEM WITH ROOM TO GROW</text>\n'
        '  <rect x="766" y="580" width="760" height="6" fill="url(#rainbow)" rx="3"/>\n'
        '  <text x="766" y="640" font-family="DejaVu Sans Mono, monospace"'
        f' font-size="24" letter-spacing="3" fill="{PERI}">0.3.0-gui / x86_64 / LIMINE</text>\n'
        '  <g opacity=".55">\n'
        f'    <circle cx="1600" cy="860" r="6" fill="{LIME}"/>\n'
        f'    <circle cx="1640" cy="860" r="6" fill="{CYAN}"/>\n'
        f'    <circle cx="1680" cy="860" r="6" fill="{BLUE}"/>\n'
        '  </g>\n'
        '</svg>\n')
    return body


def splash():
    body = head(640, 400, "NEWOS splash") + (
        '  <defs><linearGradient id="s" x1="0" y1="0" x2="1" y2="1">\n'
        f'    <stop stop-color="{NAVY}"/><stop offset="1" stop-color="#080A18"/>\n'
        '  </linearGradient></defs>\n'
        '  <rect width="640" height="400" fill="url(#s)"/>\n'
        '  <g opacity=".25">\n'
        f'    <polygon points="0,0 220,0 80,400 0,400" fill="{BLUE}"/>\n'
        f'    <polygon points="640,0 640,180 470,60 520,0" fill="{CYAN}"/>\n'
        '  </g>\n'
        '  <g transform="translate(66,52) scale(.5)">\n'
        + flag_group(shadow=True) +
        '  </g>\n'
        '  <text x="330" y="180" font-family="DejaVu Sans Mono, monospace"'
        ' font-weight="bold" font-size="54" letter-spacing="5"'
        f' fill="{WHITE}">NEWOS</text>\n'
        '  <text x="334" y="216" font-family="DejaVu Sans Mono, monospace"'
        ' font-size="13" letter-spacing="2"'
        f' fill="{PERI}">A SMALL SYSTEM WITH ROOM TO GROW</text>\n'
        '  <rect x="334" y="238" width="230" height="4" fill="url(#r)" rx="2"/>\n'
        '  <text x="334" y="278" font-family="DejaVu Sans Mono, monospace"'
        f' font-size="12" fill="{PERI}">0.3.0-gui / x86_64</text>\n'
        '</svg>\n')
    # give the splash its own gradient id
    body = body.replace('<rect x="334" y="238" width="230" height="4" fill="url(#r)" rx="2"/>',
                        f'<rect x="334" y="238" width="230" height="4" fill="{BLUE}" rx="2"/>')
    return body


def concept(name, variant):
    body = head(512, 512, name) + (
        '  <rect x="16" y="16" width="480" height="480" rx="96" fill="#0B0B1A"/>\n'
        '  <rect x="24" y="24" width="464" height="464" rx="88" fill="#1B1D36"/>\n')
    if variant == 0:
        body += flag_group(shadow=True)
    elif variant == 1:
        body += ('<g transform="rotate(-8 256 256)">' + flag_group(shadow=True) + '</g>')
    elif variant == 2:
        body += ('<g transform="scale(.82) translate(56 56)">' + flag_group(shadow=True) + '</g>')
    else:
        body += ('<g opacity=".7">' + flag_group(mono=True) + '</g>')
    body += "\n</svg>\n"
    return body


def render(svg_path, png_path, width=None):
    tool = shutil.which("rsvg-convert")
    if not tool:
        print("rsvg-convert not found; keeping existing", os.path.relpath(png_path, ROOT))
        return
    cmd = [tool, "-o", png_path]
    if width:
        cmd += ["-w", str(width)]
    cmd.append(svg_path)
    subprocess.run(cmd, check=True)
    print(os.path.relpath(png_path, ROOT))


def main():
    os.makedirs(BRAND, exist_ok=True)
    os.makedirs(ICONS, exist_ok=True)
    os.makedirs(CONCEPTS, exist_ok=True)
    os.makedirs(os.path.join(WEB, "assets"), exist_ok=True)

    write(os.path.join(BRAND, "newos.svg"), master())
    write(os.path.join(BRAND, "newos-mono.svg"), mono())
    write(os.path.join(BRAND, "newos-boot.svg"), boot())
    write(os.path.join(WEB, "favicon.svg"), favicon())
    write(os.path.join(BRAND, "newos-limine.svg"), wallpaper())
    write(os.path.join(BRAND, "newos-splash.svg"), splash())
    for name, builder in ICON_BUILDERS.items():
        write(os.path.join(ICONS, name + ".svg"), builder())
    for i, name in enumerate(("n1-split", "n2-frames", "n3-stack", "n4-flat")):
        write(os.path.join(CONCEPTS, name + ".svg"), concept(name, i))

    render(os.path.join(BRAND, "newos.svg"),
           os.path.join(BRAND, "newos-1024.png"), 1024)
    render(os.path.join(WEB, "favicon.svg"),
           os.path.join(WEB, "favicon.png"), 64)
    render(os.path.join(BRAND, "newos-limine.svg"),
           os.path.join(BRAND, "newos-limine.png"), 1920)
    render(os.path.join(BRAND, "newos-splash.svg"),
           os.path.join(BRAND, "newos-splash.png"), 640)
    bmp_tool = shutil.which("magick") or shutil.which("convert")
    if bmp_tool:
        subprocess.run([bmp_tool,
                        os.path.join(BRAND, "newos-splash.png"),
                        "-type", "TrueColor",
                        "BMP3:" + os.path.join(BRAND, "newos-splash.bmp")],
                       check=True)
        print("brand/newos-splash.bmp")


if __name__ == "__main__":
    main()
