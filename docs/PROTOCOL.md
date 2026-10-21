# Pico-Relay-B USB CDC 控制協定 v1.0

適用韌體：`PICO-RELAY-B FW1.0.0`
硬體：Waveshare Pico-Relay-B (SKU 20218) + Raspberry Pi Pico
用途：Orange Pi 5+ 等主機透過 USB 控制 8 路繼電器（治具 I/O）

---

## 1. 傳輸層

| 項目 | 值 |
|---|---|
| 介面 | USB 2.0 full-speed，CDC-ACM（虛擬序列埠） |
| 主機裝置節點 | Linux `/dev/ttyACM*`、Windows `COMx` |
| USB VID:PID | `2E8A:000A` |
| USB manufacturer / product | `Waveshare` / `Pico-Relay-B` |
| USB serial number | Pico flash unique ID（每片不同，`ID` 指令會回報同一組） |
| 鮑率 / 格式 | CDC 不使用實體 UART，鮑率任意；建議填 115200、8N1、無流控 |

**兩個必守的規則：**

1. **不要用 1200 bps 開啟連接埠。** arduino-pico 把「1200 bps + 放掉 DTR」當作進入 BOOTSEL 燒錄模式的信號，裝置會立刻重開並從 USB 上消失。
2. **主機必須拉起 DTR。** 韌體以 CDC 的連線狀態（DTR）判斷主機是否在線：
   - DTR 未拉起時韌體**不會輸出任何回應**（CDC 未連線，寫入被丟棄）。
   - pyserial `serial.Serial(port, 115200)` 預設就會拉起 DTR，不需額外處理；若你手動設過 `ser.dtr = False` 要記得改回來。

---

## 2. 行格式

### 2.1 主機 → 裝置（指令）

```
CMD [arg1] [arg2] <LF>
```

| 規則 | 說明 |
|---|---|
| 結尾 | `LF`（`\n`）或 `CRLF`（`\r\n`）都接受 |
| 分隔 | 一個或多個空白／Tab |
| 大小寫 | 指令不分大小寫（`on 1` = `ON 1`） |
| 長度上限 | 48 字元（不含結尾換行），超過回 `ERR 4 TOO_LONG` |
| 空行 | **忽略，不回應**（不會打亂一問一答的節奏） |
| `#` 開頭 | 視為主機註解，**忽略，不回應**（腳本可直接灌註解） |
| 參數過多 | 回 `ERR 2 BAD_ARG` |

### 2.2 裝置 → 主機（回應）

每一個有效指令**恰好產生一行回應**，以 `CRLF` 結尾：

```
OK [值]            成功
ERR <碼> <名稱>     失敗
```

此外裝置會主動送出以 `#` 開頭的**資訊行**（見 §6）。
**主機端的解析規則很簡單：讀到 `#` 開頭就當 log 記下來並繼續讀下一行，第一行非 `#` 的就是本次指令的回應。**

### 2.3 繼電器狀態遮罩 `hh`

8 路狀態用兩位大寫 16 進位表示，**bit0 = CH1 … bit7 = CH8**，1 = 吸合（NO 導通）。

```
0x01 = CH1 ON
0x0F = CH1~CH4 ON
0xFF = 全部 ON
```

> 所有「改變或查詢繼電器狀態」的指令都回 `OK hh`，也就是**每下一次指令就拿到完整的 8 路狀態**，主機不需要自己記帳，也能順便驗證上一步有沒有生效。

---

## 3. 指令表

