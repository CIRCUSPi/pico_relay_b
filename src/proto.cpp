#include "proto.h"

#include <stdarg.h>
#include <strings.h>

#include "config.h"
#include "indicator.h"
#include "relay.h"

namespace {

char gLine[PROTO_MAX_LINE + 1];
uint8_t gLen = 0;
bool gTooLong = false;

// 這一行指令有沒有帶 *XX 檢查碼；有的話回應也帶，主機兩邊都能驗。
bool gUseCsum = false;

uint32_t gLastRx = 0;
uint16_t gIdleSec = PROTO_IDLE_TIMEOUT_SEC;
bool gFault = false;

uint8_t xorsum(const char *s, size_t n) {
  uint8_t v = 0;
  for (size_t i = 0; i < n; i++) {
    v ^= (uint8_t)s[i];
  }
  return v;
}

void emit(const char *body) {
  if (gUseCsum) {
    Serial.printf("%s*%02X\r\n", body, xorsum(body, strlen(body)));
  } else {
    Serial.printf("%s\r\n", body);
  }
}

void emitf(const char *fmt, ...) {
  char buf[96];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  emit(buf);
}

// 所有會改變或查詢繼電器狀態的指令都用同一種回應：OK + 8 路狀態（2 位 hex）。
// 主機每下一次指令就拿到完整狀態，不必自己記帳。
void okMask() {
  emitf("OK %02X", Relay::mask());
}

void fail(uint8_t code, const char *name) {
  Indicator::blipError();
  emitf("ERR %u %s", code, name);
}

// 嚴格解析：整串都必須是合法數字，避免 "ON 3x" 被當成 3。
bool parseU32(const char *s, uint32_t *out, uint8_t base) {
  if (*s == '\0') {
    return false;
  }
  if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    s += 2;
    if (*s == '\0') {
      return false;
    }
  }
  uint32_t v = 0;
  for (; *s; s++) {
    uint8_t d;
    if (*s >= '0' && *s <= '9') {
      d = (uint8_t)(*s - '0');
    } else if (base == 16 && *s >= 'a' && *s <= 'f') {
      d = (uint8_t)(*s - 'a' + 10);
    } else if (base == 16 && *s >= 'A' && *s <= 'F') {
      d = (uint8_t)(*s - 'A' + 10);
    } else {
      return false;
    }
    if (v > (0xFFFFFFFFu - d) / base) {
      return false;  // 溢位
    }
    v = v * base + d;
  }
  *out = v;
  return true;
}

bool parseChannel(const char *s, uint8_t *ch) {
  uint32_t v;
  if (!parseU32(s, &v, 10) || v < 1 || v > Relay::COUNT) {
    return false;
  }
  *ch = (uint8_t)v;
  return true;
}

void help() {
  static const char *kLines[] = {
    "# " DEV_MODEL " FW" FW_VERSION " / LF or CRLF terminated, case-insensitive",
    "# ON n | OFF n | SET n 0|1 | TOG n    single channel, n=1..8",
    "# MASK hh | ALL 0|1 | GET             whole bank, bit0=CH1",
    "# PULSE n ms                          one-shot 10..60000ms, non-blocking",
    "# WDT [sec] | STAG [ms] | BEEP [ms]   idle failsafe / ON spacing / buzzer",
    "# ID | PING | RST | HELP",
    "# reply: OK hh (relay mask) | OK <value> | ERR <code> <name>",
  };
  for (size_t i = 0; i < sizeof(kLines) / sizeof(kLines[0]); i++) {
    Serial.printf("%s\r\n", kLines[i]);
  }
  emit("OK");
}

