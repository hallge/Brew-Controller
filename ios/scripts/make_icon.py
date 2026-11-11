"""Brew Controller app icon, 1024 x 1024, drawn at 4x and scaled down.

The board's shot ring (track, green on-target arc with round ends, orange target notch) around a
glossy espresso drop, on an espresso-dark background. Opaque, no rounded corners: iOS masks it.
Usage: python make_icon.py OUT.png [PREVIEW.png]
"""
import math
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

S = 4096
C = S / 2


def radial(size, inner, outer, cx, cy, radius):
    """Radial gradient from inner (at cx, cy) to outer (at radius and beyond)."""
    small = 256
    img = Image.new("RGB", (small, small))
    px = img.load()
    for y in range(small):
        for x in range(small):
            d = math.hypot(x / small * size - cx, y / small * size - cy) / radius
            t = min(1.0, d) ** 1.4
            px[x, y] = tuple(round(inner[i] + (outer[i] - inner[i]) * t) for i in range(3))
    return img.resize((size, size), Image.BICUBIC)


def vertical(size, top, bottom, y0, y1):
    img = Image.new("RGB", (1, 256))
    for y in range(256):
        yy = y / 255 * size
        t = min(1.0, max(0.0, (yy - y0) / (y1 - y0)))
        img.putpixel((0, y), tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(3)))
    return img.resize((size, size), Image.BICUBIC)


def hexrgb(v):
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)


def arc_point(r, deg):
    # 0 degrees at the top, clockwise (like the board's ring).
    a = math.radians(deg - 90)
    return C + r * math.cos(a), C + r * math.sin(a)


def main(out, preview=None):
    bg = radial(S, (52, 34, 24), (14, 10, 8), C, S * 0.42, S * 0.75)
    draw = ImageDraw.Draw(bg)

    ring_r = S * 0.335
    width = int(S * 0.072)
    box = [C - ring_r, C - ring_r, C + ring_r, C + ring_r]

    # Track: the whole ring, dim.
    draw.arc(box, 0, 360, fill=(58, 46, 40), width=width)

    # On-target arc, green, from the top clockwise, with round ends.
    green = hexrgb(0x2ECC71)
    end_deg = 292
    arc_layer = Image.new("L", (S, S), 0)
    ad = ImageDraw.Draw(arc_layer)
    ad.arc(box, -90, end_deg - 90, fill=255, width=width)
    mid_r = ring_r - width / 2
    for deg in (0, end_deg):
        x, y = arc_point(mid_r, deg)
        ad.ellipse([x - width / 2, y - width / 2, x + width / 2, y + width / 2], fill=255)
    # A soft glow under the arc.
    glow = arc_layer.filter(ImageFilter.GaussianBlur(S * 0.02))
    glow = glow.point(lambda v: int(v * 0.45))
    bg.paste(Image.new("RGB", (S, S), green), (0, 0), glow)
    shade = vertical(S, (88, 230, 142), (30, 160, 88), C - ring_r, C + ring_r)
    bg.paste(shade, (0, 0), arc_layer)

    # Target notch: a short orange bar across the ring, a little past the arc's end.
    orange = hexrgb(0xE67E22)
    notch = Image.new("L", (S, S), 0)
    nd = ImageDraw.Draw(notch)
    deg = end_deg + 16
    x0, y0 = arc_point(ring_r + width * 0.18, deg)
    x1, y1 = arc_point(ring_r - width * 1.18, deg)
    nd.line([x0, y0, x1, y1], fill=255, width=int(width * 0.30))
    for (x, y) in ((x0, y0), (x1, y1)):
        r = width * 0.15
        nd.ellipse([x - r, y - r, x + r, y + r], fill=255)
    bg.paste(Image.new("RGB", (S, S), orange), (0, 0), notch)

    # The drop: a circle with a pointed top.
    r = S * 0.150
    cx, cy = C, C + S * 0.055
    h = S * 0.300  # centre to tip
    tip = (cx, cy - h)
    spread = math.acos(r / h)
    pts = [tip]
    steps = 180
    start = -math.pi / 2 + spread  # angles in image coordinates, up is -pi/2
    end = -math.pi / 2 - spread + 2 * math.pi
    for i in range(steps + 1):
        a = start + (end - start) * i / steps
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    drop = Image.new("L", (S, S), 0)
    ImageDraw.Draw(drop).polygon(pts, fill=255)

    # Shadow under the drop.
    shadow = drop.filter(ImageFilter.GaussianBlur(S * 0.018)).point(lambda v: int(v * 0.55))
    shadow = ImageChops.offset(shadow, 0, int(S * 0.012))
    bg.paste(Image.new("RGB", (S, S), (6, 4, 3)), (0, 0), shadow)

    body = vertical(S, (232, 168, 98), (120, 62, 26), cy - h, cy + r)
    bg.paste(body, (0, 0), drop)

    # Crema-light highlight, upper left inside the drop.
    hl = Image.new("L", (S, S), 0)
    hd = ImageDraw.Draw(hl)
    hx, hy = cx - r * 0.42, cy - r * 0.30
    hd.ellipse([hx - r * 0.20, hy - r * 0.42, hx + r * 0.20, hy + r * 0.42], fill=255)
    hl = hl.filter(ImageFilter.GaussianBlur(S * 0.010))
    hl = ImageChops.multiply(hl, drop).point(lambda v: int(v * 0.55))
    bg.paste(Image.new("RGB", (S, S), (255, 236, 205)), (0, 0), hl)

    icon = bg.resize((1024, 1024), Image.LANCZOS)
    icon.save(out, optimize=True)
    if preview:
        # How it looks masked on a home screen, at two sizes.
        sheet = Image.new("RGB", (1024 + 240 + 60, 1024), (235, 235, 240))
        mask = Image.new("L", (1024, 1024), 0)
        ImageDraw.Draw(mask).rounded_rectangle([0, 0, 1023, 1023], radius=230, fill=255)
        sheet.paste(icon, (0, 0), mask)
        small = icon.resize((180, 180), Image.LANCZOS)
        smask = mask.resize((180, 180), Image.LANCZOS)
        sheet.paste(small, (1024 + 60, 120), smask)
        tiny = icon.resize((60, 60), Image.LANCZOS)
        sheet.paste(tiny, (1024 + 60, 360), mask.resize((60, 60), Image.LANCZOS))
        sheet.save(preview)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
