# Thread/Matter firmware for the ESP32-S3 ("Thread/Matter mode")

Custom ESP-IDF firmware (no ESPHome) for the controller's ESP32-S3-WROOM-1-N16R8:

* **Heating control** - the logic of the ESPHome configuration in [`../controller`](../controller), ported lambda by
  lambda: zones, thermostats, floor tempering and overheat lockout, boiler CH demand, OpenTherm master with the
  weather-compensated flow temperature, schedules, pumps with weekly exercise, floor NTC chain, LEDs, OLED, button.
  It starts first and runs with or without any network.
* **Thread border router** - ESP-IDF's OpenThread border router (the implementation esp-thread-br uses) with the
  ESP32-H2 as radio co-processor over UART, Ethernet or Wi-Fi backbone, automatic RCP update from an image built into
  this firmware.
* **Matter controller** - esp-matter controller/commissioner: commissions Matter-over-Thread and Matter-over-Wi-Fi
  temperature sensors and thermostats over BLE, subscribes to their temperature / humidity, and maps them to zone
  room temperatures and the outdoor temperature (mapping changeable at run time, stored in NVS).
* **MQTT** settings/state interface; Home Assistant is optional.
* **Network selection at run time**: W5500 Ethernet and Wi-Fi, Ethernet preferred while its link is up.

The H2 runs [`../h2-rcp`](../h2-rcp/README.md). For the ESPHome + Zigbee alternative see [`../README.md`](../README.md).

> **Status: the complete image (heating + border router + Matter controller + MQTT) compiles and links; nothing of
> it has run on hardware.** See "Untested / known gaps" below before relying on it.

## Versions (pinned)

| Component | Version | Why |
|---|---|---|
| ESP-IDF | **v5.5.5** | the version esp-matter release/v1.6 recommends |
| esp-matter | **release/v1.6 @ `9f2e35ac3b3420fe81ee6834e72ec3db7bdba6a6`** (2026-11-09) | latest esp-matter release branch for IDF v5.5 (release/v1.6.1 moved to IDF v6.0) |
| connectedhomeip | `75902a0c6f6b208d5db963a8082423465384c594` | submodule of that esp-matter commit |
| esp-thread-br | **v1.3** (based on IDF v5.5.2) | the BR stack itself is ESP-IDF's `openthread` component; from esp-thread-br this firmware uses the `espressif/esp_rcp_update` component **1.6.1** (`main/idf_component.yml`) |
| gn | `git_revision:97b68a0bb62b7528bc3491c7949d6804223c2b82` | the version connectedhomeip's pigweed pins |
| Other managed components | espressif/mdns 1.15.1, esp-serial-flasher 1.11.0, cjson 1.7.19~2, esp_secure_cert_mgr 2.9.3, ... | exact set in `dependencies.lock` (committed) |

The H2 RCP must be built with the same ESP-IDF (v5.5.5). The Zigbee coordinator stays on v5.5.6.

## Layout

| Path | Content |
|---|---|
| `main/app_main.cpp` | start order, USB console command `hc` |
| `main/hc_thread.cpp` | border router: RCP UART config, RCP auto-update, PTA, network formation, backbone switching |
| `main/hc_matter.cpp` | Matter controller: commissioning, subscriptions, device registry and mapping (NVS) |
| `main/hc_mqtt.cpp` | MQTT interface and the command dispatcher shared with the console |
| `components/heating/` | the heating control: `hc_zones.c` (zones, thermostat, boiler CH, pumps), `hc_flow.c` (flow temperature), `hc_schedule.c` (schedule, pump exercise), `hc_ntc.c` (floor probes), `hc_ui.c` (LED, OLED, button), `hc_core.c` (task, I/O, time), `hc_settings.c` / `hc_json.c` (settings, NVS, JSON) |
| `components/opentherm/` | OpenTherm master: RMT transmitter, edge-interrupt receiver, polling |
| `components/hc_drivers/` | I²C bus, MCP23017, ADS1115, DS3231, SSD1306 + font, board pin map (`hc_board.h`) |
| `components/hc_net/` | Ethernet (W5500) + Wi-Fi selection |
| `tools/gen_font.py` | generates the OLED font (Roboto Condensed 14 px, as in the ESPHome build) |
| `sdkconfig.defaults` | default build (BLE commissioning, no PTA) |
| `sdkconfig.pta` | overlay: PTA coexistence with the H2, without BLE |
| `partitions.csv` | 16 MB: NVS 256 KB, 2 × 6 MB app slots, 640 KB `rcp_fw` |

## Build

