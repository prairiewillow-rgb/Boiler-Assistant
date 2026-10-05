/*
 * ============================================================
 *  Boiler Assistant - Bounded BME280 Driver (v3.3.9 "Total Domination")
 *  ------------------------------------------------------------
 *  File: BoundedBME280.h
 *  Maintainer: Karl (Embedded Systems Architect)
 *  License: CC BY-NC-SA 4.0
 *
 *  Description:
 *    Reuses Adafruit compensation with asynchronous reset,
 *    calibration and sample-startup waits. Checked calibration,
 *    identity, configuration and raw sample reads gate readiness.
 *    Calibration-busy timeout is 250ms; sample startup is 150ms.
 *    Owns one allocated I2C interface through the base driver.
 *
 *  Version:
 *      Boiler Assistant v3.3.9 "Total Domination"
 * ============================================================
 */

#ifndef BOUNDED_BME280_H
#define BOUNDED_BME280_H

#include <Adafruit_BME280.h>
#include <new>

// Reuse Adafruit's compensation, but never enter its unbounded calibration
// wait. Reset/calibration/startup waits advance on later loop passes.
class BoundedBME280 : public Adafruit_BME280 {
public:
    enum InitState { IDLE, RESET_WAIT, CALIBRATION_WAIT, SAMPLE_WAIT, READY, FAILED };

    bool start() {
        if (!i2c_dev) i2c_dev = new (std::nothrow) Adafruit_I2CDevice(0x76);
        state = FAILED;
        if (!i2c_dev) {
            Serial.println("BME280: bus interface allocation failed");
            return false;
        }
        if (!deviceBegun) {
            if (!i2c_dev->begin(false)) {
                Serial.println("BME280: bus interface initialization failed");
                return false;
            }
            deviceBegun = true;
        }
        uint8_t id = 0;
        if (!readRegisters(BME280_REGISTER_CHIPID, &id, 1) || id != 0x60) {
            Serial.println("BME280: missing device or invalid chip ID");
            return false;
        }
        _sensorID = id;
        if (!writeRegister(BME280_REGISTER_SOFTRESET, 0xB6)) {
            Serial.println("BME280: reset transfer failed");
            return false;
        }
        startedMs = millis();
        lastPollMs = startedMs;
        state = RESET_WAIT;
        return true;
    }

    InitState poll() {
        unsigned long now = millis();
        if (state == RESET_WAIT) {
            if (now - startedMs < 10UL) return state;
            state = CALIBRATION_WAIT;
        }
        if (state == CALIBRATION_WAIT) {
            if (now - lastPollMs < 10UL) return state;
            lastPollMs = now;
            uint8_t status;
            if (!readRegisters(BME280_REGISTER_STATUS, &status, 1))
                return fail("BME280: calibration status read failed");
            if (status & 1) {
                if (now - startedMs >= 250UL)
                    return fail("BME280: calibration timed out");
                return state;
            }
            if (!loadCalibration() || !configure())
                return fail("BME280: calibration/configuration transfer failed");
            startedMs = millis();
            state = SAMPLE_WAIT;
            return state;
        }
        // X16 temperature/pressure/humidity conversion takes about 113ms.
        if (state == SAMPLE_WAIT && now - startedMs >= 150UL) {
            if (!measurementAvailable())
                return fail("BME280: startup identity/configuration verification failed");
            state = READY;
        }
        return state;
    }

    bool pending() const {
        return state == RESET_WAIT || state == CALIBRATION_WAIT || state == SAMPLE_WAIT;
    }

    void cancel() { state = IDLE; }
    bool ready() const { return state == READY; }

