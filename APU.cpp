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

#include "APU.h"

#ifndef __not_in_flash_func
#define __not_in_flash_func(func) __attribute__((section(".time_critical." #func), noinline)) func
#endif

static const uint8_t DUTY_TABLE[4] = { 0x01, 0x81, 0x87, 0x7E }; // 12.5%,25%,50%,75% (bit0=primo step)

uint8_t APU::readReg(uint16_t addr) {
    switch (addr) {
        case 0xFF10: return NR10 | 0x80;
        case 0xFF11: return NR11 | 0x3F;
        case 0xFF12: return NR12;
        case 0xFF13: return 0xFF;
        case 0xFF14: return NR14 | 0xBF;
        case 0xFF16: return NR21 | 0x3F;
        case 0xFF17: return NR22;
        case 0xFF18: return 0xFF;
        case 0xFF19: return NR24 | 0xBF;
        case 0xFF1A: return NR30 | 0x7F;
        case 0xFF1B: return 0xFF;
        case 0xFF1C: return NR32 | 0x9F;
        case 0xFF1D: return 0xFF;
        case 0xFF1E: return NR34 | 0xBF;
        case 0xFF20: return NR41 | 0xC0;
        case 0xFF21: return NR42;
        case 0xFF22: return NR43;
        case 0xFF23: return NR44 | 0xBF;
        case 0xFF24: return NR50;
        case 0xFF25: return NR51;
        case 0xFF26: return NR52 | 0x70;
        default:
            if (addr >= 0xFF30 && addr <= 0xFF3F) return waveRAM[addr - 0xFF30];
            return 0xFF;
    }
}

void APU::writeReg(uint16_t addr, uint8_t val) {
    if (!(NR52 & 0x80) && addr != 0xFF26 && !(addr >= 0xFF30 && addr <= 0xFF3F)) return; // APU spenta ignora scritture (eccetto wave RAM)
    switch (addr) {
        case 0xFF10: NR10 = val; break;
        case 0xFF11: NR11 = val; sq1Duty = (val >> 6) & 3; sq1LenCounter = 64 - (val & 0x3F); break;
        case 0xFF12: NR12 = val; break;
        case 0xFF13: NR13 = val; break;
        case 0xFF14: NR14 = val; sq1Freq = NR13 | ((val & 0x07) << 8);
            sq1Period = (2048 - sq1Freq) * 4;
            if (val & 0x80) trigger1();
            break;
        case 0xFF16: NR21 = val; sq2Duty = (val >> 6) & 3; sq2LenCounter = 64 - (val & 0x3F); break;
        case 0xFF17: NR22 = val; break;
        case 0xFF18: NR23 = val; break;
        case 0xFF19: NR24 = val;
            sq2Period = (2048 - (NR23 | ((val & 0x07) << 8))) * 4;
            if (val & 0x80) trigger2();
            break;
        case 0xFF1A: NR30 = val; break;
        case 0xFF1B: NR31 = val; waveLenCounter = 256 - val; break;
        case 0xFF1C: NR32 = val; break;
        case 0xFF1D: NR33 = val; break;
        case 0xFF1E: NR34 = val;
            wavePeriod = (2048 - (NR33 | ((val & 0x07) << 8))) * 2;
            if (val & 0x80) trigger3();
            break;
        case 0xFF20: NR41 = val; noiseLenCounter = 64 - (val & 0x3F); break;
        case 0xFF21: NR42 = val; break;
        case 0xFF22: NR43 = val; {
            uint8_t shift = (val >> 4) & 0x0F;
            uint8_t divCode = val & 0x07;
            static const uint8_t divisors[8] = {8,16,32,48,64,80,96,112};
            noisePeriod = ((uint32_t)divisors[divCode]) << shift;
        } break;
        case 0xFF23: NR44 = val; if (val & 0x80) trigger4(); break;
        case 0xFF24: NR50 = val; break;
        case 0xFF25: NR51 = val; break;
        case 0xFF26:
            NR52 = (NR52 & 0x0F) | (val & 0x80);
            if (!(val & 0x80)) { sq1Enabled=sq2Enabled=waveEnabled=noiseEnabled=false; }
            break;
        default:
            if (addr >= 0xFF30 && addr <= 0xFF3F) waveRAM[addr - 0xFF30] = val;
            break;
    }
}