void dispatch(uint8_t argc, char **argv) {
  const char *cmd = argv[0];
  uint8_t ch;
  uint32_t v;

  if (!strcasecmp(cmd, "ON") || !strcasecmp(cmd, "OFF")) {
    if (argc != 2) {
      return fail(2, "BAD_ARG");
    }
    if (!parseChannel(argv[1], &ch)) {
      return fail(3, "RANGE");
    }
    Relay::set(ch, !strcasecmp(cmd, "ON"));
    return okMask();
  }

  if (!strcasecmp(cmd, "SET")) {
    if (argc != 3) {
      return fail(2, "BAD_ARG");
    }
    if (!parseChannel(argv[1], &ch)) {
      return fail(3, "RANGE");
    }
    if (!parseU32(argv[2], &v, 10) || v > 1) {
      return fail(3, "RANGE");
    }
    Relay::set(ch, v != 0);
    return okMask();
  }

  if (!strcasecmp(cmd, "TOG")) {
    if (argc != 2) {
      return fail(2, "BAD_ARG");
    }
    if (!parseChannel(argv[1], &ch)) {
      return fail(3, "RANGE");
    }
    Relay::set(ch, !Relay::get(ch));
    return okMask();
  }

  if (!strcasecmp(cmd, "MASK")) {
    if (argc != 2) {
      return fail(2, "BAD_ARG");
    }
    if (!parseU32(argv[1], &v, 16) || v > 0xFF) {
      return fail(3, "RANGE");
    }
    Relay::setMask((uint8_t)v);
    return okMask();
  }

  if (!strcasecmp(cmd, "ALL")) {
    if (argc != 2) {
      return fail(2, "BAD_ARG");
    }
    if (!parseU32(argv[1], &v, 10) || v > 1) {
      return fail(3, "RANGE");
    }
    Relay::setMask(v ? 0xFF : 0x00);
    return okMask();
  }

  if (!strcasecmp(cmd, "GET")) {
    if (argc != 1) {
      return fail(2, "BAD_ARG");
    }
    return okMask();
  }

  if (!strcasecmp(cmd, "PULSE")) {
    if (argc != 3) {
      return fail(2, "BAD_ARG");
    }
    if (!parseChannel(argv[1], &ch)) {
      return fail(3, "RANGE");
    }
    if (!parseU32(argv[2], &v, 10) || v < 10 || v > 60000) {
      return fail(3, "RANGE");
    }
    Relay::pulse(ch, v);
    return okMask();
  }

  if (!strcasecmp(cmd, "RST")) {
    if (argc != 1) {
      return fail(2, "BAD_ARG");
    }
    Relay::allOff();
    Relay::setStagger(RELAY_STAGGER_MS);
    gIdleSec = PROTO_IDLE_TIMEOUT_SEC;
    return okMask();
  }

  if (!strcasecmp(cmd, "WDT")) {
    if (argc == 2) {
      if (!parseU32(argv[1], &v, 10) || v > PROTO_IDLE_TIMEOUT_MAX) {
        return fail(3, "RANGE");
      }
      gIdleSec = (uint16_t)v;
    } else if (argc != 1) {
      return fail(2, "BAD_ARG");
    }
    return emitf("OK %u", gIdleSec);
  }

  if (!strcasecmp(cmd, "STAG")) {
    if (argc == 2) {
      if (!parseU32(argv[1], &v, 10) || v > RELAY_STAGGER_MAX_MS) {
        return fail(3, "RANGE");
      }
      Relay::setStagger((uint16_t)v);
    } else if (argc != 1) {
      return fail(2, "BAD_ARG");
    }
    return emitf("OK %u", Relay::stagger());
  }

  if (!strcasecmp(cmd, "BEEP")) {
    v = 100;
    if (argc == 2) {
      if (!parseU32(argv[1], &v, 10) || v < 10 || v > BUZZER_MAX_MS) {
        return fail(3, "RANGE");
      }
    } else if (argc != 1) {
      return fail(2, "BAD_ARG");
    }
    Indicator::beep((uint16_t)v);
    return emit("OK");
  }

  if (!strcasecmp(cmd, "ID")) {
    if (argc != 1) {
      return fail(2, "BAD_ARG");
    }
    return emitf("OK %s %u FW%s %s", DEV_MODEL, Relay::COUNT, FW_VERSION,
                 rp2040.getChipID());
  }

  if (!strcasecmp(cmd, "PING")) {
    if (argc != 1) {
      return fail(2, "BAD_ARG");
    }
    return emit("OK PONG");
  }

  if (!strcasecmp(cmd, "HELP")) {
    return help();
  }

  fail(1, "UNKNOWN_CMD");
}

