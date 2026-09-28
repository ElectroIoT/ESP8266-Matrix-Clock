"""Featured image for blog posts / social previews: docs/featured.png (1200x630).

Uses the clock's real 5x7 digits (same data as src/display.cpp).

    python tools/make_featured.py
"""
import os
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

OUT = os.path.join(os.path.dirname(__file__), '..', 'docs', 'featured.png')
FONTS = 'C:/Windows/Fonts/'
W, H = 1200, 630

DIGITS = {
    1: [0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E], 2: [0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F],
    4: [0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02], 5: [0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E],
}


def font(name, size):
    return ImageFont.truetype(FONTS + name, size)


# background: dark diagonal gradient plus a soft red glow behind the display
img = Image.new('RGB', (W, H))
px = img.load()
for y in range(H):
    for x in range(W):
        t = (x / W + y / H) / 2
        px[x, y] = (int(16 + 14 * t), int(17 + 2 * t), int(24 + 4 * t))
halo = Image.new('RGB', (W, H))
ImageDraw.Draw(halo).ellipse((170, 20, 1030, 330), fill=(90, 10, 14))
img = ImageChops.add(img, halo.filter(ImageFilter.GaussianBlur(90)))

# LED matrix: 32 x 8 dots showing 12:45, seconds bar part-filled
pitch, r = 26, 9.4
mw, mh = 32 * pitch, 8 * pitch
mx, my = (W - mw) // 2, 58
d = ImageDraw.Draw(img)
d.rounded_rectangle((mx - 26, my - 26, mx + mw + 26, my + mh + 26), 26, fill=(9, 9, 11), outline=(44, 44, 52), width=2)

lit = set()
for x0, dig in zip([2, 8, 19, 25], [1, 2, 4, 5]):
    for row in range(7):
        for c in range(5):
            if DIGITS[dig][row] & (0x10 >> c):
                lit.add((x0 + c, row))
lit |= {(c, row) for c in (15, 16) for row in (1, 2, 4, 5)}   # colon
lit |= {(x, 7) for x in range(21)}                             # seconds bar


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
            d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(46, 13, 14))
img = ImageChops.add(img, glow.filter(ImageFilter.GaussianBlur(11)).point(lambda v: int(v * 0.8)))
d = ImageDraw.Draw(img)
for x, y in lit:
    cx, cy = centre(x, y)
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(255, 64, 58))
    d.ellipse((cx - r * 0.45, cy - r * 0.55, cx + r * 0.15, cy - r * 0.05), fill=(255, 150, 140))   # highlight

# text
title = 'WiFi LED Matrix Clock'
tf = font('seguibl.ttf', 64)
d.text(((W - d.textlength(title, font=tf)) / 2, 342), title, font=tf, fill=(244, 245, 249))
sub = 'ESP8266 + MAX7219  ·  set up from your phone  ·  updates itself from GitHub'
sf = font('segoeui.ttf', 27)
d.text(((W - d.textlength(sub, font=sf)) / 2, 430), sub, font=sf, fill=(170, 176, 190))

chips = ['Open source', 'PlatformIO + Arduino IDE', 'Night mode', '11 animations', 'Safe mode']
cf = font('seguisb.ttf', 22)
widths = [d.textlength(c, font=cf) + 40 for c in chips]
x = (W - (sum(widths) + 14 * (len(chips) - 1))) / 2
for c, w in zip(chips, widths):
    d.rounded_rectangle((x, 500, x + w, 546), 23, fill=(38, 20, 24), outline=(255, 59, 59), width=2)
    d.text((x + 20, 508), c, font=cf, fill=(255, 205, 200))
    x += w + 14

d.text((40, H - 50), 'ElectroIoT', font=font('seguisb.ttf', 22), fill=(255, 90, 80))
url = 'github.com/ElectroIoT/ESP8266-Matrix-Clock'
uf = font('segoeui.ttf', 20)
d.text((W - 40 - d.textlength(url, font=uf), H - 48), url, font=uf, fill=(130, 136, 150))

img.save(OUT, optimize=True)
print(OUT, os.path.getsize(OUT) // 1024, 'KB')
