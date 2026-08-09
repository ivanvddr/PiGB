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
#include "hardware/sync.h"

// ============================================================
// Pixel Processing Unit Game Boy classico (DMG)
// ============================================================
// Un solo banco VRAM (8KB), OAM (160 byte), niente palette a colori:
// le 4 tonalità sono ricavate da BGP/OBP0/OBP1 e mappate sulle 4
// sfumature di verde in DMG_SHADES (vedi Config.h).

#define VRAM_BANK_SIZE 0x2000
#define OAM_SIZE 160

class PPU {
public:
    void begin(LR35902 *cpuPtr) {
        cpu = cpuPtr;
        memset(vram, 0, sizeof(vram));
        memset(oam, 0, sizeof(oam));

        LCDC = 0x91; STAT = 0x85; SCY = 0; SCX = 0; LY = 0; LYC = 0;
        BGP = 0xFC; OBP0 = 0xFF; OBP1 = 0xFF; WY = 0; WX = 0;
        dotClock = 0;
        frameReady = false;
        memset(framebuffer, 0, sizeof(framebuffer));
    }

    // -------- accesso VRAM/OAM --------
    uint8_t vramRead(uint16_t addr) { return vram[addr & 0x1FFF]; }
    void vramWrite(uint16_t addr, uint8_t v) { vram[addr & 0x1FFF] = v; }
    uint8_t oamRead(uint16_t addr) { return oam[addr & 0xFF]; }
    void oamWrite(uint16_t addr, uint8_t v) { if ((addr & 0xFF) < OAM_SIZE) oam[addr & 0xFF] = v; }

    // -------- registri LCD 0xFF40-0xFF4B (solo DMG) --------
    uint8_t readReg(uint16_t addr) {
        switch (addr) {
            case 0xFF40: return LCDC;
            case 0xFF41: return STAT | 0x80;
            case 0xFF42: return SCY;
            case 0xFF43: return SCX;
            case 0xFF44: return LY;
            case 0xFF45: return LYC;
            case 0xFF47: return BGP;
            case 0xFF48: return OBP0;
            case 0xFF49: return OBP1;
            case 0xFF4A: return WY;
            case 0xFF4B: return WX;
        }
        return 0xFF;
    }

    void writeReg(uint16_t addr, uint8_t val, uint8_t (*memReadCb)(uint16_t)) {
        switch (addr) {
            case 0xFF40: LCDC = val; break;
            case 0xFF41: STAT = (STAT & 0x07) | (val & 0x78); break;
            case 0xFF42: SCY = val; break;
            case 0xFF43: SCX = val; break;
            case 0xFF44: break; // LY read-only
            case 0xFF45: LYC = val; break;
            case 0xFF47: BGP = val; break;
            case 0xFF48: OBP0 = val; break;
            case 0xFF49: OBP1 = val; break;
            case 0xFF4A: WY = val; break;
            case 0xFF4B: WX = val; break;
        }
    }

    void step(uint32_t tcycles);

    bool consumeFrameReady() { bool r = frameReady; frameReady = false; return r; }
    uint16_t *getFramebuffer() { return &framebuffer[0][0]; }
    //uint16_t *getDisplayFramebuffer() { return &framebufferDisplay[0][0]; }
    uint16_t *getDisplayFramebuffer() { return &framebuffer[0][0]; }
    uint8_t oam[OAM_SIZE];
    bool skipRendering = false;
    inline void oamDmaCopy(uint16_t src, uint8_t (*memReadCb)(uint16_t)) {
        for (int i = 0; i < OAM_SIZE; i++) oam[i] = memReadCb(src + i);
    }

private:
    LR35902 *cpu;
    uint8_t vram[VRAM_BANK_SIZE];

    uint8_t LCDC, STAT, SCY, SCX, LY, LYC, BGP, OBP0, OBP1, WY, WX;
    uint32_t dotClock;
    volatile bool frameReady;
    uint16_t framebuffer[GB_H][GB_W];
    // uint16_t framebufferDisplay[GB_H][GB_W];

    void renderScanline(uint8_t line);
    void checkLYC();

    // Converte un colorIndex (0-3) tramite un registro palette (BGP/OBP0/OBP1)
    // nella corrispondente sfumatura di verde.
    static inline uint16_t applyPalette(uint8_t paletteReg, uint8_t colorIndex) {
        uint8_t shade = (paletteReg >> (colorIndex * 2)) & 0x03;
    #if USE_POCKET_PALETTE
        return POCKET_SHADES[shade];
    #else
        return DMG_SHADES[shade];
    #endif
    }
};