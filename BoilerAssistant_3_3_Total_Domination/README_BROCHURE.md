# Boiler Assistant

## Smarter control for solid-fuel boiler systems

Boiler Assistant is an intelligent boiler control platform designed to help manage combustion, protect equipment, and make heating systems easier to understand.

It continuously monitors boiler and exhaust temperatures, adjusts the combustion fan, tracks operating conditions, and gives the operator a clear view of what the system is doing. The result is a more controlled, more observable heating experience from startup through shutdown.

## One controller. A clearer view of the whole burn.

Boiler Assistant brings the most important parts of boiler operation together in one system:

- Automatic demand-based fan control
- Tank and exhaust temperature monitoring
- Local LCD and keypad control
- Web dashboard access over Wi-Fi
- Optional MQTT and Home Assistant integration
- Burn history and temperature trends
- Configurable operating profiles for changing outdoor conditions
- Multiple layers of software safety monitoring
- Over-the-air firmware updates from the keypad
- Imperial or metric outdoor readings

## Built to manage the burn

Boiler Assistant can automatically start heating when the tank needs energy and stop when the selected temperature range has been reached. During an active burn, it uses exhaust temperature and configured limits to shape fan output instead of simply running the blower at one fixed speed.

Startup boost, gradual ramping, minimum and maximum fan limits, and deadband control help the system move through the stages of a burn in a deliberate way. Operators can also use a continuous operating mode when the application calls for it.

Once the burn reaches its target zone, the fan eases toward temperature rather than running at full speed. Minimum on and off times and built-in hysteresis prevent rapid fan cycling, while the adaptive curve continues to learn how the boiler responds.

## Protection that watches the important details

The controller monitors the conditions that matter during operation:

- High tank temperature limits
- Tank and exhaust sensor faults
- Low-flue-temperature conditions during active burn states
- Damper shutdown behavior during protected stops
- Local lockout and Ember Guardian status

When a monitored condition requires attention, the system presents the status locally and on the dashboard. Critical high-temperature reset actions remain local, keeping physical access part of the recovery process.

If the exhaust probe fails, the burn does not simply stop. The controller keeps the fire going with the fan at its maximum configured limit, continues to follow tank temperatures to start and stop heating, and alerts the operator that the probe needs cleaning or replacement. Outdoor sensor readings are range-checked so a faulty or disconnected sensor is reported instead of showing false values.

Software monitoring is intended to supplement approved mechanical limits, appliance controls, and other independent safety devices. It is not a replacement for them.

## Information where you need it

The built-in LCD and keypad provide direct access at the boiler. The home screen can show current and target exhaust temperature, tank temperatures, fan command, burn state, and protection status.

When connected to Wi-Fi, the responsive dashboard provides an at-a-glance view from a phone, tablet, or computer. Operators can review current conditions, environmental readings, settings, alarms, burn history, and water-temperature trends without standing beside the boiler.

## Ready for connected homes

Optional MQTT support allows Boiler Assistant to publish boiler state, temperatures, settings, alerts, and environmental readings to a home automation system. Home Assistant discovery support makes integration easier when an MQTT broker is already available.

A built-in provisioning portal helps configure Wi-Fi, MQTT, display, and control credentials. The controller can also create its own setup network when normal Wi-Fi is not available.

## Adapts to the season

Boiler Assistant supports configurable summer, spring/fall, winter, and extreme-cold profiles. Each profile can adjust operating targets such as exhaust temperature, tank limits, outdoor thresholds, and fan limits.

Seasonal operation can be selected manually or changed automatically using outdoor temperature and configured hysteresis, helping the system respond to changing weather without requiring constant adjustment.

Outdoor temperature and pressure can be shown in imperial (°F, inHg) or metric (°C, kPa) units on both the LCD and the dashboard, and seasonal outdoor thresholds are entered in the same units. Boiler water and exhaust temperatures always remain in °F.

## Stays up to date

New firmware can be installed over Wi-Fi directly from the keypad. The controller checks for a newer release, shows the available version, and asks for confirmation before installing. The boiler is placed in its idle safe state, with the fan off and damper closed, before the update begins, and all saved settings are kept.

## Designed for visibility and control

Boiler Assistant is built for people who want more than an on/off switch. It helps answer practical questions during every burn:

- Is the boiler producing the heat it should?
- Is the tank approaching its target range?
- What is the fan being asked to do?
- Is the exhaust temperature recovering normally?
- Has a sensor, limit, or protection state changed?
- How did recent burns perform?

The system records recent burn cycles and maintains short-term temperature trends in memory so operators can review behavior and make better-informed adjustments.

## A flexible platform for boiler projects

Boiler Assistant runs on an Arduino UNO R4 WiFi and is intended for configured boiler installations using compatible sensors, fan controls, damper hardware, and independent safety equipment. Its settings, sensor roles, environmental profiles, and credentials are stored with configuration validation to help preserve a consistent setup.

Whether used as a dedicated local controller or connected to a wider home automation system, Boiler Assistant provides a practical foundation for monitoring and managing a solid-fuel boiler installation.

## Important safety notice

Boiler Assistant controls real combustion equipment. Installation, commissioning, sensor placement, draft verification, electrical work, and safety validation must be performed by qualified personnel. Confirm that the appliance, blower, dimmer or controller, relay, damper, sensors, and independent safety devices are compatible and correctly rated. Firmware protections must never be used as a substitute for approved mechanical safety controls or applicable local requirements.

## Technical platform

- Arduino UNO R4 WiFi controller
- MAX31855 exhaust thermocouple
- Up to eight DS18B20 water-temperature probes
- Optional BME280 outdoor temperature, humidity, and pressure sensor
- Local LCD and I2C keypad interface
- Wi-Fi dashboard, HTTP API, and optional MQTT telemetry
- Over-the-air firmware updates
- Configurable fan and damper control

For installation details, configuration limits, pin assignments, libraries, and build instructions, see the technical [README.md](README.md).
