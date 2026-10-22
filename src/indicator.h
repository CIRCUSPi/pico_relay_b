// 板上 WS2812 狀態燈（GP13）與蜂鳴器（GP6）。
//
// 狀態燈優先序：錯誤閃紅 > 失效保護紅閃 > 未連線藍閃 > 主機指定顏色 > 有通道 ON 黃 > 待命綠。
// 主機可用 LED 指令指定顏色（治具拿燈當自己的狀態指示），但失效保護與未連線
// 的警示永遠蓋過指定顏色 —— 否則治具出事時燈還停在「測試中」的顏色。
#pragma once

#include <Arduino.h>

namespace Indicator {

void begin();

// linked：USB CDC 是否已連線；fault：閒置看門狗是否已觸發。
void tick(uint32_t now, bool linked, bool fault);

// 指令錯誤時閃一下紅燈（非阻塞）。
void blipError();

void beep(uint16_t ms);

// --- 主機指定顏色（LED / LEDB 指令）-----------------------------
// 指定 0xRRGGBB；0x000000 = 燈滅（仍算指定，不會回到自動顯示）。
void setColor(uint32_t rgb24);

// 回到依繼電器／連線狀態自動顯示。
void setAuto();

bool isAuto();
uint32_t color();  // 目前指定的顏色（0xRRGGBB），isAuto() 為真時無意義

// 整體亮度 0~255，自動顯示與指定顏色都受它影響。
void setBrightness(uint8_t level);
uint8_t brightness();

}  // namespace Indicator
