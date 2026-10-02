/*
 * ============================================================
 *  Boiler Assistant - Push Notifications (v3.3.6 "Total Domination")
 *  ------------------------------------------------------------
 *  File: PushNotify.h
 *  License: CC BY-NC-SA 4.0
 *
 *  Optional push notifications via the ntfy service (ntfy.sh).
 *  Free app on iOS and Android; no account required. Mirrors the
 *  same alert transitions as MQTT: high-temperature lockout,
 *  tank probe fault, exhaust probe fault, and Ember Guardian.
 *  A dashboard test button sends a test notification.
 *
 *  Version:
 *      Boiler Assistant v3.3.6 "Total Domination"
 * ============================================================
 */

#ifndef PUSH_NOTIFY_H
#define PUSH_NOTIFY_H

#include <Arduino.h>

void pushnotify_loop();
bool pushnotify_sendTest();

#endif // PUSH_NOTIFY_H
