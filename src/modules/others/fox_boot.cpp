#include "fox_boot.h"
#include "core/display.h"
#include <globals.h>
#include <math.h>

#if defined(HAS_SCREEN)

// The fox is built from circles and triangles only. That is not a stylistic
// choice: a leaping pose needs the whole animal rotated, and a rotated circle
// is still a circle, while fillEllipse and drawXBitmap are stuck to the axes.
// Legs and tail are strings of overlapping circles for the same reason.
//
// Geometry is in a local space where the fox faces +x with the origin between
// its hips, then scaled to the screen and rotated by the pose. The numbers were
// worked out against a 240x135 panel and scale down from there.

namespace {

// RGB565. Kept independent of the theme: the point is that it reads as a fox.
constexpr uint16_t FOX_ORANGE = 0xE3E4;
constexpr uint16_t FOX_DARK = 0xA282;
constexpr uint16_t FOX_WHITE = 0xFFFF;
constexpr uint16_t FOX_BLACK = 0x0000;
constexpr uint16_t LAPTOP_BODY = 0x7BD0;
constexpr uint16_t LAPTOP_EDGE = 0x39E7;
constexpr uint16_t LAPTOP_SCREEN = 0x10C4;
constexpr uint16_t CODE_GREEN = 0x07EB;

// Sequence, in milliseconds from the first frame.
constexpr uint32_t T_WAIT = 600;  // banner alone
constexpr uint32_t T_REAR = 1600; // reared up on the hind legs
constexpr uint32_t T_LAND = 2700; // pounce lands at the keyboard
constexpr uint32_t T_TYPE = 3100; // paws start moving
constexpr uint32_t T_END = 13000; // whole sequence

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

void dot(const Pose &p, float lx, float ly, float r, uint16_t color) {
    int x, y;
    pt(p, lx, ly, x, y);
    int rr = (int)lroundf(r * gScale);
    tft.fillCircle(x, y, rr < 1 ? 1 : rr, color);
}

void tri(const Pose &p, float ax, float ay, float bx, float by, float cx, float cy, uint16_t color) {
    int x1, y1, x2, y2, x3, y3;
    pt(p, ax, ay, x1, y1);
    pt(p, bx, by, x2, y2);
    pt(p, cx, cy, x3, y3);
    tft.fillTriangle(x1, y1, x2, y2, x3, y3, color);
}

// A limb is a short run of overlapping circles, so it bends and rotates with
// the body without needing a rotated rectangle.
void limb(const Pose &p, float ax, float ay, float bx, float by, float r, uint16_t color) {
    for (int i = 0; i <= 4; i++) {
        float f = i / 4.0f;
        dot(p, ax + (bx - ax) * f, ay + (by - ay) * f, r, color);
    }
}

void drawHead(const Pose &p, float hx, float hy, bool blink) {
    dot(p, hx, hy, 7.5f, FOX_ORANGE);
    tri(p, hx - 8, hy - 4, hx - 9, hy - 15, hx - 1, hy - 7, FOX_ORANGE);
    tri(p, hx + 1, hy - 7, hx + 4, hy - 15, hx + 8, hy - 5, FOX_ORANGE);
    tri(p, hx - 6, hy - 6, hx - 7, hy - 12, hx - 3, hy - 8, FOX_DARK);
    tri(p, hx + 3, hy - 8, hx + 4, hy - 12, hx + 6, hy - 6, FOX_DARK);
    tri(p, hx + 4, hy - 2, hx + 16, hy + 6, hx + 3, hy + 7, FOX_WHITE);
    dot(p, hx + 15, hy + 5, 1.8f, FOX_BLACK);
    if (blink) {
        int x1, y1, x2, y2;
        pt(p, hx + 3, hy - 2, x1, y1);
        pt(p, hx + 6, hy - 2, x2, y2);
        tft.drawLine(x1, y1, x2, y2, FOX_BLACK);
    } else {
        dot(p, hx + 4, hy - 2, 1.7f, FOX_BLACK);
    }
}

void drawBody(const Pose &p, float fx, float fy, float bx, float by) {
    for (int i = 0; i <= 6; i++) {
        float f = i / 6.0f;
        dot(p, bx + (fx - bx) * f, by + (fy - by) * f, 8.0f + 2.0f * sinf(f * (float)PI), FOX_ORANGE);
    }
    dot(p, fx - 2, fy + 4, 5.0f, FOX_WHITE);
}

void drawTailStraight(const Pose &p) {
    const float px[4] = {-16, -23, -29, -34};
    const float py[4] = {-2, -5, -9, -14};
    for (int i = 0; i < 4; i++) dot(p, px[i], py[i], 7.0f - i * 0.7f, FOX_ORANGE);
    dot(p, -36, -16, 4.0f, FOX_WHITE);
}

// Up on the hind legs with the front paws tucked: the wind-up before a pounce.
void poseRear(const Pose &p) {
    drawTailStraight(p);
    drawBody(p, 10, -14, -10, 6);
    limb(p, -8, 8, -8, 19, 3.5f, FOX_ORANGE);
    limb(p, 4, -6, 11, -1, 2.6f, FOX_ORANGE);
    dot(p, -8, 20, 3.0f, FOX_WHITE);
    dot(p, 12, 0, 2.8f, FOX_WHITE);
    drawHead(p, 22, -22, false);
}

// Stretched out mid-air, legs trailing, nose leading.
void poseLeap(const Pose &p) {
    drawTailStraight(p);
    drawBody(p, 15, -3, -15, 2);
    limb(p, -11, 4, -22, 9, 2.8f, FOX_ORANGE);
    limb(p, 8, 5, 17, 9, 2.6f, FOX_ORANGE);
    dot(p, -23, 10, 2.6f, FOX_WHITE);
    dot(p, 18, 10, 2.6f, FOX_WHITE);
    drawHead(p, 22, -8, false);
}

// Sitting upright on the haunches, forepaws out on the keyboard.
void poseSit(const Pose &p, float pawA, float pawB, bool blink) {
    dot(p, -1, 1, 11.0f, FOX_ORANGE);
    const float tx[4] = {-11, -11, -4, 4};
    const float ty[4] = {7, 12, 14, 14};
    for (int i = 0; i < 4; i++) dot(p, tx[i], ty[i], 6.0f - i * 0.4f, FOX_ORANGE);
    dot(p, 11, 13, 4.5f, FOX_WHITE);

    for (int i = 0; i <= 5; i++) {
        float f = i / 5.0f;
        dot(p, 2 + 7 * f, -3 - 18 * f, 9.0f - 2.5f * f, FOX_ORANGE);
    }
    dot(p, 10, -13, 3.5f, FOX_WHITE);

    limb(p, 9, -17, 17, 2 + pawA, 2.6f, FOX_ORANGE);
    limb(p, 11, -15, 19, 4 + pawB, 2.6f, FOX_ORANGE);
    dot(p, 18, 2 + pawA, 2.6f, FOX_WHITE);
    dot(p, 20, 4 + pawB, 2.6f, FOX_WHITE);

    drawHead(p, 13, -29, blink);
}

// Laptop seen three-quarters from the front: a skewed base and a lid leaning
// back, so the screen faces the viewer and the fox works from behind it.
void drawLaptop(int x, int y, int codeLines) {
    int bw = (int)(54 * gScale);
    int bh = (int)(5 * gScale);
    int skew = (int)(6 * gScale);
    int lh = (int)(34 * gScale);
    int lw = bw - (int)(8 * gScale);

    tft.fillTriangle(x - bw / 2, y, x + bw / 2, y, x + bw / 2 - skew, y - bh, LAPTOP_BODY);
    tft.fillTriangle(x - bw / 2, y, x + bw / 2 - skew, y - bh, x - bw / 2 - skew, y - bh, LAPTOP_BODY);
    tft.fillRect(x - bw / 2, y, bw, (int)(3 * gScale) + 1, LAPTOP_EDGE);

    int tx = x - skew;
    int ty = y - bh - lh;
    tft.fillTriangle(x - lw / 2 - skew, y - bh, x + lw / 2 - skew, y - bh, tx + lw / 2, ty, LAPTOP_BODY);
    tft.fillTriangle(x - lw / 2 - skew, y - bh, tx + lw / 2, ty, tx - lw / 2, ty, LAPTOP_BODY);

    int ix = tx - lw / 2 + (int)(3 * gScale);
    int iy = ty + (int)(3 * gScale);
    int iw = lw - (int)(6 * gScale);
    int ih = lh - (int)(5 * gScale);
    tft.fillRect(ix, iy, iw, ih, LAPTOP_SCREEN);

    // Lines of "code" filling the screen as the fox works.
    for (int i = 0; i < codeLines; i++) {
        int ly = iy + (int)((5 + i * 5) * gScale);
        if (ly > iy + ih - 3) break;
        int w = (int)((10 + (i * 7) % 18) * gScale);
        tft.fillRect(
            ix + (int)(3 * gScale), ly, w, (int)(2 * gScale) < 1 ? 1 : (int)(2 * gScale), CODE_GREEN
        );
    }
}

} // namespace

