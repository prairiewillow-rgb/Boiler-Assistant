#ifndef HTTP_BRIDGE_RECEIVE_H
#define HTTP_BRIDGE_RECEIVE_H

#include "RuntimeWiFiClient.h"
#include <stdio.h>

namespace HttpBridgeReceive {
constexpr size_t MAX_BYTES = 256;
constexpr int READ_ERROR = -1;
constexpr int BAD_FRAME = -2;

// Bridge reply: +CLIENTRECEIVE: <size>|<binary payload>OK\r\n.
// Only inspect status after the full binary payload has been consumed.
template <class NextByte>
int readReply(NextByte next, uint8_t* output, size_t limit) {
    int byte = next();
    while (byte == '\r' || byte == '\n') byte = next();
    size_t count = 0;
    if (byte == '+') {
        for (const char* p = PROMPT(_CLIENTRECEIVE) + 1; *p; ++p) {
            if (next() != *p) return BAD_FRAME;
        }
        do { byte = next(); } while (byte == ' ');
        if (byte < '0' || byte > '9') return BAD_FRAME;
        do {
            count = count * 10 + (byte - '0');
            if (count > limit) return BAD_FRAME;
            byte = next();
        } while (byte >= '0' && byte <= '9');
        if (byte != '|') return BAD_FRAME;
        for (size_t i = 0; i < count; ++i) {
            byte = next();
            if (byte < 0) return BAD_FRAME;
            output[i] = static_cast<uint8_t>(byte);
        }
        byte = next();
        while (byte == '\r' || byte == '\n') byte = next();
        if (byte == 'O' && next() == 'K' && next() == '\r' && next() == '\n')
            return static_cast<int>(count);
    }
    if (byte == 'E') {
        for (const char* p = "RROR\r\n"; *p; ++p) {
            if (next() != *p) return BAD_FRAME;
        }
        return READ_ERROR;
    }
    return BAD_FRAME;
}
}

class HttpBridgeClient : public RuntimeWiFiClient<WiFiClient> {
public:
    using RuntimeWiFiClient<WiFiClient>::operator=;

    int readRequest(uint8_t* output, size_t limit) {
        using namespace HttpBridgeReceive;
        if (limit == 0) return 0;
        if (limit > MAX_BYTES) limit = MAX_BYTES;
        if (_sock < 0 || !wifi_prov_prepareClientIo()) return READ_ERROR;
        if (!modem.beginned) {
            wifi_prov_requestRecovery("HTTP read: bridge not ready");
            return READ_ERROR;
        }
        // HTTP owns this socket, but drain any already-buffered bytes first.
        if (rx_buffer && rx_buffer->available()) {
            if (limit > rx_buffer->available()) limit = rx_buffer->available();
            return RuntimeWiFiClient<WiFiClient>::read(output, limit);
        }
        static char command[40];
        const int length = snprintf(command, sizeof(command), "%s%d,%u\r\n",
                                    CMD_WRITE(_CLIENTRECEIVE), _sock, static_cast<unsigned>(limit));
        if (length <= 0 || static_cast<size_t>(length) >= sizeof(command)) return READ_ERROR;

        const unsigned long started = millis();
        const auto next = [started]() -> int {
            while (millis() - started < WIFI_STEP_LIMIT_MS) {
                const int byte = Serial2.read();
                if (byte >= 0) return byte;
            }
            return -1;
        };
        int result = BAD_FRAME;
        if (Serial2.write(reinterpret_cast<const uint8_t*>(command), length) == static_cast<size_t>(length))
            result = readReply(next, output, limit);
        if (result == BAD_FRAME) {
            // A partial reply may still be on the UART. Quarantine before socket-close commands.
            wifi_prov_requestRecovery("HTTP read: unfinished bridge reply");
        }
        return result;
    }
};

#endif
