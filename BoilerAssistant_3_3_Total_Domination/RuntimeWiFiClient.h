/*
 * ============================================================
 *  Boiler Assistant - Guarded WiFi Transport (v3.3.9 "Total Domination")
 *  File: RuntimeWiFiClient.h
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    TCP/TLS wrapper for HTTP, MQTT and push clients. Gates and
 *    measures transport through provisioning recovery APIs.
 *    Quarantine invalidates local socket ownership without
 *    sending close commands into an unresponsive bridge.
 *    Underlying WiFiS3 operations remain synchronous.
 *
 *  Version: Boiler Assistant v3.3.9 "Total Domination"
 * ============================================================
 */

#ifndef RUNTIME_WIFI_CLIENT_H
#define RUNTIME_WIFI_CLIENT_H

#include <WiFiS3.h>
#include "WiFiProvisioning.h"

// MQTT can call its transport repeatedly inside connect/poll/stop. Guard each
// operation, not only the outer service pass, after a modem timeout.
template <class Base>
class RuntimeWiFiClient : public Base {
public:
    using Base::write;
    RuntimeWiFiClient& operator=(const Base& client) {
        Base::operator=(client);
        return *this;
    }

    int connect(IPAddress ip, uint16_t port) override {
        return connect(ip.toString().c_str(), port);
    }
    int connect(const char* host, uint16_t port) override {
        // Close stale sockets separately: the core's getSocket() otherwise
        // combines connected/close/allocation calls without a recovery gate.
        if (this->_sock >= 0) stop();
        return perform<int>(0, [&]() { return Base::connect(host, port); });
    }
    size_t write(uint8_t byte) override { return write(&byte, 1); }
    size_t write(const uint8_t* buffer, size_t length) override {
        return perform<size_t>(0, [&]() { return Base::write(buffer, length); });
    }
    int available() override {
        return perform<int>(0, [&]() { return Base::available(); });
    }
    int read() override {
        uint8_t byte;
        return read(&byte, 1) == 1 ? byte : -1;
    }
    int read(uint8_t* buffer, size_t length) override {
        return perform<int>(0, [&]() { return Base::read(buffer, length); });
    }
    int peek() override {
        return perform<int>(-1, [&]() { return Base::peek(); });
    }
    uint8_t connected() override {
        return perform<uint8_t>(0, [&]() { return Base::connected(); });
    }
    void flush() override {
        if (!wifi_prov_prepareClientIo()) {
            forgetSocket();
            return;
        }
        unsigned long start = millis();
        Base::flush();
        wifi_prov_finishIo(start, "client flush");
        if (!wifi_prov_prepareClientIo()) forgetSocket();
    }
    void stop() override {
        if (wifi_prov_prepareClientIo()) {
            unsigned long start = millis();
            Base::stop();
            wifi_prov_finishIo(start, "client close");
        } else {
            // Quarantined bridge sockets are reset before reuse.
            forgetSocket();
        }
    }

private:
    void forgetSocket() {
        this->_sock = -1;
        if (this->rx_buffer) this->rx_buffer->clear();
    }

    template <class Result, class Action>
    Result perform(Result failure, Action action) {
        if (!wifi_prov_prepareClientIo()) {
            forgetSocket();
            return failure;
        }
        unsigned long start = millis();
        Result result = action();
        wifi_prov_finishIo(start, "client transport");
        if (!wifi_prov_prepareClientIo()) {
            // Base::connected() checks _sock after virtual available();
            // invalidate it now so it cannot send a second AT command.
            forgetSocket();
            return failure;
        }
        return result;
    }
};

#endif
