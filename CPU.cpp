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
#include "CPU.h"

// Definizione di sicurezza per l'attributo RAM
#ifndef __not_in_flash_func
#define __not_in_flash_func(func) __attribute__((section(".time_critical." #func), noinline)) func
#endif

void LR35902::begin(MemReadFn mr, MemWriteFn mw, IoReadFn ior, IoWriteFn iow) {
    memRead = mr; memWrite = mw; ioRead = ior; ioWrite = iow;
    reset();
}

void LR35902::reset(bool cgbMode) {
    regs[7] = cgbMode ? 0x11 : 0x01; // A
    F = 0xB0;
    regs[0] = 0x00; regs[1] = 0x13; // B, C
    regs[2] = 0x00; regs[3] = 0xD8; // D, E
    regs[4] = 0x01; regs[5] = 0x4D; // H, L
    SP = 0xFFFE;
    PC = 0x0100;
    IME = false;
    halted = false;
    imeScheduled = false;
    haltBug = false;
    doubleSpeed = false;
    IF = 0xE1;
    IE = 0x00;
}

uint8_t __not_in_flash_func(LR35902::read8) (uint16_t addr) {
    if (addr == 0xFF0F) return IF | 0xE0;
    if (addr == 0xFFFF) return IE;
    if (addr >= 0xFF00 && addr <= 0xFF7F) return ioRead(addr);
    return memRead(addr);
}
void __not_in_flash_func(LR35902::write8) (uint16_t addr, uint8_t val) {
    if (addr == 0xFF0F) { IF = val & 0x1F; return; }
    if (addr == 0xFFFF) { IE = val; return; }
    if (addr >= 0xFF00 && addr <= 0xFF7F) { ioWrite(addr, val); return; }
    memWrite(addr, val);
}
uint16_t LR35902::read16(uint16_t addr) {
    return (uint16_t)read8(addr) | ((uint16_t)read8(addr + 1) << 8);
}
void LR35902::write16(uint16_t addr, uint16_t val) {
    write8(addr, val & 0xFF);
    write8(addr + 1, (val >> 8) & 0xFF);
}

uint8_t __not_in_flash_func(LR35902::fetch8)() { return read8(PC++); }
int8_t  LR35902::fetch8s() { return (int8_t)fetch8(); }
uint16_t LR35902::fetch16() { uint16_t v = read16(PC); PC += 2; return v; }

