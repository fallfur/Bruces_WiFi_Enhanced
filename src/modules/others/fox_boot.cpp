#include "fox_boot.h"
#include "core/display.h"
#include <globals.h>
#include <math.h>

#if defined(HAS_SCREEN)

// The fox is built from circles and triangles only. That is not a style
// choice: the pounce needs the whole animal rotated, and a rotated circle is
// still a circle, while fillEllipse and drawXBitmap are stuck to the axes.
// Legs and tail are strings of overlapping circles for the same reason, which
// also lets them bend between poses without a second set of artwork.
//
// Geometry is in a local space where the fox faces +x with the origin between
// its hips, laid out against a 240x135 panel and scaled from there.
//
// Every frame is composed in an off-screen sprite and pushed in one go. Drawing
// straight to the panel meant erasing and repainting in front of the viewer,
// which flickered, and any part of a pose that reached outside the erased box
// -- the tail in mid-leap, the ears when reared up -- stayed on screen as a
// smear. The drawing helpers are templates so the same code can still paint
// directly to the panel on a board too small to allocate the sprite.

namespace {

// RGB565, kept independent of the theme: the point is that it reads as a fox.
constexpr uint16_t FOX_ORANGE = 0xEB40;
constexpr uint16_t FOX_DARK = 0x9A00;
constexpr uint16_t FOX_WHITE = 0xFFFF;
constexpr uint16_t FOX_BLACK = 0x0000;
constexpr uint16_t LAPTOP_BODY = 0xC618;
constexpr uint16_t LAPTOP_EDGE = 0x8410;
constexpr uint16_t LAPTOP_SCREEN = 0x0841;
constexpr uint16_t CODE_GREEN = 0x07EB;

// Sequence, in milliseconds from the first frame.
constexpr uint32_t T_WAIT = 600;   // banner alone
constexpr uint32_t T_REAR = 1700;  // reared up on the hind legs
constexpr uint32_t T_LAND = 2900;  // pounce lands at the keyboard
constexpr uint32_t T_TYPE = 3300;  // paws start moving
constexpr uint32_t T_CURL = 10500; // work done, curls up
constexpr uint32_t T_SLEEP = 11300;
constexpr uint32_t T_END = 17000;

const char kCaption[] = "modded by Fall";
constexpr uint32_t kCaptionCharMs = 200;

float gScale = 1.0f;
int gGround = 0;

struct Pose {
    float x, y, ang;
};

void pt(const Pose &p, float lx, float ly, int &ox, int &oy) {
    float sx = lx * gScale;
    float sy = ly * gScale;
    float ca = cosf(p.ang);
    float sa = sinf(p.ang);
    ox = (int)lroundf(p.x + sx * ca - sy * sa);
    oy = (int)lroundf(p.y + sx * sa + sy * ca);
}

template <typename G> void dot(G &g, const Pose &p, float lx, float ly, float r, uint16_t color) {
    int x, y;
    pt(p, lx, ly, x, y);
    int rr = (int)lroundf(r * gScale);
    g.fillCircle(x, y, rr < 1 ? 1 : rr, color);
}

template <typename G>
void tri(G &g, const Pose &p, float ax, float ay, float bx, float by, float cx, float cy, uint16_t color) {
    int x1, y1, x2, y2, x3, y3;
    pt(p, ax, ay, x1, y1);
    pt(p, bx, by, x2, y2);
    pt(p, cx, cy, x3, y3);
    g.fillTriangle(x1, y1, x2, y2, x3, y3, color);
}

// A limb is a short run of overlapping circles, so it rotates with the body.
template <typename G>
void limb(G &g, const Pose &p, float ax, float ay, float bx, float by, float r, uint16_t color) {
    for (int i = 0; i <= 4; i++) {
        float f = i / 4.0f;
        dot(g, p, ax + (bx - ax) * f, ay + (by - ay) * f, r, color);
    }
}

template <typename G> void drawHead(G &g, const Pose &p, float hx, float hy, bool eyeShut) {
    dot(g, p, hx, hy, 7.5f, FOX_ORANGE);
    tri(g, p, hx - 8, hy - 4, hx - 9, hy - 15, hx - 1, hy - 7, FOX_ORANGE);
    tri(g, p, hx + 1, hy - 7, hx + 4, hy - 15, hx + 8, hy - 5, FOX_ORANGE);
    tri(g, p, hx - 6, hy - 6, hx - 7, hy - 12, hx - 3, hy - 8, FOX_DARK);
    tri(g, p, hx + 3, hy - 8, hx + 4, hy - 12, hx + 6, hy - 6, FOX_DARK);
    tri(g, p, hx + 4, hy - 2, hx + 16, hy + 6, hx + 3, hy + 7, FOX_WHITE);
    dot(g, p, hx + 15, hy + 5, 1.8f, FOX_BLACK);
    if (eyeShut) {
        int x1, y1, x2, y2;
        pt(p, hx + 2, hy - 2, x1, y1);
        pt(p, hx + 6, hy - 2, x2, y2);
        g.drawLine(x1, y1, x2, y2, FOX_BLACK);
    } else {
        dot(g, p, hx + 4, hy - 2, 1.7f, FOX_BLACK);
    }
}

template <typename G> void drawBody(G &g, const Pose &p, float fx, float fy, float bx, float by) {
    for (int i = 0; i <= 6; i++) {
        float f = i / 6.0f;
        dot(g, p, bx + (fx - bx) * f, by + (fy - by) * f, 8.0f + 2.0f * sinf(f * (float)PI), FOX_ORANGE);
    }
}

template <typename G> void drawTailStraight(G &g, const Pose &p) {
    const float px[4] = {-16, -23, -29, -34};
    const float py[4] = {-2, -5, -9, -14};
    for (int i = 0; i < 4; i++) dot(g, p, px[i], py[i], 7.0f - i * 0.7f, FOX_ORANGE);
    dot(g, p, -36, -16, 3.5f, FOX_WHITE);
}

// Up on the hind legs with the front paws tucked: the wind-up before a pounce.
template <typename G> void poseRear(G &g, const Pose &p) {
    drawTailStraight(g, p);
    drawBody(g, p, 10, -14, -10, 6);
    limb(g, p, -8, 8, -8, 19, 3.5f, FOX_ORANGE);
    limb(g, p, 4, -6, 11, -1, 2.6f, FOX_ORANGE);
    dot(g, p, -8, 20, 3.0f, FOX_WHITE);
    dot(g, p, 12, 0, 2.6f, FOX_WHITE);
    drawHead(g, p, 22, -22, false);
}

// Stretched out mid-air, legs trailing, nose leading.
template <typename G> void poseLeap(G &g, const Pose &p) {
    drawTailStraight(g, p);
    drawBody(g, p, 15, -3, -15, 2);
    limb(g, p, -11, 4, -22, 9, 2.8f, FOX_ORANGE);
    limb(g, p, 8, 5, 17, 9, 2.6f, FOX_ORANGE);
    dot(g, p, -23, 10, 2.4f, FOX_WHITE);
    dot(g, p, 18, 10, 2.4f, FOX_WHITE);
    drawHead(g, p, 22, -8, false);
}

// Sitting upright on the haunches, forepaws out on the keyboard.
template <typename G> void poseSit(G &g, const Pose &p, float pawA, float pawB, bool blink) {
    dot(g, p, -1, 1, 11.0f, FOX_ORANGE);
    const float tx[4] = {-11, -11, -4, 4};
    const float ty[4] = {7, 12, 14, 14};
    for (int i = 0; i < 4; i++) dot(g, p, tx[i], ty[i], 6.0f - i * 0.4f, FOX_ORANGE);
    dot(g, p, 11, 13, 4.0f, FOX_WHITE);

    for (int i = 0; i <= 5; i++) {
        float f = i / 5.0f;
        dot(g, p, 2 + 7 * f, -3 - 18 * f, 9.0f - 2.5f * f, FOX_ORANGE);
    }
    dot(g, p, 10, -13, 3.0f, FOX_WHITE);

    limb(g, p, 9, -17, 17, 2 + pawA, 2.6f, FOX_ORANGE);
    limb(g, p, 11, -15, 19, 4 + pawB, 2.6f, FOX_ORANGE);
    dot(g, p, 18, 2 + pawA, 2.4f, FOX_WHITE);
    dot(g, p, 20, 4 + pawB, 2.4f, FOX_WHITE);

    drawHead(g, p, 13, -29, blink);
}

// Curled up asleep, nose under the tail.
template <typename G> void poseCurl(G &g, const Pose &p, float tuck) {
    dot(g, p, 0, -2, 13.0f, FOX_ORANGE);
    dot(g, p, -6, 4, 9.0f, FOX_ORANGE);
    dot(g, p, 7, 3, 8.0f, FOX_ORANGE);

    // Tail wrapped around the front.
    const float tx[5] = {-13, -10, -2, 7, 13};
    const float ty[5] = {2, 9, 13, 12, 7};
    for (int i = 0; i < 5; i++) dot(g, p, tx[i], ty[i], 6.0f - i * 0.3f, FOX_ORANGE);
    dot(g, p, 16, 2, 4.5f, FOX_WHITE);

    // Head tucked in, sinking as it settles.
    float hy = -10 + 3 * tuck;
    dot(g, p, 9, hy, 7.0f, FOX_ORANGE);
    tri(g, p, 2, hy - 4, 0, hy - 13, 8, hy - 6, FOX_ORANGE);
    tri(g, p, 9, hy - 6, 12, hy - 13, 15, hy - 4, FOX_ORANGE);
    tri(g, p, 3, hy - 5, 2, hy - 10, 6, hy - 7, FOX_DARK);
    tri(g, p, 11, hy - 6, 12, hy - 11, 14, hy - 5, FOX_DARK);
    tri(g, p, 12, hy + 1, 19, hy + 5, 12, hy + 6, FOX_WHITE);
    dot(g, p, 18, hy + 4, 1.6f, FOX_BLACK);
    int x1, y1, x2, y2;
    pt(p, 11, hy - 1, x1, y1);
    pt(p, 15, hy - 1, x2, y2);
    g.drawLine(x1, y1, x2, y2, FOX_BLACK);
}

// Laptop seen three-quarters from the front: a skewed base and a lid leaning
// back, so the screen faces the viewer and the fox works from behind it.
template <typename G> void drawLaptop(G &g, int x, int y, int codeLines) {
    int bw = (int)(54 * gScale);
    int bh = (int)(5 * gScale);
    int skew = (int)(6 * gScale);
    int lh = (int)(32 * gScale);
    int lw = bw - (int)(8 * gScale);

    g.fillTriangle(x - bw / 2, y, x + bw / 2, y, x + bw / 2 - skew, y - bh, LAPTOP_BODY);
    g.fillTriangle(x - bw / 2, y, x + bw / 2 - skew, y - bh, x - bw / 2 - skew, y - bh, LAPTOP_BODY);
    g.fillRect(x - bw / 2, y, bw, (int)(3 * gScale) + 1, LAPTOP_EDGE);

    int tx = x - skew;
    int ty = y - bh - lh;
    g.fillTriangle(x - lw / 2 - skew, y - bh, x + lw / 2 - skew, y - bh, tx + lw / 2, ty, LAPTOP_BODY);
    g.fillTriangle(x - lw / 2 - skew, y - bh, tx + lw / 2, ty, tx - lw / 2, ty, LAPTOP_BODY);

    int ix = tx - lw / 2 + (int)(3 * gScale);
    int iy = ty + (int)(3 * gScale);
    int iw = lw - (int)(6 * gScale);
    int ih = lh - (int)(5 * gScale);
    g.fillRect(ix, iy, iw, ih, LAPTOP_SCREEN);

    for (int i = 0; i < codeLines; i++) {
        int ly = iy + (int)((5 + i * 5) * gScale);
        if (ly > iy + ih - 3) break;
        int w = (int)((10 + (i * 7) % 18) * gScale);
        int h = (int)(2 * gScale);
        g.fillRect(ix + (int)(3 * gScale), ly, w, h < 1 ? 1 : h, CODE_GREEN);
    }
}

// Sleep marks drifting up and to the right, largest last.
template <typename G> void drawSnooze(G &g, int x, int y, uint32_t elapsed) {
    for (int i = 0; i < 3; i++) {
        uint32_t phase = (elapsed + i * 700) % 2100;
        float f = phase / 2100.0f;
        int zx = x + (int)((6 + 14 * f) * gScale);
        int zy = y - (int)((10 + 26 * f) * gScale);
        g.setTextSize(f > 0.6f ? 2 : 1);
        g.setTextColor(FOX_WHITE);
        g.drawString(f > 0.3f ? "Z" : "z", zx, zy);
    }
    g.setTextSize(1);
}

template <typename G> void drawScene(G &g, uint32_t elapsed, int lapX, int startX, int sitX, int ground) {
    int codeLines = 0;
    if (elapsed > T_TYPE) {
        uint32_t typed = (elapsed < T_CURL ? elapsed : T_CURL) - T_TYPE;
        codeLines = (int)(typed / 500) % 6;
    }
    drawLaptop(g, lapX, ground, codeLines);

    if (elapsed >= T_WAIT && elapsed < T_REAR) {
        float f = (float)(elapsed - T_WAIT) / (float)(T_REAR - T_WAIT);
        Pose p = {(float)startX, ground - (8 + 6 * f) * gScale, -0.35f * f};
        poseRear(g, p);
    } else if (elapsed >= T_REAR && elapsed < T_LAND) {
        float f = (float)(elapsed - T_REAR) / (float)(T_LAND - T_REAR);
        Pose p;
        p.x = startX + (sitX - startX) * f;
        p.y = ground - 8 * gScale - sinf(f * (float)PI) * 46 * gScale;
        p.ang = -0.78f + 1.57f * f;
        poseLeap(g, p);
    } else if (elapsed >= T_LAND && elapsed < T_CURL) {
        float settle = (float)(elapsed - T_LAND) / 300.0f;
        if (settle > 1.0f) settle = 1.0f;
        bool typing = elapsed > T_TYPE;
        float phase = (float)(elapsed - T_LAND) / 140.0f;
        float pawA = typing ? sinf(phase) * 2.5f : 0.0f;
        float pawB = typing ? sinf(phase + 2.1f) * 2.5f : 0.0f;
        bool blink = ((elapsed / 200) % 19) == 0;
        Pose p = {(float)sitX, ground - (10 + 6 * (1.0f - settle)) * gScale, 0.0f};
        poseSit(g, p, pawA, pawB, blink);
    } else if (elapsed >= T_CURL) {
        // Sinks from sitting into a curl, then breathes.
        float f = (float)(elapsed - T_CURL) / (float)(T_SLEEP - T_CURL);
        if (f > 1.0f) f = 1.0f;
        float breathe = elapsed > T_SLEEP ? sinf((float)(elapsed - T_SLEEP) / 700.0f) * 0.8f : 0.0f;
        Pose p = {(float)sitX + 4 * gScale, ground - (14 - 4 * f + breathe) * gScale, 0.0f};
        poseCurl(g, p, f);
        if (elapsed > T_SLEEP) drawSnooze(g, (int)p.x + (int)(10 * gScale), (int)p.y, elapsed - T_SLEEP);
    }
}

} // namespace

