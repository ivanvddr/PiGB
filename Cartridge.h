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
#include "GBLoader.h"

enum MBCType { MBC_NONE, MBC1, MBC2, MBC3, MBC5 };
#define CART_RAM_MAX_SIZE (32 * 1024)

extern GBLoader loader;

class Cartridge {
public:
    void begin() {
        romBank = 1; ramBank = 0; ramEnabled = false; bankingMode = 0;
        memset(ramBanks, 0, sizeof(ramBanks));
        bank0Ptr = nullptr; // Verrà impostato in loadROMInfo
        bank1Ptr = nullptr;
    }

    void loadROMInfo() {
        bank0Ptr = loader.bank0Ptr(); // Salva il puntatore fisso al banco 0
        uint8_t cartType = bank0Ptr[0x0147];
        mbc = mapMBCType(cartType);
        ramSizeBytes = mapRAMSize(bank0Ptr[0x0149]);
        updateBank1(1); // Precarica il banco 1
    }

    bool isCGB() { return bank0Ptr[0x0143] == 0x80 || bank0Ptr[0x0143] == 0xC0; }

    // Funzione inline velocissima, usa i puntatori locali salvati
    inline uint8_t readROM(uint16_t addr) {
        if (addr < 0x4000) {
            return bank0Ptr[addr];
        } else {
            return bank1Ptr[addr - 0x4000];
        }
    }

    void writeROM(uint16_t addr, uint8_t val) {
        switch (mbc) {
            case MBC_NONE: break;
            case MBC1:
                if (addr < 0x2000) ramEnabled = (val & 0x0F) == 0x0A;
                else if (addr < 0x4000) {
                    uint8_t b = val & 0x1F; if (b == 0) b = 1;
                    romBank = (romBank & 0x60) | b;
                    updateBank1(romBank & 0x1F);
                } else if (addr < 0x6000) {
                    if (bankingMode == 0) {
                        romBank = (romBank & 0x1F) | ((val & 0x03) << 5);
                        updateBank1(romBank & 0x1F);
                    } else ramBank = val & 0x03;
                } else bankingMode = val & 0x01;
                break;
            case MBC3:
                if (addr < 0x2000) ramEnabled = (val & 0x0F) == 0x0A;
                else if (addr < 0x4000) {
                    uint8_t b = val & 0x7F; if (b == 0) b = 1;
                    romBank = b;
                    updateBank1(romBank);
                } else if (addr < 0x6000) ramBank = val;
                break;
            case MBC5:
                if (addr < 0x2000) ramEnabled = (val & 0x0F) == 0x0A;
                else if (addr < 0x3000) {
                    romBank = (romBank & 0xFF00) | val;
                    updateBank1(romBank);
                } else if (addr < 0x4000) {
                    romBank = (romBank & 0x00FF) | ((val & 0x01) << 8);
                    updateBank1(romBank);
                } else if (addr < 0x6000) ramBank = val & 0x0F;
                break;
        }
    }

    uint8_t readRAM(uint16_t addr) {
        if (!ramEnabled || ramSizeBytes == 0) return 0xFF;
        uint32_t offset = ((uint32_t)ramBank << 13) + addr;
        while (offset >= ramSizeBytes) offset -= ramSizeBytes;
        return ramBanks[offset];
    }
    void writeRAM(uint16_t addr, uint8_t val) {
        if (!ramEnabled || ramSizeBytes == 0) return;
        uint32_t offset = ((uint32_t)ramBank << 13) + addr;
        while (offset >= ramSizeBytes) offset -= ramSizeBytes;
        ramBanks[offset] = val;
    }

private:
    MBCType mbc;
    uint16_t romBank;
    uint8_t ramBank;
    bool ramEnabled;
    uint8_t bankingMode;
    uint32_t ramSizeBytes;
    uint8_t ramBanks[CART_RAM_MAX_SIZE];

    // Puntatori locali per accesso diretto in RAM (evita chiamate a funzione)
    const uint8_t* bank0Ptr;
    const uint8_t* bank1Ptr;

    // Aggiorna il banco 1 se necessario
    void updateBank1(uint16_t bank) {
        loader.setRomBank(bank);
        bank1Ptr = loader.bank1Ptr(); // Aggiorna il puntatore locale
    }

    static MBCType mapMBCType(uint8_t code) {
        switch (code) {
            case 0x00: case 0x08: case 0x09: return MBC_NONE;
            case 0x01: case 0x02: case 0x03: return MBC1;
            case 0x05: case 0x06: return MBC2;
            case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13: return MBC3;
            case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: return MBC5;
            default: return MBC_NONE;
        }
    }
    static uint32_t mapRAMSize(uint8_t code) {
        switch (code) { case 1: return 2048; case 2: return 8192; case 3: return 32768; case 4: return 131072; case 5: return 65536; default: return 0; }
    }
};