| 指令 | 參數範圍 | 回應 | 說明 |
|---|---|---|---|
| `ON n` | n = 1…8 | `OK hh` | CH n 吸合 |
| `OFF n` | n = 1…8 | `OK hh` | CH n 釋放（同時取消該路脈衝） |
| `SET n s` | s = 0 \| 1 | `OK hh` | 指定 CH n 狀態 |
| `TOG n` | n = 1…8 | `OK hh` | CH n 反相 |
| `MASK hh` | `00`…`FF`（可寫 `0x0F`） | `OK hh` | 一次設定 8 路 |
| `ALL s` | s = 0 \| 1 | `OK hh` | 全開 / 全關 |
| `GET` | — | `OK hh` | 讀取 8 路狀態 |
| `PULSE n ms` | n = 1…8, ms = 10…60000 | `OK hh` | CH n 吸合 ms 毫秒後自動釋放（非阻塞，指令立刻回應） |
| `WDT` | — | `OK sec` | 查詢閒置看門狗秒數 |
| `WDT sec` | 0…3600 | `OK sec` | 設定閒置看門狗，`0` = 關閉（預設） |
| `STAG` | — | `OK ms` | 查詢多路 ON 的錯開間隔 |
| `STAG ms` | 0…50 | `OK ms` | 設定錯開間隔（預設 5） |
| `BEEP` | — | `OK` | 蜂鳴 100ms |
| `BEEP ms` | 10…5000 | `OK` | 蜂鳴指定毫秒（非阻塞） |
| `ID` | — | `OK <model> <ch> FW<ver> <uid>` | 例：`OK PICO-RELAY-B 8 FW1.0.0 E66141040384A21F` |
| `PING` | — | `OK PONG` | 存活確認／重新同步 |
| `RST` | — | `OK hh` | 全部釋放、取消所有脈衝、`STAG`/`WDT` 回預設值 |
| `HELP` | — | 多行 `#` + `OK` | 指令摘要（給人看的） |

### 3.1 範例

```
>> ID
<< OK PICO-RELAY-B 8 FW1.0.0 E66141040384A21F
>> ON 1
<< OK 01
>> ON 3
<< OK 05
>> GET
<< OK 05
>> MASK 0F
<< OK 0F
>> TOG 1
<< OK 0E
>> PULSE 8 500
<< OK 8E              ← CH8 已吸合，500ms 後自動釋放（沒有第二行回應）
>> ALL 0
<< OK 00
>> OPEN 1
<< ERR 1 UNKNOWN_CMD
>> ON 9
<< ERR 3 RANGE
```

---

## 4. 錯誤碼

| 碼 | 名稱 | 原因 |
|---|---|---|
| 1 | `UNKNOWN_CMD` | 指令不認識 |
| 2 | `BAD_ARG` | 參數數量不符（太少、太多） |
| 3 | `RANGE` | 參數格式錯誤或超出範圍（通道、毫秒、遮罩） |
| 4 | `TOO_LONG` | 指令行超過 48 字元 |
| 5 | `CHECKSUM` | 帶了 `*XX` 但格式錯或不符 |

錯誤時繼電器狀態**完全不變**（參數在動作之前就驗完了），板上狀態燈會閃一下紅燈。

---

## 5. 失效保護（治具安全）

三層互相獨立，任何一層觸發都是「所有繼電器釋放」：

| 層 | 觸發條件 | 行為 | 解除 |
|---|---|---|---|
| USB 斷線 | DTR 放掉（主機程式結束／關閉連接埠／當掉）或實體拔線，持續 200ms | 全部釋放；狀態燈藍閃 | 主機重新開啟連接埠（會收到 `# READY`） |
| 閒置看門狗 | `WDT sec` 設定後，sec 秒內沒收到**任何一行**資料 | 全部釋放；送出 `# FAILSAFE IDLE`；狀態燈紅閃 | 收到任何一行（含 `PING` 或註解行）即解除 |
| 硬體看門狗 | 韌體本身卡死超過 4 秒 | RP2040 重開機 → 繼電器全部釋放 | 自動（重開後回到待命） |

**閒置看門狗是 opt-in 的 dead-man switch**：預設關閉。治具長時間通電的情境建議開啟，例如

```
>> WDT 5
<< OK 5
```

之後主機必須至少每 5 秒下一個指令（沒事就 `PING`），否則所有負載自動斷電。
注意：觸發後**不會自動恢復原本的通道狀態**，主機要自己重新設定。

---

## 6. 資訊行（非同步事件）

| 行 | 時機 |
|---|---|
| `# READY PICO-RELAY-B FW1.0.0 <uid>` | USB CDC 連線建立（主機開啟連接埠）時 |
| `# FAILSAFE IDLE` | 閒置看門狗觸發、所有通道已被釋放 |
| `# …`（`HELP` 的輸出） | 只在下 `HELP` 時出現 |

