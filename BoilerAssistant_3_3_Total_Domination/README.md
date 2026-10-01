# Boiler Assistant v3.3

Arduino UNO R4 WiFi firmware for boiler draft control, local LCD/keypad operation, temperature monitoring, safety handling, and optional Wi-Fi services.

## Features

### Combustion and Fan Control

- Burn states: `IDLE`, `BOOST`, `RAMP`, `HOLD`, and `EMBER GUARD`.
- `AUTO TANK` starts heating below the tank low setpoint and stops at the high setpoint. `CONTINUOUS` mode runs without tank-temperature automatic start/stop and requires local confirmation because it ignores tank temperature.
- Exhaust-temperature demand control with configurable exhaust setpoint and deadband.
- Fan minimum/maximum clamps, two deadzone modes, startup kick, and gradual output ramping.
- Deadzone mode 0 allows the fan to turn off below its minimum threshold. Mode 1 keeps it on within the configured clamp range.
- Exhaust-sensor fallback commands full fan output during an active burn after a confirmed sensor fault.
- The displayed fan percentage is the firmware's final output command. It is not a measured RPM, airflow, or AC output-voltage reading. Actual fan behavior depends on the connected dimmer/controller and motor.

### Safety Handling

- Tank sensor faults in `AUTO TANK` and the configured 190 F hard tank limit stop the burn and close the damper.
- Exhaust sensor faults are confirmed before the full-fan fallback is activated.
- Ember Guardian monitors raw exhaust temperature in `RAMP` and `HOLD`, starts a configurable timer below the low-flue threshold, and latches a shutdown if the flue does not recover.
- Lockout and Ember Guardian states are shown locally and in the dashboard. High-temperature lockout reset is local-only through the keypad.
- These software protections supplement, but do not replace, independent appliance limits and mechanical safety devices.

### Sensors and History

- MAX31855 thermocouple input for exhaust temperature, with cached readings and separate raw and smoothed values.
- Up to eight DS18B20 water probes on a shared OneWire bus. Probe roles and display names can be configured.
- Optional BME280 outdoor temperature, humidity, and pressure readings. Environmental seasonal automation requires valid BME280 data.
- Water history is sampled once per minute into a 720-sample rolling RAM buffer, approximately 12 hours. It is lost at reboot and is not stored in EEPROM.
- Burn history retains the latest 12 completed cycles, including duration, time between starts, and tank temperature when available. It is RAM-only.

### Environmental Profiles

- Four profiles: summer, spring/fall, winter, and extreme cold.
- Each profile can define its outdoor-temperature threshold, hysteresis, exhaust setpoint, tank high/low setpoints, and fan maximum clamp.
- Seasonal mode can be off, user-selected, or automatic. Automatic selection uses outdoor temperature and configured hysteresis; a lockout can limit how often the active season changes.

### Local LCD and Keypad

- Home screen shows exhaust setpoint/current temperature, tank temperatures, fan command, burn state, and Guardian countdown/status.
- Menus configure combustion, boiler run mode and tank setpoints, seasonal profiles, probe roles, BME280 status, network information, and safety status.
- `A`, `B`, `C`, and `D` open the main menu groups. `*` backs out or acknowledges/resets a fault where permitted; `#` selects or saves where shown.
- Manual BOOST and two-step network factory reset are available through the local interface.

### Wi-Fi Dashboard, API, and MQTT

- Responsive dashboard served by the UNO R4 WiFi at its network address, with Main, Burn history, Settings, and Sensors views.
- Main view includes boiler/fan state, temperatures, environmental readings, and alarm presentation. History view charts water temperature; the chart is for trend review and is not a safety instrument.
- STA-first Wi-Fi startup. If credentials are missing or the connection fails, the controller starts the `BoilerAssistant-Setup` access point and provisioning portal at `192.168.4.1`.
- Provisioning accepts Wi-Fi and optional MQTT credentials, a display name (Insert your anme there), and a control/API password.
- HTTP endpoints: `GET /api/state`, `GET /api/settings`, `GET /api/history`, and token-protected `POST /api/set`, `/api/probe`, and `/api/reset`. Write requests use the `X-Boiler-Token` header. A high-temperature reset remains local-only.
- Optional MQTT publishes state, settings, water probes, outdoor readings, and alerts under `boiler/*`; it subscribes to `boiler/cmd/#` and publishes Home Assistant MQTT discovery when connected.
- MQTT and the dashboard API are skipped while the controller is in Wi-Fi provisioning AP mode.

