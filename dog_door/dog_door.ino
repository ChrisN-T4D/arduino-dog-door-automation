/**
 * Dog door — ELEGOO Uno R3 + BTS7960 + host serial:
 * - USB Serial (Pi / debug / Arduino IDE monitor).
 * - SoftwareSerial on D8 RX / D7 TX — ESP32 UART (9600); TX optional if one-way-only.
 *
 * Electrical: Uno D7 is ~5 V when HIGH. Do not connect D7 TX directly to ESP32 GPIO RX
 * (3.3 V only) — use a logic level shifter or divider. ESP TX→Uno D8 RX is ok (3.3 V HIGH).
 *
 * Timings: defaults below; EEPROM persists after SET_* commands. GET_STATE reports phase + ms.
 */

#include <EEPROM.h>
#include <SoftwareSerial.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Door phase must appear before any function — Arduino IDE injects prototypes
// before the first function, so types used in later helpers must be defined here.
enum DoorPhase : uint8_t {
  PHASE_CLOSED_IDLE = 0,
  PHASE_OPENING,
  PHASE_OPEN_HELD,
  PHASE_CLOSING,
};

static DoorPhase phase = PHASE_CLOSED_IDLE;
static unsigned long phaseStartMs = 0;

// -----------------------------------------------------------------------------
// Motor timings — defaults; runtime vars loaded from EEPROM when valid
// -----------------------------------------------------------------------------
static const unsigned long DEFAULT_OPEN_MS = 3000;
static const unsigned long DEFAULT_CLOSE_MS = 3000;
static const unsigned long DEFAULT_DWELL_MS = 8000;

static unsigned long openMs = DEFAULT_OPEN_MS;
static unsigned long closeMs = DEFAULT_CLOSE_MS;
static unsigned long dwellMs = DEFAULT_DWELL_MS;

static const uint16_t EEPROM_MAGIC = 0xDA01;
static const int EEPROM_ADDR_MAGIC = 0;
static const int EEPROM_ADDR_OPEN = 2;
static const int EEPROM_ADDR_CLOSE = 6;
static const int EEPROM_ADDR_DWELL = 10;

static void saveTimingsToEeprom() {
  EEPROM.put(EEPROM_ADDR_MAGIC, EEPROM_MAGIC);
  uint32_t o = (uint32_t)openMs;
  uint32_t c = (uint32_t)closeMs;
  uint32_t d = (uint32_t)dwellMs;
  EEPROM.put(EEPROM_ADDR_OPEN, o);
  EEPROM.put(EEPROM_ADDR_CLOSE, c);
  EEPROM.put(EEPROM_ADDR_DWELL, d);
}

static void loadTimingsFromEeprom() {
  uint16_t mag = 0;
  EEPROM.get(EEPROM_ADDR_MAGIC, mag);
  if (mag != EEPROM_MAGIC) {
    openMs = DEFAULT_OPEN_MS;
    closeMs = DEFAULT_CLOSE_MS;
    dwellMs = DEFAULT_DWELL_MS;
    saveTimingsToEeprom();
    return;
  }
  uint32_t o, c, d;
  EEPROM.get(EEPROM_ADDR_OPEN, o);
  EEPROM.get(EEPROM_ADDR_CLOSE, c);
  EEPROM.get(EEPROM_ADDR_DWELL, d);
  openMs = o;
  closeMs = c;
  dwellMs = d;
  if (openMs < 500 || openMs > 120000) openMs = DEFAULT_OPEN_MS;
  if (closeMs < 500 || closeMs > 120000) closeMs = DEFAULT_CLOSE_MS;
  if (dwellMs < 1000 || dwellMs > 600000) dwellMs = DEFAULT_DWELL_MS;
}

// -----------------------------------------------------------------------------
// Pins — adjust to your wiring
// -----------------------------------------------------------------------------
/** BTS7960: typical naming RPWM/LPWM; enable pins HIGH = driver active (many boards). */
static const int PIN_MOTOR_RPWM = 9;
static const int PIN_MOTOR_LPWM = 10;
static const int PIN_MOTOR_REN = 11;
static const int PIN_MOTOR_LEN = 12;