void LR35902::aluAdd(uint8_t v, bool carry) {
    uint8_t c = (carry && getFlag(FLAG_C)) ? 1 : 0;
    uint16_t res = regs[7] + v + c;
    setFlag(FLAG_H, ((regs[7] & 0xF) + (v & 0xF) + c) > 0xF);
    setFlag(FLAG_C, res > 0xFF);
    regs[7] = (uint8_t)res;
    setFlag(FLAG_Z, regs[7] == 0);
    setFlag(FLAG_N, false);
}
void LR35902::aluSub(uint8_t v, bool carry) {
    uint8_t c = (carry && getFlag(FLAG_C)) ? 1 : 0;
    int16_t res = (int16_t)regs[7] - v - c;
    setFlag(FLAG_H, ((regs[7] & 0xF) - (v & 0xF) - c) < 0);
    setFlag(FLAG_C, res < 0);
    regs[7] = (uint8_t)res;
    setFlag(FLAG_Z, regs[7] == 0);
    setFlag(FLAG_N, true);
}
void LR35902::aluAnd(uint8_t v) { regs[7] &= v; setFlag(FLAG_Z, regs[7]==0); setFlag(FLAG_N,false); setFlag(FLAG_H,true); setFlag(FLAG_C,false); }
void LR35902::aluOr(uint8_t v)  { regs[7] |= v; setFlag(FLAG_Z, regs[7]==0); setFlag(FLAG_N,false); setFlag(FLAG_H,false); setFlag(FLAG_C,false); }
void LR35902::aluXor(uint8_t v) { regs[7] ^= v; setFlag(FLAG_Z, regs[7]==0); setFlag(FLAG_N,false); setFlag(FLAG_H,false); setFlag(FLAG_C,false); }
void LR35902::aluCp(uint8_t v) {
    int16_t res = (int16_t)regs[7] - v;
    setFlag(FLAG_H, ((regs[7] & 0xF) - (v & 0xF)) < 0);
    setFlag(FLAG_C, res < 0);
    setFlag(FLAG_Z, (uint8_t)res == 0);
    setFlag(FLAG_N, true);
}
uint8_t LR35902::incR8(uint8_t v) {
    uint8_t res = v + 1;
    setFlag(FLAG_H, (v & 0xF) == 0xF);
    setFlag(FLAG_Z, res == 0);
    setFlag(FLAG_N, false);
    return res;
}
uint8_t LR35902::decR8(uint8_t v) {
    uint8_t res = v - 1;
    setFlag(FLAG_H, (v & 0xF) == 0x0);
    setFlag(FLAG_Z, res == 0);
    setFlag(FLAG_N, true);
    return res;
}
void LR35902::addHL(uint16_t v) {
    uint16_t hl = getHL();
    uint32_t res = (uint32_t)hl + v;
    setFlag(FLAG_H, ((hl & 0xFFF) + (v & 0xFFF)) > 0xFFF);
    setFlag(FLAG_C, res > 0xFFFF);
    setFlag(FLAG_N, false);
    setHL((uint16_t)res);
}
void LR35902::addSP_s8() {
    int8_t v = fetch8s();
    uint16_t sp = SP;
    int32_t res = sp + v;
    setFlag(FLAG_Z, false);
    setFlag(FLAG_N, false);
    setFlag(FLAG_H, ((sp & 0xF) + (v & 0xF)) > 0xF);
    setFlag(FLAG_C, ((sp & 0xFF) + (v & 0xFF)) > 0xFF);
    SP = (uint16_t)res;
}
void LR35902::daa() {
    uint8_t corr = 0;
    bool carry = false;
    if (getFlag(FLAG_H) || (!getFlag(FLAG_N) && (regs[7] & 0xF) > 9)) corr |= 0x06;
    if (getFlag(FLAG_C) || (!getFlag(FLAG_N) && regs[7] > 0x99)) { corr |= 0x60; carry = true; }
    regs[7] += getFlag(FLAG_N) ? -corr : corr;
    setFlag(FLAG_Z, regs[7] == 0);
    setFlag(FLAG_H, false);
    setFlag(FLAG_C, carry);
}

uint8_t LR35902::rlc(uint8_t v) { bool c = v & 0x80; v = (v << 1) | (c?1:0); setFlag(FLAG_C,c); setFlag(FLAG_Z,v==0); setFlag(FLAG_N,false); setFlag(FLAG_H,false); return v; }
uint8_t LR35902::rrc(uint8_t v) { bool c = v & 0x01; v = (v >> 1) | (c?0x80:0); setFlag(FLAG_C,c); setFlag(FLAG_Z,v==0); setFlag(FLAG_N,false); setFlag(FLAG_H,false); return v; }
uint8_t LR35902::rl(uint8_t v)  { bool oldc = getFlag(FLAG_C); bool c = v & 0x80; v = (v << 1) | (oldc?1:0); setFlag(FLAG_C,c); setFlag(FLAG_Z,v==0); setFlag(FLAG_N,false); setFlag(FLAG_H,false); return v; }
uint8_t LR35902::rr(uint8_t v)  { bool oldc = getFlag(FLAG_C); bool c = v & 0x01; v = (v >> 1) | (oldc?0x80:0); setFlag(FLAG_C,c); setFlag(FLAG_Z,v==0); setFlag(FLAG_N,false); setFlag(FLAG_H,false); return v; }
uint8_t LR35902::sla(uint8_t v) { bool c = v & 0x80; v <<= 1; setFlag(FLAG_C,c); setFlag(FLAG_Z,v==0); setFlag(FLAG_N,false); setFlag(FLAG_H,false); return v; }
uint8_t LR35902::sra(uint8_t v) { bool c = v & 0x01; v = (v >> 1) | (v & 0x80); setFlag(FLAG_C,c); setFlag(FLAG_Z,v==0); setFlag(FLAG_N,false); setFlag(FLAG_H,false); return v; }
uint8_t LR35902::srl(uint8_t v) { bool c = v & 0x01; v >>= 1; setFlag(FLAG_C,c); setFlag(FLAG_Z,v==0); setFlag(FLAG_N,false); setFlag(FLAG_H,false); return v; }
uint8_t LR35902::swap(uint8_t v) { v = (v << 4) | (v >> 4); setFlag(FLAG_Z,v==0); setFlag(FLAG_N,false); setFlag(FLAG_H,false); setFlag(FLAG_C,false); return v; }

