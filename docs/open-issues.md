# Open issues

To check or decide before or during the first build.

## Enclosure

* **Enclosure depth and step dimensions:** measure them on an existing modular device.
* **JLC3DP threaded inserts:** their guidelines ask for at least 14 × 14 mm of flat area around an insert, but all 8 insert bosses (H1–H4 for the cover screws, LB1–LB4 for the logic board) end in only Ø9.8 mm. Confirm with their support, with the drawing, before ordering.
* **Labels:** check the legibility of the smallest labels (S, G, L, N: ≈ 0.51 mm stroke) and how well the paint fills them on the first print, including the LAN / S=SIG / G=GND column next to the RJ45 opening.
* **DIN hook and spring latch:** test print on a real rail. The stiffness of the spring arms and the 0.3 mm gaps depend on the printer.
* **LED light rods:** check the fit of the Ø2.8 mm rods in the Ø3.1 mm light pipes (play, whether they stay in without glue) and their brightness compared with empty pipes.
* **OLED module:** components on the back of the module may collide with the spacer frame (module backs differ between vendors).
* **Temperature inside the enclosure:** the ESP32-S3-WROOM-1-N16R8 is rated to 65 °C, and the cover resin (CBY) softens at 72–80 °C (HDT). Measure the internal temperature and the cover next to the relays and the HLK-5M05 at the installation site, with all relays on.

## Boards

* **OpenTherm interface:** prototype test on a real boiler (calculated levels: high 16.4–17.5 V, low ≈ 2.9 V, receive threshold ≈ 12.8 mA).
* **Ethernet (W5500):** untested.
  * **Link:** check at 100 Mbit/s and with traffic. The two pairs run about 15 mm on the board without impedance control.
  * **SPI over the FFC:** the 20 MHz SPI runs over the 80 mm FFC (with GND on pins 19 and 24). If there are transfer errors, lower `clock_speed` in the firmware.
  * **Cable:** the W5500 has no auto-MDIX, so use a straight-through cable to a switch or router.
* **3.3 V supply on the logic board:** the AMS1117 now carries the ESP32-S3, the ESP32-H2 and the W5500 (about 0.4 W average dissipation). Measure its temperature and the 3.3 V rail during Wi-Fi transmit bursts with Ethernet active.
* **CR1220 holder polarity:** check with a multimeter on the first board.
* **FFC cable type:** do a paper-strip test before the first assembly (type A is required).
* **NTC filter capacitors:** with 10 µF on each input, the reading needs about 5 τ ≈ 250 ms to settle after power-up. This is irrelevant with per-second sampling, but avoid fast input multiplexing on the same channel.

## Firmware

* **Controller firmware** ([firmware/controller](../firmware/controller)): written and compiled, untested on hardware. Check on the prototype:
  * OpenTherm RX/TX polarity, OLED orientation, relay and LED mapping, TERM input order, NTC supply voltages.
  * **Network switching:** Ethernet preferred while the cable is plugged in, fallback to Wi-Fi and back (unplug and replug during operation), and the Wi-Fi switch.
  * **Clock:** the DS3231 OSF check after a power cut without the cell, and setting the time from SNTP and from Home Assistant.
* **Zigbee on the controller (ESP32-H2 co-processor):** the Zigbee coordinator firmware ([firmware/zigbee-coordinator](../firmware/zigbee-coordinator)) compiles but is **untested on hardware**. It binds standard ZCL temperature, humidity and thermostat reports and passes the readings to the S3 over the UART ([coprocessor-protocol.md](coprocessor-protocol.md)). The optional ESPHome package [coprocessor.yaml](../firmware/controller/coprocessor.yaml) maps them to zone room temperatures and the outdoor temperature.
  * To check on the prototype: the UART link and the H2 reset via IO12, H2 flashing through the "RADIO" USB-C, pairing and reporting with real sensors and thermostats, and Wi-Fi/802.15.4 coexistence of the two antennas.
  * Tuya thermostats with private data points (cluster 0xEF00) are not supported.
  * Matter is a separate firmware choice (next item): the controller runs either the ESPHome/Zigbee firmware or the Thread/Matter firmware, not both.
* **Thread/Matter firmware** ([firmware/thread-matter](../firmware/thread-matter), [firmware/h2-rcp](../firmware/h2-rcp)): compiles, **untested on hardware**. To verify:
  * the own OpenTherm implementation against a real boiler;
  * RCP auto-update through IO12/IO13;
  * Thread formation and border routing on Ethernet and Wi-Fi, including the backbone switch;
  * BLE commissioning of Thread and Wi-Fi sensors;
  * memory headroom at run time.
* **Wi-Fi / Thread coexistence:** ESP-IDF v5.5 allows the external-coexistence (PTA) leader on the ESP32-S3 only without Bluetooth. The default Thread/Matter build (BLE commissioning) therefore leaves the four PTA lines unused; only the `sdkconfig.pta` variant (no BLE) uses them. Measure Thread reliability next to busy Wi-Fi, and prefer Ethernet as the backbone.

## Before any distribution

* **CE conformity** (Low Voltage Directive, EMC, Radio Equipment Directive, EN 18031).
* **Flame-rated housing:** none of the JLC3DP print materials is flame-rated (UL94). A product needs a V-0, glow-wire tested housing (IEC 60730-1), e.g. a commercial DIN-rail enclosure or an injection-moulded one.
