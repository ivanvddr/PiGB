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

// ============================================================
//  CONFIG.H — pinout e costanti globali
// ============================================================

// ---------- DISPLAY (TFT_eSPI configurato per ILI9341 + DMA) ----------
// La configurazione vera e propria di TFT_eSPI (pin SPI, frequenza, driver)
// va fatta nel file User_Setup.h della libreria TFT_eSPI (vedi README.md).
// Qui teniamo solo le costanti utili al progetto.
#define GB_W 160
#define GB_H 144
#define TFT_W 320
#define TFT_H 240
// Offset per centrare il framebuffer GB (160x144) nello schermo 320x240
#define GB_OFFSET_X ((TFT_W - GB_W) / 2)
#define GB_OFFSET_Y ((TFT_H - GB_H) / 2)

// ---------- PALETTE ----------
#define RGB565(r,g,b) ((uint16_t)(((r)>>3)<<11) | (((g)>>2)<<5) | ((b)>>3))

// ---------- PALETTE DMG (verde stile Game Boy originale, 1989) ----------
static const uint16_t DMG_SHADES[4] = {
    RGB565(155, 188, 15),  // 0 - più chiaro
    RGB565(139, 172, 15),  // 1
    RGB565(48,  98,  48),  // 2
    RGB565(15,  56,  15)   // 3 - più scuro
};

// ---------- PALETTE ALTERNATIVA (Game Boy Pocket, 1996 — grigio-verde) ----------
static const uint16_t POCKET_SHADES[4] = {
    RGB565(224, 248, 208), // 0 - più chiaro (quasi bianco-verde)
    RGB565(136, 192, 112), // 1
    RGB565(52,  104, 86),  // 2
    RGB565(8,   24,  32)   // 3 - più scuro (quasi nero-blu)
};

#define USE_POCKET_PALETTE 0   // 0 = DMG verde classico, 1 = Pocket grigio-verde

// ---------- AUDIO (PWM su GPIO15, filtro RC 2 stadi -> PAM8403) ----------
#define AUDIO_PWM_PIN     15
#define AUDIO_SAMPLE_RATE 22050UL
// Risoluzione PWM: 10 bit (0-1023) è un buon compromesso fra dinamica audio
// e frequenza di switching residua dopo il filtro passa-basso a 2 stadi.
#define AUDIO_PWM_BITS    10
#define AUDIO_PWM_MAX     ((1 << AUDIO_PWM_BITS) - 1)

// Dimensione di ciascun buffer del doppio buffer audio (in campioni).
// A 22050Hz con buffer di 256 campioni abbiamo un giro IRQ ogni ~11.6ms,
// abbastanza piccolo da non introdurre latenza percepibile, abbastanza
// grande da non sovraccaricare la IRQ su core1.
#define AUDIO_BUF_LEN     256

// ---------- JOYPAD (croce direzionale passiva a 5 vie + pulsanti) ----------
// Tutti i pin in INPUT_PULLUP, contatto a massa = premuto (logica negata)
#define PIN_BTN_UP     2
#define PIN_BTN_DOWN   3
#define PIN_BTN_LEFT   4
#define PIN_BTN_RIGHT  5
#define PIN_BTN_A      6
#define PIN_BTN_B     26
#define PIN_BTN_START 22
#define PIN_BTN_SELECT 0

// ---------- OVERCLOCK KHZ & PERFORMANCE ----------
#define PICO_CLOCK_KHZ 276000
#define FRAMESKIP 1             // 0 = 60 FPS, 1 = 30 FPS (skippa un frame), 2 = 20 FPS

// ---------- TIMING EMULAZIONE ----------
// Clock GB/GBC: 4.194304 MHz (1.05x in CGB double speed mode -> 8.388608MHz)
#define GB_CLOCK_HZ        4194304UL
#define CYCLES_PER_SCANLINE 456     // T-cycles per linea (in single speed)
#define SCANLINES_PER_FRAME 154     // 144 visibili + 10 di VBlank
#define CYCLES_PER_FRAME   (CYCLES_PER_SCANLINE * SCANLINES_PER_FRAME) // 70224

// SD CARD
#define SD_CS    13
#define SD_MISO  12
#define SD_MOSI  11
#define SD_SCLK  10

