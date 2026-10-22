"""Pico-Relay-B 實機回歸測試。

用法：
    python tools/hwtest.py [port]        # port 預設 COM5，Linux 用 /dev/ttyACM0

需要 pyserial：
    pip install pyserial

測試內容（實測約 52 秒，前半自動驗證、後半需要人眼／耳朵確認）：
    Phase 0  識別、PING、選用檢查碼的正反例
    Phase 1  CH1~CH8 逐路開關兩輪（每路吸合 350ms）
    Phase 2  MASK / ALL 整組控制，順便量 ON 邊緣錯開時間
    Phase 3  PULSE 自動釋放
    Phase 4  蜂鳴器：三短音 + 一長音
    Phase 5  狀態燈五種狀態 + LED/LEDB 顏色控制 + 閒置看門狗 + USB 斷線保護

⚠ 本測試會真的驅動 8 路繼電器（包含一次 8 路全開）。執行前請確認接在
  繼電器上的負載可以安全地反覆通斷，或先把負載拆掉只留空接點。

協定規格見 docs/PROTOCOL.md。
"""
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM5"
T0 = time.perf_counter()
CHECKS = []
NOTES = []


def log(msg):
    print(f"[{time.perf_counter() - T0:6.2f}s] {msg}", flush=True)


def check(label, got, want):
    ok = got == want
    CHECKS.append((label, ok, got, want))
    if not ok:
        log(f"  !! FAIL {label}: got {got!r} want {want!r}")
    return ok


class Dev:
    def __init__(self, port):
        self.port = port
        self.notices = []
        self.open()

    def open(self):
        for attempt in range(10):
            try:
                self.ser = serial.Serial(self.port, 115200, timeout=0.5)
                return
            except Exception:
                if attempt == 9:
                    raise
                time.sleep(0.5)

    def _line(self, timeout):
        # 每次指派 timeout 都會讓 pyserial 重設連接埠（Windows 上單次多 1~2ms），
        # 所以只在真的改變時才寫入。
        if self.ser.timeout != timeout:
            self.ser.timeout = timeout
        return self.ser.readline().decode("ascii", "replace").strip()

    def drain(self, secs):
        """等 secs 秒，期間收集裝置主動送出的行。"""
        end = time.perf_counter() + secs
        out = []
        while True:
            left = end - time.perf_counter()
            if left <= 0:
                return out
            ln = self._line(max(0.05, left))
            if ln:
                out.append(ln)
                if ln.startswith("#"):
                    self.notices.append(ln)
                    log(f"  <dev> {ln}")

    def cmd(self, line, timeout=1.5):
        t = time.perf_counter()
        self.ser.write((line + "\n").encode())
        while True:
            ln = self._line(timeout)
            if ln == "":
                raise TimeoutError(f"no reply to {line!r}")
            if ln.startswith("#"):
                self.notices.append(ln)
                log(f"  <dev> {ln}")
                continue
            return ln, (time.perf_counter() - t) * 1000


d = Dev(PORT)
log(f"已開啟 {PORT}（DTR 拉起）")
d.drain(0.6)

# ---------- Phase 0：識別與協定基本功 ----------
log("=== Phase 0：識別 ===")
r, ms = d.cmd("ID")
log(f"ID    -> {r}  ({ms:.1f}ms)")
check("ID 格式", r.startswith("OK PICO-RELAY-B 8 FW1.1.0 "), True)
r, ms = d.cmd("PING")
log(f"PING  -> {r}  ({ms:.1f}ms round-trip)")
check("PING", r, "OK PONG")
check("ALL 0 歸零", d.cmd("ALL 0")[0], "OK 00")
r, _ = d.cmd("ON 1*10")
log(f"檢查碼 ON 1*10 -> {r}")
check("檢查碼正確", r, "OK 01*25")
r, _ = d.cmd("ON 1*11")
log(f"檢查碼 ON 1*11 -> {r}")
check("檢查碼錯誤", r, "ERR 5 CHECKSUM")
check("ALL 0", d.cmd("ALL 0")[0], "OK 00")

# ---------- 開始信號：三短音，請看板子 ----------
log("=== 三短音＝請開始看板子 ===")
for _ in range(3):
    d.cmd("BEEP 80")
    time.sleep(0.25)
time.sleep(1.5)

# ---------- Phase 1：CH1~CH8 輪流 ----------
log("=== Phase 1：CH1~CH8 輪流開啟（兩輪）===")
for rnd in (1, 2):
    for ch in range(1, 9):
        want = f"OK {1 << (ch - 1):02X}"
        r, ms = d.cmd(f"ON {ch}")
        check(f"第{rnd}輪 ON {ch}", r, want)
        log(f"CH{ch} ON  -> {r} ({ms:.1f}ms)")
        time.sleep(0.35)
        check(f"第{rnd}輪 OFF {ch}", d.cmd(f"OFF {ch}")[0], "OK 00")
        time.sleep(0.1)
    time.sleep(0.5)

