# Boiler Assistant 3.3.12 - LCD screen-off and stuck-screen recovery

## 3.3.12 changes

Copied from 3.3.11 (3.3.11 is unchanged). Open
`BoilerAssistant_3_3_12_Total_Domination.ino` inside the matching folder.
Firmware/OTA identity, boot banner, source headers and diagnostic reports
now identify 3.3.12. No release binary has been published by this change.

Field reports described the LCD freezing after days of running while the
boiler and app kept working; a power cycle restored it. 3.3.11 can miss an
LCD write without detecting it (the backpack still ACKs), leaving stale text.
This is a hypothesis for the field fault, not a confirmed cause.

- **Screen off:** the LCD backlight turns off after 5 minutes without a key
  press. It stays on while a safety lockout, Ember Guardian latch or exhaust
  probe fallback is active. The PCF8574 backpack is on/off only, so there is
  no partial dimming.
- **Wake:** the first key press while the screen is off only turns it on;
  that key is not acted on, so nothing changes blind.
- **Stuck-screen recovery:** the wake press, and the first press after a
  5-second pause, fully reinitialize the LCD (`begin()`) and repaint all
  four rows. Rapid presses during menu navigation do not reinitialize, to
  avoid flicker and ~60 ms pauses on every key.

Limits: this cannot recover a held I2C bus, a failed keypad, or a hung
main loop. The 3.3.11 watchdog defect (`WDT.begin(16000)` exceeds the UNO R4
~5.6 s maximum and fails silently) is **not** changed in 3.3.12. Hardware
testing, including a multi-day soak, is required before release.

### Continuous mode rolled back (no ZC following)

At Karl's request, 3.3.12 removes the 3.3.11 Continuous ZC-following
behavior and restores the 3.3.9 Continuous behavior:

- Continuous auto-starts a BOOST at power-up and when selected from the
  keypad, then runs the burn off exhaust temperature. It stops only when the
  boiler's own control cuts power to the unit (power-switched install).
- Missing, noisy or lost ZC no longer stops the fan, closes the damper or
  shows **WAIT BOILER / NO ZC**. Both run modes use ZC phase control when a
  valid signal is present and PWM fallback otherwise.
- The run-mode confirm screen again reads **!!NO AUTO SHUTDOWN!!**.
- Kept from 3.3.11: the 10 ms triac gate pulse, ZC diagnostics on the
  dashboard, and the rule that a latched Ember Guardian does not auto-restart.
  Diagnostics bit 128 now means "ZC required" and is always 0.
- The "Continuous mode behavior change" section below is historical 3.3.11
  information and no longer describes 3.3.12.

Continuous ignores tank temperature and relies on the boiler's independent
factory protections. Have a qualified installer confirm the unit is powered
from the boiler's call circuit so it stops when the boiler cuts power.

### Wi-Fi more patient on weak links

Field report (3.3.11): Wi-Fi connected, then dropped as soon as the dashboard
was opened. Source review found that any loop pass taking 3 s or more,
including the whole HTTP pass that sends the 61 KB page, caused a full Wi-Fi
bridge reset (15 s quiet, reset, rejoin). A slow but working link could
therefore reset Wi-Fi during a page load. This is a hypothesis for the field
report; Serial `WiFi: modem slow: HTTP` would confirm it.

- Each single bridge command may now wait **5 s** (was 3 s) for its reply.
  Reaching that limit still resets the bridge: the WiFiS3 library does not
  discard late replies, so the next command would misread them.
- The whole-pass checks (link monitor, HTTP, MQTT, push) are now a **15 s**
  backstop. Several slow-but-answered steps no longer reset Wi-Fi alone.
- `WiFi.getTime()`, `WiFi.RSSI()` and the runtime join were only covered by
  the whole-pass check; they are now timed individually.
- Dashboard send window **30 s** (was 12 s) and request window **5 s** (was
  2 s). These are non-blocking (one 512-byte chunk per loop pass).

