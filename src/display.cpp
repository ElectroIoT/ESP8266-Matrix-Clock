#include "display.h"
#include "config.h"
#include "settings.h"
#include <SPI.h>

static MD_MAX72XX mx(MATRIX_HW, PIN_CS, MATRIX_MODULES);
static uint8_t fb[W];   // one byte per column, bit 0 = top row

// =================================================================================
// Low level
// =================================================================================

void displayBegin() {
    mx.begin();
    mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);   // we push whole frames ourselves
    fbClear();
    fbShow();
}

void displayIntensity(uint8_t level) {
    mx.control(MD_MAX72XX::INTENSITY, level > 15 ? 15 : level);
}

void displayPower(bool on) {
    mx.control(MD_MAX72XX::SHUTDOWN, on ? MD_MAX72XX::OFF : MD_MAX72XX::ON);
}

void fbClear() {
    memset(fb, 0, sizeof(fb));
}

void fbSet(int x, int y, bool on) {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    if (on) fb[x] |= (1 << y);
    else    fb[x] &= ~(1 << y);
}

static uint8_t reverseBits(uint8_t b) {
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

void fbShow() {
    for (int x = 0; x < W; x++) {
        uint8_t v = FLIP_VERTICAL ? reverseBits(fb[x]) : fb[x];
        mx.setColumn(FLIP_HORIZONTAL ? x : (W - 1 - x), v);   // library column 0 is the right-most one
    }
    mx.update();
}

// =================================================================================
// Text
// =================================================================================

static uint8_t glyph(char c, uint8_t* cols) {
    uint8_t ch = (uint8_t)c;
    if (ch < 32 || ch > 126) ch = '?';
    return mx.getChar(ch, 8, cols);
}

int textWidth(const String& s) {
    uint8_t cols[8];
    int w = 0;
    for (size_t i = 0; i < s.length(); i++) w += glyph(s[i], cols) + 1;
    return w > 0 ? w - 1 : 0;
}

void drawText(int x, const String& s) {
    uint8_t cols[8];
    for (size_t i = 0; i < s.length() && x < W; i++) {
        uint8_t n = glyph(s[i], cols);
        for (uint8_t c = 0; c < n; c++, x++)
            if (x >= 0 && x < W) fb[x] = cols[c];
        if (x >= 0 && x < W) fb[x] = 0;   // 1 px letter gap
        x++;
    }
}

bool Marquee::step() {
    int tw = textWidth(text);
    fbClear();
    drawText(W - pos, text);
    fbShow();
    if (++pos > W + tw) {
        pos = 0;
        return true;
    }
    return false;
}

void scrollText(const String& s) {
    Marquee m;
    m.start(s);
    while (!m.step()) serviceWait(30);
    fbClear();
    fbShow();
}

// =================================================================================
// Clock face: HH:MM with rolling digits, blinking colon and a seconds bar
// =================================================================================

// 5x7 digits, one byte per row, bit 4 = left-most pixel
static const uint8_t DIGITS[10][7] PROGMEM = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},   // 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},   // 1
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},   // 2
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},   // 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},   // 4
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},   // 5
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},   // 6
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},   // 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},   // 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},   // 9
};
static const uint8_t BLANK = 10;                    // "digit" that draws nothing (hidden leading zero)
static const int     DIGIT_X[4] = {2, 8, 19, 25};   // H H : M M  -> 28 px wide, centred
static const int     COLON_X = 15;                  // 2 px wide
static const uint8_t ROLL_FRAMES = 8;

// Draw digit d with its top-left at (x, y); rows are clipped to 0..6 so the
// seconds bar on row 7 stays clean while digits roll.
static void drawDigit(int x, int y, uint8_t d) {
    if (d > 9) return;
    for (int r = 0; r < 7; r++) {
        int yy = y + r;
        if (yy < 0 || yy > 6) continue;
        uint8_t bits = pgm_read_byte(&DIGITS[d][r]);
        for (int c = 0; c < 5; c++)
            if (bits & (0x10 >> c)) fbSet(x + c, yy, true);
    }
}

void ClockFace::reset() {
    fresh = true;
}

void ClockFace::tick() {
    for (int i = 0; i < 4; i++)
        if (phase[i]) phase[i]--;
}

void ClockFace::draw(const struct tm& t, uint16_t ms, int dx) {
    uint8_t h = t.tm_hour;
    if (!cfg.fmt24) {
        h %= 12;
        if (h == 0) h = 12;
    }
    uint8_t want[4] = {(uint8_t)(h / 10), (uint8_t)(h % 10), (uint8_t)(t.tm_min / 10), (uint8_t)(t.tm_min % 10)};
    if (want[0] == 0 && !cfg.leadingZero) want[0] = BLANK;

    for (int i = 0; i < 4; i++) {
        if (want[i] == cur[i] && !fresh) continue;
        prev[i]  = cur[i];
        cur[i]   = want[i];
        phase[i] = (fresh || cfg.roll == ROLL_NONE) ? 0 : ROLL_FRAMES;
    }
    fresh = false;

    for (int i = 0; i < 4; i++) {
        int x = DIGIT_X[i] + dx;
        if (!phase[i]) {
            drawDigit(x, 0, cur[i]);
            continue;
        }
        int s = ROLL_FRAMES - phase[i];   // 0..7 pixels travelled
        if (cfg.roll == ROLL_UP) {
            drawDigit(x, -s, prev[i]);
            drawDigit(x, ROLL_FRAMES - s, cur[i]);
        } else {
            drawDigit(x, s, prev[i]);
            drawDigit(x, s - ROLL_FRAMES, cur[i]);
        }
    }

    if (!cfg.blinkColon || ms < 500) {
        for (int c = 0; c < 2; c++) {
            fbSet(COLON_X + dx + c, 1, true);
            fbSet(COLON_X + dx + c, 2, true);
            fbSet(COLON_X + dx + c, 4, true);
            fbSet(COLON_X + dx + c, 5, true);
        }
    }

    if (cfg.secondsBar) {
        int len = ((uint32_t)t.tm_sec * 1000 + ms) * W / 60000;
        for (int x = 0; x < len; x++) fbSet(x + dx, 7, true);
    }
}

