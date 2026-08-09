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
#include <SD.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>
#include "Config.h"
#include "Joypad.h"

// Dimensione di una pagina ROM del Game Boy (16KB)
static const uint32_t GB_PAGE_SIZE = 16384UL;

// Numero di pagine extra mantenute in cache LRU, in aggiunta ai 2 buffer fissi (bank0 + bank1).
// Ogni pagina extra costa 16KB di RAM. Default: 2 pagine = 32KB (rallenta parecchio l'emulazione perché gli accessi sono numerosi).
#ifndef GB_ROM_CACHE_EXTRA_PAGES
#define GB_ROM_CACHE_EXTRA_PAGES 4
#endif

enum GBFSType { GB_FS_NONE, GB_FS_SD, GB_FS_LITTLEFS };

class GBLoader {
public:
  GBLoader() : _currentFS(GB_FS_NONE), _fsReady(false), _romLoaded(false), _romSize(0), _romPages(0) {}

  bool initFilesystem(GBFSType preferred = GB_FS_SD);
  bool isReady() const { return _fsReady; }
  GBFSType currentFS() const { return _currentFS; }

  bool loadROM(const char* path);
  void unloadROM();
  bool isLoaded() const { return _romLoaded; }

  uint32_t romSize()  const { return _romSize; }
  uint32_t romPages() const { return _romPages; }
  const char* romName() const { return _romName; }

  // Chiamato da Cartridge quando la CPU scrive un nuovo banco (es. 0x2000-0x3FFF)
  void setRomBank(uint16_t bank);

  // Puntatori ai 16KB correnti. Bank 0 è fissa (0x0000-0x3FFF), Bank 1 è paginata (0x4000-0xBFFF)
  inline const uint8_t* bank0Ptr() const { return _bank0Buf; }
  inline const uint8_t* bank1Ptr() const { return _bank1Buf; }

  // Statistiche cache (debug)
  uint32_t cacheHits()  const { return _cacheHits; }
  uint32_t cacheMisses() const { return _cacheMisses; }

  void setSelectedROM(const char* path);
  const char* getSelectedROM() const { return _romPath; }

  File openFile(const char* path, const char* mode);
  bool fileExists(const char* path);

private:
  bool initSD();
  bool openROMFile(const char* path);
  void closeROMFile();
  bool fetchPage(uint16_t pageIdx, uint8_t* dest);
  bool cacheLookup(uint16_t pageIdx, uint8_t* dest);
  void cacheInsert(uint16_t pageIdx, const uint8_t* data);
  void estraiNome(const char* path);

  GBFSType _currentFS;
  bool _fsReady;
  File _romFile;
  bool _romLoaded;
  uint32_t _romSize;
  uint32_t _romPages;
  char _romName[64];
  char _romPath[128];

  uint16_t _currentBank1; // Banco attualmente caricato in _bank1Buf

  uint8_t _bank0Buf[GB_PAGE_SIZE]; // Banco 0 (fisso)
  uint8_t _bank1Buf[GB_PAGE_SIZE]; // Banco 1+ (paginato)

#if GB_ROM_CACHE_EXTRA_PAGES > 0
  uint8_t  _cachePool[GB_ROM_CACHE_EXTRA_PAGES][GB_PAGE_SIZE];
  uint16_t _cachePageIdx[GB_ROM_CACHE_EXTRA_PAGES];
  uint32_t _cacheLastUsed[GB_ROM_CACHE_EXTRA_PAGES];
  uint32_t _cacheClock = 0;
#endif

  uint32_t _cacheHits = 0;
  uint32_t _cacheMisses = 0;
};