Prerequisites (all paths are examples):

```sh
# ESP-IDF v5.5.5 with the esp32s3 and esp32h2 tools
git clone -b v5.5.5 --recursive https://github.com/espressif/esp-idf.git
./esp-idf/install.sh esp32s3,esp32h2
. ./esp-idf/export.sh
# esp-matter release/v1.6 and the connectedhomeip submodules needed for ESP32
git clone -b release/v1.6 https://github.com/espressif/esp-matter.git
cd esp-matter && git checkout 9f2e35ac3b3420fe81ee6834e72ec3db7bdba6a6
git submodule update --init --depth 1
(cd connectedhomeip/connectedhomeip && ./scripts/checkout_submodules.py --platform esp32 --shallow)
./install.sh --no-host-tool      # or, without the pigweed bootstrap, see below
export ESP_MATTER_PATH=$PWD
```

Without running connectedhomeip's bootstrap (it downloads a large pigweed environment), the build needs only:
`gn` (revision above) on `PATH`, `pip install lark==1.1.2 stringcase==1.2.0 jinja2==3.1.6` in the ESP-IDF Python
environment, and a file `connectedhomeip/connectedhomeip/build_overrides/pigweed_environment.gni` containing
`declare_args() {}` (normally generated by the bootstrap). This is how it was built for this repository.