// =================================================================================
// Animations
// =================================================================================

static void animSparkle() {
    fbClear();
    for (int f = 0; f < 80; f++) {
        bool on = f < 40;
        for (int k = 0; k < 7; k++) fbSet(random(W), random(H), on);
        if (f >= 70) fbClear();   // make sure nothing is left over
        fbShow();
        serviceWait(28);
    }
}

static void animWipe() {
    for (int pass = 0; pass < 2; pass++) {
        for (int x = 0; x < W; x++) {
            for (int y = 0; y < H; y++) fbSet(x, y, pass == 0);   // fill left to right, then erase
            fbShow();
            serviceWait(16);
        }
    }
}

static void animRain() {
    const int N = 12;
    int8_t dx[N], dy[N];
    for (int i = 0; i < N; i++) {
        dx[i] = random(W);
        dy[i] = -random(14);
    }
    for (int f = 0; f < 110; f++) {
        fbClear();
        for (int i = 0; i < N; i++) {
            fbSet(dx[i], dy[i], true);
            fbSet(dx[i], dy[i] - 1, true);
            fbSet(dx[i], dy[i] - 2, f & 1);   // flickering tail
            dy[i]++;
            if (dy[i] > H + 2 && f < 85) {    // respawn only during the first part, then let it drain
                dx[i] = random(W);
                dy[i] = -random(6);
            }
        }
        fbShow();
        serviceWait(40);
    }
}

static void animBoxes() {
    const int cx = W / 2 - 1;
    for (int rep = 0; rep < 3; rep++) {
        for (int r = 0; r <= W / 2 + 1; r++) {
            fbClear();
            int x0 = cx - r, x1 = cx + 1 + r, y0 = 3 - r, y1 = 4 + r;
            for (int x = x0; x <= x1; x++) { fbSet(x, y0, true); fbSet(x, y1, true); }
            for (int y = y0; y <= y1; y++) { fbSet(x0, y, true); fbSet(x1, y, true); }
            fbShow();
            serviceWait(30);
        }
    }
    fbClear();
    fbShow();
}

// 7x7 sprites, bit 6 = left-most pixel
static const uint8_t PAC_OPEN[7]   = {0x1C, 0x3E, 0x78, 0x70, 0x78, 0x3E, 0x1C};
static const uint8_t PAC_CLOSED[7] = {0x1C, 0x3E, 0x7F, 0x7F, 0x7F, 0x3E, 0x1C};
static const uint8_t GHOST_A[7]    = {0x1C, 0x3E, 0x6B, 0x7F, 0x7F, 0x7F, 0x55};
static const uint8_t GHOST_B[7]    = {0x1C, 0x3E, 0x6B, 0x7F, 0x7F, 0x7F, 0x2A};

static void drawSprite(int x, const uint8_t* rows) {
    for (int r = 0; r < 7; r++)
        for (int c = 0; c < 7; c++)
            if (rows[r] & (0x40 >> c)) fbSet(x + c, r, true);
}

static void animPacman() {
    const int ghostGap = 11;
    for (int x = -8; x < W + ghostGap + 8; x++) {
        fbClear();
        for (int d = 2; d < W; d += 4)          // dots not eaten yet
            if (d > x + 3) fbSet(d, 3, true);
        drawSprite(x, (x & 2) ? PAC_OPEN : PAC_CLOSED);
        drawSprite(x - ghostGap, (x & 2) ? GHOST_A : GHOST_B);
        fbShow();
        serviceWait(55);
    }
    fbClear();
    fbShow();
}

void playAnim(uint8_t anim) {
    if (anim == ANIM_RANDOM) anim = random(ANIM_SPARKLE, ANIM_RANDOM);
    switch (anim) {
        case ANIM_SPARKLE: animSparkle(); break;
        case ANIM_WIPE:    animWipe();    break;
        case ANIM_RAIN:    animRain();    break;
        case ANIM_BOXES:   animBoxes();   break;
        case ANIM_PACMAN:  animPacman();  break;
        default: break;
    }
    fbClear();
    fbShow();
}

void spinnerFrame(uint16_t n) {
    const int len = 4, span = W - len;
    int p = n % (2 * span);
    if (p > span) p = 2 * span - p;
    fbClear();
    for (int i = 0; i < len; i++) {
        fbSet(p + i, 3, true);
        fbSet(p + i, 4, true);
    }
    fbShow();
}