/**
 * ESP32: ESP TX → Uno D8 RX. Uno D7 TX → ESP RX only with 3.3 V-safe path (level shifter).
 */
static const int PIN_ESP_HOST_RX = 8;
static const int PIN_ESP_HOST_TX = 7;

SoftwareSerial EspHostSerial(PIN_ESP_HOST_RX, PIN_ESP_HOST_TX);

// -----------------------------------------------------------------------------
// Motor + reporting
// -----------------------------------------------------------------------------
static void motorStop() {
  digitalWrite(PIN_MOTOR_RPWM, LOW);
  digitalWrite(PIN_MOTOR_LPWM, LOW);
}

static void motorDriveOpen() {
  digitalWrite(PIN_MOTOR_RPWM, LOW);
  digitalWrite(PIN_MOTOR_LPWM, HIGH);
}

static void motorDriveClose() {
  digitalWrite(PIN_MOTOR_RPWM, HIGH);
  digitalWrite(PIN_MOTOR_LPWM, LOW);
}

/** Door state announcements to USB Serial only (avoid driving ESP RX without shifter). */
static void reportState(const char* stateLine) {
  Serial.println(stateLine);
  Serial.flush();
}

static void beginOpeningSequence() {
  phase = PHASE_OPENING;
  phaseStartMs = millis();
  motorDriveOpen();
  reportState("STATE:MOVING_OPEN");
}

static void beginClosingSequence() {
  phase = PHASE_CLOSING;
  phaseStartMs = millis();
  motorDriveClose();
  reportState("STATE:MOVING_CLOSED");
}

static void finishClosingIdle() {
  motorStop();
  phase = PHASE_CLOSED_IDLE;
  reportState("STATE:CLOSED");
}

static const char* phaseToString(DoorPhase p) {
  switch (p) {
    case PHASE_CLOSED_IDLE:
      return "closed_idle";
    case PHASE_OPENING:
      return "opening";
    case PHASE_OPEN_HELD:
      return "open_held";
    case PHASE_CLOSING:
      return "closing";
    default:
      return "unknown";
  }
}

static void pollDoorMachine() {
  unsigned long now = millis();

  switch (phase) {
    case PHASE_CLOSED_IDLE:
      break;

    case PHASE_OPENING: {
      unsigned long elapsed = now - phaseStartMs;
      if (elapsed >= openMs) {
        motorStop();
        phase = PHASE_OPEN_HELD;
        phaseStartMs = now;
        reportState("STATE:OPEN");
      }
      break;
    }

    case PHASE_OPEN_HELD: {
      if (now - phaseStartMs >= dwellMs) {
        beginClosingSequence();
      }
      break;
    }

    case PHASE_CLOSING: {
      unsigned long elapsed = now - phaseStartMs;
      if (elapsed >= closeMs) {
        finishClosingIdle();
      }
      break;
    }
  }
}

// -----------------------------------------------------------------------------
// Host serial: newline-delimited commands (USB + ESP on D8)
// -----------------------------------------------------------------------------
static bool parseULongAfterEquals(const char* line, const char* prefix, unsigned long* outVal) {
  size_t pre = strlen(prefix);
  if (strncmp(line, prefix, pre) != 0) return false;
  const char* num = line + pre;
  if (!num[0]) return false;
  char* end = nullptr;
  unsigned long v = strtoul(num, &end, 10);
  if (end == num) return false;
  *outVal = v;
  return true;
}

