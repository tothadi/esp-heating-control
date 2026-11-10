# Zigbee coordinator (ESP32-H2 co-processor)

ESP-IDF firmware for the ESP32-H2-MINI-1 on the logic board. It forms a Zigbee network, lets devices join when the controller asks, and forwards room temperatures, humidity and thermostat values to the ESP32-S3 over the UART link ([protocol](../../docs/coprocessor-protocol.md)). On the S3 side, include [`../controller/coprocessor.yaml`](../controller/coprocessor.yaml).

> **Status: untested on hardware.** It compiles (ESP-IDF v5.5.6, esp-zigbee-lib 2.1.0, target esp32h2, no warnings in this project's code), but it has not run on a board or talked to a real Zigbee device yet.

## What it does

* **Network:** coordinator, forms a network on first boot on channel 25 (fallback 15 / 20 / 25, see `menuconfig`), centralised security with the Zigbee default trust-centre link key, no install codes. The network (PAN id, network key, children) is stored by the stack in the `nvs` partition and survives reboots and power cycles.
* **Joining:** closed except while the S3 asks for it (`PJ`, the "Co-processor permit join" button, 180 s by default).
* **Interview** when a device joins or re-announces itself: match descriptor for Temperature Measurement (0x0402), Relative Humidity (0x0405) and Thermostat (0x0201) server clusters → simple descriptor of each matching endpoint → Basic model id → for each supported cluster: bind it to the coordinator (endpoint 1), configure reporting, read the current value.
* **Reporting configuration** (devices may clamp these to their own limits):

  | Attribute | Min / max interval | Reportable change |
  |---|---|---|
  | 0x0402 MeasuredValue | 30 s / 600 s | 0.1 °C |
  | 0x0405 MeasuredValue | 60 s / 900 s | 1 % |
  | 0x0201 LocalTemperature | 30 s / 600 s | 0.1 °C |
  | 0x0201 OccupiedHeatingSetpoint | 1 s / 3600 s | 0.1 °C |
  | 0x0201 PIHeatingDemand (if present) | 10 s / 900 s | 5 % |

* **Forwarding:** every report or read response for these attributes becomes an `RD` line (value in °C / %). Other attributes are ignored.
* **Device table:** up to 32 devices (IEEE address, short address, capabilities, name, model) in NVS (namespace `cp`); the last values in RAM, replayed to the S3 on `DUMP`.

## Versions

| Component | Version | Why |
|---|---|---|
| ESP-IDF | **v5.5.6** | Recommended by the Espressif Zigbee SDK README for SDK 2.x |
| esp-zigbee-lib | **~2.1.0** (component registry) | Current SDK 2.x release. SDK 2.x uses Espressif's own Zigbee stack; the ZBOSS-based `esp-zboss-lib` belongs to the 1.x line (maintenance only) and is not used. |
| Target | esp32h2 (ESP32-H2-MINI-1-H4S, chip revision v1.2, 4 MB flash; see below) | |

`dependencies.lock` is generated on the first build. The UART protocol and the device table are the project-local component [`components/cp_link`](components/cp_link).

The Zigbee calls follow the SDK's `examples/home_automation_devices/thermostat` (a coordinator that binds a remote temperature sensor and configures its reporting) at commit `f2db5c6` of [espressif/esp-zigbee-sdk](https://github.com/espressif/esp-zigbee-sdk).

### Module and chip revision

The board carries an **ESP32-H2-MINI-1-H4S** (LCSC C47967030): 4 MB in-package flash, -40…105 °C, same pinout as the
earlier MINI-1. The "S" ordering codes replaced -N4/-H4 when Espressif moved the ESP32-H2 from chip revision v0.1 to
**v1.2** ([PCN20250104](https://documentation.espressif.com/en/PCN20250104_Upgrade_of_ESP32-H2_Series_Products.html)).
v1.2 is not software compatible with images built by older ESP-IDF releases: it needs at least v5.1.6 / v5.2.5 /
v5.3.3 / v5.4.1 or **v5.5** ([ESP-IDF COMPATIBILITY.md](https://github.com/espressif/esp-idf/blob/master/COMPATIBILITY.md),
[chip revision identification](https://docs.espressif.com/projects/esp-chip-errata/en/latest/esp32h2/01-chip-identification/index.html)).
The pinned ESP-IDF v5.5.6 supports revisions v0.0 up to v1.99. Leave *Minimum Supported ESP32-H2 Revision*
(`ESP32H2_REV_MIN`) at its default, as Espressif recommends; the image then also runs on older v0.x boards and devkits.
The partition table is sized for the 4 MB flash. `esptool.py chip_id` (or the boot log, `chip revision: v1.2`) shows
the revision; a v1.2 module is marked "MF" in its specification identifier line.

## Build and flash

Install ESP-IDF v5.5.6 ([Get Started](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32h2/get-started/index.html)):

```sh
git clone --branch v5.5.6 --depth 1 --recursive --shallow-submodules https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32h2 && . ./export.sh
```

Build:

```sh
cd firmware/zigbee-coordinator
idf.py set-target esp32h2
idf.py build                       # downloads esp-zigbee-lib into managed_components/
```

Flash through the H2's own USB-C port, **"RADIO"** (J4, next to "MAIN"). It is the H2's built-in USB-Serial/JTAG: it appears as `/dev/cu.usbmodem…` (macOS), `/dev/ttyACM…` (Linux) or a COM port (Windows), and esptool resets the chip into download mode through it, so no button is needed. The first time, erase the flash (it holds the other firmware's network data otherwise):

```sh
idf.py -p /dev/ttyACM0 erase-flash
idf.py -p /dev/ttyACM0 flash monitor     # Ctrl-] leaves the monitor
```

The controller can stay powered from mains while the H2 is flashed; the USB port only needs data. Without ESP-IDF, the built images can be flashed with esptool alone (from `build/`):

```sh
cd build && esptool.py --chip esp32h2 -p /dev/ttyACM0 -b 460800 write_flash @flash_args
```

Logs (USB-Serial/JTAG, 115200) show the network, joins, interviews and every forwarded value. UART0 is reserved for the link to the S3.

If the USB port does not appear (e.g. after a crash loop), the S3 can still hold the H2 in reset: press "Co-processor reset" (it pulses EN). Holding the H2 in download mode from the S3 (IO1 = BOOT low during reset) is not implemented in the ESPHome component.

## Pairing

1. Include [`coprocessor.yaml`](../controller/coprocessor.yaml) in the controller firmware and flash it.
2. Press **Co-processor permit join** (Home Assistant or the controller's web UI). "Co-processor network" shows `joining open` for 180 s.
3. Put the device into pairing mode (usually a long press on its button, or a factory reset). The log shows `Device announce`, the clusters found, `Bound … to the coordinator` and the first values.
4. The device appears in **Co-processor devices** as `<id> <model> <flags>`, and the S3 log prints `Unmapped reading: device <id> …` until it is mapped.
5. Copy the 16-digit id into `cp_zoneN_device` / `cp_outdoor_device` in `coprocessor.yaml` (or override it in `heating-controller.yaml`'s substitutions) and flash the controller again.

Battery devices sleep most of the time; if the interview did not finish (no values after a few minutes), wake the device (press its button) or pair it again. A device that is paired again keeps its id, so the mapping stays valid.

To start over: `RESETNET,CONFIRM` (no button for it on purpose; send it from a test setup) or `idf.py erase-flash`, then pair every device again.

## Supported devices

Devices that implement the standard ZCL clusters as server clusters on an HA-profile endpoint:

* **Temperature / humidity sensors** with Temperature Measurement 0x0402 (and Relative Humidity 0x0405). Most generic Zigbee 3.0 sensors do (e.g. Sonoff SNZB-02 family, many Tuya `TS0201` variants, IKEA, Philips Hue motion sensor's temperature). Not verified with any specific model yet.
* **Thermostats / radiator valves** with the standard Thermostat cluster 0x0201 (LocalTemperature, OccupiedHeatingSetpoint, optionally PIHeatingDemand), e.g. Danfoss Ally, Bosch, Sonoff TRVZB, Eurotronic Spirit Zigbee. Not verified with any specific model yet. Only *reading* is implemented: the controller does not write setpoints to the thermostat.

The type `temperature` in `coprocessor.yaml` accepts both 0x0402 MeasuredValue and a thermostat's LocalTemperature.

## Known limitations

* **Tuya "EF00" devices are not supported.** Many Tuya thermostats and some sensors (model `TS0601`, manufacturer `_TZE…`) do not use the standard clusters but tunnel "data points" through the private cluster 0xEF00. They join, but the interview finds no supported cluster (log: `has no temperature/humidity/thermostat cluster`). Supporting them needs per-model data-point tables (as in zigbee2mqtt's converters); not implemented.
* **Xiaomi / Aqara** sensors use partly non-standard behaviour and are known to drop off non-Xiaomi coordinators without frequent parent polling; untested.
* Only temperature, humidity and thermostat values are read; no switching, no OTA updates, no groups/scenes.
* No network backup: if the H2 flash is erased, every device must be paired again.
* Up to 16 direct children (sleepy end devices joined to the coordinator itself), `menuconfig` → *Zigbee coordinator*. Mains-powered routers extend this.
* The S3's Wi-Fi antenna is a few centimetres from the H2: choose a Zigbee channel away from the Wi-Fi channel (`menuconfig`); the channel can only change by forming a new network (`RESETNET,CONFIRM`).
* Uncertainties in the code: the binding/reporting flow is taken from the SDK example; the PIHeatingDemand reporting record and the thermostat client cluster on the coordinator endpoint have not been exercised; `ezb_bdb_open_network()` is assumed to open the whole network (as `bdb open` does in SDK 1.x).

## Files

| File | Content |
|---|---|
| `main/zb_coordinator.c` | Stack setup, signals, interview, binding, reporting, forwarding, S3 commands (`PJ`, `FORGET`, `RESETNET`) |
| `main/Kconfig.projbuild` | Channel, secondary channels, max children, keep-open option |
| `main/idf_component.yml` | Dependencies (esp-zigbee-lib) |
| `components/cp_link/` | UART line protocol to the S3 and the device table in NVS |
| `partitions.csv` | nvs 64 kB (Zigbee stack + device table), 1.5 MB app |
| `sdkconfig.defaults` | Target, console on USB-Serial/JTAG, Zigbee ZC/ZR, trimmed mbedTLS |
