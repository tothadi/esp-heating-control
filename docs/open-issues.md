# Open issues

To check or decide before or during the first build:

* **Enclosure depth and step dimensions:** measure them on an existing modular device.
* **JLC3DP threaded inserts:** their guidelines ask for at least 14 × 14 mm of flat area around an insert, but the insert bosses end in only Ø9.8 mm. Confirm with their support, with the drawing, before ordering.
* **Labels:** check the legibility of the smallest labels (S, G, L, N: ≈ 0.43 mm stroke) and the PAC-HP colours on the first print.
* **OpenTherm interface:** prototype test on a real boiler (calculated levels: high 16.4–17.5 V, low ≈ 2.9 V, receive threshold ≈ 12.8 mA).
* **CR1220 holder polarity:** check with a multimeter on the first board.
* **DIN hook and spring latch:** test print on a real rail. The stiffness of the spring arms and the 0.3 mm gaps depend on the printer.
* **FFC cable type:** do a paper-strip test before the first assembly (type A is required).
* **OLED module:** components on the back of the module may collide with the spacer frame (module backs differ between vendors).
* **NTC filter capacitors:** with 10 µF on each input, the reading needs about 5 τ ≈ 250 ms to settle after power-up. This is irrelevant with per-second sampling, but avoid fast input multiplexing on the same channel.
* **Firmware** ([firmware/v4](../firmware/v4)): written and compiled, untested on hardware. Check on the prototype: OpenTherm RX/TX polarity, OLED orientation, relay and LED mapping, TERM input order, NTC supply voltages.
* **Zigbee on the controller:** the ESP32-C6 has an 802.15.4 radio (Zigbee / Thread), but it is not used yet. ESPHome's `zigbee` component (2026.6) can only make the controller a Zigbee end device that exposes its own entities to a coordinator; it cannot receive other devices' readings, cannot be a coordinator, rules out the Wi-Fi fallback access point, and as a router it is unreliable while Wi-Fi shares the radio. Reading room sensors directly over Zigbee, without Home Assistant, would need custom ESP-IDF code (a coordinator, or binding standard ZCL temperature reports to the controller; Tuya thermostats with private data points cannot be bound). ESPHome has no Matter support.
* **CE conformity** (Low Voltage Directive, EMC, Radio Equipment Directive, EN 18031) before any distribution.
