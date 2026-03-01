/*
 * ============================================================
 *  Boiler Assistant – MQTT Integration Module (v2.0)
 *  ------------------------------------------------------------
 *  File: MQTTClient.h
 *  Author: The Architect Collective
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *      MQTT interface for telemetry publishing and remote
 *      control. Provides:
 *        • State + settings publishing
 *        • Command topic subscription
 *        • Home Assistant compatibility
 *        • Two‑way sync with SystemData
 *
 * ============================================================
 */

#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

void mqtt_init();
void mqtt_loop();

#endif
