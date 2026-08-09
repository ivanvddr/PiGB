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
 *
 * ============================================================
 *  Game Boy classico (DMG)
 * ============================================================
 */

#include "PPU.h"

void __not_in_flash_func(PPU::checkLYC)() {
    bool match = (LY == LYC);
    if (match) STAT |= 0x04; else STAT &= ~0x04;
    if (match && (STAT & 0x40)) cpu->requestInterrupt(INT_LCDSTAT);
}

void __not_in_flash_func(PPU::step)(uint32_t tcycles) {
    if (!(LCDC & 0x80)) {
        LY = 0; STAT = (STAT & 0xF8); return;
    }

    dotClock += tcycles;
    uint8_t mode = STAT & 0x03;

    while (true) {
        uint32_t modeLen = (mode == 2) ? 80 : (mode == 3) ? 172 : (mode == 0) ? 204 : 456;
        if (dotClock < modeLen) break;
        dotClock -= modeLen;

        if (mode == 2) {
            mode = 3;
        } else if (mode == 3) {
            if (!skipRendering) renderScanline(LY);
            mode = 0;
            if (STAT & 0x08) cpu->requestInterrupt(INT_LCDSTAT);
        } else if (mode == 0) {
            LY++;
            checkLYC();
            if (LY == GB_H) {
                mode = 1;
                if (!skipRendering) {
                    // memcpy(framebufferDisplay, framebuffer, sizeof(framebuffer));
                    __dmb();
                    frameReady = true;
                }
                cpu->requestInterrupt(INT_VBLANK);
                if (STAT & 0x10) cpu->requestInterrupt(INT_LCDSTAT);
            } else {
                mode = 2;
                if (STAT & 0x20) cpu->requestInterrupt(INT_LCDSTAT);
            }
        } else {
            LY++;
            if (LY > 153) {
                LY = 0;
                mode = 2;
                checkLYC();
                if (STAT & 0x20) cpu->requestInterrupt(INT_LCDSTAT);
            } else {
                checkLYC();
            }
        }
        STAT = (STAT & 0xFC) | mode;
    }
}

void __not_in_flash_func(PPU::renderScanline)(uint8_t line) {
    if (line >= GB_H || skipRendering) return;

    bool bgEnable    = LCDC & 0x01; // in DMG questo bit spegne BG *e* Window
    bool winEnable   = (LCDC & 0x20) && bgEnable;
    bool objEnable   = LCDC & 0x02;
    bool tallSprites = LCDC & 0x04;
    uint16_t bgTileMapBase  = (LCDC & 0x08) ? 0x1C00 : 0x1800;
    uint16_t winTileMapBase = (LCDC & 0x40) ? 0x1C00 : 0x1800;
    bool unsignedTiles    = LCDC & 0x10;
    uint16_t tileDataBase = unsignedTiles ? 0x0000 : 0x1000;

    uint8_t bgColorIndexLine[GB_W];
    memset(bgColorIndexLine, 0, sizeof(bgColorIndexLine));

    // ---------------- BACKGROUND + WINDOW ----------------
    // In DMG le tile non hanno byte di attributo: niente flip/priorità/palette
    // per-tile, un solo banco VRAM.
    uint8_t cached_lo = 0, cached_hi = 0;
    bool prevInWindow = !winEnable;

    if (!bgEnable) {
        // BG/Window spenti: il colore 0 riempie tutta la riga (bianco/verde chiaro)
        uint16_t col = applyPalette(BGP, 0);
        for (int x = 0; x < GB_W; x++) framebuffer[line][x] = col;
    } else {
        for (int x = 0; x < GB_W; x++) {
            bool inWindow = winEnable && line >= WY && x >= (WX - 7);
            uint8_t mapX, mapY;
            uint16_t mapBase;

            if (inWindow) {
                mapX = x - (WX - 7);
                mapY = line - WY;
                mapBase = winTileMapBase;
            } else {
                mapX = (x + SCX) & 0xFF;
                mapY = (line + SCY) & 0xFF;
                mapBase = bgTileMapBase;
            }

            uint8_t pixX = mapX & 7;
            uint8_t pixY = mapY & 7;

            if (pixX == 0 || x == 0 || inWindow != prevInWindow) {
                prevInWindow = inWindow;
                uint8_t tileX = mapX >> 3;
                uint8_t tileY = mapY >> 3;
                uint16_t tileMapAddr = mapBase + (tileY << 5) + tileX;
                uint8_t tileIdx = vram[tileMapAddr];

                uint16_t tileAddr = unsignedTiles ? (tileDataBase + (tileIdx << 4))
                                                   : (tileDataBase + ((int8_t)tileIdx << 4));
                uint16_t dataAddr = tileAddr + (pixY << 1);
                cached_lo = vram[dataAddr & 0x1FFF];
                cached_hi = vram[(dataAddr + 1) & 0x1FFF];
            }

            uint8_t bit = 7 - pixX;
            uint8_t colorIdx = (((cached_hi >> bit) & 1) << 1) | ((cached_lo >> bit) & 1);

            bgColorIndexLine[x] = colorIdx;
            framebuffer[line][x] = applyPalette(BGP, colorIdx);
        }
    }

    // ---------------- SPRITES ----------------
    if (objEnable) {
        int spriteHeight = tallSprites ? 16 : 8;
        int candidates[10]; int nCand = 0;

        for (int i = 0; i < 40 && nCand < 10; i++) {
            uint8_t oy = oam[i * 4 + 0];
            uint8_t target = line + 16;
            if (target >= oy && target < oy + spriteHeight) {
                candidates[nCand++] = i;
            }
        }

        for (int ci = nCand - 1; ci >= 0; ci--) {
            int i = candidates[ci];
            uint8_t oy = oam[i * 4 + 0];
            uint8_t ox = oam[i * 4 + 1];
            uint8_t tile = oam[i * 4 + 2];
            uint8_t attr = oam[i * 4 + 3];

            int sy = (int)oy - 16;
            int sx = (int)ox - 8;
            bool flipX = attr & 0x20;
            bool flipY = attr & 0x40;
            bool behindBG = attr & 0x80;
            uint8_t paletteReg = (attr & 0x10) ? OBP1 : OBP0; // bit4 = selezione palette in DMG

            int row = line - sy;
            if (flipY) row = spriteHeight - 1 - row;
            uint8_t useTile = tile;
            if (tallSprites) {
                useTile &= 0xFE;
                if (row >= 8) { useTile |= 1; row -= 8; }
            }

            uint16_t tileAddr = (useTile << 4) + (row << 1);
            uint8_t lo = vram[tileAddr & 0x1FFF];
            uint8_t hi = vram[(tileAddr + 1) & 0x1FFF];

            for (int col = 0; col < 8; col++) {
                int px = sx + col;
                if (px < 0 || px >= GB_W) continue;
                int bitcol = flipX ? col : (7 - col);
                uint8_t colorIdx = (((hi >> bitcol) & 1) << 1) | ((lo >> bitcol) & 1);
                if (colorIdx == 0) continue; // trasparente

                uint8_t bgIdx = bgColorIndexLine[px];
                if (behindBG && bgIdx != 0) continue; // sprite dietro il BG

                framebuffer[line][px] = applyPalette(paletteReg, colorIdx);
            }
        }
    }
}