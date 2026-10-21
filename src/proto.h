// ASCII 行指令協定的解析與分派。詳見 docs/PROTOCOL.md。
#pragma once

#include <Arduino.h>

namespace Proto {

void begin();

// 主迴圈呼叫：讀 USB CDC、組行、分派，並檢查閒置看門狗。
void poll();

// 主機連上／斷線時由 main 通知，用來重置閒置計時與狀態。
void onLinkUp();
void onLinkDown();

// 閒置看門狗是否已觸發（觸發時已經把所有通道關掉）。
bool faulted();

// 非同步事件通知（以 '#' 開頭的資訊行，主機可忽略）。
void notice(const char *text);

}  // namespace Proto
