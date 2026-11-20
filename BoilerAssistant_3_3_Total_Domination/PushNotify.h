/*
 * Boiler Assistant - PushNotify
 * License: CC BY-NC-SA 4.0
 */

#ifndef PUSH_NOTIFY_H
#define PUSH_NOTIFY_H

#include <Arduino.h>

// Set to 0 to omit ntfy transport for network isolation.
#define BOILER_NTFY_ENABLED 1

void pushnotify_loop();
bool pushnotify_sendTest();
const char* pushnotify_status();
void pushnotify_networkLost();

#endif // PUSH_NOTIFY_H
