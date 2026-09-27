"""Generate the README images in docs/ (animated SVG, no external assets).

The LED graphics use the same 5x7 digits, layout and sprites as the firmware
(src/display.cpp), so the pictures match what the real clock shows.

    python tools/make_images.py
"""
import os

OUT = os.path.join(os.path.dirname(__file__), '..', 'docs')
os.makedirs(OUT, exist_ok=True)

RED, OFF, PANEL = '#ff3b3b', '#2b0d0d', '#0b0b0d'
FONT = "font-family='Segoe UI,Roboto,Helvetica,Arial,sans-serif'"

# ---- same data as src/display.cpp ------------------------------------------------
DIGITS = {
    0: [0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E], 1: [0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E],
    2: [0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F], 3: [0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E],
    4: [0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02], 5: [0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E],
    6: [0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E], 7: [0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08],
    8: [0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E], 9: [0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C],
}
DIGIT_X = [2, 8, 19, 25]
COLON_X = 15
PAC_OPEN = [0x1C, 0x3E, 0x78, 0x70, 0x78, 0x3E, 0x1C]
PAC_CLOSED = [0x1C, 0x3E, 0x7F, 0x7F, 0x7F, 0x3E, 0x1C]
GHOST = [0x1C, 0x3E, 0x6B, 0x7F, 0x7F, 0x7F, 0x55]


def digit_px(d):
    return [(c, r) for r in range(7) for c in range(5) if DIGITS[d][r] & (0x10 >> c)]


def sprite_px(rows):
    return [(c, r) for r in range(7) for c in range(7) if rows[r] & (0x40 >> c)]


class Matrix:
    """32x8 LED matrix drawn at (x0, y0) with dot pitch p."""

    def __init__(self, x0, y0, p=14):
        self.x0, self.y0, self.p = x0, y0, p

    def cx(self, x):
        return self.x0 + x * self.p + self.p / 2

    def cy(self, y):
        return self.y0 + y * self.p + self.p / 2

    def w(self):
        return 32 * self.p

    def h(self):
        return 8 * self.p

    def dot(self, x, y, extra='', fill=RED):
        return f"<circle cx='{self.cx(x):g}' cy='{self.cy(y):g}' r='{self.p * 0.36:g}' fill='{fill}'{extra}/>"

    def background(self, pad=14):
        s = [f"<rect x='{self.x0 - pad}' y='{self.y0 - pad}' width='{self.w() + 2 * pad}' height='{self.h() + 2 * pad}' rx='14' fill='{PANEL}' stroke='#26262c'/>"]
        s += [self.dot(x, y, fill=OFF) for y in range(8) for x in range(32)]
        return '\n'.join(s)


GLOW = """<filter id='glow' x='-50%' y='-50%' width='200%' height='200%'>
<feGaussianBlur stdDeviation='2.2' result='b'/><feMerge><feMergeNode in='b'/><feMergeNode in='SourceGraphic'/></feMerge></filter>"""


# ---- banner: clock rolling 12:45 -> 12:46, blinking colon, seconds bar ---------------
def banner():
    W, H = 900, 330
    m = Matrix((W - 32 * 16) / 2, 52, 16)
    dur = 6.0
    s = [f"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 {W} {H}' width='{W}' height='{H}'>",
         f"<defs>{GLOW}<clipPath id='cell'><rect x='{m.x0 + 25 * m.p}' y='{m.y0}' width='{5 * m.p}' height='{7 * m.p}'/></clipPath>",
         "<linearGradient id='bg' x1='0' y1='0' x2='1' y2='1'><stop offset='0' stop-color='#15161c'/><stop offset='1' stop-color='#1d1015'/></linearGradient></defs>",
         f"<rect width='{W}' height='{H}' rx='20' fill='url(#bg)'/>",
         m.background(18), "<g filter='url(#glow)'>"]
    for i, d in enumerate([1, 2, 4]):                       # static digits 1 2 : 4
        s += [m.dot(DIGIT_X[i] + c, r) for c, r in digit_px(d)]
    # colon, blinking once per second
    s.append(f"<g><animate attributeName='opacity' values='1;0' dur='1s' calcMode='discrete' repeatCount='indefinite'/>")
    s += [m.dot(COLON_X + c, r) for c in (0, 1) for r in (1, 2, 4, 5)]
    s.append('</g>')
    # last digit rolls down from 5 to 6 at t = 3 s
    shift = 8 * m.p
    t1, t2 = 3.0 / dur, 3.4 / dur
    s.append("<g clip-path='url(#cell)'><g>")
    s.append(f"<animateTransform attributeName='transform' type='translate' values='0 0;0 0;0 {shift:g};0 {shift:g}' "
             f"keyTimes='0;{t1:.3f};{t2:.3f};1' dur='{dur}s' repeatCount='indefinite'/>")
    s += [m.dot(DIGIT_X[3] + c, r) for c, r in digit_px(5)]
    s += [m.dot(DIGIT_X[3] + c, r - 8) for c, r in digit_px(6)]
    s.append('</g></g>')
    # seconds bar on the bottom row, filling up over the loop
    for x in range(32):
        k = (x + 1) / 33
        s.append(f"<circle cx='{m.cx(x):g}' cy='{m.cy(7):g}' r='{m.p * 0.36:g}' fill='{RED}' opacity='0'>"
                 f"<animate attributeName='opacity' values='0;1' keyTimes='0;{k:.3f}' calcMode='discrete' dur='{dur}s' repeatCount='indefinite'/></circle>")
    s.append('</g>')
    s.append(f"<text x='{W / 2}' y='{H - 70}' text-anchor='middle' {FONT} font-size='38' font-weight='700' fill='#f2f3f7'>ESP8266 Matrix Clock</text>")
    s.append(f"<text x='{W / 2}' y='{H - 36}' text-anchor='middle' {FONT} font-size='18' fill='#9aa0ad'>"
             "WiFi setup from your phone · web settings · animations · updates from GitHub</text>")
    s.append('</svg>')
    return '\n'.join(s)


