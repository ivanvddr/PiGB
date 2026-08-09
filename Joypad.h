/*
 * Questo file fa parte del progetto PiGB.
 *
 * Copyright (C) 2026 Ivan Vettori
 *
 * Questo file è opera originale dell'autore e viene rilasciato
 * sotto licenza GNU General Public License v3.0.
 * Puoi ridistribuirlo e/o modificarlo secondo i termini della GPLv3.
 *
 * Questo programma è distribuito nella speranza che sia utile,
 * ma SENZA ALCUNA GARANZIA; senza neppure la garanzia implicita
 * di COMMERCIABILITÀ o IDONEITÀ PER UN PARTICOLARE SCOPO.
 * Vedi la licenza GPLv3 per maggiori dettagli.
 */

#pragma once
#include <Arduino.h>
#include "Config.h"
#include "CPU.h"

inline void inputInit() {
    pinMode(PIN_BTN_UP, INPUT_PULLUP);
    pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
    pinMode(PIN_BTN_LEFT, INPUT_PULLUP);
    pinMode(PIN_BTN_RIGHT, INPUT_PULLUP);
    pinMode(PIN_BTN_A, INPUT_PULLUP);
    pinMode(PIN_BTN_B, INPUT_PULLUP);
    pinMode(PIN_BTN_START, INPUT_PULLUP);
    pinMode(PIN_BTN_SELECT, INPUT_PULLUP);
};

enum {
  JOY_HW_UP    = 0x01,
  JOY_HW_DOWN  = 0x02,
  JOY_HW_LEFT  = 0x04,
  JOY_HW_RIGHT = 0x08,
  JOY_HW_FIRE  = 0x10,
};

inline uint8_t joystickReadHardware() {
  uint8_t state = 0xFF;

  if (digitalRead(PIN_BTN_UP)    == LOW) state &= ~JOY_HW_UP;
  if (digitalRead(PIN_BTN_DOWN)  == LOW) state &= ~JOY_HW_DOWN;
  if (digitalRead(PIN_BTN_LEFT)  == LOW) state &= ~JOY_HW_LEFT;
  if (digitalRead(PIN_BTN_DOWN) == LOW) state &= ~JOY_HW_RIGHT;
  if (digitalRead(PIN_BTN_A) == LOW) state &= ~JOY_HW_FIRE;

  return state;
};

// ============================================================
//  JOYPAD — croce direzionale passiva 5 vie + 4 pulsanti
// ============================================================
// Registro 0xFF00 (JOYP):
//   bit 5: seleziona pulsanti azione (0=selezionato)
//   bit 4: seleziona pulsanti direzione (0=selezionato)
//   bit 3-0: stato letto (0=premuto), in base alla selezione bit5/bit4
//     direzione: 3=Down 2=Up 1=Left 0=Right
//     azione:    3=Start 2=Select 1=B 0=A
class Joypad {
public:

    void begin(LR35902 *cpuPtr) {
        cpu = cpuPtr;
        inputInit();
        selectBits = 0x30;
        prevState = 0x0F;
    }

    // Da chiamare periodicamente da core0 (poche volte per frame bastano,
    // i pulsanti meccanici non hanno bisogno di essere letti ogni T-cycle).
    void poll() {
        uint8_t dirState =
            (digitalRead(PIN_BTN_RIGHT) << 0) |
            (digitalRead(PIN_BTN_LEFT)  << 1) |
            (digitalRead(PIN_BTN_UP)    << 2) |
            (digitalRead(PIN_BTN_DOWN)  << 3);
        uint8_t actState =
            (digitalRead(PIN_BTN_A)      << 0) |
            (digitalRead(PIN_BTN_B)      << 1) |
            (digitalRead(PIN_BTN_SELECT) << 2) |
            (digitalRead(PIN_BTN_START)  << 3);
        // pull-up: 1 = non premuto, 0 = premuto. Manteniamo questa polarità
        // (coincide con quella richiesta dal registro JOYP).
        dirBits = dirState;
        actBits = actState;

        uint8_t cur = (selectBits & 0x10) ? dirBits : ((selectBits & 0x20) ? actBits : 0x0F);
        if (cur != prevState && cur != 0x0F) {
            cpu->requestInterrupt(INT_JOYPAD);
        }
        prevState = cur;
    }

    uint8_t readReg() {
        uint8_t res = selectBits | 0xC0;
        if (!(selectBits & 0x10)) res |= dirBits;       // bit4=0 -> direzione selezionata
        else if (!(selectBits & 0x20)) res |= actBits;  // bit5=0 -> azione selezionata
        else res |= 0x0F;
        return res;
    }
    void writeReg(uint8_t val) {
        selectBits = val & 0x30;
    }

private:
    LR35902 *cpu;
    uint8_t dirBits = 0x0F, actBits = 0x0F;
    uint8_t selectBits;
    uint8_t prevState;
};