## Settings and Data Storage

- Combustion settings, run mode, tank setpoints, environmental profiles, probe roles/names, runtime credentials, and adaptive slope are stored in EEPROM with configuration versioning and CRC validation.
- Water and burn history are stored in RAM only and are cleared by reset or power loss.
- The exhaust adaptive slope is adjusted during fan-off mode and saved periodically; it is bounded by firmware limits.

## Hardware and Build

- Board: Arduino UNO R4 WiFi (`arduino:renesas_uno:unor4wifi`).
- Fan dimmer output: D5; uses Z-C-synchronized PSM pulses when a valid Z-C signal is detected on D0, otherwise falls back to the legacy PWM output for existing installations.
- RobotDyn-style phase dimmer: connect its low-voltage Z-C output to D0 and PSM to D5. D9 is not an external-interrupt pin on UNO R4. The control driver auto-detects Z-C so installations without that lead retain D5 PWM behavior.
- Damper relay: D6, active LOW.
- DS18B20 bus: D8.
- Exhaust MAX31855: hardware SPI clock D13 and data D12, with chip select defined in `Pinout.h`.
- LCD, keypad, and BME280 use I2C on A4/A5.
- Install the Arduino UNO R4 board package and the sketch's required libraries: OneWire, DallasTemperature, Adafruit MAX31855, Adafruit BME280, ArduinoJson, and ArduinoMqttClient.
- Compile for the UNO R4 WiFi using Arduino IDE or Arduino CLI. The VS Code task is named `Arduino: Verify UNO R4 WiFi`.

See `Pinout.h` for pin assignments and `HardwareManifest.h` for the parts reference. The optional CYD 4-inch display client is maintained separately in `BoilerAssistant_CYD_4in_v3_1` and requires the ESP32 board package, `TFT_eSPI`, `XPT2046_Touchscreen`, `ArduinoJson`, and `PNGdec`.

## Field Upgrade: RobotDyn Z-C Lead

For installations using the RobotDyn-style dimmer with low-voltage terminals labeled `VCC`, `GND`, `Z-C`, and `PSM`, the v3.3 phase-control firmware needs **one additional low-voltage signal wire**. This is not an AC neutral wire and must never be connected to a mains terminal. These instructions apply only to that terminal layout; identify the actual board and its markings before work begins.

| Signal | Existing or new connection |
| --- | --- |
| Dimmer `PSM` | Existing UNO R4 WiFi D5 connection; leave it in place. |
| Dimmer `Z-C` | **New** signal lead to UNO R4 WiFi D0 (RX). |
| Dimmer `VCC` and `GND` | Existing low-voltage supply and common ground; verify against the module's rated supply, but do not move them for this upgrade. |

D0 is reserved for Z-C in this firmware; do not also use it for `Serial1` RX. D9 is not a supported external-interrupt input on the UNO R4 WiFi. Do not move the thermocouple chip-selects or the damper relay wiring.

1. Have a qualified installer shut down the boiler, isolate **both mains and controller power**, lock out the supply, and verify de-energization before opening the enclosure. Do not connect or disconnect the dimmer while powered.
2. Identify the low-voltage `Z-C` pin on the actual dimmer and UNO R4 WiFi D0. Route and secure one insulated signal lead from `Z-C` to D0, kept separate from mains wiring according to the module's isolation instructions and local electrical requirements. Do not connect `Z-C` to neutral, line, load, or earth.
3. Confirm the existing `PSM`-to-D5, `VCC`, and `GND` connections remain intact, and check the new lead for shorts or loose terminations before closing the enclosure. No AC-side wiring changes are part of this modification.
4. Install the v3.3 firmware containing `FanDimmer.cpp` if the unit does not already have it. Reassemble and energize only after the wiring has been checked.
5. With the boiler working, check that the fan speeds up and slows down as the displayed percentage changes. Write down the unit ID, fan model, and lowest setting where the fan runs steadily.

When the new Z-C wire is working, the controller uses it to time the dimmer. Units without the wire continue using the old D5 signal. If the Z-C signal is lost after it starts working, the controller stops adjusting the dimmer and holds its control signal ON whenever the fan command is above 0%. 

## Safety

This firmware controls real combustion equipment. Confirm the blower and dimmer/controller are compatible and rated for one another; the percentage shown is a command, not measured fan speed. Verify draft, combustion, sensor placement, relay behavior, approved temperature limits, and local requirements with qualified personnel before unattended operation. Do not use firmware protections as a substitute for independent mechanical safety controls.