# ---------- Phase 2：整組控制與 ON 錯開 ----------
log("=== Phase 2：整組控制 ===")
check("MASK 0F", d.cmd("MASK 0F")[0], "OK 0F")
log("CH1~CH4 ON")
time.sleep(0.9)
check("MASK F0", d.cmd("MASK F0")[0], "OK F0")
log("CH5~CH8 ON")
time.sleep(0.9)
stag = int(d.cmd("STAG")[0].split()[1])
r, ms = d.cmd("ALL 1")
check("ALL 1", r, "OK FF")
# 此時 CH5~CH8 已吸合，只需再開 4 路 -> 3 個錯開間隔。
log(f"全部 ON -> {r}（{ms:.1f}ms；含 3 x {stag}ms ON 錯開）")
check("錯開時間合理", 3 * stag - 2 <= ms <= 3 * stag + 15, True)
time.sleep(1.2)
check("GET", d.cmd("GET")[0], "OK FF")
r, ms = d.cmd("ALL 0")
check("ALL 0", r, "OK 00")
log(f"全部 OFF -> {r}（{ms:.1f}ms；釋放不錯開）")
time.sleep(0.6)

# ---------- Phase 3：PULSE 自動釋放 ----------
log("=== Phase 3：PULSE 3 500（CH3 吸合 500ms 後自己放掉）===")
r, ms = d.cmd("PULSE 3 500")
check("PULSE 回應", r, "OK 04")
log(f"PULSE -> {r}（{ms:.1f}ms，非阻塞）")
time.sleep(0.2)
check("脈衝中 GET", d.cmd("GET")[0], "OK 04")
time.sleep(0.45)
check("脈衝結束 GET", d.cmd("GET")[0], "OK 00")
log("脈衝已自動結束")
time.sleep(0.5)

# ---------- Phase 4：蜂鳴器 ----------
log("=== Phase 4：蜂鳴器（三短音 + 一長音）===")
for _ in range(3):
    check("BEEP 80", d.cmd("BEEP 80")[0], "OK")
    time.sleep(0.3)
time.sleep(0.4)
check("BEEP 600", d.cmd("BEEP 600")[0], "OK")
log("長音 600ms")
time.sleep(1.2)

# ---------- Phase 5：狀態燈 ----------
log("=== Phase 5：狀態燈 ===")
log("(1) 綠色恆亮＝已連線、全部釋放")
check("燈測 ALL 0", d.cmd("ALL 0")[0], "OK 00")
time.sleep(2.0)

log("(2) 黃色恆亮＝至少一路吸合")
check("燈測 ON 1", d.cmd("ON 1")[0], "OK 01")
time.sleep(2.0)
check("燈測 OFF", d.cmd("ALL 0")[0], "OK 00")
time.sleep(0.5)

log("(3) 紅色閃一下 x3＝指令錯誤")
for bad in ("OPEN 1", "ON 9", "ON 3x"):
    r, _ = d.cmd(bad)
    log(f"  {bad:8s} -> {r}")
    time.sleep(0.7)
check("錯誤後狀態不變", d.cmd("GET")[0], "OK 00")
time.sleep(0.8)

log("(4) LED 指令指定顏色：紅 → 綠 → 藍 → 白，各 1.2 秒")
check("LED 預設是 AUTO", d.cmd("LED")[0], "OK AUTO")
for name, code in (("紅", "FF0000"), ("綠", "00FF00"), ("藍", "0000FF"), ("白", "FFFFFF")):
    check(f"LED {name}", d.cmd(f"LED {code}")[0], f"OK {code}")
    log(f"  LED {code}（{name}）")
    time.sleep(1.2)
check("LED 查詢回指定值", d.cmd("LED")[0], "OK FFFFFF")

log("  亮度：24（預設）→ 120 → 6 → 24，白燈應明顯變亮再變暗")
check("LEDB 預設值", d.cmd("LEDB")[0], "OK 24")
for level in (120, 6, 24):
    check(f"LEDB {level}", d.cmd(f"LEDB {level}")[0], f"OK {level}")
    log(f"  LEDB {level}")
    time.sleep(1.0)

log("  LED OFF＝主機要它暗著（1 秒）")
check("LED OFF", d.cmd("LED OFF")[0], "OK 000000")
time.sleep(1.0)

log("  指定顏色期間吸合 CH1：燈應維持紫色，不會變成黃色")
check("LED 紫", d.cmd("LED FF00FF")[0], "OK FF00FF")
check("指定顏色時 ON 1", d.cmd("ON 1")[0], "OK 01")
check("ON 不改變指定顏色", d.cmd("LED")[0], "OK FF00FF")
time.sleep(1.5)

