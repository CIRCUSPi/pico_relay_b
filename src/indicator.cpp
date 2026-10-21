#include "indicator.h"

#include <Adafruit_NeoPixel.h>

#include "config.h"
#include "relay.h"

namespace {

Adafruit_NeoPixel gPixel(1, PIN_RGB, NEO_GRB + NEO_KHZ800);

// 記住已顯示的顏色：WS2812 更新會關中斷約 30us，沒變就不要重送。
uint32_t gShown = 0xFFFFFFFFu;
uint32_t gBlipUntil = 0;
uint32_t gBeepUntil = 0;
bool gBeeping = false;

const uint32_t kErrorBlipMs = 150;

uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return gPixel.Color((uint16_t)r * LED_BRIGHTNESS / 255,
                      (uint16_t)g * LED_BRIGHTNESS / 255,
                      (uint16_t)b * LED_BRIGHTNESS / 255);
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
