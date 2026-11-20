/*
 * Boiler Assistant - HardwareManifest
 * Author: The Architect Collective
 * License: CC BY-NC-SA 4.0
 *
 * This software is open-source for personal and private use.
 * Commercial use, resale, or inclusion in commercial products
 * is strictly prohibited under the CC BY-NC-SA 4.0 license.
 */


/*
=============================================================================
Boiler Assistant
Master Hardware Parts List
=============================================================================

Main Board:
- Freenove Control Board V5 Rev4 WiFi
  https://a.co/d/0fi2CTqM

Pinout Board:
- GeeekPi Screw Terminal Hat for Arduino UNO D-1299 REV1.2
  https://a.co/d/0eYxnK8R

Keypad:
- 4x4 Matrix Keypad
  https://a.co/d/08YvBJxX

Fan Control:
- PWM fan dimmer (option 1)
  https://a.co/d/05Qgi3UM
- PWM fan dimmer (option 2)
  https://a.co/d/0efNGylN

Relays:
- 1 channel relay modules (May need 2)
  https://a.co/d/09icsKJu

LCD:
- I2C IIC LCD 20x4 Module
  https://a.co/d/0eLV26gI

Wiring:
- Dupont jumper wire kit
  https://a.co/d/07fhkUW1

Sensors:
- BME280 I2C Temperature/Humidity/Pressure
  https://a.co/d/05BaKDiR

Thermocouples:
- MAX31855 K-Type Thermocouple Amplifier
  https://a.co/d/0jifkXaX
- K-Type Thermocouple Probe (100mm, -100 degrees C to 1250 degrees C)
  https://a.co/d/0fuPfVu2
- One thermocouple only (TC1, exhaust, CS D7). Water temperatures use the
  DS18B20 probes (up to 8) on D8.

Optional Data Logging:
- Micro SD card module, SPI, with on-board 3.3V regulator / level conversion
  (generic "Micro SD Card Module TF Card Adapter ... 3.3V 5V" type)
  Wiring: VCC -> 5V, GND -> GND, CS -> D10, SCK -> D9, MOSI -> A0, MISO -> A1
  (own software-SPI pins; not shared with the thermocouple).
- Micro SD card: 8-32 GB SDHC, formatted FAT32, "High Endurance" type
  preferred. exFAT (most 64 GB and larger cards) is not supported.
  Estimated 0.7 MB/day (about 0.25 GB/year) at 30-second rows; not yet measured.

Future Development:
- CYD 4.0" Touch Display (320x480)
  https://a.co/d/0aSKZrsA

=============================================================================
This file is documentation only. It is not compiled.
=============================================================================
*/
