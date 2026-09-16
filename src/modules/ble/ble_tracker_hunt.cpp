#include "ble_tracker_hunt.h"
#include "ble_common.h"
#include "core/display.h"
#include "core/utils.h"
#include "modules/others/audio.h"
#include <algorithm>
#include <globals.h>
#include <math.h>
#include <vector>

// Item trackers are identified from what they advertise, which is public: the
// Bluetooth SIG 16 bit UUID assignments and each network's documented payload
// header. Nothing here needs to connect to a tag or talk to a phone.
//
//   Apple Find My   manufacturer data 4C 00 (Apple) with payload type 0x12.
//                   Covers AirTags and every third party Find My accessory
//                   (Chipolo ONE Spot, Pebblebee, eufy SmartTrack, Atuvos and
//                   the AliExpress clones that ride the same network), since
//                   they all have to speak the same offline finding format.
//   Samsung         service UUID 0xFD5A / 0xFD59 (SmartThings Find), used by
//                   the SmartTag and SmartTag2.
//   Tile            service UUID 0xFEED, or 0xFEEC on unactivated units.
//   Google FMDN     service data under 0xFEAA whose frame type is 0x40 or
//                   0x41. The UUID alone is Eddystone, which any beacon may
//                   use, so the frame type is what makes it a tracker.
//   Generic         the cheap anti-lost tags: they advertise Immediate Alert
//                   (0x1802) or Link Loss (0x1803), or the 0xFFE0 service of
//                   the HM-10 style module most of them are built on, or they
//                   name themselves.
//
// The generic class is a heuristic and says so on screen: an anti-lost tag and
// a keyfinder keyring are the same advertisement. The named networks above are
// exact matches on their own protocol headers.

