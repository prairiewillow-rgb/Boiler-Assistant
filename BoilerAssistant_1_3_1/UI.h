#pragma once
#include <Arduino.h>
#include "SystemState.h"

void ui_init();
void ui_showScreen(int state, double exhaustF, int fanPercent);
void ui_handleKey(char k, double exhaustF, int fanPercent);
