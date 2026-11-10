# Co-processor protocol (ESP32-S3 ↔ ESP32-H2)

The ESP32-S3 (ESPHome, all heating control) and the ESP32-H2 Zigbee co-processor talk over one UART with a simple line protocol. H2 side: the Zigbee coordinator firmware [`firmware/zigbee-coordinator`](../firmware/zigbee-coordinator), protocol code in its component [`components/cp_link`](../firmware/zigbee-coordinator/components/cp_link). S3 side: [`firmware/controller/coprocessor.yaml`](../firmware/controller/coprocessor.yaml) with the ESPHome component `h2_coprocessor`.

> Status: implemented on both sides; the H2 firmware and the ESPHome package compile; nothing has run on hardware yet.

## 1. Physical layer

| | |
|---|---|
| Wiring | S3 IO10 (TX) → H2 GPIO23 (RXD0); S3 IO11 (RX) ← H2 GPIO24 (TXD0) |
| Format | 115200 baud, 8 data bits, no parity, 1 stop bit, no flow control |
| H2 reset | S3 IO12 → H2 EN (10 kΩ pull-up, 1 µF). The S3 keeps IO12 an input and only pulls it low for 100 ms to reset the H2. |
| H2 BOOT | S3 IO13 → H2 GPIO9 (10 kΩ pull-up). Not used by the protocol; the S3 leaves it an input. |

UART0 is also the H2's ROM console: after every reset the ROM prints its boot banner there, at 115200 baud. The H2 firmware sends its own logs to the USB-Serial/JTAG port (the "RADIO" USB-C), never to UART0. The receivers ignore everything that is not a valid protocol line (section 2), so the boot banner is harmless.

## 2. Line format

```
$CP,<TYPE>[,<field>...]*<XX>\r\n
```

* Every line starts with `$CP,` and ends with `*`, two hexadecimal checksum digits and CR LF. Receivers accept LF or CR alone as the line end.
* `XX` is the XOR of all bytes **between** `$` and `*` (exclusive), as two hex digits. Senders use uppercase; receivers accept both cases. Example: `$CP,PING,1*32`.
* A receiver takes the text from the **last** `$` on a line, so garbage before it (ROM output, a partial line after a reset) is skipped. Lines without `$CP,`, without a well-formed `*XX` tail, or with a wrong checksum are dropped. The H2 answers a wrong checksum with `ERR,?,1,checksum`; the S3 only logs it.
* Maximum line length: 192 bytes including `$` and the checksum. Longer lines are dropped whole.
* Fields are separated by `,`. Fields never contain `,`, `*`, `$`, CR or LF. Names and model strings are limited to letters, digits and ` _.-+/():`; the H2 replaces anything else with `_`. Empty fields are allowed.
* Numbers are decimal unless noted. Values use `.` as the decimal separator.

### Device ids and addresses

* **Device id:** the device's Zigbee IEEE (EUI-64) address as 16 lowercase hex digits, most significant byte first.
* **Address:** the 16-bit Zigbee network (short) address, 4 hex digits. `ffff` = unknown.

### Clusters and attributes

Readings carry a cluster id and an attribute id (4 hex digits each) from the Zigbee Cluster Library. Values are already converted to physical units:

| Cluster | Attribute | Meaning | Unit in `RD` | Raw ZCL encoding |
|---|---|---|---|---|
| `0402` Temperature Measurement | `0000` MeasuredValue | temperature | °C | int16, 0.01 °C |
| `0405` Relative Humidity | `0000` MeasuredValue | relative humidity | % | uint16, 0.01 % |
| `0201` Thermostat | `0000` LocalTemperature | room temperature at the thermostat | °C | int16, 0.01 °C |
| `0201` Thermostat | `0012` OccupiedHeatingSetpoint | setpoint | °C | int16, 0.01 °C |
| `0201` Thermostat | `0008` PIHeatingDemand | heating demand (optional attribute) | % | uint8 |

The ZCL "invalid" values (0x8000 for int16, 0xffff for humidity) are never sent.

## 3. Messages from the H2 to the S3

