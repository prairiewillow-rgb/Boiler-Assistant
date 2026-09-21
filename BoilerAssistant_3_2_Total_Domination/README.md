# Boiler Assistant v3.2

UNO R4 WiFi boiler controller with local LCD/keypad control, fan and damper control, safety handling, and an optional WiFi dashboard.

## Major Changes in v3.2

- Improved fan control with idle fan-off behavior, PWM ramping, clamp limits, deadzone modes, and exhaust-fault fallback.
- Burn-state control for `IDLE`, `BOOST`, `RAMP`, `HOLD`, and `EMBER GUARD`.
- Ember Guardian alarm behavior shuts down the fan and damper safely. The dashboard shows a full red alarm state with a reset button.
- Keypad response improved with shorter debounce timing. Standard UI convention is `*` for back and `#` for select, save, or toggle where shown.
- WiFi dashboard redesigned for iPhone-sized screens with:
  - Main boiler view with fan, boiler, exhaust, and water temperature displays.
  - Animated boiler fire, smoke, embers, idle `Z` animation, and Ember Guardian shield.
  - Live environmental readings for outdoor temperature, humidity, and pressure.
  - Main, Burn history, Settings, and Sensors tabs.
  - Water-temperature history chart with a 12-hour swipeable timeline.
  - Red alarm takeover and in-card alarm reset.
- Water temperature is sampled once per minute and stored in a rolling 12-hour RAM buffer.
- The latest 12 completed burn cycles record burn duration and time between burns.
- LoRa support has been removed from the firmware.
- The CYD 4-inch display client now follows the v3.2 Main layout with fan, boiler, exhaust, water temperature, alarms, and matching history/burn presentation.

## History Behavior

Water history is stored in UNO RAM only. It is not written to EEPROM and is cleared when the controller reboots or loses power.

- Sampling interval: 1 minute
- Maximum water samples: 720
- Coverage: approximately 12 hours
- Burn history: latest 12 completed cycles
- Dashboard chart refresh: once per minute

The dashboard requests history separately from live state data to keep normal state responses smaller.

## Dashboard

The dashboard is served by the UNO R4 WiFi at the controller's IP address.

Tabs:

1. Main
2. Burn history
3. Settings
4. Sensors

The history chart is intended for trend review. Live boiler state, temperatures, fan output, alarms, and sensor status remain available on the Main and Sensors views.

## Hardware Notes

- Board target: Arduino UNO R4 WiFi
- Fan PWM: D5
- Damper relay: D6, active LOW
- DS18B20 water probes: D8 OneWire bus
- MAX31855 thermocouples: hardware SPI, with chip selects defined in `Pinout.h`
- LCD, BME280, and keypad share the primary I2C bus on A4/A5
- BME280 modules may be 3.3 V only. Confirm the breakout has level shifting/regulation before connecting to 5 V I2C.

See `Pinout.h` for the authoritative pin assignments and `HardwareManifest.h` for the parts reference. The CYD client is in `BoilerAssistant_CYD_4in_v3_1` and requires the ESP32 board package plus `TFT_eSPI`, `XPT2046_Touchscreen`, `ArduinoJson`, and `PNGdec`.


## Safety

This project controls real boiler hardware. Verify wiring, relay polarity, fan behavior, sensor placement, high-temperature limits, and local safety requirements before operating unattended. The firmware safety logic should not replace independent mechanical safety controls.
