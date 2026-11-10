# Firmware and control logic

> The controller firmware ([`firmware/controller/`](../firmware/controller)) is written, passes ESPHome validation and compiles for the ESP32-S3, but it has **not run on real hardware yet** (the boards are not built). Its control logic is the one that has been running for a season as Home Assistant automations driving the predecessor gateway ([`firmware/reference/`](../firmware/reference)).

## 1. Principle: the controller decides, Home Assistant watches

All control runs on the ESP32-S3. Two firmware options exist (see [firmware/README.md](../firmware/README.md)): **Zigbee mode**, the ESPHome configuration described here plus the Zigbee coordinator on the ESP32-H2, and **Thread/Matter mode**, a custom ESP-IDF firmware with the same control logic, a Thread border router and a Matter controller (section 6). Home Assistant is a dashboard and remote control: it shows the controller's entities, changes setpoints, schedule times and settings, and supplies room and outdoor temperatures. Nothing in Home Assistant is needed for the heating to work.

| Without Home Assistant / network | What happens |
|---|---|
| Zones with a wired thermostat (TERM input) | Run normally. |
| Zones on "Room sensor" | After 5 minutes the room temperature is dropped (NaN). Floor tempering, which always runs, then carries the zone alone: the slab is held at setpoint + offset (laminate: the higher offset), which keeps the room warm. |
| Floor overheat protection, boiler CH demand, pumps | Unchanged (all local). |
| Flow temperature | Heating curve on the boiler's outdoor sensor (OpenTherm ID 27) if fitted, otherwise on a fixed fallback (5 °C). No demand boost (it needs room temperatures). |
| Schedules | Unchanged: they run from the DS3231 RTC, which is set from SNTP or from Home Assistant whenever either is reachable. |
| Settings | Kept in flash; editable again when Home Assistant or the built-in web UI is reachable. |

The controller never reboots because Home Assistant or the network is missing (`reboot_timeout: 0s`).

**Network (ESPHome):** wired Ethernet (WIZnet W5500 on the power/IO board) and Wi-Fi. The default package `network_dual.yaml` runs both, with `network: priority: [ethernet, wifi]`: ESPHome (2026.9 and newer) routes through Ethernet while its link is up and falls back to Wi-Fi when the cable is unplugged; a "Wi-Fi" switch disables the Wi-Fi station for a wired-only setup. ESPHome versions before 2026.9 cannot run `ethernet:` and `wifi:` in one firmware; there the single-interface packages `network_ethernet.yaml` / `network_wifi.yaml` make it a compile-time choice. The Thread/Matter firmware selects the network at run time (section 6).

## 2. Control functions

| Function | Behaviour |
|---|---|
| Zone thermostat | One ESPHome `thermostat` climate per zone (setpoint, Heat/Off), 0.2 °C deadband, 2 min minimum on/off. Setpoints survive reboots. *Off* closes the zone completely. |
| Demand source (per zone) | *Room sensor* (zone thermostat with the Home Assistant room temperature), *Wired thermostat* (dry contact), *Floor only* (tempering only). |
| Floor tempering | Always active (with or without Home Assistant, whatever the demand source) in every zone with a probe and not *Off*: the valve opens whenever the floor is below its target, even if the room is warm enough. Floor target = setpoint + 2 °C (tiles) or + 3 °C (laminate: the probe sits on the screed below the cooler laminate surface; + 2 °C once the room is 0.3 °C above setpoint). During the night window, zones with night setback use the night floor target (25 °C). 0.3 °C hysteresis. Offsets and the night target are settings. |
| Zone valve | Open if (room demand **or** floor demand) **and not** floor lockout. A relay switches at most every 2 minutes; closing for a lockout or *Off* is immediate. |
| Floor overheat protection | Lockout at the per-zone floor limit (defaults: 28 °C tiles, 34 °C laminate, 36 °C for a probe above a supply pipe), released 2 °C below. A probe that becomes unknown keeps the current state. |
| Boiler CH demand | On after a zone has been open for 1 minute (thermal actuators are open by then), off as soon as no zone is open, so the boiler pump never runs against closed valves. *Heating mode: Off* closes everything (summer). |
| Flow temperature | Curve `ref + offset + slope × (ref − outdoor)`, clamped to 30–42 °C, + demand boost `(largest deficit − 0.3) × 5` (max 8) + sustained-deficit term (+1 °C every 30 min while a room stays > 0.3 °C below target, reset when all reach it) + evening preheat (+2 °C), capped at 45 °C. Written to the boiler in 0.5 °C steps on a change of ≥ 1 °C. *Flow mode: Manual* uses a fixed setpoint. |
| Evening preheat | Outdoor temperature fell ≥ 3 °C in 2 h between 16:00 and 23:00 → +2 °C flow until it has been calm for 30 min. |
| Schedule | Per zone: comfort, night and boost temperatures, and night setback and evening boost switches. Global times: day start (05:00, early because the floor is slow, so rooms are warm *by* 07:00), night start (23:00), boost window (17:30–20:00). The setpoint is set when the period changes; a manual change holds until the next change. |
| Pumps P1 / P2 | *Auto*: run while any zone is open, plus an overrun (5 min); *On*; *Off*. Weekly 1-minute exercise (Monday 10:00). Defaults: both Off (the boiler pump circulates); set *Auto* where a pump is wired. |
| Front panel | Zone LEDs follow the valves, pump LEDs the pumps. Status LED: steady = OK, slow blink = no Home Assistant, fast blink = floor lockout or boiler fault. OLED: a boiler page and one page per active zone, rotating every 5 s; the button steps through the pages and holds the current one for 30 s. |

## 3. Floor NTC measurement

Lessons from the predecessor gateway, all applied in `zone.yaml`:

