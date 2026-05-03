/**
 * Dog door — ELEGOO Uno R3 + RDM6300 + BTS7960 + (optional) Raspberry Pi via USB serial.
 *
 * Bench mode: REQUIRE_PI_HEARTBEAT = 0 → RFID works without Pi.
 * Production: REQUIRE_PI_HEARTBEAT = 1 → RFID denied unless Pi sends EXIT_ALLOWED heartbeats.
 */

#include <SoftwareSerial.h>
#include <string.h>

// -----------------------------------------------------------------------------
// Policy (Pi)
// -----------------------------------------------------------------------------
/** Set to 1 when Pi is connected and should gate RFID exit windows. */
#define REQUIRE_PI_HEARTBEAT 1

/** No EXIT_ALLOWED from Pi for this long → fail-safe deny RFID (production). */
static const unsigned long HEARTBEAT_TIMEOUT_MS = 35000;

// -----------------------------------------------------------------------------
// Motor timings (tune on hardware)
// -----------------------------------------------------------------------------
static const unsigned long OPEN_MS = 3000;
static const unsigned long CLOSE_MS = 3000;
static const unsigned long DWELL_MS = 8000;

// -----------------------------------------------------------------------------
// Pins — adjust to your wiring
// -----------------------------------------------------------------------------
/** RDM6300 TX → Arduino RX pin (D2). RDM6300 RX can be left floating or tied high. */
static const int PIN_RFID_RX = 2;
static const int PIN_RFID_TX = 3;  // Not connected to module; required by SoftwareSerial.

/** BTS7960: typical naming RPWM/LPWM; enable pins HIGH = driver active (many boards). */
static const int PIN_MOTOR_RPWM = 9;
static const int PIN_MOTOR_LPWM = 10;
static const int PIN_MOTOR_REN = 11;
static const int PIN_MOTOR_LEN = 12;

// -----------------------------------------------------------------------------
// RFID (RDM6300: 9600 8N1, 14-byte frame)
// -----------------------------------------------------------------------------
static const int RDM6300_FRAME_LEN = 14;
SoftwareSerial RfidSerial(PIN_RFID_RX, PIN_RFID_TX);

/** Set to 1 to print RFID:CHK_FAIL + raw 14 bytes (rate-limited) when a frame parses but checksum fails — use to debug wiring/protocol. */
#define RFID_DEBUG_BAD_FRAMES 0

static uint8_t rfidBuf[RDM6300_FRAME_LEN];
static int rfidBufIndex = 0;

/**
 * Whitelist: use the 8-digit ID from `TAG:` (not TAGFULL). Leading zeros matter.
 * Example: TAG:000015C9 TAGFULL:00000015C9 → use  0x000015C9  here.
 */
static const uint32_t AUTHORIZED_TAGS[] = {
    0xDEADBEEF,  // replace with your tag ID(s)
};
static const int AUTHORIZED_TAG_COUNT =
    (int)(sizeof(AUTHORIZED_TAGS) / sizeof(AUTHORIZED_TAGS[0]));

// -----------------------------------------------------------------------------
// Door state
// -----------------------------------------------------------------------------
enum DoorPhase : uint8_t {
  PHASE_CLOSED_IDLE = 0,
  PHASE_OPENING,
  PHASE_OPEN_HELD,
  PHASE_CLOSING,
};

static DoorPhase phase = PHASE_CLOSED_IDLE;
static unsigned long phaseStartMs = 0;

static bool exitAllowedByPi = false;
static unsigned long lastPiMessageMs = 0;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
static void serialPrintUint32Hex8(uint32_t id) {
  const char digits[] = "0123456789ABCDEF";
  for (int shift = 28; shift >= 0; shift -= 4) {
    Serial.print(digits[(id >> (unsigned)shift) & 0xFU]);
  }
}

/** All 5 EM4100 payload bytes as 10 hex digits (matches many keyfob printouts). */
static void serialPrint5BytesHex10(const uint8_t b[5]) {
  const char d[] = "0123456789ABCDEF";
  for (int i = 0; i < 5; i++) {
    Serial.print(d[(b[i] >> 4) & 0xF]);
    Serial.print(d[b[i] & 0xF]);
  }
}

