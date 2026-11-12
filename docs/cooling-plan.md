# Surface cooling: plan

Status: **plan only, nothing implemented.** Goal: optional cooling for heat-pump systems with ceiling (or wall) surface
cooling. The flow temperature (or a mixing valve) is controlled from room humidity, measured by Zigbee or Matter sensors
or supplied by Home Assistant, so that no surface ever drops below the dew point.

Decided (2026-11-12):
1. **Two topologies** (section 1): the ceiling heats and cools in the same circuit (wired relays), or the floor heats
   and separate ceiling circuits cool. The current hardware has no outputs for the separate ceiling circuits, so those
   are supported **only with Zigbee or Matter thermal actuators**. Floor cooling is not supported: it is rarely
   allowed. Wall circuits behave like ceiling circuits.
2. **Optional in both firmwares**: without the feature, every zone is a floor heating zone and nothing changes
   (section 6).
3. **Fail safe**: a zone without a fresh humidity reading does not cool. There is no fallback value as there is for
   the outdoor temperature, because a wet ceiling causes real damage.
4. **The mixing valve can be a Zigbee or Matter device** (section 4, option D), so it needs no relay.

## 1. Topology and zone types

Today every zone is a floor heating circuit: one relay, one floor NTC and one TERM input. With this feature, each zone
gets a **zone type**:

| Zone type | Heating mode | Cooling mode | Output | Probe |
|---|---|---|---|---|
| **Floor** (default) | as today | closed, no floor tempering | zone relay | floor NTC |
| **Ceiling** | heats | cools | zone relay | ceiling surface or supply-pipe NTC |
| **Ceiling, cool only** | closed | cools | Zigbee/Matter thermal actuator(s) | Zigbee/Matter probe or the linked zone's spare NTC (optional) |

**Topology A: the ceiling heats and cools in the same circuit.** One room = one zone = one relay (the thermal actuators
of that circuit). The relay is the same in both modes; only the logic changes. Mixed systems are allowed: some rooms
with ceiling heating and cooling, others with floor heating only (those rooms are not cooled).

**Topology B: floor heating, separate ceiling cooling circuits.** The floor circuits keep the 8 zone relays, so the
ceiling circuits' thermal actuators must be **Zigbee or Matter devices** (section 1.1). A *Ceiling, cool only* zone is
a virtual zone (zones 9–16, no wired I/O) with a **room link** to a floor zone: it takes that zone's room temperature,
humidity and setpoint, so the room keeps one thermostat in Home Assistant and in the web UI. One ceiling zone can drive
several actuators (one room with more circuits).

### 1.1 Wireless thermal actuators (topology B)

* **Devices**: mains-powered (router) devices only, either a Zigbee/Matter relay or switch module driving the existing
  thermal actuators, or a thermal actuator with a radio of its own. Zigbee: On/Off (0x0006), the current state read back
  through reporting. Matter: On/Off cluster. Battery valve heads and Tuya "EF00" devices are not supported: battery
  devices sleep and take commands too late.
* **Write path**: the same as for the wireless mixing valve (section 4, option D): a new S3 → H2 command in the
  co-processor protocol, and the Matter controller's command path.
* **Confirmation**: a command counts only once the device reports the new state. The 2-minute relay guard applies too.
* **Fail safe**: an unreachable actuator could be stuck open, and the controller cannot close it. So if a ceiling
  actuator does not confirm a *close* within 2 min, or has not reported for 10 min, **cooling stops for the whole
  ceiling circuit** (cooling enable off or mixing valve closed, ceiling pump off after the overrun) until it is back.
  Where the actuator closes without power (normally closed heads on a relay module), the hardware dew-point switch
  should cut that module's supply.

## 2. Humidity sources

Per zone, in the same order of precedence as the room temperature:

| Source | Firmware | State today | Work |
|---|---|---|---|
| Zigbee sensor (ZCL 0x0405 MeasuredValue) | ESPHome + Zigbee coordinator | the coordinator binds and reports it, and the protocol carries it ([coprocessor-protocol.md](coprocessor-protocol.md)) | in [coprocessor.yaml](../firmware/controller/coprocessor.yaml): one `type: humidity` sensor per zone |
| Matter sensor (Relative Humidity Measurement cluster 0x0405) | Thread/Matter | only temperatures are mapped ([hc_matter.cpp](../firmware/thread-matter/main/hc_matter.cpp)) | subscribe to 0x0405 and map it per zone like the temperature |
| Home Assistant | ESPHome (API), Thread/Matter (MQTT) | – | substitution `zoneN_humidity_sensor` / MQTT topic `zone/<n>/humidity/set` |

