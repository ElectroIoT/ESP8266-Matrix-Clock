"""Portfolio card image: docs/card.png (1536x600).

Made for a fixed-height, full-width card slot with object-fit: cover (manoranjan.dev
project cards: 192 px high, ~1.8:1 on phones up to ~2.55:1 on desktop). Everything
important sits in the centre ~1000 px, so a 1.8:1 crop (plus a 10% hover zoom) never
cuts it; the edges only carry background.

    python tools/make_card.py
"""
import os
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

OUT = os.path.join(os.path.dirname(__file__), '..', 'docs', 'card.png')
FONTS = 'C:/Windows/Fonts/'
W, H = 1536, 600

DIGITS = {
    1: [0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E], 2: [0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F],
    4: [0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02], 5: [0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E],
}


def font(name, size):
    return ImageFont.truetype(FONTS + name, size)


# background: dark red-to-black gradient with a faint LED-dot texture across the full width
img = Image.new('RGB', (W, H))
px = img.load()
for y in range(H):
    for x in range(W):
        t = abs(x - W / 2) / (W / 2)
        px[x, y] = (int(34 - 20 * t), int(10 - 3 * t), int(16 - 4 * t))
d = ImageDraw.Draw(img)
for y in range(12, H, 24):
    for x in range(12, W, 24):
        d.ellipse((x - 3, y - 3, x + 3, y + 3), fill=(40, 16, 20))
halo = Image.new('RGB', (W, H))
ImageDraw.Draw(halo).ellipse((330, 40, 1206, 360), fill=(110, 14, 18))
img = ImageChops.add(img, halo.filter(ImageFilter.GaussianBlur(80)))

# LED panel, centred
pitch, r = 26, 9.4
mw, mh = 32 * pitch, 8 * pitch
mx, my = (W - mw) // 2, 70
d = ImageDraw.Draw(img)
d.rounded_rectangle((mx - 22, my - 22, mx + mw + 22, my + mh + 22), 22, fill=(8, 8, 10), outline=(60, 40, 44), width=2)

lit = set()
for x0, dig in zip([2, 8, 19, 25], [1, 2, 4, 5]):
    for row in range(7):
        for c in range(5):
            if DIGITS[dig][row] & (0x10 >> c):
                lit.add((x0 + c, row))
lit |= {(c, row) for c in (15, 16) for row in (1, 2, 4, 5)}
lit |= {(x, 7) for x in range(21)}


def centre(x, y):
    return mx + x * pitch + pitch / 2, my + y * pitch + pitch / 2


glow = Image.new('RGB', (W, H))
gd = ImageDraw.Draw(glow)
for y in range(8):
    for x in range(32):
        cx, cy = centre(x, y)
        if (x, y) in lit:
            gd.ellipse((cx - r * 1.9, cy - r * 1.9, cx + r * 1.9, cy + r * 1.9), fill=(255, 40, 40))
        else:
            d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(48, 14, 16))
img = ImageChops.add(img, glow.filter(ImageFilter.GaussianBlur(11)).point(lambda v: int(v * 0.8)))
d = ImageDraw.Draw(img)
for x, y in lit:
    cx, cy = centre(x, y)
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(255, 64, 58))
    d.ellipse((cx - r * 0.45, cy - r * 0.55, cx + r * 0.15, cy - r * 0.05), fill=(255, 150, 140))

# title + subtitle, centred and large enough to read at 192 px card height
title = 'WiFi LED Matrix Clock'
tf = font('seguibl.ttf', 74)
d.text(((W - d.textlength(title, font=tf)) / 2, 348), title, font=tf, fill=(246, 246, 250))
sub = 'ESP8266  ·  MAX7219  ·  Open Source'
sf = font('seguisb.ttf', 36)
d.text(((W - d.textlength(sub, font=sf)) / 2, 458), sub, font=sf, fill=(255, 150, 140))

img.save(OUT, optimize=True)
print(OUT, os.path.getsize(OUT) // 1024, 'KB')
