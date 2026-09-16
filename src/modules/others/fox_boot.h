#ifndef __FOX_BOOT_H__
#define __FOX_BOOT_H__

#include <WString.h>
#include <stdint.h>

// Boot animation: a fox rears up, pounces onto a laptop and starts typing,
// spelling out "modded by Fall" as it works. Call once per frame with the
// milliseconds elapsed since the boot screen appeared; returns false once the
// sequence has played out.
bool drawFoxBootFrame(uint32_t elapsed);

// Total length of the sequence, so the boot loop knows how long to run.
uint32_t foxBootDurationMs();

// The caption as it stands at this point of the animation: "modded by Fall"
// spelled out one character per keystroke, with a blinking cursor.
String foxBootCaption(uint32_t elapsed);

#endif