void LR35902::push16(uint16_t v) { SP -= 2; write16(SP, v); }
uint16_t LR35902::pop16() { uint16_t v = read16(SP); SP += 2; return v; }

void LR35902::jumpRel(bool cond, uint32_t &cyc) {
    int8_t off = fetch8s();
    if (cond) { PC += off; cyc += 4; }
}
void LR35902::jumpAbs(bool cond, uint32_t &cyc) {
    uint16_t addr = fetch16();
    if (cond) { PC = addr; cyc += 4; }
}
void LR35902::call(bool cond, uint32_t &cyc) {
    uint16_t addr = fetch16();
    if (cond) { push16(PC); PC = addr; cyc += 12; }
}
void LR35902::ret(bool cond, uint32_t &cyc) {
    if (cond) { PC = pop16(); cyc += 12; }
}

void LR35902::requestInterrupt(GBInterrupt which) { IF |= (1 << which); }
void LR35902::serviceInterrupt(uint8_t vector) { IME = false; push16(PC); PC = vector; }

uint32_t __not_in_flash_func(LR35902::step) () {
    bool applyIme = imeScheduled;
    if (applyIme) imeScheduled = false;

    uint8_t pending = IF & IE & 0x1F;

    if (halted) {
        if (pending) { halted = false; } else { return 4; }
    }

    if (IME && pending) {
        uint8_t bit = 0;
        for (; bit < 5; bit++) if (pending & (1 << bit)) break;
        static const uint16_t vectors[5] = {0x40, 0x48, 0x50, 0x58, 0x60};
        IF &= ~(1 << bit);
        serviceInterrupt(vectors[bit]);
        return 20;
    }

    if (applyIme) IME = true;

    uint8_t op = fetch8();
    if (haltBug) { PC--; haltBug = false; }

    if (op == 0xCB) return execCB();
    return execOpcode(op);
}

