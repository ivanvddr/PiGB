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
#include "CPU.h"

// ============================================================
//  DIV / TIMA / TMA / TAC (0xFF04-0xFF07)
// ============================================================
class GBTimer {
public:
    void begin(LR35902 *cpuPtr) { cpu = cpuPtr; reset(); }
    void reset() { divCounter = 0; timaCounter = 0; DIV = 0; TIMA = 0; TMA = 0; TAC = 0xF8; }

    // Avanza il timer di N T-cycle (chiamato dal main loop dopo ogni step CPU)
    void step(uint32_t tcycles) {
        divCounter += tcycles;
        while (divCounter >= 256) { divCounter -= 256; DIV++; }

        if (!(TAC & 0x04)) return; // timer disabilitato

        uint16_t freqDiv;
        switch (TAC & 0x03) {
            case 0: freqDiv = 1024; break; // 4096 Hz
            case 1: freqDiv = 16;   break; // 262144 Hz
            case 2: freqDiv = 64;   break; // 65536 Hz
            default: freqDiv = 256; break; // 16384 Hz
        }
        timaCounter += tcycles;
        while (timaCounter >= freqDiv) {
            timaCounter -= freqDiv;
            TIMA++;
            if (TIMA == 0) { // overflow
                TIMA = TMA;
                cpu->requestInterrupt(INT_TIMER);
            }
        }
    }

    uint8_t readReg(uint16_t addr) {
        switch (addr) {
            case 0xFF04: return DIV;
            case 0xFF05: return TIMA;
            case 0xFF06: return TMA;
            case 0xFF07: return TAC;
        }
        return 0xFF;
    }
    void writeReg(uint16_t addr, uint8_t val) {
        switch (addr) {
            case 0xFF04: DIV = 0; divCounter = 0; break; // scrivere su DIV lo azzera
            case 0xFF05: TIMA = val; break;
            case 0xFF06: TMA = val; break;
            case 0xFF07: TAC = val; break;
        }
    }

private:
    LR35902 *cpu;
    uint8_t DIV, TIMA, TMA, TAC;
    uint32_t divCounter, timaCounter;
};