static void handleSerialLine(char* line, Print& ackOut, Print* mirrorAckToUsb) {
  auto sendAck = [&](const char* msg) {
    ackOut.println(msg);
    if (mirrorAckToUsb) mirrorAckToUsb->println(msg);
  };

  if (strcmp(line, "GET_STATE") == 0) {
    char buf[96];
    snprintf(buf, sizeof(buf), "INFO:PHASE:%s", phaseToString(phase));
    sendAck(buf);
    snprintf(buf, sizeof(buf), "INFO:TUNE:open_ms=%lu close_ms=%lu dwell_ms=%lu",
             openMs, closeMs, dwellMs);
    sendAck(buf);
    sendAck("ACK:GET_STATE");
    return;
  }

  unsigned long v;
  if (parseULongAfterEquals(line, "SET_OPEN_MS=", &v)) {
    if (v < 500UL || v > 120000UL) {
      sendAck("ERR:set_open_range");
      return;
    }
    openMs = v;
    saveTimingsToEeprom();
    sendAck("ACK:SET_OPEN_MS");
    return;
  }
  if (parseULongAfterEquals(line, "SET_CLOSE_MS=", &v)) {
    if (v < 500UL || v > 120000UL) {
      sendAck("ERR:set_close_range");
      return;
    }
    closeMs = v;
    saveTimingsToEeprom();
    sendAck("ACK:SET_CLOSE_MS");
    return;
  }
  if (parseULongAfterEquals(line, "SET_DWELL_MS=", &v)) {
    if (v < 1000UL || v > 600000UL) {
      sendAck("ERR:set_dwell_range");
      return;
    }
    dwellMs = v;
    saveTimingsToEeprom();
    sendAck("ACK:SET_DWELL_MS");
    return;
  }

  if (strcmp(line, "CMD_OPEN") == 0) {
    if (phase == PHASE_CLOSED_IDLE) {
      beginOpeningSequence();
      sendAck("ACK:CMD_OPEN");
    } else {
      sendAck("ERR:busy");
    }
    return;
  }
  if (strcmp(line, "CMD_CLOSE") == 0) {
    if (phase == PHASE_OPENING || phase == PHASE_OPEN_HELD) {
      beginClosingSequence();
      sendAck("ACK:CMD_CLOSE");
    } else if (phase == PHASE_CLOSING) {
      sendAck("ERR:already_closing");
    } else {
      sendAck("ACK:already_closed");
    }
    return;
  }
  if (strcmp(line, "PING") == 0) {
    sendAck("PONG");
    return;
  }
}

static char usbLineBuf[48];
static uint8_t usbLineLen = 0;

static void pollUsbSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      usbLineBuf[usbLineLen] = '\0';
      if (usbLineLen > 0) handleSerialLine(usbLineBuf, Serial, nullptr);
      usbLineLen = 0;
    } else if (usbLineLen < sizeof(usbLineBuf) - 1) {
      usbLineBuf[usbLineLen++] = c;
    } else {
      usbLineLen = 0;
    }
  }
}

static char espLineBuf[48];
static uint8_t espLineLen = 0;

static void pollEspHostSerial() {
  while (EspHostSerial.available()) {
    char c = (char)EspHostSerial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      espLineBuf[espLineLen] = '\0';
      if (espLineLen > 0) {
        Serial.print(F("ESP_LINK rx line: "));
        Serial.println(espLineBuf);
        handleSerialLine(espLineBuf, EspHostSerial, &Serial);
      }
      espLineLen = 0;
    } else if (espLineLen < sizeof(espLineBuf) - 1) {
      espLineBuf[espLineLen++] = c;
    } else {
      espLineLen = 0;
    }
  }
}

void setup() {
  Serial.begin(9600);
  EspHostSerial.begin(9600);

  loadTimingsFromEeprom();

  pinMode(PIN_MOTOR_RPWM, OUTPUT);
  pinMode(PIN_MOTOR_LPWM, OUTPUT);
  pinMode(PIN_MOTOR_REN, OUTPUT);
  pinMode(PIN_MOTOR_LEN, OUTPUT);
  digitalWrite(PIN_MOTOR_REN, HIGH);
  digitalWrite(PIN_MOTOR_LEN, HIGH);
  motorStop();

  phase = PHASE_CLOSED_IDLE;
  reportState("STATE:CLOSED");
  Serial.println(F("BOOT:dog_door timings+GET_STATE+SET_* USB+ESP_d8rx EEPROM"));
}

void loop() {
  pollUsbSerial();
  pollEspHostSerial();
  pollDoorMachine();
}
