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
 * ma SENZA ALCUNA GARANZIA; senza neppure la garanzia implicita di
 * COMMERCIABILITÀ o IDONEITÀ PER UN PARTICOLARE SCOPO.
 * Vedi la licenza GPLv3 per maggiori dettagli.
 *
 *  Emulatore Game Boy (DMG) per Raspberry Pi Pico (RP2040), Arduino IDE.
 *
 *  ARCHITETTURA DUALE (Approccio "Pi64"):
 *   - core0: Esegue la CPU (LR35902), avanza la PPU e il Timer in base ai 
 *            T-cycle reali consumati. Gestisce il joypad e gli accessi alla 
 *            ROM letta dalla SD card (tramite cache LRU). Non si occupa più 
 *            di generare i campioni audio, ma si limita ad accumulare i cicli 
 *            eseguiti in una variabile condivisa (sharedCpuCycles).
 *   - core1: Inizializza il display (TFT_eSPI + DMA) e l'hardware PWM. Un 
 *            Timer Hardware scatta a frequenza di campionamento (22050 Hz): 
 *            legge i cicli accumulati dal Core 0, fa avanzare l'APU di quei 
 *            cicli e scrive il campione audio diretto nel registro PWM. 
 *            Il resto del tempo pompa il framebuffer pronto sul display.
 *            
 *  Vantaggi: Spostando la generazione audio su Core 1 tramite timer hardware, 
 *  si elimina completamente l'underrun causato dalle latenze della SD card. 
 *  Se il Core 0 si blocca per leggere un banco ROM, il Core 1 genererà lo 
 *  stesso campione dell'istante precedente, evitando fastidiosi "tick" o 
 *  "pop" audio a discapito di un impercettibile freeze della nota musicale.
 * ============================================================
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include "hardware/pwm.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/vreg.h"    // Per vreg_set_voltage
#include "hardware/clocks.h"  // Per clock_configure e clk_peri
#include "hardware/timer.h"
#include "pico/multicore.h"
#include "Config.h"
#include "CPU.h"
#include "PPU.h"
#include "APU.h"
#include "PiGBLogo.h"
#include "GBLoader.h"
#include "GBMenu.h"
#include "GBGameBrowser.h"
#include "Timer.h"
#include "Joypad.h"
#include "Cartridge.h"

//abilita il log dei tempi frame (60Hz target = 16666 us)
//#define PROFILE_PERF

// Actions (id > 0; 0 = no action)
//Menu principale
#define ACTION_START_GAME 1
#define ACTION_START_OPT  2

//Menu opzioni
#define ACTION_OPT_VIDEO  10
#define ACTION_OPT_VOLUME 11
#define ACTION_OPT_BACK   12

GBLoader loader;

// ---------------- oggetti globali ----------------
TFT_eSPI tft = TFT_eSPI();
LR35902 cpu;
PPU ppu;
APU apu;
GBTimer gbtimer;
Joypad joypad;
Cartridge cart;

//--------------- MENU ----------------------
// ===== Voci menu principale =====
const GBMenuItem mainMenuItems[] = {
    { "GAMES BROWSER", ACTION_START_GAME },
    { "OPTIONS",       ACTION_START_OPT  },
};
const int mainMenuItemsCount = sizeof(mainMenuItems) / sizeof(mainMenuItems[0]);

// ===== Voci menu opzioni =====
const GBMenuItem optMenuItems[] = {
    { "VIDEO",  ACTION_OPT_VIDEO },
    { "VOLUME", ACTION_OPT_VOLUME },
    { "BACK",   ACTION_OPT_BACK },
};
const int optMenuItemsCount = sizeof(optMenuItems) / sizeof(optMenuItems[0]);

enum AppState {
    STATE_MENU,
    STATE_GAME,
    STATE_OPT,
};
AppState currentState = STATE_MENU;
GBMenu* menu = nullptr;
GBMenu* optMenu = nullptr;
GBGameBrowser* gameBrowser = nullptr;

// ---------------- memoria non posseduta dagli altri moduli ----------------
static uint8_t wram[2][0x1000];   // DMG: 8KB fissi, C000-CFFF e D000-DFFF
static uint8_t hram[0x80];        // FF80-FFFE

// ---------------- audio: generazione diretta su Core 1 ----------------
static volatile uint32_t sharedCpuCycles = 0; // Cicli passati da Core0 a Core1

