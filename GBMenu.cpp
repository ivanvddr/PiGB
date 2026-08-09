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
#include "GBMenu.h"

GBMenu::GBMenu(TFT_eSPI* display, const GBMenuItem* items, int itemCount) {
    tft          = display;
    this->items  = items;
    this->itemCount = itemCount;

    selectedItem      = (itemCount > 0) ? 0 : -1;
    lastSelectedItem  = selectedItem;
    lastJoyTime       = 0;

    logo       = nullptr;
    logoWidth  = 0;
    logoHeight = 0;
    hasLogo    = false;

    title   = "piGB";
    credits = "@IV GAME BOY emulator";
}

GBMenu::~GBMenu() {
    // Niente da fare: items e' di proprieta' del chiamante
}

void GBMenu::setLogo(const unsigned short* logoData, int width, int height) {
    logo       = logoData;
    logoWidth  = width;
    logoHeight = height;
    hasLogo    = (logo != nullptr);
}

void GBMenu::setTitle(const char* t) {
    title = (t != nullptr) ? t : "piGB";
}

void GBMenu::setCredits(const char* text) {
    credits = (text != nullptr) ? text : "";
}

void GBMenu::setSelected(int idx) {
    if (idx >= 0 && idx < itemCount) {
        selectedItem     = idx;
        lastSelectedItem = idx;
    }
}

void GBMenu::begin() {
    Serial.println("GBMenu: Init...");
    draw();
}

void GBMenu::draw() {
    drawBackground();
    drawLogo();
    drawMenuItems();
    drawCredits();
}

void GBMenu::drawBackground() {
    tft->fillScreen(TFT_WHITE);
}

void GBMenu::drawLogo() {
    if (!hasLogo) {
        // Titolo testuale centrato
        tft->setTextColor(TFT_BLACK);
        tft->setTextSize(3);
        tft->setTextDatum(TC_DATUM);
        tft->drawString(title, 160, 20);
        return;
    }

    int logoX = (320 - logoWidth) / 2;
    int logoY = 20;

    tft->setSwapBytes(true);
    tft->pushImage(logoX, logoY, logoWidth, logoHeight, logo, 0xFFFF);
    tft->setSwapBytes(false);
}

void GBMenu::drawCredits() {
    if (!credits || credits[0] == '\0') return;
    tft->setTextSize(1);
    tft->setTextColor(TFT_BLACK);
    tft->setTextDatum(BR_DATUM);
    tft->drawString(credits, 315, 235);
}

void GBMenu::drawMenuItems() {
    for (int i = 0; i < itemCount; i++) {
        drawMenuItem(i, i == selectedItem);
    }
}

void GBMenu::drawMenuItem(int itemIdx, bool selected) {
    int y = getMenuItemY(itemIdx);
    const char* text = items[itemIdx].text;

    // Cancella area voce
    tft->fillRect(0, y - 2, 320, 26, TFT_WHITE);

    tft->setTextSize(2);
    tft->setTextDatum(TC_DATUM);

    if (selected) {
        tft->setTextColor(TFT_BLUE);

        int textWidth = strlen(text) * 12; // approssimazione per font size 2
        int cursorX   = (320 - textWidth) / 2 - 20;

        tft->drawString(">", cursorX, y);
        tft->drawString(text, 160, y);
    } else {
        tft->setTextColor(TFT_BLACK);
        tft->drawString(text, 160, y);
    }
}

int GBMenu::getMenuItemY(int itemIdx) {
    int startY = 110;
    int spacing = 35;
    return startY + (itemIdx * spacing);
}

int GBMenu::update() {
    uint8_t joyState = joystickReadHardware();

    uint32_t now = millis();
    if (now - lastJoyTime < JOY_DEBOUNCE_MS) {
        return GB_ACTION_NONE;
    }

    // UP
    if (!(joyState & 0x01)) {
        lastJoyTime = now;

        if (selectedItem > 0) selectedItem--;
        else                  selectedItem = itemCount - 1; // wrap

        if (selectedItem != lastSelectedItem) {
            drawMenuItem(lastSelectedItem, false);
            drawMenuItem(selectedItem, true);
            lastSelectedItem = selectedItem;
        }
        return GB_ACTION_NONE;
    }

    // DOWN
    if (!(joyState & 0x02)) {
        lastJoyTime = now;

        if (selectedItem < itemCount - 1) selectedItem++;
        else                              selectedItem = 0; // wrap

        if (selectedItem != lastSelectedItem) {
            drawMenuItem(lastSelectedItem, false);
            drawMenuItem(selectedItem, true);
            lastSelectedItem = selectedItem;
        }
        return GB_ACTION_NONE;
    }

    // FIRE
    if (!(joyState & 0x10)) {
        lastJoyTime = now;

        // feedback lampeggio
        drawMenuItem(selectedItem, false);
        delay(100);
        drawMenuItem(selectedItem, true);
        delay(100);

        return items[selectedItem].action;
    }

    return GB_ACTION_NONE;
}