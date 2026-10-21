#include "relay.h"

#include "config.h"

namespace {

// CH1 在 index 0。
const uint8_t kPins[Relay::COUNT] = {21, 20, 19, 18, 17, 16, 15, 14};

#if RELAY_ACTIVE_HIGH
constexpr uint8_t kLevelOn = HIGH;
constexpr uint8_t kLevelOff = LOW;
#else
constexpr uint8_t kLevelOn = LOW;
constexpr uint8_t kLevelOff = HIGH;
#endif

uint8_t gMask = 0;
bool gPulse[Relay::COUNT];
uint32_t gPulseEnd[Relay::COUNT];
uint16_t gStagger = RELAY_STAGGER_MS;

inline void drive(uint8_t i, bool on) {
  digitalWrite(kPins[i], on ? kLevelOn : kLevelOff);
}

// 錯開 ON 邊緣時的等待。期間仍跑 tick()，否則同一批指令裡的脈衝會被延後。
void spin(uint16_t ms) {
  const uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    Relay::tick(millis());
  }
}

}  // namespace

void Relay::begin() {
  for (uint8_t i = 0; i < COUNT; i++) {
    // 先寫好電位再切成輸出，避免 pinMode 瞬間的不定態讓繼電器誤動作。
    digitalWrite(kPins[i], kLevelOff);
    pinMode(kPins[i], OUTPUT);
    digitalWrite(kPins[i], kLevelOff);
    gPulse[i] = false;
    gPulseEnd[i] = 0;
  }
  gMask = 0;
}

void Relay::tick(uint32_t now) {
  for (uint8_t i = 0; i < COUNT; i++) {
    if (gPulse[i] && (int32_t)(now - gPulseEnd[i]) >= 0) {
      gPulse[i] = false;
      drive(i, false);
      gMask &= ~(uint8_t)(1u << i);
    }
  }
}

uint8_t Relay::mask() {
  return gMask;
}

bool Relay::get(uint8_t ch) {
  return (gMask >> (ch - 1)) & 1u;
}

void Relay::set(uint8_t ch, bool on) {
  const uint8_t i = ch - 1;
  gPulse[i] = false;
  drive(i, on);
  if (on) {
    gMask |= (uint8_t)(1u << i);
  } else {
    gMask &= ~(uint8_t)(1u << i);
  }
}

void Relay::setMask(uint8_t m) {
  const uint8_t toOff = (uint8_t)(gMask & ~m);
  const uint8_t toOn = (uint8_t)(m & ~gMask);

  // 釋放不會有湧浪，一次做完。
  for (uint8_t i = 0; i < COUNT; i++) {
    if (toOff & (1u << i)) {
      gPulse[i] = false;
      drive(i, false);
    }
  }
  gMask &= ~toOff;

  bool first = true;
  for (uint8_t i = 0; i < COUNT; i++) {
    if (!(toOn & (1u << i))) {
      continue;
    }
    if (!first && gStagger) {
      spin(gStagger);
    }
    first = false;
    gPulse[i] = false;
    drive(i, true);
    gMask |= (uint8_t)(1u << i);
  }
}

void Relay::allOff() {
  setMask(0);
}

void Relay::pulse(uint8_t ch, uint32_t ms) {
  const uint8_t i = ch - 1;
  set(ch, true);
  gPulseEnd[i] = millis() + ms;
  gPulse[i] = true;
}

bool Relay::pulsePending(uint8_t ch) {
  return gPulse[ch - 1];
}

void Relay::setStagger(uint16_t ms) {
  gStagger = ms;
}

uint16_t Relay::stagger() {
  return gStagger;
}