static uint pwmSlice;
static uint pwmChan; // 0 = A, 1 = B

static volatile bool core0SetupDone = false;

bool upscale = false;
uint8_t nextvol = 10;
uint8_t volume = 100;

// --- SCALING 1.5x ---
#define SCALED_W 240
#define SCALED_H 216
#define OFFSET_X ((TFT_W - SCALED_W) / 2) // 40
#define OFFSET_Y ((TFT_H - SCALED_H) / 2) // 12
// Minibuffer per una singola riga scalata (240 pixel * 2 byte = 480 byte)
static uint16_t scaledLine[SCALED_W]; 
static int srcX_map[SCALED_W];
static int srcY_map[SCALED_H];


// ============================================================
//  BUS DISPATCH — le 4 funzioni richieste dalla CPU parametrica
// ============================================================
uint8_t memRead(uint16_t addr) {
    switch (addr >> 12) {
        case 0: case 1: case 2: case 3:
        case 4: case 5: case 6: case 7:
            return cart.readROM(addr);
        case 8: case 9:
            return ppu.vramRead(addr - 0x8000);
        case 0xA: case 0xB:
            return cart.readRAM(addr - 0xA000);
        case 0xC:
            return wram[0][addr - 0xC000];
        case 0xD:
            return wram[1][addr - 0xD000];
        case 0xE:
            return wram[0][addr - 0xE000];
        case 0xF:
            if (addr < 0xFE00) return wram[1][addr - 0xF000];
            if (addr < 0xFEA0) return ppu.oamRead(addr - 0xFE00);
            if (addr < 0xFF00) return 0xFF;
            if (addr < 0xFF80) return 0xFF; // gestito da ioRead nella CPU
            if (addr < 0xFFFF) return hram[addr - 0xFF80];
            return 0xFF;
    }
    return 0xFF;
}

void memWrite(uint16_t addr, uint8_t val) {
    switch (addr >> 12) {
        case 0: case 1: case 2: case 3:
        case 4: case 5: case 6: case 7:
            cart.writeROM(addr, val); return;
        case 8: case 9:
            ppu.vramWrite(addr - 0x8000, val); return;
        case 0xA: case 0xB:
            cart.writeRAM(addr - 0xA000, val); return;
        case 0xC:
            wram[0][addr - 0xC000] = val; return;
        case 0xD:
            wram[1][addr - 0xD000] = val; return;
        case 0xE:
            wram[0][addr - 0xE000] = val; return;
        case 0xF:
            if (addr < 0xFE00) { wram[1][addr - 0xF000] = val; return; }
            if (addr < 0xFEA0) { ppu.oamWrite(addr - 0xFE00, val); return; }
            if (addr < 0xFF00) return;
            if (addr < 0xFF80) return; // gestito da ioWrite nella CPU
            if (addr < 0xFFFF) { hram[addr - 0xFF80] = val; return; }
            return;
    }
}

uint8_t ioRead(uint16_t addr) {
    if (addr == 0xFF00) return joypad.readReg();
    if (addr >= 0xFF04 && addr <= 0xFF07) return gbtimer.readReg(addr);
    if (addr >= 0xFF10 && addr <= 0xFF26) return apu.readReg(addr);
    if (addr >= 0xFF30 && addr <= 0xFF3F) return apu.readReg(addr);
    if (addr >= 0xFF40 && addr <= 0xFF4B) return ppu.readReg(addr);
    return 0xFF;
}

void ioWrite(uint16_t addr, uint8_t val) {
    if (addr == 0xFF00) { joypad.writeReg(val); return; }
    if (addr >= 0xFF04 && addr <= 0xFF07) { gbtimer.writeReg(addr, val); return; }
    if (addr == 0xFF46) { // OAM DMA
        ppu.oamDmaCopy(val << 8, memRead); 
        return;
    }
    if (addr >= 0xFF10 && addr <= 0xFF26) { apu.writeReg(addr, val); return; }
    if (addr >= 0xFF30 && addr <= 0xFF3F) { apu.writeReg(addr, val); return; }
    if (addr >= 0xFF40 && addr <= 0xFF4B) { ppu.writeReg(addr, val, memRead); return; }
}

// ============================================================
//  AUDIO: Timer Hardware su Core 1 (Approccio Pi64)
// ============================================================
#define AUDIO_SAMPLE_US (1000000UL / AUDIO_SAMPLE_RATE)

