/**
 * Dog door — ELEGOO Uno R3 + BTS7960 + host serial:
 * - USB Serial (Pi / debug / Arduino IDE monitor).
 * - SoftwareSerial on D8 RX / D7 TX — ESP32 UART (9600); TX optional if one-way-only.
 *
 * Opens only on explicit host commands (CMD_OPEN). Policy / mmWave / schedules live off-board.
 */

#include <SoftwareSerial.h>
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

/**
 * ESP32 (or Pi-level shifter host): HardwareSerial-equivalent pins on Uno.
 * ESP TX2 (pin 28) → Uno D8 RX. ESP RX2 ← Uno D7 TX only via 3.3 V-safe path (level shifter).
 */
static const int PIN_ESP_HOST_RX = 8;
/** TX line; leave disconnected for one-way receive-only until level shifter is installed. */
static const int PIN_ESP_HOST_TX = 7;

SoftwareSerial EspHostSerial(PIN_ESP_HOST_RX, PIN_ESP_HOST_TX);

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
// Host serial: newline-delimited commands (USB + ESP on D8)
// -----------------------------------------------------------------------------
static void handleSerialLine(char* line, Print& ackOut) {
  if (strcmp(line, "CMD_OPEN") == 0) {
    if (phase == PHASE_CLOSED_IDLE) {
      beginOpeningSequence();
      ackOut.println("ACK:CMD_OPEN");
    } else {
      ackOut.println("ERR:busy");
    }
    return;
  }
  if (strcmp(line, "CMD_CLOSE") == 0) {
    if (phase == PHASE_OPENING || phase == PHASE_OPEN_HELD) {
      beginClosingSequence();
      ackOut.println("ACK:CMD_CLOSE");
    } else if (phase == PHASE_CLOSING) {
      ackOut.println("ERR:already_closing");
    } else {
      ackOut.println("ACK:already_closed");
    }
    return;
  }
  if (strcmp(line, "PING") == 0) {
    ackOut.println("PONG");
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
      if (usbLineLen > 0) handleSerialLine(usbLineBuf, Serial);
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
      if (espLineLen > 0) handleSerialLine(espLineBuf, EspHostSerial);
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

  pinMode(PIN_MOTOR_RPWM, OUTPUT);
  pinMode(PIN_MOTOR_LPWM, OUTPUT);
  pinMode(PIN_MOTOR_REN, OUTPUT);
  pinMode(PIN_MOTOR_LEN, OUTPUT);
  digitalWrite(PIN_MOTOR_REN, HIGH);
  digitalWrite(PIN_MOTOR_LEN, HIGH);
  motorStop();

  phase = PHASE_CLOSED_IDLE;
  reportState("STATE:CLOSED");
  Serial.println(F("BOOT:dog_door usb_serial+esp_d8rx"));
}

void loop() {
  pollUsbSerial();
  pollEspHostSerial();
  pollDoorMachine();
}