void APU::trigger1() {
    sq1Enabled = true;
    if (sq1LenCounter == 0) sq1LenCounter = 64;
    sq1Vol = (NR12 >> 4) & 0x0F;
    sq1EnvCounter = NR12 & 0x07;
    sq1ShadowFreq = sq1Freq;
    sq1SweepCounter = (NR10 >> 4) & 0x07;
    sq1SweepEnabled = ((NR10 >> 4) & 0x07) || (NR10 & 0x07);
}
void APU::trigger2() {
    sq2Enabled = true;
    if (sq2LenCounter == 0) sq2LenCounter = 64;
    sq2Vol = (NR22 >> 4) & 0x0F;
    sq2EnvCounter = NR22 & 0x07;
}
void APU::trigger3() {
    waveEnabled = (NR30 & 0x80) != 0;
    if (waveLenCounter == 0) waveLenCounter = 256;
    wavePos = 0;
}
void APU::trigger4() {
    noiseEnabled = true;
    if (noiseLenCounter == 0) noiseLenCounter = 64;
    noiseVol = (NR42 >> 4) & 0x0F;
    noiseEnvCounter = NR42 & 0x07;
    lfsr = 0x7FFF;
}

void APU::clockLength() {
    if ((NR14 & 0x40) && sq1LenCounter > 0) { if (--sq1LenCounter == 0) sq1Enabled = false; }
    if ((NR24 & 0x40) && sq2LenCounter > 0) { if (--sq2LenCounter == 0) sq2Enabled = false; }
    if ((NR34 & 0x40) && waveLenCounter > 0) { if (--waveLenCounter == 0) waveEnabled = false; }
    if ((NR44 & 0x40) && noiseLenCounter > 0) { if (--noiseLenCounter == 0) noiseEnabled = false; }
}
void APU::clockSweep() {
    if (!sq1SweepEnabled) return;
    uint8_t period = (NR10 >> 4) & 0x07;
    if (sq1SweepCounter > 0) sq1SweepCounter--;
    if (sq1SweepCounter == 0) {
        sq1SweepCounter = period ? period : 8;
        if (period > 0) {
            uint8_t shift = NR10 & 0x07;
            int16_t delta = sq1ShadowFreq >> shift;
            int16_t newFreq = (NR10 & 0x08) ? (sq1ShadowFreq - delta) : (sq1ShadowFreq + delta);
            if (newFreq > 2047) { sq1Enabled = false; }
            else if (shift > 0) {
                sq1ShadowFreq = newFreq;
                sq1Freq = newFreq;
                sq1Period = (2048 - sq1Freq) * 4;
            }
        }
    }
}
void APU::clockEnvelope() {
    auto stepEnv = [](uint8_t nrX2, uint8_t &vol, int8_t &counter) {
        uint8_t period = nrX2 & 0x07;
        if (period == 0) return;
        if (counter > 0) counter--;
        if (counter == 0) {
            counter = period;
            bool up = nrX2 & 0x08;
            if (up && vol < 15) vol++;
            else if (!up && vol > 0) vol--;
        }
    };
    stepEnv(NR12, sq1Vol, sq1EnvCounter);
    stepEnv(NR22, sq2Vol, sq2EnvCounter);
    stepEnv(NR42, noiseVol, noiseEnvCounter);
}

void __not_in_flash_func(APU::stepCycles)(uint32_t tcycles) {
    if (!(NR52 & 0x80)) return;

    // Frame sequencer: 512Hz -> ogni 8192 T-cycle
    frameSeqCounter += tcycles;
    while (frameSeqCounter >= 8192) {
        frameSeqCounter -= 8192;
        switch (frameSeqStep) {
            case 0: clockLength(); break;
            case 2: clockLength(); clockSweep(); break;
            case 4: clockLength(); break;
            case 6: clockLength(); clockSweep(); break;
            case 7: clockEnvelope(); break;
        }
        frameSeqStep = (frameSeqStep + 1) & 7;
    }

    advanceSq1(tcycles);
    advanceSq2(tcycles);
    advanceWave(tcycles);
    advanceNoise(tcycles);
}