void __not_in_flash_func(audioTimerIrq)() {
    // Pulisce il flag di interrupt
    timer_hw->intr = 1u << 0; 

    // Legge i cicli accumulati dal Core 0
    static uint32_t lastCycles = 0;
    uint32_t curCycles = sharedCpuCycles;
    uint32_t delta = curCycles - lastCycles;
    lastCycles = curCycles;

    // Avanza l'APU solo dei cicli effettivamente passati
    if (delta > 0) {
        apu.stepCycles(delta);
    }

    // Genera il campione e scrivilo direttamente nel registro PWM
    uint16_t sample = apu.renderSample();
    pwm_set_chan_level(pwmSlice, pwmChan, sample);

    // Imposta il prossimo allarme
    timer_hw->alarm[0] = timer_hw->timerawl + AUDIO_SAMPLE_US;
}

void cleanupTFT() {
    Serial.println("Cleanup TFT...");
    
    tft.endWrite();
    
    uint32_t timeout = millis();
    while (tft.dmaBusy() && (millis() - timeout < 200)) {
        delay(1);
    }
    
    if (tft.dmaBusy()) {
        Serial.println("TFT DMA force stop!");
        dma_channel_abort(0);
        dma_channel_abort(1);
    }
    
    tft.fillScreen(TFT_BLACK);
    delay(50); 
    
    tft.setViewport(0, 0, 320, 240);
    tft.setTextDatum(TL_DATUM);
    
    Serial.println("TFT clean");
}

void showMainMenu() {
    if (!menu) {
        menu = new GBMenu(&tft, mainMenuItems, mainMenuItemsCount);
        menu->setLogo(logoPiGB, 143, 70);
        menu->setCredits("@IV GAME BOY emulator");
        menu->begin();
    } else {
        menu->draw();   
    }
}

void closeMainMenu()   { delete menu;    menu    = nullptr; }
void closeOptMenu()    { delete optMenu; optMenu = nullptr; }

void showOptMenu() {
    if (!optMenu) {
        optMenu = new GBMenu(&tft, optMenuItems, optMenuItemsCount);
        optMenu->setLogo(logoPiGB, 143, 70);
        optMenu->setTitle("OPTIONS");
        optMenu->setCredits("@IV GAME BOY emulator");
        optMenu->begin();
    } else {
        optMenu->draw();
    }
}

void handleMenu() {
    int action = menu->update();
    switch (action) {
        case ACTION_START_GAME:
            closeMainMenu();
            currentState = STATE_GAME;
            break;
        case ACTION_START_OPT:
            closeMainMenu();
            showOptMenu();
            currentState = STATE_OPT;
            break;
        case GB_ACTION_NONE:
        default:
            break;
    }
}

void handleOpts() {
    int action = optMenu->update();
    int action2 = 0;
    uint8_t joyState2;

    switch (action) {
        case ACTION_OPT_VIDEO:
            joyState2 = joystickReadHardware();
            action2 = optMenu->update();
            if (action2 == ACTION_OPT_VIDEO) {
                tft.fillRect(220, 100, 80, 34, TFT_WHITE);
                tft.setTextSize(2);
                tft.setTextDatum(TC_DATUM);
                tft.setTextColor(TFT_DARKGREEN);
                if (!upscale) {
                    tft.drawString("1:1.5",270,110);
                    upscale = true;
                } else {
                    tft.drawString("1:1",270,110);
                    upscale = false;
                }
            } else {
              action = optMenu->update();
            }
            break;

        case ACTION_OPT_VOLUME:
            joyState2 = joystickReadHardware();
            action2 = optMenu->update();
            if (action2 == ACTION_OPT_VOLUME) {
                tft.fillRect(220, 140, 80, 50, TFT_WHITE);
                tft.setTextSize(2);
                tft.setTextDatum(TC_DATUM);
                tft.setTextColor(TFT_DARKGREEN);
                switch (nextvol) {
                  case 10:
                    nextvol = 8;
                    tft.drawString("10",270,143);
                    volume = 100;
                    apu.setUserVolume(volume);
                    break;
                  case 8:
                    nextvol = 6;
                    tft.drawString("8",270,143);
                    volume = 80;
                    apu.setUserVolume(volume);
                    break;
                  case 6:
                    nextvol = 4;
                    tft.drawString("6",270,143);
                    volume = 60;
                    apu.setUserVolume(volume);
                    break;
                  case 4:
                    nextvol = 2;
                    tft.drawString("4",270,143);
                    volume = 40;
                    apu.setUserVolume(volume);
                    break;
                  case 2:
                    nextvol = 10;
                    tft.drawString("2",270,143);
                    volume = 20;
                    apu.setUserVolume(volume);
                    break;
                  default:
                    break;  
                }
            } else {
              action = optMenu->update();
            }
            break;

        case ACTION_OPT_BACK:
            closeOptMenu();
            showMainMenu();
            currentState = STATE_MENU;
            break;

        case GB_ACTION_NONE:
        default:
            break;
    }
}

