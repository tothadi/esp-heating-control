# Firmware

The logic board has two chips: the **ESP32-S3** (USB-C "MAIN") runs the heating control, the **ESP32-H2** (USB-C
"RADIO") is the 802.15.4 radio. There are two ways to run them:

| Mode | ESP32-S3 | ESP32-H2 | Room / outdoor sensors | User interface | Network |
|---|---|---|---|---|---|
| **Zigbee mode** (supported path) | [`controller/`](controller/README.md) (ESPHome) | [`zigbee-coordinator/`](zigbee-coordinator/README.md) | Zigbee (optional `coprocessor.yaml`) or Home Assistant | Home Assistant (API), built-in web UI | Ethernet + Wi-Fi, Ethernet preferred while plugged in (ESPHome ≥ 2026.9); single-interface builds optional |
| **Thread/Matter mode** | [`thread-matter/`](thread-matter/README.md) (ESP-IDF + esp-matter) | [`h2-rcp/`](h2-rcp/README.md) (OpenThread RCP, flashed automatically by the S3) | Matter over Thread or Wi-Fi, or MQTT | MQTT (Home Assistant optional), USB console | Ethernet and Wi-Fi, chosen at run time (Ethernet preferred) |

Both modes contain the same heating control (zones, floor protection, OpenTherm boiler control with weather
compensation, schedules, pumps, display) and work without Home Assistant and without any network. Nothing of it has
run on hardware yet.

To switch modes, flash both chips: Zigbee mode = ESPHome on the S3 + `zigbee-coordinator` on the H2 (through
"RADIO"); Thread/Matter mode = `thread-matter` on the S3 (it carries the RCP image and writes it into the H2 by
itself; flashing `h2-rcp` through "RADIO" first also works). Settings are not shared between the two.

## [`controller/`](controller/README.md): ESPHome firmware (Zigbee mode)

ESPHome configuration of the controller. All heating control runs on the device; Home Assistant is only dashboard and
remote control. Network packages: Ethernet + Wi-Fi with Ethernet preferred (default), Ethernet only, Wi-Fi only. See [controller/README.md](controller/README.md) and
[docs/firmware.md](../docs/firmware.md).

## [`zigbee-coordinator/`](zigbee-coordinator/README.md): ESP32-H2 Zigbee coordinator (Zigbee mode)

ESP-IDF firmware (Espressif Zigbee SDK 2.x) for the H2, flashed through its own USB-C ("RADIO"): a Zigbee coordinator
that pairs standard Zigbee temperature/humidity sensors and thermostats, binds and configures their reports and
forwards the values to the S3 over the UART ([docs/coprocessor-protocol.md](../docs/coprocessor-protocol.md)). On the
S3, include [`controller/coprocessor.yaml`](controller/coprocessor.yaml). Compiles with ESP-IDF v5.5.6.

## [`thread-matter/`](thread-matter/README.md): ESP-IDF firmware (Thread/Matter mode)

Custom ESP-IDF firmware for the S3: the heating control ported from the ESPHome configuration, a Thread border router
(OpenThread BR with the H2 as radio co-processor, Wi-Fi or Ethernet backbone), a Matter controller that commissions
Matter temperature sensors and thermostats and maps them to zones, and an MQTT interface. ESP-IDF v5.5.5,
esp-matter release/v1.6.

## [`h2-rcp/`](h2-rcp/README.md): ESP32-H2 OpenThread RCP (Thread/Matter mode)

The ESP-IDF `ot_rcp` example configured for this board (UART0 on GPIO23/GPIO24 at 460800 baud, logs on "RADIO").
Built before `thread-matter`, which embeds it.

## `reference/opentherm_gateway.yaml`: the predecessor

ESPHome configuration of the predecessor ESP32 OpenTherm gateway, kept as a reference: the NTC measurement chain and the OpenTherm setup of the controller come from here. In that installation the zone control ran as Home Assistant automations and the zone relays were Zigbee devices; those automations are not part of this repository (the controller runs the same logic locally).

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