log("  錯誤參數：紅燈閃一下，指定的顏色與亮度都不受影響")
for bad, want in (("LED 1000000", "ERR 3 RANGE"),
                  ("LED GG0000", "ERR 3 RANGE"),
                  ("LED FF0000 00FF00", "ERR 2 BAD_ARG"),
                  ("LEDB 256", "ERR 3 RANGE")):
    r, _ = d.cmd(bad)
    log(f"  {bad:18s} -> {r}")
    check(f"{bad} 被拒", r, want)
    time.sleep(0.5)
check("錯誤後顏色不變", d.cmd("LED")[0], "OK FF00FF")
check("錯誤後亮度不變", d.cmd("LEDB")[0], "OK 24")

log("  LED AUTO：交還給韌體，CH1 仍吸合所以應轉黃燈")
check("LED AUTO", d.cmd("LED AUTO")[0], "OK AUTO")
time.sleep(1.5)
check("AUTO 後 ALL 0", d.cmd("ALL 0")[0], "OK 00")
log("  已全部釋放，燈應回綠色")
time.sleep(1.0)

log("(5) 紅色閃爍＝閒置看門狗觸發（開 WDT 2，吸合 4 路後故意不理它）")
check("WDT 2", d.cmd("WDT 2")[0], "OK 2")
check("MASK 0F", d.cmd("MASK 0F")[0], "OK 0F")
log("  4 路吸合中，2 秒後應該聽到繼電器全部釋放…")
lines = d.drain(3.0)
check("收到 FAILSAFE 通知", any(x == "# FAILSAFE IDLE" for x in lines), True)
log("  紅燈閃爍中，再觀察 2 秒")
d.drain(2.0)
r, _ = d.cmd("GET")
check("看門狗已釋放所有通道", r, "OK 00")
log(f"  GET -> {r}（看門狗已把 8 路全部釋放）")
check("PING 解除故障", d.cmd("PING")[0], "OK PONG")
log("  已下 PING，紅燈應恢復綠色")
check("WDT 關閉", d.cmd("WDT 0")[0], "OK 0")
time.sleep(1.5)

log("(6) 藍色閃爍＝USB 斷線保護（吸合 4 路、指定燈色後直接關閉連接埠）")
check("斷線前 MASK 0F", d.cmd("MASK 0F")[0], "OK 0F")
# 斷線時主機指定的燈色要自動回 AUTO，否則看不到藍閃（見 PROTOCOL.md §10.1）。
check("斷線前 LED 指定色", d.cmd("LED 00FFFF")[0], "OK 00FFFF")
time.sleep(0.8)
log("  關閉連接埠（放掉 DTR）→ 應聽到繼電器釋放、燈轉藍閃")
d.ser.close()
time.sleep(3.0)
log("  重新開啟連接埠")
d.open()
lines = d.drain(1.0)
# '# READY' 是在 DTR 拉起後數微秒內送出的，主機 CDC 驅動在開埠流程中常常會
# 把它丟掉（見 docs/PROTOCOL.md §6），所以這裡只記錄、不當成失敗。
if any(x.startswith("# READY PICO-RELAY-B") for x in lines):
    log("  收到 # READY")
else:
    NOTES.append("開埠時沒收到 '# READY'（主機 CDC 驅動丟棄，屬已知行為，見 PROTOCOL.md §6）")
    log("  沒收到 # READY（已知的主機端行為，不影響控制）")
r, _ = d.cmd("GET")
check("斷線期間已全部釋放", r, "OK 00")
log(f"  GET -> {r}（斷線時已全部釋放）")
check("斷線後 LED 回 AUTO", d.cmd("LED")[0], "OK AUTO")

# ---------- 收尾 ----------
log("=== 收尾 ===")
check("LED/LEDB 改過再 RST", d.cmd("LED 123456")[0], "OK 123456")
check("LEDB 改過再 RST", d.cmd("LEDB 200")[0], "OK 200")
check("RST", d.cmd("RST")[0], "OK 00")
check("STAG 預設", d.cmd("STAG")[0], "OK 5")
check("WDT 預設", d.cmd("WDT")[0], "OK 0")
check("RST 後 LED 預設", d.cmd("LED")[0], "OK AUTO")
check("RST 後 LEDB 預設", d.cmd("LEDB")[0], "OK 24")
d.cmd("BEEP 150")
d.ser.close()

bad = [c for c in CHECKS if not c[1]]
print()
log(f"完成：{len(CHECKS)} 項檢查，{len(bad)} 項失敗")
for label, _, got, want in bad:
    print(f"  FAIL {label}: got {got!r} want {want!r}")
for n in NOTES:
    print(f"  NOTE {n}")
print("裝置資訊行：")
for n in d.notices:
    print("  " + n)
sys.exit(1 if bad else 0)