資訊行不帶檢查碼，也**不計入**指令的回應。主機忽略即可。

> ⚠ **不要把 `# READY` 當成連線觸發條件。**
> 實機測試（Windows 11 + pyserial 3.5）：剛開啟連接埠時，韌體在 DTR 拉起後數微秒內就送出這一行，
> 但主機端 CDC 驅動在開埠流程中把它丟掉了（開埠後 `in_waiting` 持續為 0，重現 2/2 次）；
> 相對地，對**已開啟**的連接埠手動 `dtr = False → True`，則每次都能立刻收到完整的 `# READY`。
> 所以主機端的正確做法是：開埠後自己下一次 `PING` 或 `ID` 來同步（§9 範例就是這樣寫），
> 把 `# READY` 當成除錯用的輔助訊息而已。

---

## 7. 時序

| 項目 | 數值 |
|---|---|
| 單路指令（`ON`/`OFF`/`SET`/`TOG`/`GET`/`PULSE`） | 實測 round-trip 1.5～2.4ms（Orange Pi 等級的主機應相近） |
| `MASK` / `ALL 1` 多路同時開 | ON 的邊緣逐路錯開 `STAG`（預設 5ms），回應時間 ≈ round-trip + (開啟路數 − 1) × `STAG`，最壞 7 × 5 = 35ms。實測一次開 4 路為 15.6ms |
| 整組釋放（`ALL 0`） | 1.5ms（釋放沒有湧浪問題，不錯開） |
| 回應語意 | 收到 `OK` 時 GPIO 已全部驅動完成 |
| 繼電器機械動作 | 吸合約 5～10ms、釋放約 5ms（規格值，不含在上面的回應時間裡）。若主機要確保負載真的切換，收到 `OK` 後再等 10ms |
| `PULSE` 精度 | ±1ms（實測 `PULSE 3 500`：200ms 時仍吸合、650ms 時已釋放） |
| 回應缺失 | 若 1 秒內沒有任何回應，視為異常：送空行 + `PING`，丟棄舊資料直到讀到 `OK PONG` 為止（重新同步） |

### 為什麼要錯開（STAG）

8 路 SRD-05VDC 線圈各約 70mA，全開接近 600mA。八路同時吸合的湧浪會把 USB 供電的 VSYS 拉下來，可能讓 Pico 重開。錯開 ON 邊緣可大幅降低峰值電流。
若治具用外部供電的 USB hub 且追求最短切換時間，可以 `STAG 0` 關掉；反之供電吃緊可調大到 `STAG 20`。

> ⚠ USB 描述元宣告 500mA。8 路全開已經超過 USB 2.0 單埠的供電上限，**長時間多路同時吸合請用帶外部供電的 USB hub**，或改用板上的外部電源端子供電。

---

## 8. 選用的檢查碼 `*XX`

指令結尾可以加上 `*XX`，`XX` 是 `*` 之前**所有字元**的 XOR（2 位大寫 16 進位）：

- 帶了就驗，不符回 `ERR 5 CHECKSUM`（繼電器不動作）
- 沒帶就不驗 —— 所以用終端機手打除錯時完全不受影響
- 指令帶檢查碼時，**回應也會帶**（對回應本體做 XOR），主機兩個方向都能驗

```
>> ON 1*10
<< OK 01*25
>> MASK 0F*42
<< OK 0F*52
>> PING*10
<< OK PONG*32
>> ON 1*11
<< ERR 5 CHECKSUM
```

Python 計算方式：

```python
def csum(s: str) -> str:
    v = 0
    for c in s.encode():
        v ^= c
    return f"{v:02X}"
```

---

## 9. 主機端範例（Python / pyserial）

