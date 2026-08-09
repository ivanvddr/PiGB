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
#include "GBLoader.h"

extern TFT_eSPI tft;
extern Joypad joypad;

// ============================================================================
// FILESYSTEM
// ============================================================================
bool GBLoader::initFilesystem(GBFSType preferred) {
  if (_fsReady) {
    Serial.printf("GBLoader: filesystem gia' avviato (%s)\n",
                  _currentFS == GB_FS_SD ? "SD" : "LittleFS");
    return true;
  }

  Serial.println("GBLoader: init filesystem...");

  auto trySD = [&]() -> bool {
    Serial.println("  tentativo SD...");
    if (initSD()) {
      _currentFS = GB_FS_SD; _fsReady = true;
      Serial.println("  SD OK"); return true;
    }
    Serial.println("  SD non disponibile"); return false;
  };
  auto tryLFS = [&]() -> bool {
    Serial.println("  tentativo LittleFS...");
    if (LittleFS.begin()) {
      _currentFS = GB_FS_LITTLEFS; _fsReady = true;
      Serial.println("  LittleFS OK"); return true;
    }
    Serial.println("  LittleFS non disponibile"); return false;
  };

  if (preferred == GB_FS_SD) {
    if (trySD()) return true;
    if (tryLFS()) return true;
  } else {
    if (tryLFS()) return true;
    if (trySD()) return true;
  }

  Serial.println("GBLoader ERROR: nessun filesystem disponibile (SD ne' LittleFS)");
  return false;
}

bool GBLoader::initSD() {
  SPI1.setRX(SD_MISO);
  SPI1.setTX(SD_MOSI);
  SPI1.setSCK(SD_SCLK);
  SPI1.setCS(SD_CS);
  SPI1.begin();
  delay(100);
  const uint32_t SD_SPI_CLOCK = 20000000; // 20 MHz
  return SD.begin(SD_CS, SD_SPI_CLOCK, SPI1);
}

bool GBLoader::loadROM(const char* path) {
  if (!openROMFile(path)) return false;
  
  strcpy(_romPath, path);
  estraiNome(path);
  _romSize = _romFile.size();
  _romPages = (_romSize + GB_PAGE_SIZE - 1) / GB_PAGE_SIZE;
  _currentBank1 = 0xFFFF; // Invalida

  // Precarica Bank 0
  _romFile.seek(0);
  _romFile.read(_bank0Buf, GB_PAGE_SIZE);

  _romLoaded = true;
  return true;
}

bool GBLoader::openROMFile(const char* path) {
  if (_currentFS == GB_FS_SD) _romFile = SD.open(path, FILE_READ);
  else if (_currentFS == GB_FS_LITTLEFS) _romFile = LittleFS.open(path, "r");
  return _romFile ? true : false;
}

void GBLoader::closeROMFile() {
  if (_romFile) _romFile.close();
  _romLoaded = false;
}

File GBLoader::openFile(const char* path, const char* mode) {
    if (!_fsReady) {
        Serial.println("GBLoader ERROR: uninitialized filesystem");
        return File();
    }
    if (_currentFS == GB_FS_SD)       return SD.open(path, mode);
    if (_currentFS == GB_FS_LITTLEFS) return LittleFS.open(path, mode);
    return File();
}

bool GBLoader::fileExists(const char* path) {
    if (!_fsReady) return false;
    if (_currentFS == GB_FS_SD)       return SD.exists(path);
    if (_currentFS == GB_FS_LITTLEFS) return LittleFS.exists(path);
    return false;
}

void GBLoader::setRomBank(uint16_t bank) {
  if (bank == _currentBank1) return; // Già caricato
  fetchPage(bank, _bank1Buf);
  _currentBank1 = bank;
}

bool GBLoader::fetchPage(uint16_t pageIdx, uint8_t* dest) {
#if GB_ROM_CACHE_EXTRA_PAGES > 0
  if (cacheLookup(pageIdx, dest)) {
    _cacheHits++;
    return true;
  }
#endif
  _cacheMisses++;
  
  // Leggi da file
  uint32_t offset = (uint32_t)pageIdx * GB_PAGE_SIZE;
  _romFile.seek(offset);
  _romFile.read(dest, GB_PAGE_SIZE);

#if GB_ROM_CACHE_EXTRA_PAGES > 0
  cacheInsert(pageIdx, dest);
#endif
  return true;
}

#if GB_ROM_CACHE_EXTRA_PAGES > 0
bool GBLoader::cacheLookup(uint16_t pageIdx, uint8_t* dest) {
  for (int i = 0; i < GB_ROM_CACHE_EXTRA_PAGES; i++) {
    if (_cachePageIdx[i] == pageIdx) {
      memcpy(dest, _cachePool[i], GB_PAGE_SIZE);
      _cacheLastUsed[i] = ++_cacheClock;
      return true;
    }
  }
  return false;
}

void GBLoader::cacheInsert(uint16_t pageIdx, const uint8_t* data) {
  int lru = 0;
  uint32_t minTime = 0xFFFFFFFF;
  for (int i = 0; i < GB_ROM_CACHE_EXTRA_PAGES; i++) {
    if (_cachePageIdx[i] == 0xFFFF) { lru = i; break; } // Slot vuoto
    if (_cacheLastUsed[i] < minTime) { minTime = _cacheLastUsed[i]; lru = i; }
  }
  memcpy(_cachePool[lru], data, GB_PAGE_SIZE);
  _cachePageIdx[lru] = pageIdx;
  _cacheLastUsed[lru] = ++_cacheClock;
}
#endif

void GBLoader::estraiNome(const char* path) {
  const char* slash = strrchr(path, '/');
  if (slash) strcpy(_romName, slash + 1);
  else strcpy(_romName, path);
}

void GBLoader::setSelectedROM(const char* path) {
  strcpy(_romPath, path);
}