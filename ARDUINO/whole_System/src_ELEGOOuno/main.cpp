#include <Arduino.h>
#include <Wire.h>

// CNC Shield V3 axis pin mapping
const uint8_t STEP_PINS[] = {2, 3, 4};
const uint8_t DIR_PINS[] = {5, 6, 7};
const uint8_t EN_PIN = 8;
const uint8_t STATUS_LED_PIN = 13;
const uint8_t AXIS_COUNT = 3;
const uint8_t AXIS_ALL = 255;
const uint8_t I2C_ADDRESS = 0x08;

enum CommandId : uint8_t {
  CMD_MOVE = 0x01,
  CMD_ENABLE = 0x02,
  CMD_DISABLE = 0x03,
};

struct I2CCommand {
  uint8_t cmd;
  uint8_t axis;
  uint8_t dir;
  uint16_t steps;
  uint16_t delayUs;
};

volatile bool commandReady = false;
I2CCommand currentCommand = {0, 0, 0, 0, 0};

void stepAxis(uint8_t axis, uint16_t steps, uint8_t dir, uint16_t delayUs) {
  if (axis >= AXIS_COUNT || steps == 0) {
    return;
  }

  digitalWrite(DIR_PINS[axis], dir ? HIGH : LOW);

  for (uint16_t i = 0; i < steps; i++) {
    digitalWrite(STEP_PINS[axis], HIGH);
    delayMicroseconds(delayUs);
    digitalWrite(STEP_PINS[axis], LOW);
    delayMicroseconds(delayUs);
  }
}

void receiveI2C(int count) {
  if (count < 1) {
    return;
  }

  I2CCommand cmd = {0, AXIS_ALL, 0, 0, 0};
  cmd.cmd = Wire.read();
  bool validCommand = false;

  if (cmd.cmd == CMD_MOVE && count >= 7) {
    cmd.axis = Wire.read();
    cmd.dir = Wire.read();
    uint8_t lowSteps = Wire.read();
    uint8_t highSteps = Wire.read();
    cmd.steps = (uint16_t)lowSteps | ((uint16_t)highSteps << 8);
    uint8_t lowDelay = Wire.read();
    uint8_t highDelay = Wire.read();
    cmd.delayUs = (uint16_t)lowDelay | ((uint16_t)highDelay << 8);
    validCommand = true;
  } else if (cmd.cmd == CMD_ENABLE || cmd.cmd == CMD_DISABLE) {
    if (count >= 2) {
      cmd.axis = Wire.read();
    } else {
      cmd.axis = AXIS_ALL;
    }
    validCommand = true;
  }

  if (!validCommand) {
    return;
  }

  currentCommand = cmd;
  commandReady = true;
  digitalWrite(STATUS_LED_PIN, HIGH);
}

void requestI2C() {
  Wire.write((uint8_t)(commandReady ? 1 : 0));
}

void executeCommand() {
  noInterrupts();
  I2CCommand cmd = currentCommand;
  commandReady = false;
  interrupts();
  digitalWrite(STATUS_LED_PIN, HIGH);
  delay(100);
  digitalWrite(STATUS_LED_PIN, LOW);

  if (cmd.cmd == CMD_MOVE) {
    if (cmd.axis < AXIS_COUNT) {
      digitalWrite(EN_PIN, LOW);
      stepAxis(cmd.axis, cmd.steps, cmd.dir, cmd.delayUs);
    }
  } else if (cmd.cmd == CMD_ENABLE) {
    digitalWrite(EN_PIN, LOW);
  } else if (cmd.cmd == CMD_DISABLE) {
    digitalWrite(EN_PIN, HIGH);
  }
}

void setup() {
  pinMode(EN_PIN, OUTPUT);
  digitalWrite(EN_PIN, LOW);

  for (uint8_t i = 0; i < AXIS_COUNT; i++) {
    pinMode(STEP_PINS[i], OUTPUT);
    pinMode(DIR_PINS[i], OUTPUT);
    digitalWrite(STEP_PINS[i], LOW);
    digitalWrite(DIR_PINS[i], LOW);
  }

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  Wire.begin(I2C_ADDRESS);
  Wire.onReceive(receiveI2C);
  Wire.onRequest(requestI2C);

  Serial.begin(115200);
  while (!Serial) {
    ;
  }
  Serial.println("UNO I2C-Slave bereit, Adresse 0x08");
}

void loop() {
  if (commandReady) {
    executeCommand();
  }
}
