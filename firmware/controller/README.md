# Controller firmware (ESPHome)

ESPHome configuration for the controller. It runs the complete heating control locally; Home Assistant is
optional and only used as dashboard, remote control and source of room/outdoor temperatures. The control logic is
described in [docs/firmware.md](../../docs/firmware.md).

> Not yet tested on hardware. Validated and compiled with ESPHome 2026.9.1 for the ESP32-S3 (all six combinations of
> the three network packages with and without `coprocessor.yaml`).

This firmware runs on the ESP32-S3-WROOM-1-N16R8 (`esp32s3`, ESP-IDF framework, 16 MB flash, 8 MB octal PSRAM
enabled with `psram: mode: octal`; logs on the built-in USB-Serial/JTAG, USB-C "MAIN"). It is the "Zigbee mode" of
the controller ([../README.md](../README.md)). The ESP32-H2 co-processor on the logic board is optional: it runs the
Zigbee coordinator firmware in [`../zigbee-coordinator`](../zigbee-coordinator), flashed separately through its own
USB-C port ("RADIO"), and is used only if `coprocessor.yaml` is included (see below).
IO10 / IO11 are the UART to the H2. IO12 drives the H2's EN line: the `h2_coprocessor` component keeps it an input and
only pulses it low for a reset. IO13 (H2 BOOT) stays unconfigured; pulling it low during a reset would put the H2 into
download mode. Without the package, leave IO12 / IO13 unconfigured as well. The coexistence lines IO14 / IO21 / IO47 /
IO48 are not used by this firmware.

## Files

| File | Content |
|---|---|
| `heating-controller.yaml` | Main configuration: buses, OpenTherm, RTC, pumps, display, control loop, flow temperature, schedule, and the 8 zones |
| `zone.yaml` | One zone (relay, LED, TERM input, floor NTC, room temperature, thermostat, settings), included 8 times with `vars` |
| `network_dual.yaml` | Network package (default): Ethernet + Wi-Fi, Ethernet preferred while its link is up (ESPHome >= 2026.9) |
| `network_ethernet.yaml` | Network package: W5500 Ethernet only (SCK IO39, MOSI IO40, MISO IO41, CS IO42, INT IO38) |
| `network_wifi.yaml` | Network package: Wi-Fi only, with fallback access point, captive portal and Improv |
| `coprocessor.yaml` | Optional package: Zigbee sensors through the ESP32-H2 (UART, permit-join button, device list, per-zone and outdoor mapping) |
| `components/h2_coprocessor/` | ESPHome external component used by `coprocessor.yaml` ([protocol](../../docs/coprocessor-protocol.md)) |
| `secrets.yaml.example` | Template for `secrets.yaml` (Wi-Fi, API key, OTA and fallback AP passwords) |

## Build and flash

```sh
cd firmware/controller
cp secrets.yaml.example secrets.yaml     # fill in; API key: openssl rand -base64 32
esphome run heating-controller.yaml
```

First flash over USB-C "MAIN" (the ESP32-S3's built-in USB-Serial/JTAG): hold the front button (BOOT) while plugging
in if the port does not appear. Later updates over the air. With the Wi-Fi package and no known network the controller
opens the "Heating Controller Setup" access point (captive portal); Improv over USB serial also works.

## Network: Ethernet and/or Wi-Fi

One network package is included under `packages:` in `heating-controller.yaml`:

```yaml
  net: !include network_dual.yaml           # default: Ethernet + Wi-Fi, Ethernet preferred
  # net: !include network_ethernet.yaml     # Ethernet only
  # net: !include network_wifi.yaml         # Wi-Fi only
```

* **`network_dual.yaml` (default):** both interfaces are up, listed under `network: priority: [ethernet, wifi]`.
  ESPHome's network component pins the default route to the first *connected* interface of that list, so with the
  cable plugged in (link up and an address) the controller uses Ethernet, and when the link drops it falls back to
  Wi-Fi (and returns to Ethernet when the cable is back). A "Wi-Fi" switch (config entity, restored after reboot)
  turns the Wi-Fi station off for a wired-only setup. This needs **ESPHome 2026.9 or newer**: older versions refuse
  `ethernet:` and `wifi:` in the same firmware.
* **`network_ethernet.yaml` / `network_wifi.yaml`:** a single interface. With ESPHome before 2026.9 this is the only
  option, and switching between wired and Wi-Fi is then a compile-time choice: edit the line and flash again (over
  USB, or OTA from the running build, which comes back on the other interface). The Ethernet-only build has no
  fallback access point, captive portal or Improv.

The IP addresses appear in the "Ethernet IP address" / "WiFi IP address" sensors (single-interface builds: "IP
address"). The Thread/Matter firmware ([../thread-matter](../thread-matter/README.md)) has its own run-time selection
(auto / ethernet / wifi).

## Adapting

Everything in `substitutions` at the top of `heating-controller.yaml`:

- `timezone` - schedules run in local time.
- `zoneN_name` - zone names (they also form the Home Assistant entity IDs).
- `zoneN_room`, `outdoor_sensor` - Home Assistant sensors for room and outdoor temperature.
- `ntc_supply_voltage_a/b` - measure on the finished board (see docs/firmware.md, section 3); `ntc_b_constant` and
  `ntc_nominal` for your probes.
- `oled_rotation` - if the display is upside down.

Per-zone defaults (mode, schedule temperatures, floor type, floor limit) are the `vars` in the `packages:` block;
they only apply at first boot. After that every setting is changed at run time (Home Assistant or the built-in web
UI at `http://<controller IP>/`) and stored in flash.

## Zigbee sensors (optional)

To take room and outdoor temperatures from Zigbee sensors paired to the ESP32-H2 instead of from Home
Assistant:

1. Flash the H2 with [`../zigbee-coordinator`](../zigbee-coordinator/README.md) through its USB-C port "RADIO".
2. Uncomment `coprocessor: !include coprocessor.yaml` under `packages:` in `heating-controller.yaml` and flash the
   controller.
3. Press "Co-processor permit join" and pair the sensors. Their ids appear in "Co-processor devices" and in the log.
4. Set `cp_zone1_device` … `cp_zone8_device` and `cp_outdoor_device` (16 hex digits) in `coprocessor.yaml` or in
   `heating-controller.yaml`'s substitutions, and flash again.

A mapped sensor takes precedence over the zone's Home Assistant sensor (and over the Home Assistant outdoor sensor).
If it has not reported for `cp_timeout` (2 h) it becomes unknown and the Home Assistant value or the fallbacks apply
again. Without the package the firmware behaves exactly as before. The package passes `esphome config` and compiles
with ESPHome 2026.9.1; it has not run on hardware.