```python
import serial


class PicoRelay:
    """Pico-Relay-B USB CDC 控制。一行指令一行回應，'#' 開頭的行是 log。"""

    def __init__(self, port="/dev/ttyACM0", timeout=1.0):
        self.ser = serial.Serial(port, 115200, timeout=timeout)  # 不可用 1200
        self.ser.reset_input_buffer()
        self.ping()                      # 開場先同步

    def cmd(self, line: str) -> str:
        self.ser.write((line + "\n").encode())
        while True:
            resp = self.ser.readline().decode("ascii", "replace").strip()
            if not resp:
                raise TimeoutError(f"no reply to {line!r}")
            if resp.startswith("#"):     # 資訊行，記 log 後繼續讀
                print("[dev]", resp)
                continue
            if resp.startswith("ERR"):
                raise RuntimeError(f"{line!r} -> {resp}")
            return resp[3:] if len(resp) > 3 else ""   # 去掉 "OK "

    # --- 便利包裝 -------------------------------------------------
    def ping(self):              return self.cmd("PING")
    def ident(self):             return self.cmd("ID")
    def get(self) -> int:        return int(self.cmd("GET"), 16)
    def set(self, ch, on) -> int:
        return int(self.cmd(f"SET {ch} {1 if on else 0}"), 16)
    def mask(self, m) -> int:    return int(self.cmd(f"MASK {m:02X}"), 16)
    def all_off(self) -> int:    return int(self.cmd("ALL 0"), 16)
    def pulse(self, ch, ms):     return int(self.cmd(f"PULSE {ch} {ms}"), 16)
    def watchdog(self, sec):     return int(self.cmd(f"WDT {sec}"))
    def beep(self, ms=100):      self.cmd(f"BEEP {ms}")

    def close(self):
        self.all_off()
        self.ser.close()        # 關閉 = 放掉 DTR，韌體也會自己全部釋放


if __name__ == "__main__":
    r = PicoRelay()
    print(r.ident())
    r.watchdog(10)              # 主機當掉 10 秒後自動全斷
    r.mask(0x0F)
    assert r.get() == 0x0F
    r.close()
```

### 固定裝置節點（多台治具並接）

`ID` 回報的 uid 就是 USB serial number，可以直接寫 udev rule：

```
SUBSYSTEM=="tty", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000a", \
  ATTRS{serial}=="E66141040384A21F", SYMLINK+="pico-relay-0"
```

---

## 10. 板上狀態指示

| 狀態燈（GP13 WS2812） | 意義 |
|---|---|
| 藍色閃爍 | USB CDC 未連線（等主機開啟連接埠） |
| 綠色恆亮 | 已連線、8 路全部釋放 |
| 黃色恆亮 | 已連線、至少一路吸合 |
| 紅色閃一下（150ms） | 上一個指令回了 `ERR` |
| 紅色閃爍 | 閒置看門狗已觸發（通道已被強制釋放） |

蜂鳴器（GP6）只有兩種情形會響：開機自檢 60ms，以及主機下 `BEEP`。

---

## 11. 版本與相容性

- 本文件對應 `FW1.0.0`，可由 `ID` 指令確認。
- 相容性約定：同一個 major 版本只會**新增**指令或**新增** `#` 資訊行，不會改變既有指令的語法與回應格式。主機端解析時請忽略不認識的 `#` 行。

### 驗證狀態（2026-10-21）

`FW1.0.0` 已在 Pico-Relay-B (SKU 20218) + Raspberry Pi Pico（uid `E6605838836C7639`）實機跑完整輪測試，
66 項檢查全數通過，涵蓋：

- 完整指令表、大小寫、多餘空白、嚴格參數解析（`ON 3x` / `ON 9` / 溢位皆回 `ERR`，且繼電器狀態不變）
- CH1~CH8 逐路開關兩輪、`MASK` / `ALL` 整組控制、ON 邊緣錯開時間
- `PULSE` 自動釋放與 `OFF` 取消脈衝
- 選用檢查碼的正例與反例
- 閒置看門狗觸發（送出 `# FAILSAFE IDLE` 並釋放全部通道）與任意指令解除
- USB 斷線保護（關閉連接埠後所有通道釋放）
- 狀態燈五種狀態與蜂鳴器（目視／聽測確認）

另有主機端行為一則已記在 §6：剛開埠時的 `# READY` 會被主機 CDC 驅動丟掉，不可當連線觸發條件。