namespace {

enum TrackerKind {
    TRACKER_NONE = 0,
    TRACKER_FINDMY,
    TRACKER_SAMSUNG,
    TRACKER_TILE,
    TRACKER_GOOGLE,
    TRACKER_GENERIC,
};

const char *kindLabel(TrackerKind kind) {
    switch (kind) {
        case TRACKER_FINDMY: return "FindMy";
        case TRACKER_SAMSUNG: return "SmartTag";
        case TRACKER_TILE: return "Tile";
        case TRACKER_GOOGLE: return "Google";
        case TRACKER_GENERIC: return "Tag?";
        default: return "";
    }
}

struct TrackerHit {
    String address;
    String name;
    TrackerKind kind;
    int rssi;
};

// Names carried by the common no-brand tags. Matched case insensitively on a
// substring, which is why the list holds tag words rather than anything a
// phone or a headset would also call itself.
const char *const kGenericNames[] = {
    "itag",
    "i-tag",
    "anti-lost",
    "antilost",
    "nut",
    "trackerpa",
    "keyfinder",
    "key finder",
    "smart tag",
    "smarttag",
    "mitag",
    "chipolo",
    "pebblebee",
    "atuvos",
    "cube tracker",
    "trackr",
};

bool uuidPresent(const NimBLEAdvertisedDevice *device, const char *uuid16) {
    for (uint8_t i = 0; i < device->getServiceUUIDCount(); i++) {
        // toString() renders a 16 bit UUID inside its 128 bit base, so the
        // substring matches whichever form the stack hands back -- the same
        // way the rest of the BLE code tests for a UUID.
        if (device->getServiceUUID(i).toString().find(uuid16) != std::string::npos) return true;
    }
    return false;
}

bool isAppleFindMy(const NimBLEAdvertisedDevice *device) {
    if (!device->haveManufacturerData()) return false;
    for (uint8_t i = 0; i < device->getManufacturerDataCount(); i++) {
        std::string mfg = device->getManufacturerData(i);
        if (mfg.length() < 3) continue;
        const uint8_t *data = (const uint8_t *)mfg.data();
        // Company ID is little endian, so Apple's 0x004C is 4C 00.
        if (data[0] == 0x4C && data[1] == 0x00 && data[2] == 0x12) return true;
    }
    return false;
}

bool isGoogleFMDN(const NimBLEAdvertisedDevice *device) {
    if (!device->haveServiceData()) return false;
    for (uint8_t i = 0; i < device->getServiceDataCount(); i++) {
        if (device->getServiceDataUUID(i).toString().find("feaa") == std::string::npos) continue;
        std::string data = device->getServiceData(i);
        if (data.empty()) continue;
        uint8_t frame = (uint8_t)data[0];
        if (frame == 0x40 || frame == 0x41) return true;
    }
    return false;
}

bool nameLooksLikeTag(const String &name) {
    if (name.isEmpty()) return false;
    String lower = name;
    lower.toLowerCase();
    for (const char *candidate : kGenericNames) {
        if (lower.indexOf(candidate) >= 0) return true;
    }
    return false;
}

TrackerKind classify(const NimBLEAdvertisedDevice *device, const String &name) {
    if (isAppleFindMy(device)) return TRACKER_FINDMY;
    if (uuidPresent(device, "fd5a") || uuidPresent(device, "fd59")) return TRACKER_SAMSUNG;
    if (uuidPresent(device, "feed") || uuidPresent(device, "feec")) return TRACKER_TILE;
    if (isGoogleFMDN(device)) return TRACKER_GOOGLE;
    if (uuidPresent(device, "1802") || uuidPresent(device, "1803") || uuidPresent(device, "ffe0"))
        return TRACKER_GENERIC;
    if (nameLooksLikeTag(name)) return TRACKER_GENERIC;
    return TRACKER_NONE;
}

// Free space path loss, the usual BLE approximation: -59 dBm at one metre and
// an exponent for indoor clutter. It is worth an order of magnitude, not a
// measurement, which is why the screen prints it with a "~".
float estimateMeters(int rssi) {
    const float txPowerAt1m = -59.0f;
    const float pathLossExponent = 2.2f;
    return powf(10.0f, (txPowerAt1m - (float)rssi) / (10.0f * pathLossExponent));
}

std::vector<TrackerHit> scanForTrackers(uint32_t scanMs) {
    std::vector<TrackerHit> hits;
    pBLEScan->clearResults();

    BLEScanResults found = pBLEScan->getResults(scanMs, false);
    for (int i = 0; i < found.getCount(); i++) {
        const NimBLEAdvertisedDevice *device = found.getDevice(i);
        if (!device) continue;

        String name = device->getName().c_str();
        TrackerKind kind = classify(device, name);
        if (kind == TRACKER_NONE) continue;

        TrackerHit hit;
        hit.address = device->getAddress().toString().c_str();
        hit.name = name;
        hit.kind = kind;
        hit.rssi = device->getRSSI();
        hits.push_back(hit);
    }
    pBLEScan->clearResults();

    std::sort(hits.begin(), hits.end(), [](const TrackerHit &a, const TrackerHit &b) {
        return a.rssi > b.rssi;
    });
    return hits;
}

constexpr uint32_t kHuntBurstMs = 400;  // one scan burst between beeps
constexpr uint32_t kLostAfterMs = 6000; // silence after this long unheard
constexpr int kRssiFloor = -95;         // mapped to the slowest beep
constexpr int kRssiCeiling = -40;       // mapped to the fastest

// Reads one tracker's current signal. Returns false when the tag did not
// answer this burst, which is normal: these things advertise every couple of
// seconds, not continuously.
bool pollTracker(const String &address, int &rssi) {
    pBLEScan->clearResults();
    BLEScanResults found = pBLEScan->getResults(kHuntBurstMs, false);
    bool seen = false;
    for (int i = 0; i < found.getCount(); i++) {
        const NimBLEAdvertisedDevice *device = found.getDevice(i);
        if (!device) continue;
        if (address != device->getAddress().toString().c_str()) continue;
        rssi = device->getRSSI();
        seen = true;
        break;
    }
    pBLEScan->clearResults();
    return seen;
}

// The border and the identity of the tag are painted once. Redrawing the whole
// screen on every burst made it flicker at four frames a second, which is hard
// to read exactly when you are walking and watching the bar.
constexpr int kRowRssi = 66;
constexpr int kRowDistance = 88;
constexpr int kRowBar = 104;

void drawHuntStatic(const TrackerHit &target) {
    drawMainBorderWithTitle("TRACKER HUNT");
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(FP);

    String header = String(kindLabel(target.kind));
    if (!target.name.isEmpty()) header += " " + target.name;
    tft.setCursor(8, 34);
    tft.print(header.substring(0, (tftWidth - 16) / 6));

    tft.setCursor(8, 48);
    tft.print(target.address);

    tft.setCursor(8, tftHeight - 18);
    if (bruceConfig.soundEnabled) tft.print("Esc: back");
    else tft.print("Esc: back  (sound off)");
}

void drawHuntStatus(int rssi, int bestRssi, bool lost) {
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);

    // The number people actually steer by.
    tft.setTextSize(FM);
    tft.setCursor(8, kRowRssi);
    if (lost) tft.print("--  dBm ");
    else tft.printf("%-4d dBm", rssi);