uint32_t foxBootDurationMs() { return T_END; }

bool drawFoxBootFrame(uint32_t elapsed) {
    if (elapsed > T_END) return false;

    // Fit the scene to the panel: it was drawn for 240x135 and the smaller
    // screens in the family get everything shrunk by the same factor.
    gScale = (float)tftHeight / 135.0f;
    if (gScale > 1.0f) gScale = 1.0f;
    if (gScale < 0.55f) gScale = 0.55f;
    gGround = tftHeight - (int)(15 * gScale);

    int bandTop = (int)(48 * gScale) + 8;
    int lapX = tftWidth - (int)(66 * gScale);
    int startX = (int)(40 * gScale);
    int sitX = lapX - (int)(60 * gScale);

    // Where the fox is this frame, and in which pose.
    enum { NONE, REAR, LEAP, SIT } which = NONE;
    Pose p = {0, 0, 0};
    float pawA = 0, pawB = 0;
    bool blink = false;

    if (elapsed >= T_WAIT && elapsed < T_REAR) {
        float f = (float)(elapsed - T_WAIT) / (float)(T_REAR - T_WAIT);
        p = {(float)startX, (float)gGround - 8 * gScale - 6 * gScale * f, -0.35f * f};
        which = REAR;
    } else if (elapsed >= T_REAR && elapsed < T_LAND) {
        float f = (float)(elapsed - T_REAR) / (float)(T_LAND - T_REAR);
        p.x = startX + (sitX - startX) * f;
        p.y = gGround - 8 * gScale - sinf(f * (float)PI) * 46 * gScale;
        p.ang = -0.78f + 1.57f * f;
        which = LEAP;
    } else if (elapsed >= T_LAND) {
        float settle = (float)(elapsed - T_LAND) / 300.0f;
        if (settle > 1.0f) settle = 1.0f;
        bool typing = elapsed > T_TYPE;
        float phase = (float)(elapsed - T_LAND) / 140.0f;
        pawA = typing ? sinf(phase) * 2.5f : 0.0f;
        pawB = typing ? sinf(phase + 2.1f) * 2.5f : 0.0f;
        blink = ((elapsed / 200) % 19) == 0;
        p = {(float)sitX, (float)gGround - (10 + 6 * (1.0f - settle)) * gScale, 0.0f};
        which = SIT;
    }

    // Erase only where the fox was and where it is going, not the whole band.
    // A full-band repaint at 30 fps is a visible flicker on these panels, and
    // the laptop never moves.
    int boxW = (int)(96 * gScale);
    int boxH = (int)(78 * gScale);
    int boxX = (int)p.x - boxW / 2;
    int boxY = (int)p.y - (int)(56 * gScale);

    static int prevX = -1, prevY = -1, prevW = 0, prevH = 0;
    static uint32_t prevElapsed = 0xFFFFFFFF;
    if (elapsed < prevElapsed) { // first frame of a run
        tft.fillRect(0, bandTop, tftWidth, tftHeight - bandTop, bruceConfig.bgColor);
        prevX = -1;
    }
    prevElapsed = elapsed;

    if (prevX >= 0) {
        int x0 = prevX < boxX ? prevX : boxX;
        int y0 = prevY < boxY ? prevY : boxY;
        int x1 = (prevX + prevW) > (boxX + boxW) ? (prevX + prevW) : (boxX + boxW);
        int y1 = (prevY + prevH) > (boxY + boxH) ? (prevY + prevH) : (boxY + boxH);
        if (y0 < bandTop) y0 = bandTop;
        tft.fillRect(x0, y0, x1 - x0, y1 - y0, bruceConfig.bgColor);
    }

    int codeLines = 0;
    if (elapsed > T_TYPE) codeLines = (int)((elapsed - T_TYPE) / 500) % 6;
    drawLaptop(lapX, gGround, codeLines);

    switch (which) {
        case REAR: poseRear(p); break;
        case LEAP: poseLeap(p); break;
        case SIT: poseSit(p, pawA, pawB, blink); break;
        default: break;
    }

    if (which == NONE) {
        prevX = -1;
    } else {
        prevX = boxX;
        prevY = boxY;
        prevW = boxW;
        prevH = boxH;
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
    if (((elapsed / 350) % 2) == 0) out += "_";
    return out;
}

#endif // HAS_SCREEN