void noteRx() {
  gLastRx = millis();
  // 收到任何一行就代表主機還活著，解除失效保護狀態。
  gFault = false;
}

void handleLine() {
  gUseCsum = false;

  char *p = gLine;
  while (*p == ' ' || *p == '\t') {
    p++;
  }
  if (*p == '\0') {
    return;  // 只有空白，當空行：不回應
  }

  noteRx();

  if (*p == '#') {
    return;  // 主機送來的註解行，忽略且不回應
  }

  // 選用的 *XX 檢查碼：有帶就驗，沒帶就不驗。
  char *star = strrchr(p, '*');
  if (star) {
    const char *given = star + 1;
    uint32_t want;
    *star = '\0';
    if (strlen(given) != 2 || !parseU32(given, &want, 16)) {
      return fail(5, "CHECKSUM");
    }
    if ((uint8_t)want != xorsum(p, strlen(p))) {
      return fail(5, "CHECKSUM");
    }
    gUseCsum = true;
    // 檢查碼算進去的是 '*' 之前的全部內容，但後面切 token 時不要留下尾端空白。
    char *end = star;
    while (end > p && (end[-1] == ' ' || end[-1] == '\t')) {
      *--end = '\0';
    }
  }

  // 切出指令與最多兩個參數。
  char *argv[3];
  uint8_t argc = 0;
  while (*p && argc < 3) {
    while (*p == ' ' || *p == '\t') {
      p++;
    }
    if (*p == '\0') {
      break;
    }
    argv[argc++] = p;
    while (*p && *p != ' ' && *p != '\t') {
      p++;
    }
    if (*p) {
      *p++ = '\0';
    }
  }
  while (*p == ' ' || *p == '\t') {
    p++;
  }
  if (argc == 0) {
    return;
  }
  if (*p != '\0') {
    return fail(2, "BAD_ARG");  // 參數太多
  }

  dispatch(argc, argv);
}

void checkIdle() {
  if (gIdleSec == 0 || gFault) {
    return;
  }
  if ((millis() - gLastRx) / 1000u < gIdleSec) {
    return;
  }
  gFault = true;
  Relay::allOff();
  Proto::notice("FAILSAFE IDLE");
}

}  // namespace

void Proto::begin() {
  gLastRx = millis();
}

void Proto::poll() {
  while (Serial.available()) {
    const int c = Serial.read();
    if (c < 0) {
      break;
    }
    if (c == '\n' || c == '\r') {
      if (gTooLong) {
        gTooLong = false;
        gLen = 0;
        noteRx();
        fail(4, "TOO_LONG");
        continue;
      }
      if (gLen == 0) {
        continue;  // 空行忽略
      }
      gLine[gLen] = '\0';
      gLen = 0;
      handleLine();
      continue;
    }
    if (c == '\0') {
      continue;
    }
    if (gLen >= PROTO_MAX_LINE) {
      gTooLong = true;  // 丟到換行為止
      continue;
    }
    gLine[gLen++] = (char)c;
  }
  checkIdle();
}

void Proto::onLinkUp() {
  gLen = 0;
  gTooLong = false;
  gFault = false;
  gLastRx = millis();
  Serial.printf("# READY %s FW%s %s\r\n", DEV_MODEL, FW_VERSION,
                rp2040.getChipID());
}

void Proto::onLinkDown() {
  gLen = 0;
  gTooLong = false;
  gFault = false;
  gLastRx = millis();
}

bool Proto::faulted() {
  return gFault;
}

void Proto::notice(const char *text) {
  Serial.printf("# %s\r\n", text);
}