    tft.setTextSize(FP);
    tft.setCursor(8, kRowDistance);
    if (lost) {
        tft.print("lost - walk around    ");
    } else {
        float meters = estimateMeters(rssi);
        if (meters < 10.0f) tft.printf("~%.1f m est   best %-4d", meters, bestRssi);
        else tft.printf("~%-3d m est   best %-4d", (int)meters, bestRssi);
    }

    // Signal bar: empty at the floor, full when the tag is on top of you.
    int barW = tftWidth - 16;
    int barH = 10;
    int filled = 0;
    if (!lost) {
        int clamped = constrain(rssi, kRssiFloor, kRssiCeiling);
        filled = map(clamped, kRssiFloor, kRssiCeiling, 0, barW - 2);
    }
    tft.drawRect(8, kRowBar, barW, barH, bruceConfig.priColor);
    tft.fillRect(9, kRowBar + 1, barW - 2, barH - 2, bruceConfig.bgColor);
    if (filled > 0) tft.fillRect(9, kRowBar + 1, filled, barH - 2, bruceConfig.priColor);
}

// Geiger counter behaviour: the interval between clicks collapses and the
// pitch rises as the signal comes up.
void beepFor(int rssi) {
    int clamped = constrain(rssi, kRssiFloor, kRssiCeiling);
    uint16_t frequency = map(clamped, kRssiFloor, kRssiCeiling, 900, 2800);
    _tone(frequency, 35);
}

uint32_t beepIntervalFor(int rssi) {
    int clamped = constrain(rssi, kRssiFloor, kRssiCeiling);
    return (uint32_t)map(clamped, kRssiFloor, kRssiCeiling, 1100, 90);
}

void huntTracker(const TrackerHit &target) {
    int rssi = target.rssi;
    int bestRssi = target.rssi;
    // Advertisements bounce several dB between bursts; smoothing keeps the bar
    // and the beeps from flapping while you move.
    float smoothed = (float)target.rssi;
    uint32_t lastSeen = millis();
    uint32_t lastBeep = 0;
    bool lost = false;

    drawHuntStatic(target);
    drawHuntStatus(rssi, bestRssi, false);

    while (!check(EscPress) && !returnToMenu) {
        int fresh = 0;
        if (pollTracker(target.address, fresh)) {
            smoothed = (smoothed * 0.6f) + ((float)fresh * 0.4f);
            rssi = (int)lroundf(smoothed);
            if (fresh > bestRssi) bestRssi = fresh;
            lastSeen = millis();
            lost = false;
        } else if (millis() - lastSeen > kLostAfterMs) {
            lost = true;
        }

        drawHuntStatus(rssi, bestRssi, lost);

        if (!lost && millis() - lastBeep >= beepIntervalFor(rssi)) {
            beepFor(rssi);
            lastBeep = millis();
        }

        delay(20);
    }
}

} // namespace

void bleTrackerHunt() {
    displayTextLine("Scanning..");

    if (!ble_scan_setup() || pBLEScan == nullptr) {
        displayError("Failed to init BLE scan");
        return;
    }

    bool running = true;
    while (running && !returnToMenu) {
        drawMainBorderWithTitle("TRACKER DETECTOR");
        displayTextLine("Scanning..");

        std::vector<TrackerHit> hits = scanForTrackers(6000);

        // -1 rescan, -2 leave, -3 the user pressed Esc out of the list.
        std::vector<Option> menu;
        int chosen = -3;
        for (size_t i = 0; i < hits.size(); i++) {
            String label = String("[") + kindLabel(hits[i].kind) + "] " + String(hits[i].rssi) + " ";
            label += hits[i].name.isEmpty() ? hits[i].address : hits[i].name;
            menu.emplace_back(label.c_str(), [&chosen, i]() { chosen = (int)i; });
        }
        if (hits.empty()) menu.emplace_back("No trackers found", [&chosen]() { chosen = -1; });
        menu.emplace_back("Rescan", [&chosen]() { chosen = -1; });
        menu.emplace_back("Back", [&chosen]() { chosen = -2; });

        loopOptions(menu, MENU_TYPE_SUBMENU, "Trackers", 0, false);

        if (chosen == -2 || chosen == -3 || returnToMenu) running = false;
        else if (chosen >= 0 && chosen < (int)hits.size()) huntTracker(hits[chosen]);
    }

    if (pBLEScan) pBLEScan->stop();
    stopBLEStack();
}