Each reading has a timeout (default 30 min, the `cp_timeout` mechanism). After it, the value is dropped (NaN).

## 3. Control logic

### 3.1 Heating / cooling changeover

* *Heating mode* gets a new value, **Cool**: Off / Heat / Cool / Auto.
* **Auto** switches on the outdoor temperature (the same source chain as the heating curve), with hysteresis and a
  delay. Defaults: cool above 24 °C (24 h average), heat below 18 °C, at most one changeover per 24 h.
* On a changeover, all zones close, the pumps run their overrun, and the new mode starts after a pause (default
  10 min), so warm water never reaches a cold ceiling and the other way round.
* In cooling mode, floor tempering and the heating curve do not run, and OpenTherm CH demand is off.

### 3.2 Dew point

Per room with a valid temperature `T` (°C) and relative humidity `RH` (%), Magnus formula (a = 17.62, b = 243.12 °C):

```
γ  = ln(RH / 100) + a·T / (b + T)
Td = b·γ / (a − γ)
```

### 3.3 Cooling flow setpoint

```
T_flow = max( cooling_curve(outdoor),  max(Td over ceiling zones that are open) + margin )
T_flow = clamp(T_flow, cool_flow_min, cool_flow_max)
```

* `cooling_curve`: a straight line, colder flow at higher outdoor temperatures (for example 20 °C at 24 °C outdoor,
  17 °C at 32 °C). Defaults to be fixed after the first season.
* `margin` default 2 K. Range `cool_flow_min`–`cool_flow_max` default 16–20 °C.
* Written to the actuator (section 4) in 0.5 °C steps on a change of ≥ 0.5 °C.

### 3.4 Zone valves in cooling mode

A *Ceiling* or *Ceiling, cool only* zone is open if **all** of these hold:

1. **Cooling demand**: the room is above its cooling setpoint (0.2 °C deadband, 2 min minimum on/off, as in heating),
   or the wired thermostat asks for cooling (section 3.6).
2. **Humidity known**: the room's humidity reading is fresh.
3. **Dew point reachable**: `Td + margin ≤ cool_flow_max`. Otherwise this room would pull the flow for every room up to
   the maximum, so only this zone closes (condensation lockout). This works like the floor overheat lockout, mirrored.
4. **Surface above the dew point**: the zone's probe (ceiling surface or supply pipe) is above `Td + 1 K`. Released at
   `Td + 2 K`. A probe that reads unknown closes the zone. A *Ceiling, cool only* zone without a probe relies on the
   flow setpoint and the dew-point switch.
5. **No dew-point switch alarm** (section 3.5).
6. **Actuator reachable** (*Ceiling, cool only* only): section 1.1.

*Floor* zones are closed in cooling mode. *Ceiling, cool only* zones are closed in heating mode.

### 3.5 Dew-point switch

A dew-point switch (dry contact on the supply pipe) is the usual protection in ceiling cooling. It **should be wired in
hardware**, in series with the valve actuators or the circuit pump, so that it protects without the firmware. The
firmware also reads it, on a TERM input whose role is set to *Dew-point switch*:

* On a zone's own TERM input (when that zone uses a room sensor instead of a wired thermostat): closes that zone, and
  in topology B also the linked ceiling zone.
* On a spare TERM input marked *global*: stops all cooling.

An alarm is shown on the status LED (fast blink), on the OLED and in Home Assistant.

### 3.6 Zone thermostats and wired thermostats

* **ESPHome**: the zone `thermostat` climate of a *Ceiling* zone gets a `cool_action` as well. In cooling mode it runs
  on the cooling setpoint (separate setpoint, own comfort/night values; the schedule applies to both). In topology B
  the floor zone's climate gets the `cool_action` instead, and drives the linked ceiling zone; the ceiling zone has no
  climate of its own.
* **Wired thermostats**: in cooling mode, a closed contact means "cooling wanted". This only works with thermostats
  that have their own heat/cool changeover. Otherwise the room sensor is the only demand source in cooling mode.
* **TERM role** becomes a per-zone setting: *Thermostat* (default) / *Dew-point switch* / *Unused*.

### 3.7 Heating in ceiling zones

* No floor tempering and no floor limits in *Ceiling* zones. Instead there is a **surface maximum** on the zone's probe
  (default 30 °C), with the same lockout logic.
* The flow temperature in heating gets a **ceiling flow cap** (default 35 °C). If floor and ceiling zones share one
  supply without separate mixing, the lower cap of the open zone types applies.

## 4. Actuators

