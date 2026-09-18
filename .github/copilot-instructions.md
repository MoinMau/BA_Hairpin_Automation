# System-Richtlinien für BA_Hairpin_Automation

## Architektur & System-Topologie
Dieses Projekt automatisiert die Zuführung von Hairpins für das laserbasierte Abisolieren. Es besteht aus drei Controllern, die über den I2C-Bus kommunizieren:
1. **Master (ESP32):** Koordiniert den Gesamtablauf und steuert die Kommunikation nach außen. 
Pin-Belegung: 22 und 21 für I2C-Kommunikation.
Befindet sich in `src_esp32/`.
2. **Slave 1 (Elegoo Uno R3):** Steuert die Schrittmotoren über ein CNC-Shield für die mechanische Bewegung. 
Pin-Belegung: 2 für X.STP, 3 für Y.STP, 4 für Z.STP, 5 für X.DIR, 6 für Y.DIR, 7 für Z.DIR, A4 und A5 für I2C-Kommunikation.
Befindet sich in `src_ELEGOOuno/`.
3. **Slave 2 (Arduino Nano):** Überwacht die Stromaufnahme, Verarbeutet Sensor Daten und Steuert Servo Motoren.
- Pin-Belegung: D7 bis D12 für Servo-Motoren, A0 für Strommessung über Shunt-Widerstand, A4 und A5 für I2C-Kommunikation, D2 und A7 für MH-Sensor.
Befindet sich in `src_Nano/`.

## Programmier-Regeln für die Agenten
- **Architektur:** Verwende sauberen, modularen und übersichtlichen C++ Code für PlatformIO. Bei externen Bibliotheken müssen diese klar dokumentiert in `include/` abgelegt werden und in der platformIO.ini konfiguriert werden.
- **Nicht-blockierend:** Nutze niemals `delay()`. Zeitsteuerungen müssen zwingend zustandsbasiert mit `millis()` gelöst werden, damit die I2C-Kommunikation und Motorsteuerung flüssig laufen.
- **I2C-Bus:** Der ESP32 ist der Master. Der Nano läuft auf Adresse 0x32, der Uno auf 0x33. Datenstrukturen müssen exakt übereinstimmen.
- **Sprache:** Generiere Code-Kommentare und Erklärungen auf Deutsch.