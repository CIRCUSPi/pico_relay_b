// 8 路繼電器狀態機：位元遮罩、脈衝計時、ON 邊緣錯開。
//
// 通道編號對外一律是 1~8（和板子絲印一致），遮罩的 bit0 對應 CH1。
#pragma once

#include <Arduino.h>

namespace Relay {

constexpr uint8_t COUNT = 8;

void begin();

// 要在主迴圈持續呼叫，負責讓 PULSE 到期的通道自動 OFF。
void tick(uint32_t now);

uint8_t mask();
bool get(uint8_t ch);

// 單路變更：沒有同時動作的問題，所以不錯開、立即生效。會取消該路的脈衝。
void set(uint8_t ch, bool on);

// 整組變更：先關要關的，再逐路開要開的（間隔 stagger()）。回來時已全部動作完成。
void setMask(uint8_t m);
void allOff();

// 讓該路 ON，ms 毫秒後由 tick() 自動 OFF。非阻塞，指令馬上回應。
void pulse(uint8_t ch, uint32_t ms);
bool pulsePending(uint8_t ch);

void setStagger(uint16_t ms);
uint16_t stagger();

}  // namespace Relay