void handleGameBrowser() {
    if (!gameBrowser) {
        if (!loader.initFilesystem(GB_FS_SD)) {
            tft.fillScreen(TFT_RED);
            tft.setTextColor(TFT_WHITE);
            tft.setTextSize(2);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("NO FILESYSTEM!", 160, 120);
            delay(3000);
            currentState = STATE_MENU;
            menu->draw();
            return;
        }
        
        gameBrowser = new GBGameBrowser(&tft, &loader, SD_CS);
        gameBrowser->setLogo(logoPiGB_small, 61, 30);
        gameBrowser->setPath("/games");
        gameBrowser->setFileExt(".gb");
        
        if (!gameBrowser->begin(loader.currentFS())) {
            delete gameBrowser;
            gameBrowser = nullptr;
            currentState = STATE_MENU;
            menu->draw();
            return;
        }
        
        delete menu;
        menu = nullptr;
    }
    
    uint8_t joyState = joystickReadHardware();
    bool gameSelected = gameBrowser->update(joyState);
    
    if (gameSelected) {
        String romToLoad = gameBrowser->getPrgSelected();
        
        delete gameBrowser;
        gameBrowser = nullptr;
                
        cleanupTFT();
        delay(100);
        
        if (!loader.loadROM(romToLoad.c_str())) {
            tft.fillScreen(TFT_WHITE);
            tft.setTextColor(TFT_RED);
            tft.setTextSize(2);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("LOAD ERROR!", 160, 120);
            delay(3000);
            return;
        }

        GBsetup();               
        delay(500);
        GBloop();
    }
    
    delay(10);
    yield();
}

void setup() {
    // Overclock a 276MHz richiede 1.30V per essere stabile
    vreg_set_voltage(VREG_VOLTAGE_1_30);
    sleep_ms(10); 
    set_sys_clock_khz(PICO_CLOCK_KHZ, true);

    clock_configure(
      clk_peri,
      0, 
      CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, 
      PICO_CLOCK_KHZ * 1000, 
      PICO_CLOCK_KHZ * 1000  
    );

    Serial.begin(115200);
    delay(1000);  

    inputInit();

    tft.init();
    tft.initDMA();
    tft.setRotation(1); 
    tft.setSwapBytes(true);
    tft.fillScreen(TFT_BLACK);
    
    showMainMenu();

    Serial.println("Setup completed!");
}

void loop() {
    switch (currentState) {
        case STATE_MENU:
            handleMenu();
            break;
        case STATE_GAME:
            handleGameBrowser();
            break;
        case STATE_OPT:
            handleOpts();
            break;
    } 
}

// ============================================================
//  CORE0 — CPU + PPU + Joypad
// ============================================================
// ============================================================
//  CORE0 — CPU + PPU + Joypad
// ============================================================
void GBsetup() {
    cart.begin();
    cart.loadROMInfo(); 
    
    cpu.begin(memRead, memWrite, ioRead, ioWrite);
    cpu.reset(false); // DMG Mode
    
    ppu.begin(&cpu);
    
    apu.begin();
    apu.setUserVolume(volume);
    gbtimer.begin(&cpu);
    joypad.begin(&cpu);
    
    sharedCpuCycles = 0; 
    core0SetupDone = true;
}

