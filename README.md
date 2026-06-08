# BA_Hairpin_Automation
This Project is using a CleanLaser and a Universal Robot Arm to strip the isolation of the Ends of a Hairpin
# BA - Hairpin Automation System

System zur automatischen Vereinzelung und Zuführung von Hairpins für robotergestütztes Laserstrippen.

## Aktueller Projektstatus
- [x] Repository-Struktur mit PlatformIO Multi-Environment steht.
- [x] Erste Hardware Tests mit externen Bauteile etc.
- [ ] Implementierung der analogen Strommessung auf dem Nano (Nächster Schritt).
- [ ] I2C-Protokoll-Definition in `include/`.
- [ ] Schrittmotor-Ansteuerung via CNC-Shield auf dem Uno.

## I2C-Registerbelegung
| Board | Adresse | Funktion |
| :--- | :--- | :--- |
| ESP32 | Master | Ablaufsteuerung & API |
| Uno   | 0x33   | Schrittmotoren (CNC-Shield) |
| Nano  | 0x32   | Analoge Stromüberwachung |