* Sample every second, and pass on the **median of the raw ADS voltage** over 60 samples (one value per minute). Probe wires that share a cable with 230 V conductors pick up 50 Hz hum; the median of many randomly phased samples removes what the 10 µF input capacitor leaves, and also drops single bad I²C reads.
* Only after the median: open circuit (> 3.2 V, also unconnected inputs) and short circuit (< 0.05 V) → NaN. Then resistance → temperature, then a 0–60 °C range filter, then a 5-minute moving average. If the median ran on temperature instead, occasional bad I²C reads (e.g. code 0x3FFF = 25.8 °C) would reach Home Assistant even on unconnected inputs.
* For a truly ratiometric reading, set `ntc_supply_voltage_a/b` to the raw ADS voltage of an unconnected input, not the 3V3 rail measured with a multimeter. On the predecessor the rail read 3.32 V while the two ADCs saw 3.244 V and 3.286 V, which would have caused about 1 °C error.
* Calibrate per probe with the `offset` filter against a reference thermometer, after a long closed-circuit period when the slab has equalised.

## 4. Zigbee sensors through the ESP32-H2 (optional)

Room and outdoor temperatures can also come straight from Zigbee sensors and thermostats, without Home Assistant. The ESP32-H2 co-processor runs a Zigbee coordinator ([`firmware/zigbee-coordinator`](../firmware/zigbee-coordinator)), flashed through its own USB-C, and sends the readings to the S3 over the UART ([coprocessor-protocol.md](coprocessor-protocol.md)). On the S3, the optional package [`coprocessor.yaml`](../firmware/controller/coprocessor.yaml) adds a "permit join" button, a device list and one sensor per zone plus one for outdoors, mapped by device id. A mapped sensor takes precedence over the Home Assistant sensor; after its timeout (default 2 h without a report) it is dropped and the Home Assistant value or the fallbacks apply again. Without the package nothing changes. Untested on hardware.

## 5. Hardware mapping

* Relays: MCP23017 #1 (0x20) GPA0–7 = Z1–Z8, GPB0–1 = P1–P2, through 2 × ULN2003A. All pins are inputs at power-up, so no relay energises during boot.
* Thermostat inputs: MCP23017 #2 (0x21) GPA0–7 = **TERM8–TERM1** (reversed on the PCB, mapped in `heating-controller.yaml`). Zone LEDs: GPB0–7. The MCP interrupt (IO15) and the RTC interrupt (IO7) are wired but unused; the inputs are polled.
* NTC: ADS1115 0x48 AIN3…AIN0 = NTC1…NTC4, 0x49 AIN3…AIN0 = NTC5…NTC8.
* Clock: at boot the DS3231's OSF flag ("oscillator stopped", status register 0x0F bit 7) is checked. It is set when the CR1220 was flat or missing during a power cut; the RTC registers may then hold a plausible but wrong time, so it is ignored and the clock stays invalid: schedules (night setback, boost, pump exercise) pause and the stored setpoints stay in force until SNTP (Internet) or Home Assistant (local network) set the time. Each sync writes the RTC and clears OSF. All settings are in flash and do not depend on the cell.
* I²C: SDA IO8, SCL IO18. OpenTherm: TX IO17, RX IO16 (RX inverted, see the comment in the YAML). Pump and status LEDs: IO4, IO5, IO6. Button: IO0 (also BOOT). RTC DS3231M (0x68), OLED SSD1306 (0x3C).
* Ethernet: W5500 on SPI, SCK IO39, MOSI IO40, MISO IO41, CS IO42, INT IO38 (no reset line).
* ESP32-H2: UART IO10 (TX) / IO11 (RX), EN IO12, BOOT IO13; coexistence (PTA) lines IO14 / IO21 / IO47 / IO48 (used only by the Thread/Matter firmware).
* Logs and first flash: the S3's built-in USB-Serial/JTAG on USB-C "MAIN".

See [hardware.md](hardware.md) for the full pinout.

## 6. Thread/Matter mode (custom ESP-IDF firmware)

[`firmware/thread-matter`](../firmware/thread-matter) replaces ESPHome on the S3 when the H2 runs the OpenThread RCP ([`firmware/h2-rcp`](../firmware/h2-rcp)) instead of the Zigbee coordinator. It contains:

* **The same control logic** as sections 2 and 3, ported from the YAML lambdas to C (component `heating`): zones with thermostat, floor tempering and lockout, boiler CH demand, flow temperature (curve, demand boost, sustained-deficit term, evening preheat), schedules, pumps with exercise, NTC chain (median of 60, open/short detection, B-equation, 5-value average), LEDs, OLED pages and button. Settings and setpoints are kept in NVS. OpenTherm master and the I²C drivers are its own components (`opentherm`, `hc_drivers`).
* **A Thread border router** (ESP-IDF OpenThread BR, as in esp-thread-br) with the H2 as radio co-processor; the S3 flashes the H2 with the RCP image embedded in its own firmware when the versions differ.
* **A Matter controller** (esp-matter) that commissions Matter-over-Thread and Matter-over-Wi-Fi temperature sensors and thermostats over BLE and maps them to zone room temperatures and the outdoor temperature at run time. A mapped Matter sensor plays the role of the Home Assistant / Zigbee sensor in section 1.
* **MQTT** for state and settings instead of the ESPHome API and web UI; Home Assistant is optional.
* **Runtime network selection**: Ethernet and Wi-Fi are both up; with mode *auto* (default) Ethernet carries the default route, the border-router backbone and Matter while its link is up, and Wi-Fi takes over when the cable is unplugged. Modes *ethernet* and *wifi* force one interface. Stored in NVS, set over MQTT or the USB console.

Build, flash and interface details, sizes and the list of untested parts: [firmware/thread-matter/README.md](../firmware/thread-matter/README.md).
