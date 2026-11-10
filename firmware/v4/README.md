# v4 firmware (ESPHome)

ESPHome configuration for the v4 controller. It runs the complete heating control locally; Home Assistant is
optional and only used as dashboard, remote control and source of room/outdoor temperatures. The control logic is
described in [docs/firmware.md](../../docs/firmware.md).

> Not yet tested on hardware. Validated and compiled with ESPHome 2026.6.

## Files

| File | Content |
|---|---|
| `heating-controller.yaml` | Main configuration: buses, OpenTherm, RTC, pumps, display, control loop, flow temperature, schedule, and the 8 zones |
| `zone.yaml` | One zone (relay, LED, TERM input, floor NTC, room temperature, thermostat, settings), included 8 times with `vars` |
| `secrets.yaml.example` | Template for `secrets.yaml` (Wi-Fi, API key, OTA and fallback AP passwords) |

## Build and flash

```sh
cd firmware/v4
cp secrets.yaml.example secrets.yaml     # fill in; API key: openssl rand -base64 32
esphome run heating-controller.yaml
```

First flash over USB-C (the ESP32-C6's built-in USB-serial port): hold the front button (BOOT) while plugging in
if the port does not appear. Later updates over the air. Without known Wi-Fi the controller opens the
"Heating Controller Setup" access point (captive portal); Improv over USB serial also works.

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
