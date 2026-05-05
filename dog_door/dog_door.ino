/**
 * Dog door — ELEGOO Uno R3 + BTS7960 + host over USB serial (e.g. Raspberry Pi or Home Assistant).
 *
 * Opens only on explicit host commands (CMD_OPEN). Policy, mmWave (ESP32 + LD2410C), and
 * schedules live in Home Assistant; the Pi app (optional) forwards CMD_OPEN over USB.
 */

#include <string.h>

// -----------------------------------------------------------------------------
// Motor timings (tune on hardware)
// -----------------------------------------------------------------------------
static const unsigned long OPEN_MS = 3000;
static const unsigned long CLOSE_MS = 3000;
static const unsigned long DWELL_MS = 8000;

// -----------------------------------------------------------------------------
// Pins — adjust to your wiring
// -----------------------------------------------------------------------------
/** BTS7960: typical naming RPWM/LPWM; enable pins HIGH = driver active (many boards). */
static const int PIN_MOTOR_RPWM = 9;
static const int PIN_MOTOR_LPWM = 10;
static const int PIN_MOTOR_REN = 11;
static const int PIN_MOTOR_LEN = 12;

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

// -----------------------------------------------------------------------------
// USB serial: newline-delimited commands from host
// -----------------------------------------------------------------------------
static char serialLineBuf[48];
static uint8_t serialLineLen = 0;

static void handleSerialLine(char* line) {
  if (strcmp(line, "CMD_OPEN") == 0) {
    if (phase == PHASE_CLOSED_IDLE) {
      beginOpeningSequence();
      Serial.println("ACK:CMD_OPEN");
    } else {
      Serial.println("ERR:busy");
    }
    return;
  }
  if (strcmp(line, "CMD_CLOSE") == 0) {
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

void setup() {
  Serial.begin(9600);

  pinMode(PIN_MOTOR_RPWM, OUTPUT);
  pinMode(PIN_MOTOR_LPWM, OUTPUT);
  pinMode(PIN_MOTOR_REN, OUTPUT);
  pinMode(PIN_MOTOR_LEN, OUTPUT);
  digitalWrite(PIN_MOTOR_REN, HIGH);
  digitalWrite(PIN_MOTOR_LEN, HIGH);
  motorStop();

  phase = PHASE_CLOSED_IDLE;
  reportState("STATE:CLOSED");
  Serial.println(F("BOOT:dog_door host_serial"));
}

void loop() {
  pollHostSerial();
  pollDoorMachine();
}
