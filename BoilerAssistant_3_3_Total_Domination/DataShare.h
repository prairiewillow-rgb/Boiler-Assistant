/*
 * Boiler Assistant - boiler make/model and data-sharing consent (v3.4.0)
 * Stores the owner's boiler make, model and opt-in to share anonymous burn
 * data. Sharing is only active while consent is given AND the SD logger is
 * ready, because the shared data comes from the SD logs. Nothing is uploaded
 * yet; the upload destination is still to be decided.
 * Maintainer: Karl | License: CC BY-NC-SA 4.0
 */
#ifndef DATA_SHARE_H
#define DATA_SHARE_H

#include <Arduino.h>

constexpr uint8_t BOILER_TEXT_MAX = 31;
// Raise when the consent wording changes so owners must agree again.
constexpr uint8_t DATASHARE_CONSENT_VERSION = 1;

void datashare_init();
const char* datashare_make();
const char* datashare_model();
bool datashare_consent();
// Consent given and SD logging ready.
bool datashare_active();
// Validates and saves; writes EEPROM only when something changed.
// Turning consent on requires a ready SD card plus make and model.
bool datashare_set(const char* make, const char* model, bool consent, const char*& error);
void datashare_clearConsent();

#endif