void APU::advanceSq1(uint32_t cyc) {
    if (!sq1Enabled || sq1Period <= 0) return;
    sq1Timer += cyc;
    while (sq1Timer >= sq1Period) { 
        sq1Timer -= sq1Period; 
        sq1DutyPos = (sq1DutyPos + 1) & 7; 
    }
}
void APU::advanceSq2(uint32_t cyc) {
    if (!sq2Enabled || sq2Period <= 0) return;
    sq2Timer += cyc;
    while (sq2Timer >= sq2Period) { 
        sq2Timer -= sq2Period; 
        sq2DutyPos = (sq2DutyPos + 1) & 7; 
    }
}
void APU::advanceWave(uint32_t cyc) {
    if (!waveEnabled || wavePeriod <= 0) return;
    waveTimerCounter += cyc;
    while (waveTimerCounter >= wavePeriod) { 
        waveTimerCounter -= wavePeriod; 
        wavePos = (wavePos + 1) & 31; 
    }
}
void APU::advanceNoise(uint32_t cyc) {
    if (!noiseEnabled || noisePeriod <= 0) return;
    noiseTimerCounter += cyc;
    while (noiseTimerCounter >= noisePeriod) {
        noiseTimerCounter -= noisePeriod;
        bool bit = ((lfsr & 1) ^ ((lfsr >> 1) & 1)) != 0;
        lfsr >>= 1;
        if (bit) lfsr |= (1 << 14);
        if (NR43 & 0x08) { lfsr &= ~(1 << 6); if (bit) lfsr |= (1 << 6); }
    }
}

uint8_t APU::sq1Output() {
    if (!sq1Enabled) return 0;
    bool bit = (DUTY_TABLE[sq1Duty] >> sq1DutyPos) & 1;
    return bit ? sq1Vol : 0;
}
uint8_t APU::sq2Output() {
    if (!sq2Enabled) return 0;
    bool bit = (DUTY_TABLE[sq2Duty] >> sq2DutyPos) & 1;
    return bit ? sq2Vol : 0;
}
uint8_t APU::waveOutput() {
    if (!waveEnabled || !(NR30 & 0x80)) return 0;
    uint8_t byteVal = waveRAM[wavePos >> 1];
    uint8_t nibble = (wavePos & 1) ? (byteVal & 0x0F) : (byteVal >> 4);
    uint8_t shiftCode = (NR32 >> 5) & 0x03;
    static const uint8_t shiftAmt[4] = {4,0,1,2}; // mute,100%,50%,25%
    return nibble >> shiftAmt[shiftCode];
}
uint8_t APU::noiseOutput() {
    if (!noiseEnabled) return 0;
    bool bit = (lfsr & 1) == 0;
    return bit ? noiseVol : 0;
}

uint16_t __not_in_flash_func(APU::renderSample)() {
    if (!(NR52 & 0x80)) return AUDIO_PWM_MAX / 2; // APU spenta: vero silenzio (metà scala)

    // 1. Somma dei canali (max teorico 60)
    int16_t mix = sq1Output() + sq2Output() + waveOutput() + noiseOutput();
    
    // 2. Compressione per evitare picchi aggressivi
    int16_t compressedMix = (mix * 3) >> 2; 

    // 3. Volume master (media L/R)
    uint8_t volL = (NR50 >> 4) & 7, volR = NR50 & 7;
    uint8_t vol = (volL + volR) >> 1; // 0..7
    
    // 4. Scala finale (max 36 * 8 = 288)
    int32_t scaled = (int32_t)compressedMix * (vol + 1);
    
    // 5. Centro perfetto su metà scala PWM (DC Bias = 0)
    int32_t centered = scaled - 144; 
    
    // 6. Guadagno verso PWM (molto conservativo)
    int32_t out = (AUDIO_PWM_MAX / 2) + ((centered * 3) >> 1);

    // 7. APPLICA IL VOLUME DELL'UTENTE (0-100)
    // *** OTTIMIZZAZIONE: Il 100% dell'utente corrisponde ora al 75% fisico ***
    // Moltiplichiamo per 75 invece di 100, e dividiamo per 10000 (invece di 100)
    // per gestire la matematica intera a 32 bit senza overflow.
    if (userVolume > 0) {
        int32_t dev = out - (AUDIO_PWM_MAX / 2);
        out = (AUDIO_PWM_MAX / 2) + ((dev * userVolume * 75) / 10000);
    } else {
        out = AUDIO_PWM_MAX / 2; // Muto totale se volume è 0
    }

    // 8. Clamp di sicurezza
    if (out < 0) out = 0;
    if (out > AUDIO_PWM_MAX) out = AUDIO_PWM_MAX;
    return (uint16_t)out;
}