# ---- pac-man strip ------------------------------------------------------------------
def pacman():
    m = Matrix(24, 24, 14)
    W, H = int(m.w() + 48), int(m.h() + 48)
    frames = 32 + 11 + 16          # pac-man from x=-8 until the ghost has left
    step = 0.09
    dur = frames * step
    s = [f"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 {W} {H}' width='{W}' height='{H}'>",
         f"<defs>{GLOW}<clipPath id='mx'><rect x='{m.x0}' y='{m.y0}' width='{m.w()}' height='{m.h()}'/></clipPath></defs>",
         m.background(16), "<g filter='url(#glow)' clip-path='url(#mx)'>"]

    def xs(offset):   # discrete x positions of a sprite for each frame
        return ';'.join(f"{(f - 8 + offset) * m.p:g} 0" for f in range(frames))

    for d in range(2, 32, 4):                  # dots, eaten when pac-man passes
        eat = min(max((d - 3 + 8) / frames, 0.001), 0.999)
        s.append(f"<circle cx='{m.cx(d):g}' cy='{m.cy(3):g}' r='{m.p * 0.36:g}' fill='{RED}'>"
                 f"<animate attributeName='opacity' values='1;0' keyTimes='0;{eat:.3f}' calcMode='discrete' dur='{dur:.2f}s' repeatCount='indefinite'/></circle>")
    for rows, phase in ((PAC_OPEN, '1;0'), (PAC_CLOSED, '0;1')):
        s.append(f"<g><animateTransform attributeName='transform' type='translate' values='{xs(0)}' calcMode='discrete' dur='{dur:.2f}s' repeatCount='indefinite'/>"
                 f"<animate attributeName='opacity' values='{phase}' dur='{step * 4:.2f}s' calcMode='discrete' repeatCount='indefinite'/>")
        s += [m.dot(c, r) for c, r in sprite_px(rows)]
        s.append('</g>')
    s.append(f"<g><animateTransform attributeName='transform' type='translate' values='{xs(-11)}' calcMode='discrete' dur='{dur:.2f}s' repeatCount='indefinite'/>")
    s += [m.dot(c, r) for c, r in sprite_px(GHOST)]
    s.append('</g></g></svg>')
    return '\n'.join(s)