| Type | Fields | When |
|---|---|---|
| `HELLO` | `<fw>,<version>,<proto>` | After every H2 boot. `fw` = `zigbee`, `proto` = protocol version (currently `1`). |
| `PONG` | `<token>,<fw>,<version>,<proto>,<uptime_s>` | Answer to `PING`. `token` is echoed. |
| `NET` | `<state>,<channel>,<pan>,<permit_s>` | On every change and as the answer to `NET` and `PJ`. `state` = `down`, `forming` or `up`; `pan` = PAN id, 4 hex digits; `permit_s` = seconds joining stays open (0 = closed). |
| `JOIN` | `<id>,<addr>,<new>` | A device joined or re-announced itself (`new` = 1 if it was not in the table). |
| `LEAVE` | `<id>` | A device left the network. |
| `DEV` | `<id>,<addr>,<flags>,<age_s>,<name>,<model>` | One per device, answer to `LIST`. `flags`: letters `T` (temperature), `H` (humidity), `S` (thermostat), or `-`. `age_s`: seconds since the device was last heard, `-1` = not since the H2 booted. |
| `DEVEND` | `<count>` | Ends a `LIST` answer. |
| `RD` | `<id>,<ep>,<cluster>,<attr>,<value>,<age_s>` | A reading. `ep` = source endpoint (decimal). `age_s` = seconds since the H2 received it: 0 for live readings, more in a `DUMP`. Example: `$CP,RD,00124b0012345678,1,0402,0000,21.37,0*XX`. |
| `DUMPEND` | `<count>` | Ends a `DUMP` answer. |
| `OK` | `<cmd>` | Command accepted. |
| `ERR` | `<cmd>,<code>,<text>` | Command failed or line rejected (`cmd` = `?` for a checksum error, `DEV` for "table full"). |

The H2 has no clock: timestamps are relative (`age_s`, `uptime_s`). The S3 timestamps a reading when it receives it, minus `age_s`.

### Error codes

| Code | Meaning |
|---|---|
| 1 | Checksum error |
| 2 | Unknown command |
| 3 | Bad arguments |
| 4 | Unknown device |
| 5 | Not ready (network not formed) |
| 6 | Device table full (default 32 devices) |
| 7 | Not supported by this firmware |
| 8 | Internal error |

## 4. Commands from the S3 to the H2

| Command | Answer | Effect |
|---|---|---|
| `PING,<token>` | `PONG` | Keep-alive and version query. The S3 sends one every 60 s and calls the link offline after 3 minutes of silence. |
| `NET` | `NET` | Network status. |
| `PJ,<seconds>` | `OK` + `NET`, or `ERR` | Open joining (permit join on the whole network) for 1–254 s, or close it with 0. |
| `LIST` | `DEV`… `DEVEND` | Device table. |
| `DUMP` | `RD`… `DUMPEND` | The last value of every reading the H2 still holds (since its boot), with its age. The S3 sends it after it starts, so it does not wait for the next report. |
| `NAME,<id>,<name>` | `OK` / `ERR` | Store a name (1–20 characters: letters, digits, space, `_.-`) for a device, kept in the H2's NVS. Informational only: the S3 maps devices by id. |
| `FORGET,<id>` | `OK` / `ERR` | Remove a device from the table. A leave request (without rejoin) is sent to it first. A device that reports again reappears. |
| `RESETNET,CONFIRM` | `OK`, then the H2 reboots | Clear the device table and the network credentials and form a new network. All devices must be paired again. |

Commands are answered in order; the S3 does not wait for answers before sending the next command. The H2 stores the device table (id, address, flags, name, model) in NVS; readings are kept in RAM only.

## 5. Typical session

```
H2: (ROM boot banner, ignored)
H2: $CP,HELLO,zigbee,0.1.0,1*XX
S3: $CP,NET*XX
H2: $CP,NET,up,25,1a2b,0*XX
S3: $CP,LIST*XX
H2: $CP,DEV,00124b0012345678,3f21,TH,12,,TS0201*XX
H2: $CP,DEVEND,1*XX
S3: $CP,PJ,180*XX                       (user pressed "Co-processor permit join")
H2: $CP,OK,PJ*XX
H2: $CP,NET,up,25,1a2b,180*XX
H2: $CP,JOIN,0c4314fffe8a1b2c,9e01,1*XX
H2: $CP,RD,0c4314fffe8a1b2c,1,0201,0000,21.50,0*XX
H2: $CP,RD,0c4314fffe8a1b2c,1,0201,0012,22.00,0*XX
```

## 6. Testing without the other side

On the bench (an ESP32-H2 devkit flashed with the Zigbee firmware, a USB-UART adapter on GPIO23/GPIO24), any serial terminal at 115200 8N1 can stand in for the S3. On the controller, the S3 logs every line it sends at DEBUG level and every received line at VERBOSE level (`logger: logs: h2_coprocessor: VERBOSE`). A checksum can be computed in Python:

```python
from functools import reduce
body = "CP,PJ,60"
print("$%s*%02X" % (body, reduce(lambda a, c: a ^ ord(c), body, 0)))
```

## 7. Versioning

`proto` in `HELLO`/`PONG` is incremented on incompatible changes. New message types or extra trailing fields are not incompatible: receivers ignore unknown types and extra fields.