static int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  return -1;
}

static bool parseHexByte(char hi, char lo, uint8_t* out) {
  int a = hexNibble(hi);
  int b = hexNibble(lo);
  if (a < 0 || b < 0) return false;
  *out = (uint8_t)((a << 4) | b);
  return true;
}

/**
 * RDM6300 frame: 0x02 + 10 ASCII hex chars + 2 ASCII hex checksum + 0x03.
 * checksum = XOR of five payload bytes (EM4100).
 * Whitelist uint32 uses last four payload bytes (typical UID); TAGFULL prints all ten hex digits.
 */
static bool parseRdm6300Frame(const uint8_t* buf, uint32_t* outTagId, uint8_t outPayload5[5]) {
  if (buf[0] != 0x02 || buf[13] != 0x03) return false;

  uint8_t dataBytes[5];
  for (int i = 0; i < 5; i++) {
    if (!parseHexByte((char)buf[1 + i * 2], (char)buf[2 + i * 2], &dataBytes[i]))
      return false;
  }

  uint8_t xorSum = 0;
  for (int i = 0; i < 5; i++) xorSum ^= dataBytes[i];

  uint8_t checksumFromFrame = 0;
  if (!parseHexByte((char)buf[11], (char)buf[12], &checksumFromFrame)) return false;
  if (xorSum != checksumFromFrame) return false;

  for (int i = 0; i < 5; i++) outPayload5[i] = dataBytes[i];

  uint32_t id =
      ((uint32_t)dataBytes[1] << 24) | ((uint32_t)dataBytes[2] << 16)
      | ((uint32_t)dataBytes[3] << 8) | (uint32_t)dataBytes[4];
  *outTagId = id;
  return true;
}

static bool isTagAuthorized(uint32_t tagId) {
  if (AUTHORIZED_TAG_COUNT == 0) return false;
  for (int i = 0; i < AUTHORIZED_TAG_COUNT; i++) {
    if (AUTHORIZED_TAGS[i] == tagId) return true;
  }
  return false;
}

static bool rfidExitAllowedNow() {
#if REQUIRE_PI_HEARTBEAT
  if ((millis() - lastPiMessageMs) > HEARTBEAT_TIMEOUT_MS) return false;
  return exitAllowedByPi;
#else
  (void)exitAllowedByPi;
  return true;
#endif
}

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