| Option | How | Hardware |
|---|---|---|
| **A) OpenTherm heat pump** | master status *cooling enable* (ID 0, bit 2), flow setpoint (ID 1); *cooling control signal* (ID 7) where the unit wants it | none. Whether a heat pump accepts ID 1 as the cooling flow setpoint is vendor-specific: check per model |
| **B) 3-point mixing valve** (230 V open/close) | two relays with a software interlock (never both on, 1 s dead time at reversal). The mixed flow is measured by an NTC (strap-on probe after the mixer), and a PI loop drives the valve in pulses | two relays and one NTC input. There is no spare relay on the board, so this costs a zone (or P2) |
| **C) 0–10 V valve actuator** | I²C DAC (for example GP8403) on the existing bus | a hardware change or an external module |
| **D) Zigbee or Matter valve actuator** | a wireless actuator with a position input (0–100 %), driven by the same PI loop as B. The mixed flow temperature comes from a wired NTC (strap-on probe) or from a Zigbee/Matter temperature probe | none; no relay is used |

Notes:
* In cooling mode the mixing valve works the other way round from heating: it mixes return water into the cold supply
  to keep the flow above the dew point.
* Option B: relay wear limits the pulse rate. Use a deadband (±0.3 K), a minimum pulse (1 s), at most one pulse per
  30 s, and the valve run time as a setting (default 120 s). Check the pulse count per season against the G5NB rating.
* Option D:
  * **Zigbee**: today the coordinator only reads values ("no switching", [zigbee-coordinator/README.md](../firmware/zigbee-coordinator/README.md)).
    It needs a write path: a new S3 → H2 command in the [co-processor protocol](coprocessor-protocol.md) (for example
    `SET <id>,<cluster>,<value>`), and support for the actuator's cluster on the H2: Level Control (0x0008,
    *Move to Level*) or Window Covering (0x0102, *Go to Lift Percentage*), whichever the device uses. The current
    position is read back through reporting. Tuya "EF00" devices stay unsupported.
  * **Matter**: the controller writes to the Valve Configuration and Control cluster (0x0081, *Open* with
    *TargetLevel*, Matter 1.3 and newer) or to Level Control / Window Covering, and subscribes to the current level.
  * **Loop timing**: commands go out at most once per 60 s, and only on a change of ≥ 2 %. A wireless flow probe
    reports every 60 s at best, so a wired NTC on the mixed flow is preferred.
  * **Fail safe**: if the actuator does not confirm a command, or has not reported for 10 min, cooling stops (all
    zones close, the pumps stop after the overrun). Sleepy (battery) actuators are not supported: they answer too late.
  * The same write path also drives the wireless thermal actuators of topology B (section 1.1).
* Heat pumps without OpenTherm usually take a heat/cool changeover as a dry contact. That costs one more relay.

The actuator is a setting: *OpenTherm* / *Mixing valve (3-point)* / *Mixing valve (0–10 V)* / *Mixing valve (Zigbee)*
(ESPHome) or *Mixing valve (Matter)* (Thread/Matter) / *None* (the heat pump controls its flow itself, so the controller
only switches the zones).

## 5. Pumps

P1 and P2 get a **circuit** setting: *All* (default, as today) / *Floor* / *Ceiling*. In *Auto* mode a pump runs only
while a zone of its own circuit is open. Typical topology B setup: P1 = floor circuit, P2 = ceiling circuit. It is also
useful in topology A when floor and ceiling zones are on different manifolds, each with its own pump.

## 6. Optional in both firmwares

### ESPHome (Zigbee mode)

* New package [`cooling.yaml`](../firmware/controller/), included like `coprocessor.yaml`. Without it the firmware is
  unchanged.
* The package adds:
  * the *Cool* and *Auto* heating modes and a `cooling_available` flag that the main control lambda checks
    (`if (cooling_available && cooling_active) …`);
  * per zone: zone type and TERM role (selects, restored from flash), dew point, condensation lockout, humidity (from
    HA, or from `coprocessor.yaml` when both packages are included);
  * up to 8 *Ceiling, cool only* zones (topology B): room link and the Zigbee device ids of their thermal actuators
    (needs `coprocessor.yaml`);
  * settings: margin, cooling flow range and curve, surface limits, changeover thresholds, actuator, valve run time,
    pump circuits;
  * the actuator outputs: OpenTherm cooling enable and setpoint, the valve relays chosen by substitution, or the Zigbee
    valve's device id (needs `coprocessor.yaml`).
* The control lambda in [heating-controller.yaml](../firmware/controller/heating-controller.yaml) is restructured so
  that zone opening, flow setpoint and pumps go through one function per mode. Heating behaviour stays identical.

