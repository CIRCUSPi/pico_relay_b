#include "indicator.h"

#include <Adafruit_NeoPixel.h>

#include "config.h"
#include "relay.h"

namespace {

Adafruit_NeoPixel gPixel(1, PIN_RGB, NEO_GRB + NEO_KHZ800);

// 記住已顯示的顏色：WS2812 更新會關中斷約 30us，沒變就不要重送。
// 0xFFFFFFFF 當「未知」用（實際顏色經亮度縮放後不會是這個值）。
const uint32_t kUnknown = 0xFFFFFFFFu;
uint32_t gShown = kUnknown;
uint32_t gBlipUntil = 0;
uint32_t gBeepUntil = 0;
bool gBeeping = false;

uint8_t gBright = LED_BRIGHTNESS;
bool gAuto = true;
uint32_t gColor = 0;  // 主機指定的 0xRRGGBB

const uint32_t kErrorBlipMs = 150;

uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return gPixel.Color((uint16_t)r * gBright / 255,
                      (uint16_t)g * gBright / 255,
                      (uint16_t)b * gBright / 255);
}

uint32_t rgb24(uint32_t c) {
  return rgb((uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
}

void show(uint32_t color) {
  if (color == gShown) {
    return;
  }
  gShown = color;
  gPixel.setPixelColor(0, color);
  gPixel.show();
}

}  // namespace

void Indicator::begin() {
  gPixel.begin();
  gPixel.clear();
  gPixel.show();

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  analogWriteFreq(BUZZER_HZ);
  analogWriteRange(255);

  if (BOOT_BEEP_MS > 0) {
    beep(BOOT_BEEP_MS);
  }
}

void Indicator::tick(uint32_t now, bool linked, bool fault) {
  if (gBeeping && (int32_t)(now - gBeepUntil) >= 0) {
    gBeeping = false;
    analogWrite(PIN_BUZZER, 0);
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
  }

  const bool phase = ((now / 400) & 1u) != 0;

  uint32_t color;
  if ((int32_t)(now - gBlipUntil) < 0) {
    color = rgb(255, 0, 0);                       // 指令錯誤
  } else if (fault) {
    color = phase ? rgb(255, 0, 0) : 0;           // 閒置看門狗已觸發
  } else if (!linked) {
    color = phase ? rgb(0, 0, 255) : 0;           // 等主機開啟 CDC
  } else if (!gAuto) {
    color = rgb24(gColor);                        // 主機用 LED 指定的顏色
  } else if (Relay::mask() != 0) {
    color = rgb(255, 160, 0);                     // 至少一路吸合
  } else {
    color = rgb(0, 255, 0);                       // 連線中、全部釋放
  }
  show(color);
}

void Indicator::blipError() {
  gBlipUntil = millis() + kErrorBlipMs;
}

void Indicator::beep(uint16_t ms) {
  gBeeping = true;
  gBeepUntil = millis() + ms;
  analogWrite(PIN_BUZZER, 128);
}

void Indicator::setColor(uint32_t c) {
  gAuto = false;
  gColor = c & 0xFFFFFFu;
}

void Indicator::setAuto() {
  gAuto = true;
}

bool Indicator::isAuto() {
  return gAuto;
}

uint32_t Indicator::color() {
  return gColor;
}

void Indicator::setBrightness(uint8_t level) {
  if (level == gBright) {
    return;
  }
  gBright = level;
  // 縮放後的顏色變了，但下一輪 tick 可能算出和上次相同的值（例如都被縮到 0），
  // 所以直接讓快取失效，強制重送一次。
  gShown = kUnknown;
}

uint8_t Indicator::brightness() {
  return gBright;
}
