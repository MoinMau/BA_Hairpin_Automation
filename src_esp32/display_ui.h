#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include <Arduino.h>

// ============================================================================
// Display-UI (TEST-STUFE)
// ----------------------------------------------------------------------------
// Diese Stufe dient ausschliesslich dazu, Hardware und Bedienung zu pruefen:
//   - laeuft das Panel (Groesse, Offsets, Farben)?
//   - sind alle fuenf Taster richtig verdrahtet?
//   - fuehlt sich die Navigation gut an?
//
// Es wird noch KEIN I2C-Kommando ausgeloest. Die Anbindung an die
// Ablaufsteuerung kommt in der naechsten Stufe (siehe docs/menu_plan.md).
//
// Aufruf: ui_begin() in setup(), ui_update() in jedem loop()-Durchlauf.
// Beides ist nicht-blockierend.
// ============================================================================

void ui_begin();
void ui_update();

#endif // DISPLAY_UI_H
