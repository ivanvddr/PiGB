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
#ifndef GB_MENU_H
#define GB_MENU_H

#include <TFT_eSPI.h>
#include "Joypad.h"

// action = 0  -> nessuna azione (tasto non premuto)
// action > 0  -> id azione restituito da update()
const int GB_ACTION_NONE = 0;

// Singola voce di menu: testo + id azione associato.
// L'array di queste struct viene passato al costruttore e NON viene
// copiato: deve restare valido per tutta la vita del menu (tipicamente
// e' un `const GBMenuItem[]` globale in flash, quindi zero malloc).
struct GBMenuItem {
    const char* text;
    int         action;
};

class GBMenu {
public:
    // display  : puntatore al TFT_eSPI gia' inizializzato
    // items     : array di voci (NON copiato)
    // itemCount : numero di voci
    GBMenu(TFT_eSPI* display, const GBMenuItem* items, int itemCount);
    ~GBMenu();

    // Disegna tutto e resetta la selezione alla prima voce
    void begin();

    // Da chiamare nel loop. Ritorna GB_ACTION_NONE finche' non viene
    // confermata una voce, altrimenti ritorna l'action della voce scelta.
    int  update();

    // Ridisegna tutto (utile dopo cleanupTFT o cambio schermata)
    void draw();

    // --- Configurazione opzionale (chiamare PRIMA di begin()) ---

    // Logo RGB565 centrato in alto. Se assente, viene mostrato il titolo.
    void setLogo(const unsigned short* logoData, int width, int height);

    // Titolo testuale mostrato se non c'e' logo (default "piGB")
    void setTitle(const char* title);

    // Testo in basso a destra (default "@IV GAME BOY emulator")
    void setCredits(const char* text);

    // Voce selezionata iniziale (0..itemCount-1). Default = 0.
    void setSelected(int idx);
    int  getSelected() const { return selectedItem; }

private:
    TFT_eSPI*            tft;
    const GBMenuItem*    items;
    int                  itemCount;

    // Logo
    const unsigned short* logo;
    int                   logoWidth;
    int                   logoHeight;
    bool                  hasLogo;

    // Testo opzionale
    const char* title;
    const char* credits;

    // Stato
    int selectedItem;
    int lastSelectedItem;

    // Debouncing
    uint32_t lastJoyTime;
    static const uint32_t JOY_DEBOUNCE_MS = 200;

    // Disegno
    void drawBackground();
    void drawLogo();
    void drawMenuItems();
    void drawMenuItem(int itemIdx, bool selected);
    void drawCredits();

    int  getMenuItemY(int itemIdx);
};

#endif