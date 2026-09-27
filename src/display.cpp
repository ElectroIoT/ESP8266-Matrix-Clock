#include "display.h"
#include "config.h"
#include "settings.h"
#include <SPI.h>

int W = 32;
static MD_MAX72XX* mx = nullptr;   // created in displayBegin(), once the module count is known
static uint8_t fb[MAX_W];          // one byte per column, bit 0 = top row

// =================================================================================
// Low level
// =================================================================================

void displayBegin() {
    static const MD_MAX72XX::moduleType_t TYPES[] = {MD_MAX72XX::FC16_HW, MD_MAX72XX::GENERIC_HW,
                                                     MD_MAX72XX::PAROLA_HW, MD_MAX72XX::ICSTATION_HW};
    W = cfg.modules * 8;
    mx = new MD_MAX72XX(TYPES[cfg.hwType], PIN_CS, cfg.modules);
    mx->begin();
    mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);   // we push whole frames ourselves
    fbClear();
    fbShow();
}

void displayIntensity(uint8_t level) {
    mx->control(MD_MAX72XX::INTENSITY, level > 15 ? 15 : level);
}

void displayPower(bool on) {
    mx->control(MD_MAX72XX::SHUTDOWN, on ? MD_MAX72XX::OFF : MD_MAX72XX::ON);
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
        uint8_t v = cfg.flipV ? reverseBits(fb[x]) : fb[x];
        mx->setColumn(cfg.flipH ? x : (W - 1 - x), v);   // library column 0 is the right-most one
    }
    mx->update();
}

// =================================================================================
// Text
// =================================================================================

