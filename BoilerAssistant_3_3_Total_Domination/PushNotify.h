/*
 * ============================================================
 *  Boiler Assistant - Push Notifications (v3.3.9 "Total Domination")
 *  ------------------------------------------------------------
 *  File: PushNotify.h
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *  Optional push notifications via the ntfy service (ntfy.sh).
 *  Free app on iOS and Android; no account required. Mirrors the
 *  same alert transitions as MQTT: high-temperature lockout,
 *  tank probe fault, exhaust probe fault, and Ember Guardian.
 *  A dashboard test button queues a test notification. HTTP response
 *  waiting is spread across loop passes; WiFiS3 TLS connect still runs
 *  synchronously with a 2-second requested connection timeout.
 *  Runtime AT response waits are 3 seconds per command; a top-level
 *  TLS operation may contain multiple commands. Quarantined networking
 *  pauses delivery while alert transitions can still queue.
 *  Delivery status is exposed through /api/state and Serial.
 *  Up to eight alert transitions can queue while WiFi is offline.
 *  An interrupted in-flight delivery is reported as outcome unknown,
 *  not retried blindly (the service may already have accepted it).
 *
 *  Version:
 *      Boiler Assistant v3.3.9 "Total Domination"
 * ============================================================
 */

#ifndef PUSH_NOTIFY_H
#define PUSH_NOTIFY_H

#include <Arduino.h>

void pushnotify_loop();
bool pushnotify_sendTest();
const char* pushnotify_status();
void pushnotify_networkLost();

#endif // PUSH_NOTIFY_H