### Thread/Matter (ESP-IDF)

* Kconfig `CONFIG_HC_COOLING` (default on) compiles the feature in. A runtime switch `cooling.enabled` in NVS turns it
  on (default off).
* `components/heating/cooling.c`: the logic in section 3, sharing the zone and flow code with heating.
* Matter: 0x0405 subscription and mapping (section 2), the valve actuator (option D) and the topology B thermal
  actuators: commissioning and mapping like the sensors, plus the write path. MQTT: `settings/cooling/*`, `zone/<n>/humidity`, `zone/<n>/dew_point`, lockout
  states, valve position.

Both firmwares use the same setting names, ranges and defaults (the generated limits header from the
[web UI plan](web-ui-plan.md)).

## 7. Web UI and API

Additions to the [web UI plan](web-ui-plan.md), section 3:

| Area | Endpoints | Content |
|---|---|---|
| Cooling | `GET/PATCH /settings/cooling` | enabled, changeover (mode, thresholds, pause), margin, flow range and curve, surface limits, actuator, valve run time, valve device |
| Zones | `GET/PATCH /zones/{n}` (extended) | zone type, TERM role, cooling setpoints, humidity source |
| Ceiling zones | `GET /cooling-zones`, `GET/PATCH/DELETE /cooling-zones/{n}` | topology B: room link, thermal actuator devices, state and reachability |
| Pumps | `GET/PATCH /settings/pumps` (extended) | circuit per pump |
| Live state | `GET /state`, SSE (extended) | per zone: humidity, dew point, surface temperature, condensation lockout; dew-point switch alarms; cooling flow setpoint and actual; valve position and reachability |

The UI shows the cooling settings only when the firmware reports the feature (`GET /info` feature flags). The heating
curve page gets a cooling curve tab, and the dashboard zone cards get humidity, dew point and lockout. The Zigbee and
Matter device pages can map a device as the mixing valve or as a ceiling zone's thermal actuator.

## 8. Phases

| Phase | Deliverable | Depends on |
|---|---|---|
| 0 | Review of this plan; defaults agreed; check OpenTherm cooling support of the target heat pump | – |
| 1 | Humidity readings in both firmwares: Zigbee mapping in `coprocessor.yaml`, Matter 0x0405, HA/MQTT input; dew point per zone | 0 |
| 2 | Zone types, TERM role and pump circuits (heating behaviour unchanged for *Floor* zones); ceiling heating limits | 0 |
| 3 | Cooling logic (changeover, flow setpoint, zone gating, dew-point switch), actuator A (OpenTherm) | 1, 2 |
| 4 | Wireless write path (protocol command, Zigbee On/Off and Level Control / Window Covering, Matter commands); mixing valve PI loop with actuator D and B (3-point relays); actuator C only if the hardware gets a DAC | 3 |
| 4b | Topology B: *Ceiling, cool only* zones with room link and wireless thermal actuators, fail safe | 4 |
| 5 | Web UI and API additions | 3, web UI phase 2 |
| 6 | Test on a real system: dew-point switch trip, sensor and actuator timeouts, changeover, valve loop tuning | hardware |

## 9. Risks

* **Condensation damage** from a wrong reading, a stale sensor or a firmware bug. Mitigations: the hardware dew-point
  switch (section 3.5), fail-safe handling of missing data, the surface probe, and a conservative default margin.
* **Humidity sensor accuracy**: cheap sensors are often off by ±3–5 % RH, which is about ±0.7–1 K of dew point. The
  margin must cover that; a per-sensor offset setting allows calibration.
* **Sensor placement**: the humidity sensor must be in the cooled room, not next to a bathroom door or a window.
  Document this.
* **OpenTherm cooling support** varies between heat pumps and is often incomplete. Option A must be checked per model;
  the mixing valve options work with any heat pump that delivers cold water.
* **Wireless valve and thermal actuators**: the radio link becomes part of a control loop and of the condensation
  protection. Mitigations: the fail safes in sections 1.1 and 4, mains-powered (router) devices only, the wired flow
  probe, and the hardware dew-point switch cutting the ceiling circuit.
* **More code paths in the control loop**: heating behaviour must not change. Keep the heating tests and compare
  behaviour before and after phase 2.

## 10. Still open

* Which Zigbee/Matter thermal actuators and relay modules to recommend for manifolds (mains-powered, reliable state
  reporting).
* Default cooling curve and changeover thresholds (after the first season of real data).
* Whether a heat/cool changeover relay output for non-OpenTherm heat pumps is needed often enough to be a standard
  option.
* Whether the hardware should get a DAC (0–10 V) or a spare relay in a later revision.