static uint8_t glyph(char c, uint8_t* cols) {
    uint8_t ch = (uint8_t)c;
    if (ch < 32 || ch > 126) ch = '?';
    return mx->getChar(ch, 8, cols);
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
// Clock face: HH:MM with animated digits, blinking colon and a seconds bar
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
// Layout, relative to the left edge of the face; the face is centred on the display.
static const int     FACE_HM_W = 28;                             // H H : M M
static const int     FACE_HMS_W = 45;                            // H H : M M : S S (needs 6+ modules)
static const int     DIGIT_X[6] = {0, 6, 17, 23, 34, 40};
static const int     COLON_X[2] = {13, 30};                      // 2 px wide each
static const uint8_t FRAMES = 12;                   // digit change length: 12 frames x 30 ms

static bool digitPx(uint8_t d, int r, int c) {      // pixel of digit d; false outside the 5x7 cell
    if (d > 9 || r < 0 || r > 6 || c < 0 || c > 4) return false;
    return pgm_read_byte(&DIGITS[d][r]) & (0x10 >> c);
}

// Draw digit d into the 5x7 cell whose top-left is x, shifted by (ox, oy) and
// clipped to the cell, so neighbours and the seconds bar (row 7) stay untouched.
static void drawDigit(int x, uint8_t d, int ox = 0, int oy = 0) {
    for (int r = 0; r < 7; r++)
        for (int c = 0; c < 5; c++)
            if (digitPx(d, r - oy, c - ox)) fbSet(x + c, r, true);
}

// Digit squeezed to h rows (0..7) around the middle row, for the flip effect.
static void drawDigitSquashed(int x, uint8_t d, int h) {
    if (h <= 0) return;
    int top = 3 - h / 2;
    for (int r = 0; r < h; r++)
        for (int c = 0; c < 5; c++)
            if (digitPx(d, r * 7 / h, c)) fbSet(x + c, top + r, true);
}

// Order in which the 35 cell pixels switch over in the dissolve effect:
// (i * 23 + k) mod 35 visits every value once because 23 and 35 are coprime.
static uint8_t dissolveOrder(int slot, int r, int c) {
    return ((r * 5 + c) * 23 + slot * 11) % 35;
}

void ClockFace::reset() {
    fresh = true;
}

void ClockFace::demo() {
    demoPending = true;
}

void ClockFace::tick() {
    for (int i = 0; i < SLOTS; i++)
        if (phase[i]) phase[i]--;
}

void ClockFace::drawSlot(int i, int x) {
    if (!phase[i]) {
        drawDigit(x, cur[i]);
        return;
    }
    const int p = FRAMES - phase[i];   // 0 .. FRAMES-1
    const uint8_t a = prev[i], b = cur[i];
    switch (style) {
        case ROLL_UP: {
            int s = p * 8 / FRAMES;
            drawDigit(x, a, 0, -s);
            drawDigit(x, b, 0, 8 - s);
            break;
        }
        case ROLL_DISSOLVE: {
            int k = p * 35 / FRAMES;
            for (int r = 0; r < 7; r++)
                for (int c = 0; c < 5; c++)
                    if (dissolveOrder(i, r, c) < k ? digitPx(b, r, c) : digitPx(a, r, c)) fbSet(x + c, r, true);
            break;
        }
        case ROLL_SLIDE: {
            int s = p * 6 / FRAMES;
            drawDigit(x, a, -s, 0);
            drawDigit(x, b, 6 - s, 0);
            break;
        }
        case ROLL_FLIP: {
            const int half = FRAMES / 2;
            if (p < half) drawDigitSquashed(x, a, 7 - p * 7 / half);
            else          drawDigitSquashed(x, b, (p - half + 1) * 7 / half);
            break;
        }
        case ROLL_DROP: {
            static const int8_t BOUNCE[FRAMES] = {-8, -6, -4, -2, 0, -2, -3, -2, 0, -1, 0, 0};
            drawDigit(x, a, 0, p * p / 2);   // old digit falls away, accelerating
            drawDigit(x, b, 0, BOUNCE[p]);   // new one drops in and bounces
            break;
        }
        default: {                           // ROLL_DOWN
            int s = p * 8 / FRAMES;
            drawDigit(x, a, 0, s);
            drawDigit(x, b, 0, s - 8);
            break;
        }
    }
}

void ClockFace::draw(const struct tm& t, uint16_t ms, int dx) {
    uint8_t h = t.tm_hour;
    if (!cfg.fmt24) {
        h %= 12;
        if (h == 0) h = 12;
    }
    const bool secs = cfg.showSecs && W >= FACE_HMS_W + 2;   // seconds need 6+ modules
    const int  slots = secs ? 6 : 4;
    const int  x0 = dx + (W - (secs ? FACE_HMS_W : FACE_HM_W)) / 2;
    uint8_t want[SLOTS] = {(uint8_t)(h / 10), (uint8_t)(h % 10), (uint8_t)(t.tm_min / 10), (uint8_t)(t.tm_min % 10),
                           (uint8_t)(t.tm_sec / 10), (uint8_t)(t.tm_sec % 10)};
    if (want[0] == 0 && !cfg.leadingZero) want[0] = BLANK;

    bool changed = false;
    for (int i = 0; i < slots; i++) {
        if (want[i] == cur[i] && !fresh) continue;
        prev[i]  = cur[i];
        cur[i]   = want[i];
        phase[i] = (fresh || cfg.roll == ROLL_NONE) ? 0 : FRAMES;
        changed |= !fresh;
    }
    fresh = false;

    if (demoPending) {
        demoPending = false;
        if (cfg.roll != ROLL_NONE) {
            for (int i = 0; i < slots; i++) {
                prev[i]  = cur[i] == 8 ? 0 : 8;
                phase[i] = FRAMES;
            }
            changed = true;
        }
    }

    if (changed) {
        static const uint8_t PICK[] = {ROLL_DOWN, ROLL_UP, ROLL_DISSOLVE, ROLL_SLIDE, ROLL_FLIP, ROLL_DROP};
        style = cfg.roll == ROLL_RANDOM ? PICK[random(sizeof(PICK))] : cfg.roll;
    }

    for (int i = 0; i < slots; i++) drawSlot(i, x0 + DIGIT_X[i]);

    if (!cfg.blinkColon || ms < 500) {
        for (int k = 0; k < (secs ? 2 : 1); k++)
            for (int c = 0; c < 2; c++)
                for (int y : {1, 2, 4, 5}) fbSet(x0 + COLON_X[k] + c, y, true);
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

// 3x5 digits for two-digit module numbers, one byte per row, bit 2 = left-most pixel
static const uint8_t TINY[10][5] PROGMEM = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 3, 1, 7}, {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 2, 2, 2}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
};

static void drawTiny(int x, int y, uint8_t d) {
    for (int r = 0; r < 5; r++) {
        uint8_t bits = pgm_read_byte(&TINY[d][r]);
        for (int c = 0; c < 3; c++)
            if (bits & (4 >> c)) fbSet(x + c, y + r, true);
    }
}

void showModuleNumbers() {
    fbClear();
    for (int m = 0; m < W / 8; m++) {
        int x = m * 8, n = m + 1;
        if (n < 10) {
            drawDigit(x + 1, n);
        } else {
            drawTiny(x, 1, n / 10);
            drawTiny(x + 4, 1, n % 10);
        }
        fbSet(x, 7, true);        // module edges on the bottom row
        fbSet(x + 7, 7, true);
    }
    fbShow();
    serviceWait(5000);
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