Trade-off: one slow Wi-Fi step can now pause the main loop for up to 5 s
(was 3 s). Fan phase output continues from its timers, but keypad/LCD and
control updates wait. A future watchdog fix (UNO R4 maximum ~5.6 s) must
account for this. To fit flash, 14 long Serial log messages were shortened
(same meaning). Not hardware tested; test on a weak-signal install.

### Dashboard "What's new" card

`WhatsNewDashboard.h` adds a card listing this release's new features and
fixes in plain language. The version comes from `FW_VERSION` in `Version.h`.

- The first time a phone or browser opens the dashboard after an update, the
  card appears at the top of **Main** with a **Got it** button.
- **Got it** saves the version in that browser's local storage and moves the
  card to the bottom of **Settings**, where it can be read again.
- Each phone/browser tracks this separately. If local storage is blocked,
  the card shows on each visit until Got it is pressed.
- No API, EEPROM or controller-logic change. **Update the list in
  `WhatsNewDashboard.h` for every release.**

Flash space: 3.3.12 is now essentially full (245,700 of 245,760 bytes, 60
bytes free after the Wi-Fi change). To fit the card, leading indentation was removed inside the
Diagnostics and Health dashboard scripts (whitespace only).
`tools\test_diagnostics.js` now extracts functions by brace matching so it no
longer depends on that indentation. Any further 3.3.x addition needs space
freed first, for example the 3.4 `JsonConfig.h` change (about 3.8 KB).

## 3.3.11 release preparation (retained)

Renamed from the 3.3.10 working sketch to 3.3.11. Open
`BoilerAssistant_3_3_11_Total_Domination.ino` inside the matching folder.
Firmware/OTA identity, boot banner, source headers and diagnostic reports
now identify 3.3.11. OTA download endpoints remain unchanged; no remote
version marker or release binary has been published by this rename.

Any pre-existing `build` files bearing the 3.3.10 sketch name are old
artifacts, not 3.3.11 release binaries. They were preserved, not renamed.
The fresh user-generated 3.3.11 BIN/HEX were built from this release source.
The OTA workflow packages the supplied BIN without recompiling it. No
hardware upload or OTA installation has been verified; Karl will test the
published update.

## Diagnostics

The read-only **Sensors > Diagnostic Test** sub-tab displays the ZC signal and selected fan
driver, plus current status/fault codes with plain-English explanations.
ZC status distinguishes missing, invalid timing, stabilizing, stable and
noise-suppressed pulses. Driver status distinguishes ZC phase control,
Auto Tank PWM/unsynchronized fallback, zero output command, Continuous
inhibition and unavailable phase timer/ZC interrupt.

Codes use OK, INFO, ACTIVE, LATCHED or UNKNOWN where applicable. Missing ZC
can be normal while the factory boiler is not calling; it is not automatically
a fault. Codes describe current conditions, not stored fault history.
Software does not measure fan speed, AC voltage or hot/neutral polarity.

**Copy Report** copies a current snapshot with an explicit allowlist of
readings, commands, fault/status codes and loop/network timings. Passwords,
tokens, Wi-Fi credentials and probe names are not included. If clipboard
access is blocked on local HTTP, copy the selected read-only text manually.
**Save Report** requests a timestamped UTF-8 `.txt` download for saving on a
phone and sending by email/message. Depending on the browser, use its
Downloads/Files or save/share menu. A download request does not confirm a
physical save. Errors select the report for manual copying; nothing is sent
automatically. Failed requests and samples older than ten seconds become
Unavailable. Previously generated report text remains a captured snapshot.

### Sensor / system health

The tab lists exhaust raw/filtered readings and last-good age, outdoor
BME280 health/temperature/humidity/pressure, and each discovered water
probe's index, configured name, control/monitor role, temperature and
last-good age. Finite fresh readings show READING OK; reported failures
show FAULT, while absent/stale data shows UNKNOWN. Exhaust freshness is
1 second and water 3 seconds. Outdoor sample age is not tracked. Probes
never discovered cannot be identified as missing from this list.