void GBloop() {
    const uint32_t FRAME_TARGET_US = 1000000UL / 60; // 16666 us
    uint32_t nextFrameTime = time_us_32();

    while(true) {
        #if FRAMESKIP > 0
            static uint8_t fsCount = 0;
            ppu.skipRendering = (fsCount < FRAMESKIP);
            if (fsCount >= FRAMESKIP) fsCount = 0;
            else fsCount++;
        #else
            ppu.skipRendering = false;
        #endif

        #ifdef PROFILE_PERF
            uint32_t t_start = time_us_32();
        #endif

        uint32_t frameCycles = 0;
        
        while (frameCycles < CYCLES_PER_FRAME) {
            uint32_t c = cpu.step();
            ppu.step(c);
            gbtimer.step(c);
            
            // Passa i cicli eseguiti al Core 1 per la generazione audio
            sharedCpuCycles += c;
            
            frameCycles += c;
        }
        joypad.poll();

        #ifdef PROFILE_PERF
            uint32_t frame_time = time_us_32() - t_start;
            static uint16_t frameCount = 0;
            if (++frameCount >= 60) {
                frameCount = 0;
                Serial.printf("[PERF] Frame: %u us (%.1f FPS)\n", frame_time, 1000000.0 / frame_time);
            }
        #endif

        // --- FRAME LIMITER SMOOTH (Anti-Audio Freeze) ---
        int32_t wait = nextFrameTime - time_us_32();
        if (wait > 0) {
            uint32_t targetTime = time_us_32() + wait;
            uint32_t lastTime = time_us_32();
            
            // Aspetta a piccoli passi, simulando il passaggio del tempo reale
            // per mantenere il Core 1 (audio) sempre fluido.
            while (time_us_32() < targetTime) {
                uint32_t now = time_us_32();
                uint32_t elapsed = now - lastTime;
                if (elapsed > 0) {
                    sharedCpuCycles += (elapsed * GB_CLOCK_HZ) / 1000000UL;
                    lastTime = now;
                }
                delayMicroseconds(20); // Pausa leggera per non saturare il bus
            }
            nextFrameTime += FRAME_TARGET_US;
        } else {
            // Siamo in ritardo (lag), riallineiamo il timer con il tempo reale
            nextFrameTime = time_us_32() + FRAME_TARGET_US;
        }
    }
}

// ============================================================
//  CORE1 — Display + Timer Audio
// ============================================================
void setup1() {
    while (!core0SetupDone) { tight_loop_contents(); }
    
    tft.init();
    tft.setRotation(1);
    tft.initDMA();           
    tft.fillScreen(TFT_BLACK);
    tft.setSwapBytes(true);

    // --- Inizializzazione PWM Audio ---
    pwmSlice = pwm_gpio_to_slice_num(AUDIO_PWM_PIN);
    pwmChan  = pwm_gpio_to_channel(AUDIO_PWM_PIN);
    gpio_set_function(AUDIO_PWM_PIN, GPIO_FUNC_PWM);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_wrap(&cfg, AUDIO_PWM_MAX);
    pwm_config_set_clkdiv(&cfg, 1.0f); 
    pwm_init(pwmSlice, &cfg, true);
    pwm_set_chan_level(pwmSlice, pwmChan, AUDIO_PWM_MAX / 2); // Silenzio iniziale

    // --- Inizializzazione Timer Hardware per il campionamento ---
    hw_set_bits(&timer_hw->inte, 1u << 0); // Abilita allarme 0
    irq_set_exclusive_handler(TIMER_IRQ_0, audioTimerIrq);
    irq_set_enabled(TIMER_IRQ_0, true); // L'IRQ gira su Core 1
    timer_hw->alarm[0] = timer_hw->timerawl + AUDIO_SAMPLE_US;

    // Precalcolo mappe upscale
    for (int x = 0; x < SCALED_W; x++) srcX_map[x] = (x * GB_W) / SCALED_W;
    for (int y = 0; y < SCALED_H; y++) srcY_map[y] = (y * GB_H) / SCALED_H;

    Serial.println("[CORE1] Display + Timer Audio pronti");
}

void loop1() {
    if (ppu.consumeFrameReady()) {
        tft.startWrite();
        uint16_t *fb = ppu.getDisplayFramebuffer();
        if (!upscale) {
              tft.setAddrWindow(GB_OFFSET_X, GB_OFFSET_Y, GB_W, GB_H);
              tft.pushPixelsDMA(fb, GB_W * GB_H); 
        } else {
            tft.setAddrWindow(OFFSET_X, OFFSET_Y, SCALED_W, SCALED_H);     
            for (int y = 0; y < SCALED_H; y++) {
                uint16_t* srcLine = &fb[srcY_map[y] * GB_W];   
                for (int x = 0; x < SCALED_W; x++) {
                    scaledLine[x] = srcLine[srcX_map[x]];
                }               
                tft.pushColors(scaledLine, SCALED_W);
            }
        }
        tft.endWrite();
    }
}