uint32_t foxBootDurationMs() { return T_END; }

bool drawFoxBootFrame(uint32_t elapsed) {
    if (elapsed > T_END) return false;

    // The scene was laid out for 240x135; smaller panels get the same scene
    // shrunk rather than a cropped one.
    gScale = (float)tftHeight / 135.0f;
    if (gScale > 1.0f) gScale = 1.0f;
    if (gScale < 0.55f) gScale = 0.55f;

    int bandTop = (int)(50 * gScale) + 6;
    int bandH = tftHeight - bandTop;
    gGround = bandH - (int)(10 * gScale); // ground, in band coordinates
    int lapX = tftWidth - (int)(64 * gScale);
    int startX = (int)(38 * gScale);
    int sitX = lapX - (int)(58 * gScale);

    static bool spriteTried = false;
    static bool spriteOk = false;
    if (!spriteTried) {
        spriteTried = true;
        sprite.deleteSprite();
        sprite.createSprite(tftWidth, bandH);
        spriteOk = sprite.width() >= tftWidth && sprite.height() >= bandH;
        if (!spriteOk) sprite.deleteSprite();
    }

    if (spriteOk) {
        sprite.fillScreen(bruceConfig.bgColor);
        drawScene(sprite, elapsed, lapX, startX, sitX, gGround);
        sprite.pushSprite(0, bandTop);
    } else {
        // No room for the sprite: paint the band directly. This flickers, but
        // a boot screen that flickers beats one that does not draw at all.
        tft.fillRect(0, bandTop, tftWidth, bandH, bruceConfig.bgColor);
        drawScene(tft, elapsed, lapX, startX, sitX, gGround + bandTop);
    }

    if (elapsed >= T_END) {
        sprite.deleteSprite();
        spriteOk = false;
    }
    return true;
}

// The caption spells itself out under the version while the fox types, one
// character per keystroke, with a cursor blinking after it.
String foxBootCaption(uint32_t elapsed) {
    if (elapsed <= T_TYPE) return "";
    size_t shown = (elapsed - T_TYPE) / kCaptionCharMs;
    size_t total = sizeof(kCaption) - 1;
    if (shown > total) shown = total;
    String out = String(kCaption).substring(0, shown);
    // The cursor stops blinking once the fox has stopped typing.
    if (elapsed < T_CURL && ((elapsed / 350) % 2) == 0) out += "_";
    return out;
}

#endif // HAS_SCREEN
