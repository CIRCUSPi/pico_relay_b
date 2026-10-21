# Pico-Relay-B USB CDC 繼電器控制器

Waveshare **Pico-Relay-B (SKU 20218)** + Raspberry Pi Pico 的通用 8 路繼電器韌體。
主機（Orange Pi 5+）透過 USB CDC 下 ASCII 行指令控制，供電與通訊都走同一條 USB。

- 協定文件：[docs/PROTOCOL.md](docs/PROTOCOL.md)
- 框架：Arduino（earlephilhower arduino-pico core）+ PlatformIO

```
>> ON 1            >> MASK 0F         >> PULSE 3 500     >> WDT 5
<< OK 01           << OK 0F           << OK 04           << OK 5
```

一行指令一行回應，回應一律帶 8 路狀態（2 位 hex，bit0 = CH1），主機不必自己記帳。

## 硬體對應

| 項目 | 腳位 |
|---|---|
| CH1 … CH8 | GP21, GP20, GP19, GP18, GP17, GP16, GP15, GP14（拉 HIGH 吸合，已實機確認） |
| RGB LED (WS2812) | GP13（`NEO_GRB`，已實機確認顏色） |
| 蜂鳴器 | GP6（2.7kHz 硬體 PWM 驅動，不佔 PIO） |

⚠ 8 路 SRD-05VDC 線圈各約 70mA，全開接近 600mA，超過 USB 2.0 單埠上限。韌體已內建
ON 邊緣錯開（預設 5ms，`STAG` 可調）降低湧浪 —— 實測由 PC 的 USB 埠直接供電、8 路全開 1.2 秒
未發生重開，但**長時間多路同時吸合仍建議用帶外部供電的 USB hub**。

## 建置與燒錄

```bash
pio run                 # 編譯
pio run -t upload       # 燒錄（第一次請按住 BOOTSEL 再插 USB）
pio device monitor      # 可直接手打指令除錯，例如 ON 1 / GET / HELP
```

## 實機回歸測試

```bash
pip install pyserial
python tools/hwtest.py COM5          # Linux: python tools/hwtest.py /dev/ttyACM0
```

約 70 秒跑完 66 項檢查：完整指令表、錯誤處理、`PULSE` 計時、檢查碼、ON 錯開時間、
三層失效保護，以及狀態燈與蜂鳴器（這兩項要人眼／耳朵確認，腳本會用三短音提示你開始看板子）。
全部通過時 exit code 為 0，可以直接接進 CI 或產線流程。

⚠ 測試會真的驅動 8 路繼電器（包含一次 8 路全開），執行前確認負載可安全反覆通斷。

## 治具安全機制

| 機制 | 行為 |
|---|---|
| USB 斷線（DTR 放掉或拔線，去抖 200ms） | 所有繼電器釋放 |
| 閒置看門狗 `WDT <sec>`（預設關閉） | sec 秒沒收到任何資料 → 全部釋放 + `# FAILSAFE IDLE` |
| RP2040 硬體看門狗（4 秒） | 韌體卡死 → 重開機 → 繼電器全部釋放 |

主機端務必注意兩件事（細節見協定文件 §1）：**不要用 1200 bps 開啟連接埠**（會進 BOOTSEL），
**必須拉起 DTR**（pyserial 預設如此，否則收不到任何回應）。

## 原始碼結構

| 檔案 | 內容 |
|---|---|
| [src/config.h](src/config.h) | 腳位對應與所有可調參數（驅動極性、錯開間隔、亮度、超時） |
| [src/relay.cpp](src/relay.cpp) | 8 路狀態機：遮罩、脈衝計時、ON 邊緣錯開 |
| [src/proto.cpp](src/proto.cpp) | 行指令解析、分派、錯誤碼、閒置看門狗 |
| [src/indicator.cpp](src/indicator.cpp) | WS2812 狀態燈與蜂鳴器 |
| [src/main.cpp](src/main.cpp) | 主迴圈、USB 連線狀態偵測、硬體看門狗 |
| [tools/hwtest.py](tools/hwtest.py) | 實機回歸測試（pyserial） |

狀態燈：藍閃＝未連線、綠＝連線待命、黃＝有通道吸合、紅閃一下＝指令錯誤、紅閃＝看門狗觸發。

## 狀態

`FW1.0.0` 已實機驗證（2026-10-21，uid `E6605838836C7639`）：66 項檢查全過，涵蓋完整指令表、
錯誤處理、`PULSE` 計時、檢查碼、三層失效保護、狀態燈與蜂鳴器。實測單路指令 round-trip 0.3～0.5ms、8 路全開 34.9ms（含 7 段 5ms 錯開）。
細節與主機端注意事項見 [docs/PROTOCOL.md §11](docs/PROTOCOL.md)。
