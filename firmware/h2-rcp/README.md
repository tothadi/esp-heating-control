# ESP32-H2 OpenThread RCP ("Thread/Matter mode")

OpenThread radio co-processor firmware for the ESP32-H2-MINI-1-H4S on the logic board. The ESP32-S3 runs the Thread
border router and the Matter controller ([`../thread-matter`](../thread-matter/README.md)) and uses the H2 only as
an 802.15.4 radio over Spinel/HDLC.

Based on the ESP-IDF v5.5.5 example `examples/openthread/ot_rcp` (CC0; `main/` is copied unchanged), configured in
`sdkconfig.defaults`:

| | |
|---|---|
| Host link | UART0: GPIO23 (RXD0) ← S3 IO10, GPIO24 (TXD0) → S3 IO11; **460800 baud** 8N1, no flow control (`main/esp_ot_config.h`; the border router uses the same rate) |
| Console | USB-Serial/JTAG = USB-C "RADIO" (logs are compiled out to keep the RCP small and fast) |
| Reset / download | EN and GPIO9 (BOOT) are driven by S3 IO12 / IO13 (10 kΩ pull-ups) |
| Flash | 4 MB, 1 MB factory app (image ≈ 210 KB) |
| Coexistence (PTA) | off by default; `sdkconfig.coex` enables the 4-wire follower on GPIO2 (REQUEST) / GPIO3 (PRIORITY) / GPIO4 (GRANT) / GPIO5 (TX_LINE) |

The project name is `esp_ot_rcp` on purpose: the RCP update component of the border router looks for
`build/esp_ot_rcp.bin`.

> Status: compiles (ESP-IDF v5.5.5, both variants); not run on hardware.

## Build

Use the **same ESP-IDF version as `thread-matter` (v5.5.5)**: the Spinel/RCP API version of host and RCP must match.

```sh
. $IDF_PATH/export.sh                          # ESP-IDF v5.5.5
cd firmware/h2-rcp
idf.py build                                   # default: no PTA
# PTA variant (only with the thread-matter PTA build, which has no BLE):
idf.py -B build-coex -D SDKCONFIG=build-coex/sdkconfig \
       -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.coex" build
```

## Getting it onto the H2

Normally nothing to do: `thread-matter` embeds `build/` of this project (its `CMakeLists.txt` points
`CONFIG_RCP_SRC_DIR` at it; override with the environment variable `HC_RCP_BUILD_DIR`). At every boot the S3 asks the
RCP for its version and, if it differs from the embedded image (or the RCP does not answer), puts the H2 into download
mode through EN/BOOT and flashes it over the UART at 460800 baud (`esp_rcp_update`, from esp-thread-br).

Manual flashing (first bring-up, or recovery) through USB-C "RADIO":

```sh
idf.py -p /dev/cu.usbmodemXXXX flash
```

## Switching back to Zigbee mode

Flash [`../zigbee-coordinator`](../zigbee-coordinator/README.md) through "RADIO" and ESPHome on the S3. The Zigbee
firmware erases nothing the RCP needs and vice versa, but the Zigbee network has to be formed and paired again.
