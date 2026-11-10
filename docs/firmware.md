# Firmware and control logic

> The v4 firmware ([`firmware/v4/`](../firmware/v4)) is written and passes ESPHome validation, but it has **not run on real hardware yet** (the boards are not built). Its control logic is the one that has been running for a season as Home Assistant automations driving the predecessor gateway ([`firmware/reference/`](../firmware/reference)).

## 1. Principle: the controller decides, Home Assistant watches

All control runs on the ESP32-C6. Home Assistant is a dashboard and remote control: it shows the controller's entities, changes setpoints, schedule times and settings, and supplies room and outdoor temperatures. Nothing in Home Assistant is needed for the heating to work.

| Without Home Assistant / Wi-Fi | What happens |
|---|---|
| Zones with a wired thermostat (TERM input) | Run normally. |
| Zones on "Room sensor" | After 5 minutes the room temperature is dropped (NaN). Floor tempering, which always runs, then carries the zone alone: the slab is held at setpoint + offset (laminate: the higher offset), which keeps the room warm. |
| Floor overheat protection, boiler CH demand, pumps | Unchanged (all local). |
| Flow temperature | Heating curve on the boiler's outdoor sensor (OpenTherm ID 27) if fitted, otherwise on a fixed fallback (5 °C). No demand boost (it needs room temperatures). |
| Schedules | Unchanged: they run from the DS3231 RTC, which is set from SNTP whenever the network is up. |
| Settings | Kept in flash; editable again when Home Assistant or the built-in web UI is reachable. |

The controller never reboots because Home Assistant or Wi-Fi is missing (`reboot_timeout: 0s`).

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

## 4. Hardware mapping

* Relays: MCP23017 #1 (0x20) GPA0–7 = Z1–Z8, GPB0–1 = P1–P2, through 2 × ULN2003A. All pins are inputs at power-up, so no relay energises during boot.
* Thermostat inputs: MCP23017 #2 (0x21) GPA0–7 = **TERM8–TERM1** (reversed on the PCB, mapped in `heating-controller.yaml`). Zone LEDs: GPB0–7. The MCP interrupt (IO22) and the RTC interrupt (IO21) are wired but unused; the inputs are polled.
* NTC: ADS1115 0x48 AIN3…AIN0 = NTC1…NTC4, 0x49 AIN3…AIN0 = NTC5…NTC8.
* OpenTherm: TX IO10, RX IO11 (RX inverted, see the comment in the YAML). Pump and status LEDs: IO18, IO19, IO20. Button: IO9 (also BOOT). RTC DS3231M (0x68), OLED SSD1306 (0x3C).

See [hardware.md](hardware.md) for the full pinout.