    bool measurementAvailable() {
        uint8_t id = 0;
        uint8_t registers[4] = {};
        uint8_t raw[8] = {};
        // Identity and configuration readback distinguish this sensor from
        // stale bus data; status alone is not proof of a working BME280.
        if (!readRegisters(BME280_REGISTER_CHIPID, &id, 1) || id != 0x60 ||
            !readRegisters(BME280_REGISTER_CONTROLHUMID, registers, sizeof(registers)) ||
            registers[0] != _humReg.get() || (registers[1] & 1) ||
            registers[2] != _measReg.get() || registers[3] != _configReg.get() ||
            !readRegisters(BME280_REGISTER_PRESSUREDATA, raw, sizeof(raw))) return false;
        uint32_t pressure = (static_cast<uint32_t>(raw[0]) << 12) |
                            (static_cast<uint32_t>(raw[1]) << 4) | (raw[2] >> 4);
        uint32_t temperature = (static_cast<uint32_t>(raw[3]) << 12) |
                               (static_cast<uint32_t>(raw[4]) << 4) | (raw[5] >> 4);
        uint16_t humidity = (static_cast<uint16_t>(raw[6]) << 8) | raw[7];
        return pressure != 0 && pressure != 0xFFFFF && pressure != 0x80000 &&
               temperature != 0 && temperature != 0xFFFFF && temperature != 0x80000 &&
               humidity != 0x8000 && humidity != 0xFFFF;
    }

private:
    InitState state = IDLE;
    bool deviceBegun = false;
    unsigned long startedMs = 0;
    unsigned long lastPollMs = 0;

    InitState fail(const char* message) {
        Serial.println(message);
        state = FAILED;
        return state;
    }

    bool readRegisters(uint8_t reg, uint8_t* data, size_t count) {
        return i2c_dev && i2c_dev->write_then_read(&reg, 1, data, count);
    }

    bool writeRegister(uint8_t reg, uint8_t value) {
        uint8_t data[2] = {reg, value};
        return i2c_dev->write(data, sizeof(data));
    }

    static uint16_t little16(const uint8_t* data) {
        return static_cast<uint16_t>(data[0]) |
               (static_cast<uint16_t>(data[1]) << 8);
    }

    bool loadCalibration() {
        uint8_t tp[26], h[7];
        if (!readRegisters(0x88, tp, sizeof(tp)) ||
            !readRegisters(0xE1, h, sizeof(h))) return false;
        _bme280_calib.dig_T1 = little16(tp);
        _bme280_calib.dig_T2 = static_cast<int16_t>(little16(tp + 2));
        _bme280_calib.dig_T3 = static_cast<int16_t>(little16(tp + 4));
        _bme280_calib.dig_P1 = little16(tp + 6);
        _bme280_calib.dig_P2 = static_cast<int16_t>(little16(tp + 8));
        _bme280_calib.dig_P3 = static_cast<int16_t>(little16(tp + 10));
        _bme280_calib.dig_P4 = static_cast<int16_t>(little16(tp + 12));
        _bme280_calib.dig_P5 = static_cast<int16_t>(little16(tp + 14));
        _bme280_calib.dig_P6 = static_cast<int16_t>(little16(tp + 16));
        _bme280_calib.dig_P7 = static_cast<int16_t>(little16(tp + 18));
        _bme280_calib.dig_P8 = static_cast<int16_t>(little16(tp + 20));
        _bme280_calib.dig_P9 = static_cast<int16_t>(little16(tp + 22));
        _bme280_calib.dig_H1 = tp[25];
        _bme280_calib.dig_H2 = static_cast<int16_t>(little16(h));
        _bme280_calib.dig_H3 = h[2];
        _bme280_calib.dig_H4 = static_cast<int8_t>(h[3]) * 16 + (h[4] & 0x0F);
        _bme280_calib.dig_H5 = static_cast<int8_t>(h[5]) * 16 + (h[4] >> 4);
        _bme280_calib.dig_H6 = static_cast<int8_t>(h[6]);
        return _bme280_calib.dig_T1 != 0 && _bme280_calib.dig_T1 != 0xFFFF &&
               _bme280_calib.dig_P1 != 0 && _bme280_calib.dig_P1 != 0xFFFF;
    }

    bool configure() {
        _measReg.mode = MODE_NORMAL;
        _measReg.osrs_t = _measReg.osrs_p = SAMPLING_X16;
        _humReg.osrs_h = SAMPLING_X16;
        _configReg.t_sb = STANDBY_MS_0_5;
        _configReg.filter = FILTER_OFF;
        _configReg.spi3w_en = 0;
        return writeRegister(BME280_REGISTER_CONTROL, MODE_SLEEP) &&
               writeRegister(BME280_REGISTER_CONTROLHUMID, _humReg.get()) &&
               writeRegister(BME280_REGISTER_CONFIG, _configReg.get()) &&
               writeRegister(BME280_REGISTER_CONTROL, _measReg.get());
    }
};

#endif
