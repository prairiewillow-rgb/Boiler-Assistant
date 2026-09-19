Boiler Assistant Windows Updater

1. Connect the UNO R4 WiFi to USB.
2. Close Arduino IDE and serial monitors.
3. Double-click Update-BoilerAssistant.cmd.
4. Press Enter when prompted.

The updater downloads the newest firmware from GitHub automatically, then
uploads it. It does not require the user to install Arduino IDE or compile
source code. If there is no internet connection, it falls back to any .hex
file already sitting in the firmware folder.

Package contents:
- firmware\: holds the downloaded (or fallback) release firmware image
- tools\arduino-cli.exe: bundled Arduino command-line uploader
- tools\arduino-data\: bundled Arduino UNO R4 board package data

Do not unplug the controller during upload.