static void reportState(const char* stateLine) {
  Serial.println(stateLine);
  Serial.flush();  // Ensure full line reaches Pi before RFID/other serial work
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

static void pollDoorMachine() {
  unsigned long now = millis();

  switch (phase) {
    case PHASE_CLOSED_IDLE:
      break;

    case PHASE_OPENING: {
      unsigned long elapsed = now - phaseStartMs;
      if (elapsed >= OPEN_MS) {
        motorStop();
        phase = PHASE_OPEN_HELD;
        phaseStartMs = now;
        reportState("STATE:OPEN");
      }
      break;
    }

    case PHASE_OPEN_HELD: {
      if (now - phaseStartMs >= DWELL_MS) {
        beginClosingSequence();
      }
      break;
    }

    case PHASE_CLOSING: {
      unsigned long elapsed = now - phaseStartMs;
      if (elapsed >= CLOSE_MS) {
        finishClosingIdle();
      }
      break;
    }
  }
}

static void tryStartFromAuthorizedRfid(uint32_t tagId, const uint8_t payload5[5]) {
  if (phase != PHASE_CLOSED_IDLE) return;
  if (!isTagAuthorized(tagId)) {
    Serial.print(F("DENY:tag="));
    serialPrintUint32Hex8(tagId);
    Serial.print(F(" TAGFULL:"));
    serialPrint5BytesHex10(payload5);
    Serial.println();
    return;
  }
  if (!rfidExitAllowedNow()) {
    Serial.println("DENY:policy");
    return;
  }
  beginOpeningSequence();
}

static char serialLineBuf[48];
static uint8_t serialLineLen = 0;

static void handleSerialLine(char* line) {
  if (strcmp(line, "EXIT_ALLOWED") == 0) {
    exitAllowedByPi = true;
    lastPiMessageMs = millis();
    Serial.println("ACK:EXIT_ALLOWED");
    return;
  }
  if (strcmp(line, "EXIT_DENIED") == 0) {
    exitAllowedByPi = false;
    lastPiMessageMs = millis();
    Serial.println("ACK:EXIT_DENIED");
    return;
  }
  if (strcmp(line, "CMD_OPEN") == 0) {
    lastPiMessageMs = millis();
    if (phase == PHASE_CLOSED_IDLE) {
      beginOpeningSequence();
      Serial.println("ACK:CMD_OPEN");
    } else {
      Serial.println("ERR:busy");
    }
    return;
  }
  if (strcmp(line, "CMD_CLOSE") == 0) {
    lastPiMessageMs = millis();
    if (phase == PHASE_OPENING || phase == PHASE_OPEN_HELD) {
      beginClosingSequence();
      Serial.println("ACK:CMD_CLOSE");
    } else if (phase == PHASE_CLOSING) {
      Serial.println("ERR:already_closing");
    } else {
      Serial.println("ACK:already_closed");
    }
    return;
  }
  if (strcmp(line, "PING") == 0) {
    lastPiMessageMs = millis();
    Serial.println("PONG");
    return;
  }
}

static void pollHostSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      serialLineBuf[serialLineLen] = '\0';
      if (serialLineLen > 0) handleSerialLine(serialLineBuf);
      serialLineLen = 0;
    } else if (serialLineLen < sizeof(serialLineBuf) - 1) {
      serialLineBuf[serialLineLen++] = c;
    } else {
      serialLineLen = 0;
    }
  }
}

static void pollRfid() {
  while (RfidSerial.available()) {
    uint8_t b = (uint8_t)RfidSerial.read();

    if (rfidBufIndex == 0 && b != 0x02) continue;

    rfidBuf[rfidBufIndex++] = b;

    if (rfidBufIndex >= RDM6300_FRAME_LEN) {
      uint32_t tagId = 0;
      uint8_t payload5[5];
      bool ok = parseRdm6300Frame(rfidBuf, &tagId, payload5);

      if (ok) {
        Serial.print(F("TAG:"));
        serialPrintUint32Hex8(tagId);
        Serial.print(F(" TAGFULL:"));
        serialPrint5BytesHex10(payload5);
        Serial.println();
        tryStartFromAuthorizedRfid(tagId, payload5);
      } else {
#if RFID_DEBUG_BAD_FRAMES
        static unsigned long lastChkFailMs = 0;
        unsigned long t = millis();
        if ((t - lastChkFailMs) >= 3000) {
          lastChkFailMs = t;
          Serial.print(F("RFID:CHK_FAIL "));
          for (int i = 0; i < RDM6300_FRAME_LEN; i++) {
            if (rfidBuf[i] < 0x10) Serial.print('0');
            Serial.print(rfidBuf[i], HEX);
            Serial.print(i < RDM6300_FRAME_LEN - 1 ? ' ' : '\n');
          }
        }
#endif
      }

      rfidBufIndex = 0;
    }
  }
}

void setup() {
  Serial.begin(9600);

  pinMode(PIN_MOTOR_RPWM, OUTPUT);
  pinMode(PIN_MOTOR_LPWM, OUTPUT);
  pinMode(PIN_MOTOR_REN, OUTPUT);
  pinMode(PIN_MOTOR_LEN, OUTPUT);
  digitalWrite(PIN_MOTOR_REN, HIGH);
  digitalWrite(PIN_MOTOR_LEN, HIGH);
  motorStop();

  RfidSerial.begin(9600);

  phase = PHASE_CLOSED_IDLE;
  reportState("STATE:CLOSED");
  Serial.print(F("BOOT:dog_door REQUIRE_PI_HEARTBEAT="));
  Serial.println(REQUIRE_PI_HEARTBEAT);
}

void loop() {
  pollHostSerial();
  pollRfid();
  pollDoorMachine();
}