**Run Diagnostic Test** refreshes ZC/driver, safety and sensor/system status
in one panel and prepares one combined report. It requests current controller status only. It does
not force conversions, rescan probes, move fan/damper outputs or change
settings. It also displays Wi-Fi RSSI and loop/network timings. SD logging
is explicitly not included in this version. LCD, keypad, actual fan speed
and mains wiring are NOT TESTED; this is not an all-hardware pass/fail test
or proof of sensor accuracy/calibration.

Copy/Save Report includes sensor health, numeric probe indexes and roles,
but omits configured probe names for privacy. Names appear as dashboard
text only. The API adds `exhaust_age_ms`, `env.sensor_ok`, and water
`age_ms`; absent age means no known last-good sample, not zero age.
State-document overflow is logged and explicitly returned as an error.

## Continuous mode behavior change (3.3.11 only; rolled back in 3.3.12)

At Karl's request, all Continuous installations now require the same
ZC-following behavior as 3.4. The board may remain separately powered while
the factory boiler cuts AC **upstream of the dimmer**. Its isolated ZC output
on D0 must disappear when that input power is removed. No extra hardware
detector is added. Have a qualified installer verify compatibility/isolation;
never connect mains to an Arduino pin or bypass independent boiler protections.

- Eight consecutive valid 60 Hz half-cycle periods (8000-8700 us), followed
  by at least 500 ms of continued qualification, permit a fresh BOOST.
  Existing five-second damper/fan delay and BOOST/RAMP/HOLD rules remain.
- Missing, invalid or noisy ZC ends the active call, commands zero, closes
  the damper and shows **WAIT BOILER / NO ZC**. The phase timer inhibits output
  after 25 ms without a pulse, independently of foreground-loop service.
- Dropouts seen between foreground passes are retained until the engine
  ends the call. Power return does not clear safety/Guardian latches.
- Continuous never uses nonzero PWM/HIGH fallback, including timer/IRQ
  failures. Auto Tank retains its prior fallback and tank control.
- Water/exhaust sampling, Wi-Fi, and existing RAM-based water/burn histories
  continue while waiting. No 3.4 SD logger, heat-loss monitoring, or automatic
  daily update-check feature is introduced by this backport.
- A broken ZC wire also prevents starting. This is inferred call status,
  not authoritative AC detection, and does not prove the fire is out.
- 50 Hz remains unsupported by this driver, even if the dimmer is rated
  50/60 Hz. No-ZC/internal-ZC modules no longer run in Continuous mode.
- Continuous ignores normal tank-low/high cycling. The pre-existing required
  tank sensor and tank high-limit check are Auto Tank only: Continuous relies
  on independent factory protections. Manual OTA remains an explicit action
  and must only be used when the boiler is safely shut down.

3.3.9 remains unchanged. 3.4 is not modified by this backport.

## Validation

After the reported 280-byte FLASH overflow, 32 stored Serial messages in
WiFiAPI.cpp and WiFiProvisioning.cpp were shortened, retaining all logging
calls and error distinctions. Their combined UTF-8 literal payload shrank
by 697 bytes. Source comparison confirms that all non-message code in
these files is unchanged; no diagnostics feature or control rule was removed.
This is source-text savings, not a measured linked-firmware reduction.
Recompile to confirm actual flash fit. The WDT `renesas` / `renesas_uno`
architecture warning is separate from the linker FLASH overflow; library
metadata was not changed to hide it.

Run `node tools\test_diagnostics.js`. These source-derived JavaScript mocks
are not C++ compilation or hardware timing validation. They check driver
gating, Auto fallback, status/report behavior and assembled dashboard syntax.
The sibling 3.4 `tools\test_health.js` checks health behavior in both versions.
No firmware compilation or upload was performed here; compile in Arduino IDE
to verify flash/RAM fit before upload. Controlled hardware tests must confirm
ZC disappearance/return, output shutdown/restart, latch retention and continued
monitoring. Phone-specific download behavior also requires testing.

3.3.12 What's-new update: `node tools\test_diagnostics.js` passes (five
dashboard scripts; card shown before Got it, moved to Settings after, and
works when local storage is blocked). Arduino CLI compile (renesas_uno 1.6.0):
245,740 bytes flash, 18,088 bytes RAM. Checked in a desktop browser preview;
phone check still required.