Build notes from that environment: the project adds `-Wformat=0` (as esp-matter's controller example does), because
connectedhomeip `75902a0c6` otherwise fails with `-Werror=format` on the Xtensa toolchain. If the build tree sits
below a symlinked path (macOS `/tmp` -> `/private/tmp`), connectedhomeip's `create_args_gn.py` does not recognise the
ESP-IDF include paths and passes them as `-I` instead of `-isystem`, and NimBLE headers then fail with
`-Werror=conversion`; keep ESP-IDF and the build directory on a real path (or patch the path match in
`config/esp32/components/chip/create_args_gn.py`, as was done here).

Then:

```sh
cd firmware/h2-rcp && idf.py build                 # 1. the RCP image (embedded in step 2)
cd ../thread-matter && idf.py build                # 2. this firmware
idf.py -p /dev/cu.usbmodemXXXX flash monitor       # USB-C "MAIN"; hold the button (BOOT) if the port is missing
```

`CMakeLists.txt` takes the RCP image from `../h2-rcp/build` (or `$HC_RCP_BUILD_DIR`) and packs it into the `rcp_fw`
partition, which `idf.py flash` writes too.

PTA variant (see "Coexistence"): build `h2-rcp` with `sdkconfig.coex`, point `HC_RCP_BUILD_DIR` at that build, and
build this project with `-D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.pta"`.

## First start

1. Flash over USB-C "MAIN". The heating control starts immediately with the defaults of the ESPHome build
   (zone names, temperatures, curve, schedule).
2. Console (USB-C "MAIN", 115200; commands are prefixed with `matter esp`):
   ```
   matter esp hc wifi <ssid> <password>       # optional with Ethernet; needed for Matter-over-Wi-Fi devices
   matter esp hc net auto|ethernet|wifi       # default auto
   matter esp hc mqtt mqtt://192.168.1.10 [user] [password] [base_topic]
   matter esp hc status                       # state, network, Thread, Matter devices, MQTT
   matter esp hc send zone/3/set {"target":21.5}
   ```
3. On first start the S3 compares the RCP firmware of the H2 with the embedded image and flashes the H2 if needed
   (EN = IO12, BOOT = IO13, UART IO10/IO11), then forms a Thread network ("HeatingCtl", random channel, PAN and keys,
   stored in NVS) and starts the border router as soon as Ethernet or Wi-Fi has an address.

## Network selection

* **auto** (default): the W5500 and the Wi-Fi station both run. The Ethernet netif has route priority 200, Wi-Fi 100,
  and the firmware sets the default netif explicitly on every change: with the cable plugged in (link up and an IPv4
  address) Ethernet carries the default route, MQTT, SNTP, the Thread border-router backbone and Matter's IP traffic;
  when the link drops, Wi-Fi takes over, and Ethernet again when it comes back.
* **ethernet**: Wi-Fi station disabled (BLE still works).
* **wifi**: Ethernet stopped.

The mode is stored in NVS and set with `matter esp hc net …` or MQTT `<base>/net/set` `{"mode":"auto"}`; Wi-Fi
credentials with `{"wifi_ssid":"…","wifi_password":"…"}`. The Wi-Fi station itself is driven by the Matter stack
(reconnects with back-off). When the active interface changes, the border router is re-initialised on the new
backbone (`esp_openthread_border_router_deinit()` / `_init()`); Thread devices keep talking to the controller during
the switch because the controller *is* the Thread leader/border router (no backbone needed for that).

## MQTT interface

Why MQTT and not a web UI: it needs only a broker (any broker; Home Assistant's MQTT integration, Node-RED, a phone
app or `mosquitto_pub` can all drive it), it is small next to the border router and Matter stack, and it gives both
state and settings in one place. Home Assistant is not required; without a broker the controller runs on its stored
settings and the USB console.

| Topic (`<base>` = `heating-controller`) | Direction | Content |
|---|---|---|
| `<base>/status` | out, retained | `online` / `offline` (last will) |
| `<base>/state` | out, 10 s | outdoor temperature, calculated flow, flow setpoint, sustained-deficit boost, preheat, CH demand, pumps, boiler values, network, Thread state |
| `<base>/zone/<n>/state` | out, 10 s | name, mode, action, target, room / floor temperature, floor target, valve, wired input, lockout, state text, room source |
| `<base>/settings` | out, retained | all settings |
| `<base>/net/state` | out, retained | mode, active interface, link, addresses |
| `<base>/matter/devices` | out, retained | commissioned devices, last readings per endpoint, mappings |
| `<base>/matter/event` | out | commissioning / subscription events |
| `<base>/result` | out | `ok …` / `error …` for every command |
| `<base>/set` | in | JSON with global settings (names below) and `outdoor_temperature` |
| `<base>/zone/<n>/set` | in | JSON with zone settings and `room_temperature` |
| `<base>/net/set` | in | `{"mode": "auto"\|"ethernet"\|"wifi", "wifi_ssid": …, "wifi_password": …}` |
| `<base>/matter/cmd` | in | Matter commands (below) |

Global settings (ranges as in the ESPHome build): `heating_mode` (auto/off), `flow_mode` (auto/manual),
`pump1_mode`, `pump2_mode` (auto/on/off), `hot_water`, `curve_ref`, `curve_slope`, `curve_offset`, `curve_min`,
`curve_max`, `flow_max`, `boost_max`, `flow_manual`, `floor_offset_tiles`, `floor_offset_laminate`,
`night_floor_target`, `pump_overrun`, `day_start`, `night_start`, `boost_start`, `boost_end` ("HH:MM"),
`ntc_supply_a`, `ntc_supply_b`, `ntc_ref_resistor`, `ntc_nominal`, `ntc_b_constant`, `ntc_open_v`, `ntc_short_v`,
`fallback_outdoor_temp`, `room_timeout`, `sensor_timeout` (s), `oled_rotate180`, `timezone` (POSIX TZ, default
Europe/Budapest `CET-1CEST,M3.5.0,M10.5.0/3`).

Zone settings: `name`, `mode` (heat/off), `target`, `comfort`, `night`, `boost`, `night_setback`, `evening_boost`,
`source` (room/wired/floor), `floor_type` (tiles/laminate/none), `floor_limit`, `floor_calibration`.

External temperatures (`room_temperature`, `outdoor_temperature`, e.g. published by a Home Assistant automation) play
the role of the Home Assistant sensors of the ESPHome build: used while the broker connection is up (dropped
`room_timeout`, 300 s, after it went down) and for at most `sensor_timeout` (2 h) after the last update.

## Matter devices

Commission (`<base>/matter/cmd`, or `matter esp hc send matter/cmd '{…}'`):

```json
{"cmd": "commission", "code": "MT:Y.K90AFN00KA0648G00", "transport": "thread", "label": "Living room"}
```

`code` is the QR payload or the 11/21-digit manual code; `transport` is `thread` (BLE, with this border router's
dataset), `wifi` (BLE, with the controller's Wi-Fi credentials) or `network` (device already on IP, e.g. through
another ecosystem's open commissioning window). Node ids are assigned from 0x10 upwards and shown in the event and
the device list. Then map:

```json
{"cmd": "map", "node": 16, "target": "zone1"}
{"cmd": "map", "node": 17, "endpoint": 2, "target": "outdoor"}
{"cmd": "map", "node": 16, "target": "none"}
```

`target`: `zone1`…`zone8`, a zone name, `outdoor`, or `none`. Without `endpoint` any endpoint of the node matches.
Other commands: `remove` (unpair: removes the controller's fabric from the device), `forget` (drop locally only),
`label`, `resubscribe`.

The controller subscribes to Temperature Measurement / MeasuredValue (0x0402), Relative Humidity /
MeasuredValue (0x0405) and Thermostat / LocalTemperature (0x0201) on all endpoints (min 10 s, max 600 s). A
(node, endpoint) that reports Temperature Measurement uses it, otherwise the thermostat's LocalTemperature. A mapped
Matter value has priority over MQTT values and the boiler's outdoor sensor, and is dropped after `sensor_timeout`
(2 h) without a report. Humidity is published only. Device attestation uses the CSA DCL (`DCL_ATTESTATION_TRUST_STORE`,
so commissioning production devices needs Internet access); test devices are accepted.

## Coexistence (PTA)

The S3's Wi-Fi and the H2's 802.15.4 share 2.4 GHz a few centimetres apart. The board has four PTA lines for
Espressif's external coexistence, 4-wire mode, S3 = leader, H2 = follower:

| Signal | S3 | H2 | Direction |
|---|---|---|---|
| REQUEST | IO14 | GPIO2 | H2 → S3 |
| PRIORITY | IO21 | GPIO3 | H2 → S3 |
| GRANT | IO47 | GPIO4 | S3 → H2 |
| TX_LINE | IO48 | GPIO5 | S3 → H2 |

**ESP-IDF v5.5 does not allow the external-coexistence leader on the ESP32-S3 together with the Bluetooth
controller** (`ESP_COEX_EXTERNAL_COEXIST_ENABLE` depends on `!BT_CONTROLLER_ENABLED || SOC_EXTERNAL_COEX_ADVANCE`, and
the S3 lacks the latter). BLE is required to commission Thread devices, so:

* **default build**: BLE on, PTA off on both chips (the H2 image is built without coexistence). Wi-Fi and Thread
  rely on time sharing by their own CSMA; with Ethernet as backbone and Wi-Fi off (`net ethernet`) the problem does
  not arise. Choose the Thread channel away from the Wi-Fi channel (the network is formed on a random channel; change
  it with the `matter esp ot_cli dataset …` OpenThread CLI if needed).
* **`sdkconfig.pta` build**: PTA on, BLE off - commissioning only with `transport: network`. Requires the H2 image
  built with `../h2-rcp/sdkconfig.coex`.

## Size (default build, ESP-IDF v5.5.5, `idf.py size`)

Everything in one image - heating control + OpenThread border router + Matter controller/commissioner + BLE + Wi-Fi
+ W5500 Ethernet + MQTT - builds and links:

| | Used | Available |
|---|---|---|
| App image `heating-thread-matter.bin` | 2.66 MB (0x2A8160) | 6 MB slot (56 % free) |
| Internal RAM, static (DIRAM: .text 116.6 KB, .data 26.2 KB, .bss 18.3 KB) | 161.2 KB | 341.8 KB (47 % used) |
| BSS moved to PSRAM (CHIP, esp-matter, OpenThread, main) | 86.5 KB | 8 MB PSRAM |
| RCP image in `rcp_fw` (SPIFFS) | 225 KB | 640 KB |
| H2 RCP image (`esp_ot_rcp.bin`) | 210 KB | 1 MB factory partition |
| PTA variant (`sdkconfig.pta`, no BLE) app image | 2.43 MB (0x26E430) | 6 MB slot; RCP with `sdkconfig.coex`: 215 KB |

Run-time heap usage (Matter sessions, Thread, mDNS, TLS for DCL) is not measured; Espressif runs the same stack
combination (esp-matter controller example with OTBR) on an ESP32-S3 with PSRAM, and heaps here are steered to PSRAM.

## Untested / known gaps

* Nothing has run on hardware: OpenTherm timing and polarity, I²C devices, W5500, RCP update over IO12/IO13, PTA,
  Thread formation, BLE commissioning and subscriptions are all compile-tested only.
* OpenTherm: own implementation (RMT transmitter, edge-timed receiver following the ihormelnyk / ESPHome decoder);
  one frame every 250 ms (status every 500 ms). Unsupported data IDs are dropped after the first "unknown data ID".
* Backbone switching re-initialises the border router (`deinit` / `init`), which ESP-IDF provides but which is not
  part of Espressif's examples; if it misbehaves, a reboot after a change of the active interface is the fallback.
* The Matter controller keeps up to 24 devices and 16 mappings; attestation via DCL needs Internet during
  commissioning.
* No OTA update path is implemented yet (the partition table has two app slots for it).
* No Home Assistant MQTT discovery (entities have to be configured by hand in Home Assistant if wanted).
* The Thread network credentials are not exported (other border routers cannot join this network yet except via the
  OpenThread CLI on the console: `matter esp ot_cli dataset active -x`).
