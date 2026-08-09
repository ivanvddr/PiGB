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
//  Sharp LR35902 (core GB/GBC), bus-agnostico
// ============================================================

typedef uint8_t (*MemReadFn)(uint16_t addr);
typedef void    (*MemWriteFn)(uint16_t addr, uint8_t val);
typedef uint8_t (*IoReadFn)(uint16_t addr);
typedef void    (*IoWriteFn)(uint16_t addr, uint8_t val);

enum GBInterrupt {
    INT_VBLANK = 0, INT_LCDSTAT = 1, INT_TIMER = 2, INT_SERIAL = 3, INT_JOYPAD = 4
};

class LR35902 {
public:
    void begin(MemReadFn mr, MemWriteFn mw, IoReadFn ior, IoWriteFn iow);
    void reset(bool cgbMode = true);

    uint32_t step();
    void requestInterrupt(GBInterrupt which);

    bool doubleSpeed;

    // Stato pubblico: usiamo un array per B, C, D, E, H, L, (HL), A
    // Mappatura: 0=B, 1=C, 2=D, 3=E, 4=H, 5=L, 6=(HL), 7=A
    uint8_t regs[8];
    uint8_t F;
    uint16_t SP, PC;
    bool IME;
    bool halted;

    uint8_t getIF() const { return IF; }
    uint8_t getIE() const { return IE; }
    void setIF(uint8_t v) { IF = v; }
    void setIE(uint8_t v) { IE = v; }

private:
    MemReadFn  memRead;
    MemWriteFn memWrite;
    IoReadFn   ioRead;
    IoWriteFn  ioWrite;

    uint8_t IF, IE;
    bool imeScheduled;
    bool haltBug;

    uint8_t  read8(uint16_t addr);
    void     write8(uint16_t addr, uint8_t val);
    uint16_t read16(uint16_t addr);
    void     write16(uint16_t addr, uint16_t val);

    uint8_t fetch8();
    int8_t  fetch8s();
    uint16_t fetch16();

    // Accesso registri veloce tramite array, forzato in RAM/inline
    __attribute__((always_inline)) inline uint8_t getR(uint8_t idx) {
        if (idx == 6) return read8(getHL());
        return regs[idx];
    }
    __attribute__((always_inline)) inline void setR(uint8_t idx, uint8_t val) {
        if (idx == 6) write8(getHL(), val);
        else regs[idx] = val;
    }

    uint16_t getBC() { return (regs[0] << 8) | regs[1]; }
    void setBC(uint16_t v) { regs[0] = v >> 8; regs[1] = v & 0xFF; }
    uint16_t getDE() { return (regs[2] << 8) | regs[3]; }
    void setDE(uint16_t v) { regs[2] = v >> 8; regs[3] = v & 0xFF; }
    uint16_t getHL() { return (regs[4] << 8) | regs[5]; }
    void setHL(uint16_t v) { regs[4] = v >> 8; regs[5] = v & 0xFF; }
    uint16_t getAF() { return (regs[7] << 8) | (F & 0xF0); }
    void setAF(uint16_t v) { regs[7] = v >> 8; F = v & 0xF0; }

    static const uint8_t FLAG_Z = 0x80;
    static const uint8_t FLAG_N = 0x40;
    static const uint8_t FLAG_H = 0x20;
    static const uint8_t FLAG_C = 0x10;
    inline void setFlag(uint8_t mask, bool v) { if (v) F |= mask; else F &= ~mask; }
    inline bool getFlag(uint8_t mask) { return (F & mask) != 0; }

    void aluAdd(uint8_t v, bool carry);
    void aluSub(uint8_t v, bool carry);
    void aluAnd(uint8_t v);
    void aluOr(uint8_t v);
    void aluXor(uint8_t v);
    void aluCp(uint8_t v);
    uint8_t incR8(uint8_t v);
    uint8_t decR8(uint8_t v);
    void addHL(uint16_t v);
    void addSP_s8();
    void daa();

    uint8_t rlc(uint8_t v); uint8_t rrc(uint8_t v);
    uint8_t rl(uint8_t v);  uint8_t rr(uint8_t v);
    uint8_t sla(uint8_t v); uint8_t sra(uint8_t v);
    uint8_t srl(uint8_t v); uint8_t swap(uint8_t v);

    void push16(uint16_t v);
    uint16_t pop16();
    void jumpRel(bool cond, uint32_t &cyc);
    void jumpAbs(bool cond, uint32_t &cyc);
    void call(bool cond, uint32_t &cyc);
    void ret(bool cond, uint32_t &cyc);

    uint32_t execOpcode(uint8_t op);
    uint32_t execCB();
    void serviceInterrupt(uint8_t vector);
};