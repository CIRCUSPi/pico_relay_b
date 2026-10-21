// Waveshare Pico-Relay-B (SKU 20218) — USB CDC 8 路繼電器控制器
//
// 主機（Orange Pi 5+）以 ASCII 行指令控制 8 路繼電器，一行指令一行回應。
// 協定與主機端範例：docs/PROTOCOL.md
//
// 迴圈裡沒有任何長時間阻塞：PULSE 用計時器、BEEP 用 PWM、WS2812 只在顏色
// 改變時更新。唯一的等待是 MASK/ALL 1 的 ON 錯開（最壞 7 x STAG，預設 35ms）。
#include <Arduino.h>

#include "config.h"
#include "indicator.h"
#include "proto.h"
#include "relay.h"

namespace {

bool gLinked = false;
bool gLinkDownPending = false;
uint32_t gLinkDownSince = 0;

}  // namespace

void setup() {
  // 第一件事就是把 8 路設成輸出並釋放，縮短開機期間的不定態。
  Relay::begin();
  Indicator::begin();

  // CDC 不理鮑率，主機想設多少都可以；1200bps 會觸發 core 的 BOOTSEL 重開。
  Serial.begin(115200);
  Proto::begin();

  // 韌體若卡死超過 HW_WDT_MS 就重開機；重開等於繼電器全部釋放，對治具是安全方向。
  rp2040.wdt_begin(HW_WDT_MS);
}

void loop() {
  const uint32_t now = millis();
  rp2040.wdt_reset();

  // Serial 的 bool 轉換就是 tud_cdc_connected()：主機開啟連接埠（拉 DTR）才為真。
  // 程式當掉、關閉連接埠或實體拔線都會變假，去抖動後把所有負載釋放。
  const bool linked = (bool)Serial;
  if (linked != gLinked) {
    if (linked) {
      gLinked = true;
      gLinkDownPending = false;
      Proto::onLinkUp();
    } else if (!gLinkDownPending) {
      gLinkDownPending = true;
      gLinkDownSince = now;
    } else if (now - gLinkDownSince >= LINK_LOSS_DEBOUNCE_MS) {
      gLinked = false;
      gLinkDownPending = false;
      Relay::allOff();
      Proto::onLinkDown();
    }
  } else {
    gLinkDownPending = false;
  }

  Proto::poll();
  Relay::tick(millis());
  Indicator::tick(millis(), gLinked, Proto::faulted());
}
