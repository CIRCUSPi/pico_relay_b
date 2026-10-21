// 全專案的硬體對應與可調參數。
//
// 硬體：Waveshare Pico-Relay-B (SKU 20218) + Raspberry Pi Pico
//   CH1~CH8 -> GP21, GP20, GP19, GP18, GP17, GP16, GP15, GP14
//   RGB LED (WS2812) -> GP13
//   Buzzer -> GP6
#pragma once

#include <Arduino.h>

#define FW_VERSION "1.0.0"
#define DEV_MODEL  "PICO-RELAY-B"

// 繼電器經光耦合 + 電晶體驅動，GPIO 拉 HIGH 吸合。
// 若換到驅動極性相反的板子，只改這裡（不必動 relay.cpp）。
#ifndef RELAY_ACTIVE_HIGH
#define RELAY_ACTIVE_HIGH 1
#endif

// 多路同時變更時，ON 的邊緣逐路間隔這麼多毫秒才動作。
// 8 路線圈一起吸合的湧浪會把 USB 供電的 VSYS 拉下來讓 Pico 重開，所以錯開。
// 代價是 MASK/ALL 1 最壞情況多 7 x RELAY_STAGGER_MS 才回應。可用 STAG 指令改。
#ifndef RELAY_STAGGER_MS
#define RELAY_STAGGER_MS 5
#endif
#define RELAY_STAGGER_MAX_MS 50

#define PIN_RGB    13
#define PIN_BUZZER 6

// 蜂鳴器用硬體 PWM 驅動（不佔 PIO，避免和 WS2812 搶資源），
// 被動式壓電片需要這個頻率；主動式蜂鳴器給方波也會響。
#define BUZZER_HZ      2700
#define BUZZER_MAX_MS  5000
#define BOOT_BEEP_MS   60    // 0 = 開機不叫

// WS2812 很刺眼，治具上只要看得出顏色就好。
#define LED_BRIGHTNESS 24    // 0~255

// 指令行的長度上限（含指令與參數，不含結尾換行）。
#define PROTO_MAX_LINE 48

// 開機時的閒置看門狗秒數；0 = 關閉，由主機用 WDT 指令自行開啟。
#define PROTO_IDLE_TIMEOUT_SEC 0
#define PROTO_IDLE_TIMEOUT_MAX 3600

// USB CDC 斷線（DTR 放掉或實體拔線）後多久判定為斷線並全部 OFF。
#define LINK_LOSS_DEBOUNCE_MS 200

// RP2040 硬體看門狗：韌體若卡死超過這個時間就重開機（重開＝繼電器全部釋放，
// 對治具來說是安全方向）。單路指令不會阻塞這麼久，MASK 最壞也只有 35ms。
#define HW_WDT_MS 4000
