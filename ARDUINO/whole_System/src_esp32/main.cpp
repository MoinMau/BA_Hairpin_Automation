#include <Arduino.h>
#include <Wire.h>

static const int kI2cSdaPin = 21;
static const int kI2cSclPin = 22;
static const int kLedPin = 2;
static const uint8_t kSlaveAddress = 0x08;
static const uint8_t kAxisCount = 3;
static const char kAxisNames[kAxisCount] = {'x', 'y', 'z'};

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

uint8_t axisFromToken(const String& token) {
  if (token.length() == 0) {
    return 255;
  }
  char c = tolower(token.charAt(0));
  for (uint8_t i = 0; i < kAxisCount; ++i) {
    if (c == kAxisNames[i]) {
      return i;
    }
  }
  char* endPtr = nullptr;
  long value = strtol(token.c_str(), &endPtr, 10);
  if (*endPtr == '\0' && value >= 0 && value < kAxisCount) {
    return static_cast<uint8_t>(value);
  }
  return 255;
}

bool parseDirection(const String& token, uint8_t& dir) {
  if (token.length() == 0) {
    return false;
  }
  char c = tolower(token.charAt(0));
  if (c == '+' || c == 'f' || c == '1') {
    dir = 1;
    return true;
  }
  if (c == '-' || c == 'b' || c == '0') {
    dir = 0;
    return true;
  }
  return false;
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

void scanI2CBus() {
  Serial.println("I2C-Scan startet...");
  bool foundAny = false;

  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();
    if (error == 0) {
      Serial.printf("I2C-Gerät gefunden: 0x%02X\n", address);
      foundAny = true;
    } else if (error == 4) {
      Serial.printf("I2C-Fehler bei Adresse: 0x%02X\n", address);
    }
  }

  if (!foundAny) {
    Serial.println("Keine I2C-Geräte gefunden.");
  }
  Serial.println("I2C-Scan fertig.");
}

void printUsage() {
  Serial.println("ESP32 I2C Master Interface");
  Serial.println("Commands:");
  Serial.println("  move <axis> <dir> <speed> <duration>  - MOVE with speed [steps/s] and duration [ms]");
  Serial.println("  step <axis> <dir> <steps> <delay>     - MOVE with explicit steps and step delay [us]");
  Serial.println("  enable <axis>                         - ENABLE axis");
  Serial.println("  disable <axis>                        - DISABLE axis");
  Serial.println("  status                                - STATUS request");
  Serial.println("  scan                                  - I2C scan");
  Serial.println("Axis: x, y, z or 0,1,2");
  Serial.println("Dir: +, -, f, b, 1, 0");
  Serial.println("Example: move x + 200 1500");
  Serial.println("         step 1 0 400 800");
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
  scanI2CBus();
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

  int argCount = 0;
  String args[6];
  int pos = 0;
  while (pos < line.length() && argCount < 6) {
    int start = pos;
    while (pos < line.length() && !isSpace(line.charAt(pos))) {
      pos++;
    }
    args[argCount++] = line.substring(start, pos);
    while (pos < line.length() && isSpace(line.charAt(pos))) {
      pos++;
    }
  }

  String cmd = args[0];
  cmd.toLowerCase();

  if (cmd == "move" && argCount == 5) {
    uint8_t axis = axisFromToken(args[1]);
    uint8_t dir = 0;
    uint16_t speed = static_cast<uint16_t>(args[3].toInt());
    uint16_t durationMs = static_cast<uint16_t>(args[4].toInt());
    if (axis == 255 || !parseDirection(args[2], dir) || speed == 0 || durationMs == 0) {
      Serial.println("Invalid MOVE command format.");
      printUsage();
      return;
    }
    uint32_t steps = (static_cast<uint32_t>(speed) * durationMs + 999) / 1000;
    if (steps > 65535) {
      Serial.println("Steps value too large for this command.");
      return;
    }
    uint32_t delayUs = 500000UL / speed;
    if (delayUs < 10) {
      delayUs = 10;
    }
    sendMoveCommand(axis, dir, static_cast<uint16_t>(steps), static_cast<uint16_t>(delayUs));
    Serial.printf("MOVE %c dir=%u speed=%u duration=%ums -> steps=%u delay=%u\n", kAxisNames[axis], dir, speed, durationMs, steps, delayUs);
  } else if (cmd == "step" && argCount == 5) {
    uint8_t axis = axisFromToken(args[1]);
    uint8_t dir = 0;
    uint32_t steps = static_cast<uint32_t>(args[3].toInt());
    uint32_t delayUs = static_cast<uint32_t>(args[4].toInt());
    if (axis == 255 || !parseDirection(args[2], dir) || steps == 0 || delayUs == 0 || steps > 65535 || delayUs > 65535) {
      Serial.println("Invalid STEP command format.");
      printUsage();
      return;
    }
    sendMoveCommand(axis, dir, static_cast<uint16_t>(steps), static_cast<uint16_t>(delayUs));
    Serial.printf("STEP %c dir=%u steps=%u delay=%u\n", kAxisNames[axis], dir, steps, delayUs);
  } else if (cmd == "enable" && argCount == 2) {
    uint8_t axis = axisFromToken(args[1]);
    if (axis == 255) {
      Serial.println("Invalid ENABLE command format.");
      printUsage();
      return;
    }
    sendEnableCommand(axis);
    Serial.printf("ENABLE %c\n", kAxisNames[axis]);
  } else if (cmd == "disable" && argCount == 2) {
    uint8_t axis = axisFromToken(args[1]);
    if (axis == 255) {
      Serial.println("Invalid DISABLE command format.");
      printUsage();
      return;
    }
    sendDisableCommand(axis);
    Serial.printf("DISABLE %c\n", kAxisNames[axis]);
  } else if (cmd == "status") {
    bool ready = requestSlaveStatus();
    Serial.printf("Slave command ready: %s\n", ready ? "YES" : "NO");
  } else if (cmd == "scan") {
    scanI2CBus();
  } else {
    Serial.println("Unknown command.");
    printUsage();
  }
}
