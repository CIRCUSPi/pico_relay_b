// 板上 WS2812 狀態燈（GP13）與蜂鳴器（GP6）。
//
// 狀態燈優先序：錯誤閃紅 > 失效保護紅閃 > 未連線藍閃 > 有通道 ON 黃 > 待命綠。
#pragma once

#include <Arduino.h>

namespace Indicator {

void begin();

// linked：USB CDC 是否已連線；fault：閒置看門狗是否已觸發。
void tick(uint32_t now, bool linked, bool fault);

// 指令錯誤時閃一下紅燈（非阻塞）。
void blipError();

void beep(uint16_t ms);

}  // namespace Indicator
