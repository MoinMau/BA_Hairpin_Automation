#include <Arduino.h>
#include <Wire.h>

static const int kI2cSdaPin = 21;
static const int kI2cSclPin = 22;
static const int kLedPin = 2;
static const uint8_t kSlaveAddress = 0x08;

enum CommandId : uint8_t {
  CMD_MOVE = 0x01,
  CMD_ENABLE = 0x02,
  CMD_DISABLE = 0x03,
};

static void blinkLed() {
  digitalWrite(kLedPin, HIGH);
  delay(80);
  digitalWrite(kLedPin, LOW);
}

void sendMoveCommand(uint8_t axis, uint8_t dir, uint16_t steps, uint16_t delayUs) {
  Wire.beginTransmission(kSlaveAddress);
  Wire.write(CMD_MOVE);
  Wire.write(axis);
  Wire.write(dir);
  Wire.write(static_cast<uint8_t>(steps & 0xFF));
  Wire.write(static_cast<uint8_t>((steps >> 8) & 0xFF));
  Wire.write(static_cast<uint8_t>(delayUs & 0xFF));
  Wire.write(static_cast<uint8_t>((delayUs >> 8) & 0xFF));
  Wire.endTransmission();
  blinkLed();
}

void sendEnableCommand(uint8_t axis) {
  Wire.beginTransmission(kSlaveAddress);
  Wire.write(CMD_ENABLE);
  Wire.write(axis);
  Wire.endTransmission();
  blinkLed();
}

void sendDisableCommand(uint8_t axis) {
  Wire.beginTransmission(kSlaveAddress);
  Wire.write(CMD_DISABLE);
  Wire.write(axis);
  Wire.endTransmission();
  blinkLed();
}

bool requestSlaveStatus() {
  Wire.requestFrom(kSlaveAddress, static_cast<uint8_t>(1));
  if (Wire.available()) {
    blinkLed();
    return Wire.read() != 0;
  }
  return false;
}

void printUsage() {
  Serial.println("ESP32 I2C Master Interface");
  Serial.println("Commands:");
  Serial.println("  m <axis> <dir> <steps> <delay>  - MOVE");
  Serial.println("  e <axis>                       - ENABLE");
  Serial.println("  d <axis>                       - DISABLE");
  Serial.println("  s                              - STATUS");
  Serial.println("Example: m 0 1 200 500");
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  pinMode(kLedPin, OUTPUT);
  digitalWrite(kLedPin, LOW);

  Wire.begin(kI2cSdaPin, kI2cSclPin);
  Serial.println("I2C master initialized.");
  printUsage();
}

void loop() {
  if (!Serial.available()) {
    return;
  }

  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) {
    return;
  }

  char type = line.charAt(0);
  if (type == 'm' || type == 'M') {
    uint8_t axis = 0;
    uint8_t dir = 0;
    uint16_t steps = 0;
    uint16_t delayUs = 0;
    int parsed = sscanf(line.c_str(), "%*c %hhu %hhu %hu %hu", &axis, &dir, &steps, &delayUs);
    if (parsed == 4) {
      sendMoveCommand(axis, dir, steps, delayUs);
      Serial.println("MOVE command sent.");
    } else {
      Serial.println("Invalid MOVE command format.");
    }
  } else if (type == 'e' || type == 'E') {
    uint8_t axis = 0;
    int parsed = sscanf(line.c_str(), "%*c %hhu", &axis);
    if (parsed == 1) {
      sendEnableCommand(axis);
      Serial.println("ENABLE command sent.");
    } else {
      Serial.println("Invalid ENABLE command format.");
    }
  } else if (type == 'd' || type == 'D') {
    uint8_t axis = 0;
    int parsed = sscanf(line.c_str(), "%*c %hhu", &axis);
    if (parsed == 1) {
      sendDisableCommand(axis);
      Serial.println("DISABLE command sent.");
    } else {
      Serial.println("Invalid DISABLE command format.");
    }
  } else if (type == 's' || type == 'S') {
    bool ready = requestSlaveStatus();
    Serial.printf("Slave command ready: %s\n", ready ? "YES" : "NO");
  } else {
    Serial.println("Unknown command.");
    printUsage();
  }
}