uint32_t __not_in_flash_func(LR35902::execOpcode) (uint8_t op) {
    uint32_t cyc = 4;

    if (op >= 0x40 && op <= 0x7F && op != 0x76) {
        uint8_t dst = (op >> 3) & 7;
        uint8_t src = op & 7;
        setR(dst, getR(src));
        return (dst == 6 || src == 6) ? 8 : 4;
    }
    if (op >= 0x80 && op <= 0xBF) {
        uint8_t kind = (op >> 3) & 7;
        uint8_t src = op & 7;
        uint8_t v = getR(src);
        switch (kind) {
            case 0: aluAdd(v, false); break;
            case 1: aluAdd(v, true);  break;
            case 2: aluSub(v, false); break;
            case 3: aluSub(v, true);  break;
            case 4: aluAnd(v); break;
            case 5: aluXor(v); break;
            case 6: aluOr(v);  break;
            case 7: aluCp(v);  break;
        }
        return (src == 6) ? 8 : 4;
    }

    switch (op) {
        case 0x00: return 4;
        case 0x10: fetch8(); return 4;
        case 0x76: if (!IME && (IF & IE & 0x1F)) haltBug = true; halted = true; return 4;
        case 0xF3: IME = false; return 4;
        case 0xFB: imeScheduled = true; return 4;

        case 0x06: setR(0, fetch8()); return 8;
        case 0x0E: setR(1, fetch8()); return 8;
        case 0x16: setR(2, fetch8()); return 8;
        case 0x1E: setR(3, fetch8()); return 8;
        case 0x26: setR(4, fetch8()); return 8;
        case 0x2E: setR(5, fetch8()); return 8;
        case 0x36: setR(6, fetch8()); return 12;
        case 0x3E: setR(7, fetch8()); return 8;

        case 0x01: setBC(fetch16()); return 12;
        case 0x11: setDE(fetch16()); return 12;
        case 0x21: setHL(fetch16()); return 12;
        case 0x31: SP = fetch16(); return 12;

        case 0x02: write8(getBC(), regs[7]); return 8;
        case 0x12: write8(getDE(), regs[7]); return 8;
        case 0x22: write8(getHL(), regs[7]); setHL(getHL()+1); return 8;
        case 0x32: write8(getHL(), regs[7]); setHL(getHL()-1); return 8;
        case 0x0A: regs[7] = read8(getBC()); return 8;
        case 0x1A: regs[7] = read8(getDE()); return 8;
        case 0x2A: regs[7] = read8(getHL()); setHL(getHL()+1); return 8;
        case 0x3A: regs[7] = read8(getHL()); setHL(getHL()-1); return 8;

        case 0x08: write16(fetch16(), SP); return 20;
        case 0xF9: SP = getHL(); return 8;

        case 0xF8: { int8_t v=fetch8s(); uint16_t sp=SP; int32_t res=sp+v;
            setFlag(FLAG_Z,false); setFlag(FLAG_N,false);
            setFlag(FLAG_H, ((sp&0xF)+(v&0xF))>0xF);
            setFlag(FLAG_C, ((sp&0xFF)+(v&0xFF))>0xFF);
            setHL((uint16_t)res); return 12; }
        case 0xE8: addSP_s8(); return 16;

        case 0xE0: write8(0xFF00 + fetch8(), regs[7]); return 12;
        case 0xF0: regs[7] = read8(0xFF00 + fetch8()); return 12;
        case 0xE2: write8(0xFF00 + regs[1], regs[7]); return 8;
        case 0xF2: regs[7] = read8(0xFF00 + regs[1]); return 8;
        case 0xEA: write8(fetch16(), regs[7]); return 16;
        case 0xFA: regs[7] = read8(fetch16()); return 16;

        case 0x04: regs[0] = incR8(regs[0]); return 4;
        case 0x0C: regs[1] = incR8(regs[1]); return 4;
        case 0x14: regs[2] = incR8(regs[2]); return 4;
        case 0x1C: regs[3] = incR8(regs[3]); return 4;
        case 0x24: regs[4] = incR8(regs[4]); return 4;
        case 0x2C: regs[5] = incR8(regs[5]); return 4;
        case 0x34: write8(getHL(), incR8(read8(getHL()))); return 12;
        case 0x3C: regs[7] = incR8(regs[7]); return 4;
        case 0x05: regs[0] = decR8(regs[0]); return 4;
        case 0x0D: regs[1] = decR8(regs[1]); return 4;
        case 0x15: regs[2] = decR8(regs[2]); return 4;
        case 0x1D: regs[3] = decR8(regs[3]); return 4;
        case 0x25: regs[4] = decR8(regs[4]); return 4;
        case 0x2D: regs[5] = decR8(regs[5]); return 4;
        case 0x35: write8(getHL(), decR8(read8(getHL()))); return 12;
        case 0x3D: regs[7] = decR8(regs[7]); return 4;

        case 0x03: setBC(getBC()+1); return 8;
        case 0x13: setDE(getDE()+1); return 8;
        case 0x23: setHL(getHL()+1); return 8;
        case 0x33: SP++; return 8;
        case 0x0B: setBC(getBC()-1); return 8;
        case 0x1B: setDE(getDE()-1); return 8;
        case 0x2B: setHL(getHL()-1); return 8;
        case 0x3B: SP--; return 8;

        case 0x09: addHL(getBC()); return 8;
        case 0x19: addHL(getDE()); return 8;
        case 0x29: addHL(getHL()); return 8;
        case 0x39: addHL(SP); return 8;

        case 0xC6: aluAdd(fetch8(), false); return 8;
        case 0xCE: aluAdd(fetch8(), true);  return 8;
        case 0xD6: aluSub(fetch8(), false); return 8;
        case 0xDE: aluSub(fetch8(), true);  return 8;
        case 0xE6: aluAnd(fetch8()); return 8;
        case 0xEE: aluXor(fetch8()); return 8;
        case 0xF6: aluOr(fetch8());  return 8;
        case 0xFE: aluCp(fetch8());  return 8;

        case 0x07: regs[7] = rlc(regs[7]); setFlag(FLAG_Z,false); return 4;
        case 0x0F: regs[7] = rrc(regs[7]); setFlag(FLAG_Z,false); return 4;
        case 0x17: regs[7] = rl(regs[7]);  setFlag(FLAG_Z,false); return 4;
        case 0x1F: regs[7] = rr(regs[7]);  setFlag(FLAG_Z,false); return 4;

        case 0x27: daa(); return 4;
        case 0x2F: regs[7] = ~regs[7]; setFlag(FLAG_N,true); setFlag(FLAG_H,true); return 4;
        case 0x37: setFlag(FLAG_C,true); setFlag(FLAG_N,false); setFlag(FLAG_H,false); return 4;
        case 0x3F: setFlag(FLAG_C, !getFlag(FLAG_C)); setFlag(FLAG_N,false); setFlag(FLAG_H,false); return 4;

        case 0x18: jumpRel(true, cyc); return cyc + 4;
        case 0x20: jumpRel(!getFlag(FLAG_Z), cyc); return cyc + 4;
        case 0x28: jumpRel(getFlag(FLAG_Z), cyc); return cyc + 4;
        case 0x30: jumpRel(!getFlag(FLAG_C), cyc); return cyc + 4;
        case 0x38: jumpRel(getFlag(FLAG_C), cyc); return cyc + 4;

        case 0xC3: jumpAbs(true, cyc); return cyc + 8;
        case 0xC2: jumpAbs(!getFlag(FLAG_Z), cyc); return cyc + 8;
        case 0xCA: jumpAbs(getFlag(FLAG_Z), cyc); return cyc + 8;
        case 0xD2: jumpAbs(!getFlag(FLAG_C), cyc); return cyc + 8;
        case 0xDA: jumpAbs(getFlag(FLAG_C), cyc); return cyc + 8;
        case 0xE9: PC = getHL(); return 4;

        case 0xCD: call(true, cyc); return cyc + 8;
        case 0xC4: call(!getFlag(FLAG_Z), cyc); return cyc + 8;
        case 0xCC: call(getFlag(FLAG_Z), cyc); return cyc + 8;
        case 0xD4: call(!getFlag(FLAG_C), cyc); return cyc + 8;
        case 0xDC: call(getFlag(FLAG_C), cyc); return cyc + 8;

        case 0xC9: PC = pop16(); return 16;
        case 0xD9: PC = pop16(); IME = true; return 16;
        case 0xC0: ret(!getFlag(FLAG_Z), cyc); return cyc + 8;
        case 0xC8: ret(getFlag(FLAG_Z), cyc); return cyc + 8;
        case 0xD0: ret(!getFlag(FLAG_C), cyc); return cyc + 8;
        case 0xD8: ret(getFlag(FLAG_C), cyc); return cyc + 8;

        case 0xC7: push16(PC); PC = 0x00; return 16;
        case 0xCF: push16(PC); PC = 0x08; return 16;
        case 0xD7: push16(PC); PC = 0x10; return 16;
        case 0xDF: push16(PC); PC = 0x18; return 16;
        case 0xE7: push16(PC); PC = 0x20; return 16;
        case 0xEF: push16(PC); PC = 0x28; return 16;
        case 0xF7: push16(PC); PC = 0x30; return 16;
        case 0xFF: push16(PC); PC = 0x38; return 16;

        case 0xC5: push16(getBC()); return 16;
        case 0xD5: push16(getDE()); return 16;
        case 0xE5: push16(getHL()); return 16;
        case 0xF5: push16(getAF()); return 16;
        case 0xC1: setBC(pop16()); return 12;
        case 0xD1: setDE(pop16()); return 12;
        case 0xE1: setHL(pop16()); return 12;
        case 0xF1: setAF(pop16()); return 12;

        default:
#if SERIAL_DEBUG
            Serial.printf("CPU: opcode illegale 0x%02X @ PC=0x%04X\n", op, PC - 1);
#endif
            return 4;
    }
}

uint32_t __not_in_flash_func(LR35902::execCB) () {
    uint8_t op = fetch8();
    uint8_t reg = op & 7;
    uint8_t group = (op >> 6) & 3;
    uint8_t sub = (op >> 3) & 7;

    uint8_t v = getR(reg);
    uint32_t cyc = (reg == 6) ? 12 : 8;

    if (group == 0) {
        switch (sub) {
            case 0: v = rlc(v); break;
            case 1: v = rrc(v); break;
            case 2: v = rl(v);  break;
            case 3: v = rr(v);  break;
            case 4: v = sla(v); break;
            case 5: v = sra(v); break;
            case 6: v = swap(v);break;
            case 7: v = srl(v); break;
        }
        setR(reg, v);
        return cyc;
    } else if (group == 1) {
        bool z = (v & (1 << sub)) == 0;
        setFlag(FLAG_Z, z);
        setFlag(FLAG_N, false);
        setFlag(FLAG_H, true);
        return (reg == 6) ? 12 : 8;
    } else if (group == 2) {
        v &= ~(1 << sub);
        setR(reg, v);
        return cyc;
    } else {
        v |= (1 << sub);
        setR(reg, v);
        return cyc;
    }
}