# ---- wiring -------------------------------------------------------------------------
def wiring():
    W, H = 900, 400
    s = [f"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 {W} {H}' width='{W}' height='{H}'>",
         f"<rect width='{W}' height='{H}' rx='20' fill='#14161c'/>"]
    # NodeMCU
    s.append("<rect x='50' y='70' width='230' height='270' rx='12' fill='#10353a' stroke='#1f6b72' stroke-width='2'/>")
    s.append("<rect x='120' y='40' width='90' height='42' rx='6' fill='#c9ccd4'/><rect x='140' y='52' width='50' height='18' rx='3' fill='#8b9099'/>")
    s.append("<rect x='95' y='120' width='140' height='90' rx='6' fill='#1e2126' stroke='#3a3f48'/>")
    s.append(f"<text x='165' y='160' text-anchor='middle' {FONT} font-size='18' font-weight='700' fill='#e8eaf0'>ESP8266</text>")
    s.append(f"<text x='165' y='184' text-anchor='middle' {FONT} font-size='13' fill='#9aa0ad'>NodeMCU / D1 mini</text>")
    # MAX7219 module
    s.append("<rect x='560' y='90' width='300' height='220' rx='12' fill='#12324f' stroke='#2a5d8a' stroke-width='2'/>")
    for i in range(4):
        x = 580 + i * 68
        s.append(f"<rect x='{x}' y='110' width='58' height='58' rx='4' fill='#0b0b0d'/>")
        for r in range(4):
            for c in range(4):
                s.append(f"<circle cx='{x + 9 + c * 13.3:.1f}' cy='{119 + r * 13.3:.1f}' r='4' fill='{RED if (r + c + i) % 3 == 0 else OFF}'/>")
    s.append(f"<text x='710' y='205' text-anchor='middle' {FONT} font-size='16' font-weight='700' fill='#e8eaf0'>MAX7219 4-in-1 (8x32)</text>")
    wires = [('VCC', '5V / VIN', '#ff4d4d'), ('GND', 'GND', '#9aa0ad'), ('DIN', 'D7', '#35d07f'),
             ('CS', 'D6', '#ffc53d'), ('CLK', 'D5', '#4da3ff')]
    for i, (mod, esp, col) in enumerate(wires):
        y = 235 + i * 16
        ey = 235 + i * 22
        s.append(f"<circle cx='575' cy='{y}' r='5' fill='{col}'/>")
        s.append(f"<text x='590' y='{y + 5}' {FONT} font-size='13' fill='#e8eaf0'>{mod}</text>")
        s.append(f"<circle cx='265' cy='{ey}' r='5' fill='{col}'/>")
        s.append(f"<text x='252' y='{ey + 5}' text-anchor='end' {FONT} font-size='13' fill='#e8eaf0'>{esp}</text>")
        s.append(f"<path d='M270 {ey} C 420 {ey}, 420 {y}, 570 {y}' fill='none' stroke='{col}' stroke-width='3.5' stroke-linecap='round'/>")
    s.append(f"<text x='{W / 2}' y='375' text-anchor='middle' {FONT} font-size='14' fill='#9aa0ad'>"
             "Use a 5 V / 1 A supply · CLK and DIN are the ESP8266 hardware SPI pins</text>")
    s.append('</svg>')
    return '\n'.join(s)


# ---- setup flow ---------------------------------------------------------------------
def setup_flow():
    W, H = 900, 250
    steps = [('Power on', 'Clock shows', '"WiFi setup"'), ('Join WiFi', 'MatrixClock-XXXX', 'on your phone'),
             ('Pick network', 'Setup page opens,', 'enter password'), ('Done!', 'Clock scrolls its IP,', 'open it for settings')]
    s = [f"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 {W} {H}' width='{W}' height='{H}'>",
         f"<rect width='{W}' height='{H}' rx='20' fill='#14161c'/>"]
    cw, gap = 185, 32
    x0 = (W - (4 * cw + 3 * gap)) / 2
    for i, (title, l1, l2) in enumerate(steps):
        x = x0 + i * (cw + gap)
        delay = i * 0.6
        s.append('<g>')
        s.append(f"<rect x='{x}' y='40' width='{cw}' height='170' rx='16' fill='#1c1f28' stroke='#3a3f4b' stroke-width='2'>"
                 f"<animate attributeName='stroke' values='#3a3f4b;#ff3b3b;#ff3b3b;#3a3f4b' keyTimes='0;0.1;0.25;0.35' "
                 f"dur='2.4s' begin='{delay}s' repeatCount='indefinite'/></rect>")
        s.append(f"<circle cx='{x + cw / 2}' cy='82' r='22' fill='#ff3b3b'>"
                 f"<animate attributeName='r' values='22;27;22;22' keyTimes='0;0.1;0.25;1' dur='2.4s' begin='{delay}s' repeatCount='indefinite'/></circle>")
        s.append(f"<text x='{x + cw / 2}' y='90' text-anchor='middle' {FONT} font-size='22' font-weight='700' fill='#fff'>{i + 1}</text>")
        s.append(f"<text x='{x + cw / 2}' y='138' text-anchor='middle' {FONT} font-size='19' font-weight='700' fill='#f2f3f7'>{title}</text>")
        s.append(f"<text x='{x + cw / 2}' y='164' text-anchor='middle' {FONT} font-size='13.5' fill='#9aa0ad'>{l1}</text>")
        s.append(f"<text x='{x + cw / 2}' y='184' text-anchor='middle' {FONT} font-size='13.5' fill='#9aa0ad'>{l2}</text>")
        s.append('</g>')
        if i < 3:
            ax = x + cw + 6
            s.append(f"<path d='M{ax} 125 l{gap - 14} 0 m-7 -7 l7 7 l-7 7' fill='none' stroke='#5a5f6b' stroke-width='3' stroke-linecap='round'/>")
    s.append('</svg>')
    return '\n'.join(s)


for name, fn in (('banner', banner), ('pacman', pacman), ('wiring', wiring), ('setup', setup_flow)):
    path = os.path.join(OUT, name + '.svg')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(fn() + '\n')
    print(f'{path}: {os.path.getsize(path) // 1024} KB')
