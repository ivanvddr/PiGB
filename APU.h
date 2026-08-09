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

// ============================================================
//  APU.H — Audio Processing Unit (sound chip GB/GBC)
// ============================================================
// Implementazione semplificata ma funzionale dei 4 canali. L'uscita è
// MONO (sommo i canali L/R, ignorando il panning NR51: per un mixer
// stereo vero servirebbero due catene PWM, qui una sola sul
// GPIO15). Il frame sequencer (length/envelope/sweep, 512Hz) viene
// aggiornato in base ai T-cycle CPU reali passati a stepCycles(); il
// campionamento per il buffer PWM avviene con renderSample()


class APU {
public:
    void begin() {
        memset(waveRAM, 0, sizeof(waveRAM));
        NR10=NR11=NR12=NR13=NR14=0;
        NR21=NR22=NR23=NR24=0;
        NR30=NR31=NR32=NR33=NR34=0;
        NR41=NR42=NR43=NR44=0;
        NR50=0x77; NR51=0xF3; NR52=0xF1;
        sq1Enabled=sq2Enabled=waveEnabled=noiseEnabled=false;
        sq1Period=sq2Period=1; sq1Timer=sq2Timer=0;
        sq1Duty=sq2Duty=0; sq1DutyPos=sq2DutyPos=0;
        sq1Vol=sq2Vol=0; sq1EnvCounter=sq2EnvCounter=0;
        sq1LenCounter=sq2LenCounter=0;
        sq1Freq=0; sq1SweepCounter=0; sq1SweepEnabled=false; sq1ShadowFreq=0;
        wavePeriod=1; waveTimerCounter=1; wavePos=0; waveLenCounter=0;
        noisePeriod=1; noiseTimerCounter=1; lfsr=0x7FFF;
        noiseVol=0; noiseEnvCounter=0; noiseLenCounter=0;
        frameSeqCounter = 0; frameSeqStep = 0;
        userVolume = 100;
    }

    uint8_t readReg(uint16_t addr);
    void writeReg(uint16_t addr, uint8_t val);

    // Avanza length/envelope/sweep (frame sequencer) e i generatori di
    // forma d'onda, in base ai T-cycle CPU effettivamente trascorsi.
    void stepCycles(uint32_t tcycles);

    // Ritorna il prossimo campione mixato, range [0, AUDIO_PWM_MAX], pronto
    // per essere scritto nel duty-cycle PWM.
    uint16_t renderSample();

    inline void setUserVolume(uint8_t vol) {
        if (vol > 100) vol = 100;
        userVolume = vol;
    }

private:
    // ---- registri raw (0xFF10-0xFF26) ----
    uint8_t NR10,NR11,NR12,NR13,NR14;       // square1 + sweep
    uint8_t NR21,NR22,NR23,NR24;            // square2
    uint8_t NR30,NR31,NR32,NR33,NR34;       // wave
    uint8_t NR41,NR42,NR43,NR44;            // noise
    uint8_t NR50,NR51,NR52;                 // master volume/panning/power
    uint8_t waveRAM[16];                    // 0xFF30-0xFF3F (32 campioni a 4 bit)

    // ---- stato canale 1 (square + sweep) ----
    bool sq1Enabled; int32_t sq1Period; uint16_t sq1Timer; uint8_t sq1Duty;
    uint8_t sq1Vol; int8_t sq1EnvCounter; uint8_t sq1LenCounter;
    uint16_t sq1Freq; int8_t sq1SweepCounter; bool sq1SweepEnabled; uint16_t sq1ShadowFreq;

    // ---- stato canale 2 (square) ----
    bool sq2Enabled; int32_t sq2Period; uint16_t sq2Timer; uint8_t sq2Duty;
    uint8_t sq2Vol; int8_t sq2EnvCounter; uint8_t sq2LenCounter;

    // ---- stato canale 3 (wave) ----
    bool waveEnabled; int32_t wavePeriod; int32_t waveTimerCounter;
    uint8_t wavePos; uint16_t waveLenCounter;

    // ---- stato canale 4 (noise) ----
    bool noiseEnabled; int32_t noisePeriod; int32_t noiseTimerCounter;
    uint16_t lfsr; uint8_t noiseVol; int8_t noiseEnvCounter; uint8_t noiseLenCounter;

    uint32_t frameSeqCounter; uint8_t frameSeqStep;

    void trigger1(); void trigger2(); void trigger3(); void trigger4();
    void clockLength(); void clockSweep(); void clockEnvelope();
    uint8_t sq1Output(); uint8_t sq2Output(); uint8_t waveOutput(); uint8_t noiseOutput();
    void advanceSq1(uint32_t cyc); void advanceSq2(uint32_t cyc);
    void advanceWave(uint32_t cyc); void advanceNoise(uint32_t cyc);

    uint8_t sq1DutyPos, sq2DutyPos;
    uint8_t userVolume;
};
