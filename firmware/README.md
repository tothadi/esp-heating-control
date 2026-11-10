# Firmware

## [`v4/`](v4/README.md): the controller firmware

ESPHome configuration of the v4 controller. All heating control runs on the device; Home Assistant is only dashboard and remote control. See [v4/README.md](v4/README.md) and [docs/firmware.md](../docs/firmware.md).

## `reference/opentherm_gateway.yaml`: the predecessor

ESPHome configuration of the predecessor ESP32 OpenTherm gateway, kept as a reference: the NTC measurement chain and the OpenTherm setup of v4 come from here. In that installation the zone control ran as Home Assistant automations and the zone relays were Zigbee devices; those automations are not part of this repository (v4 runs the same logic locally).

### Hardware

- ESP32 D1 Mini (`wemos_d1_mini32`) + DIYLess OpenTherm Master Shield (boiler side). OpenTherm in/out: GPIO21 / GPIO22 - verify against your shield before flashing.
- 2x ADS1115 on I2C (GPIO17 SDA / GPIO16 SCL), addresses 0x48 and 0x49, for up to 8 floor NTC probes (10k, B 3950). Divider per probe: 3V3 - 10 kOhm 0.1 % - ADS input - NTC - GND, 100 nF from the input to GND.
- DS18B20 cabinet temperature sensor on GPIO18 (part of the DIYLess kit).

### What it does

- **OpenTherm**: writable flow setpoint (`t_set`, 25-45 °C), CH enable, DHW enable, boiler flow/return/DHW temperatures, modulation, water pressure, fault code, flame/CH/DHW/fault states.
- **Floor probes**: per channel 1 s sampling, median of 60 raw voltages (removes 50 Hz pickup on long cables and bad I2C reads), open/short detection on the voltage (-> `unknown`), resistance -> NTC temperature, plausibility 0-60 °C, 5-minute moving average. Unconnected channels read `unknown`.
- **HA failsafe**: if the Home Assistant API is disconnected for 10 minutes the flow setpoint drops to 30 °C (`failsafe_flow_temp`). CH demand stays as it was, so floor tempering continues.
- **Local floor protection**: only when HA has been unreachable for at least 2 minutes. If any probe reaches its local limit (HA limit + 2 °C), CH demand is turned off for the whole house (the gateway cannot reach the Zigbee zone relays). Released when all probes are 2 °C below their limits or HA is back. While HA runs, the per-zone HA floor protection handles it.

### Entities created (device name `opentherm`)

`number.opentherm_boiler_flow_setpoint`, `switch.opentherm_ch_enable`, `switch.opentherm_dhw_enable`, `sensor.opentherm_boiler_flow_temperature`, `sensor.opentherm_boiler_return_temperature`, `sensor.opentherm_dhw_temperature`, `sensor.opentherm_relative_modulation_level`, `sensor.opentherm_ch_water_pressure`, `sensor.opentherm_oem_fault_code`, `sensor.opentherm_boiler_max_capacity`, `sensor.opentherm_floor_temperature_<zone>` (`bedroom`, `kids_room_1`, `kids_room_2`, `bathroom`, `hallway`, `living_room`, `attic_1`, `attic_2`), `sensor.opentherm_cabinet_temperature`, `sensor.opentherm_wifi_signal`, `sensor.opentherm_uptime`, `binary_sensor.opentherm_flame`, `binary_sensor.opentherm_ch_active`, `binary_sensor.opentherm_dhw_active`, `binary_sensor.opentherm_fault`, `binary_sensor.opentherm_status`, `binary_sensor.opentherm_floor_overheat_lockout`.

### Build and flash

```sh
cd firmware/reference
cp secrets.yaml.example secrets.yaml   # fill in Wi-Fi, API key (openssl rand -base64 32), AP password
esphome run opentherm_gateway.yaml
```

Requires an ESPHome version with the native `opentherm` component and OTA encryption via the API key (2026.9+; on older versions add an OTA `password:` instead of `encryption: {}`). A static IP can be added under `wifi:` (commented example in the file).

### Adapting

- **Zones / probes**: the channel-to-zone mapping is in the sensor blocks and in the `floor_protection` interval lambda (arrays `t[]` and `limit[]`). Keep the zone names in line with the Home Assistant packages (entity IDs derive from the sensor names).
- **Calibration**: measure the open-circuit voltage of an unconnected channel on each ADS and set `ntc_supply_voltage_a/b`; fine-tune each probe with its `offset` filter against a reference thermometer.
- **Limits**: `floor_limit_*` are defaults for the reference floor build-up (tiles 30 °C, probe under laminate 36 °C, a probe above a supply pipe 38 °C). Keep them 2 °C above the HA limits.
- **Without ADS1115s**: remove `i2c`, `ads1115`, the floor sensor blocks, the `Floor overheat lockout` binary sensor and the `floor_protection` interval.
- Not every boiler supports every OpenTherm data ID; remove sensors that only log "unsupported" warnings.
