# Surface cooling: plan

Status: **plan only, nothing implemented.** Goal: optional cooling for heat-pump systems with ceiling (or wall) surface
cooling. The flow temperature (or a mixing valve) is controlled from room humidity, measured by Zigbee or Matter sensors
or supplied by Home Assistant, so that no surface ever drops below the dew point.

Decided (2026-11-12):
1. **Two topologies only** (section 1): the ceiling heats and cools in the same circuit, or the floor heats and the
   ceiling cools. Floor cooling is not supported. It is rarely allowed, and leaving it out keeps the settings simple.
   Wall circuits behave like ceiling circuits.
2. **Optional in both firmwares**: without the feature, every zone is a floor heating zone and nothing changes
   (section 6).
3. **Fail safe**: a zone without a fresh humidity reading does not cool. There is no fallback value as there is for
   the outdoor temperature, because a wet ceiling causes real damage.

## 1. Topologies and zone types

Today every zone is a floor heating circuit: one relay, one floor NTC and one TERM input. With this feature, each zone
gets a **zone type**:

| Zone type | Heating mode | Cooling mode | NTC input | Used in |
|---|---|---|---|---|
| **Floor** (default) | as today | closed, no floor tempering | floor probe | today, topology B |
| **Ceiling, heat + cool** | heats | cools | ceiling surface or supply-pipe probe | topology A |
| **Ceiling, cool only** | closed | cools | ceiling surface or supply-pipe probe | topology B |

**Topology A: ceiling heats and cools in the same circuit.** One room = one zone = one relay. The relay stays the same
in both modes; only the logic changes.

**Topology B: floor heating, ceiling cooling.** One room = two zones = two relays: a *Floor* zone and a *Ceiling, cool
only* zone. The ceiling zone has a **room link** setting ("room of zone N"): it takes the room temperature, humidity and
setpoint from the linked floor zone and does not have a thermostat of its own. A room therefore still has one thermostat
in Home Assistant and in the web UI. In topology B, 8 relays cover at most 4 rooms when every room has both circuits.
Rooms without ceiling cooling need only their floor zone.

Mixed systems are allowed (for example, some rooms with ceiling heat + cool, others floor only).

## 2. Humidity sources

Per zone (topology B: per floor zone, shared with the linked ceiling zone), in the same order of precedence as the room
temperature:

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
T_flow = max( cooling_curve(outdoor),  max(Td over cooling zones that are open) + margin )
T_flow = clamp(T_flow, cool_flow_min, cool_flow_max)
```

* `cooling_curve`: a straight line, colder flow at higher outdoor temperatures (for example 20 °C at 24 °C outdoor,
  17 °C at 32 °C). Defaults to be fixed after the first season.
* `margin` default 2 K. Range `cool_flow_min`–`cool_flow_max` default 16–20 °C.
* Written to the actuator (section 4) in 0.5 °C steps on a change of ≥ 0.5 °C.

### 3.4 Zone valves in cooling mode

A cooling zone (*Ceiling, heat + cool* or *Ceiling, cool only*) is open if **all** of these hold:

1. **Cooling demand**: the room is above its cooling setpoint (0.2 °C deadband, 2 min minimum on/off, as in heating),
   or the wired thermostat asks for cooling (section 3.6).
2. **Humidity known**: the room's humidity reading is fresh.
3. **Dew point reachable**: `Td + margin ≤ cool_flow_max`. Otherwise this room would pull the flow for every room up to
   the maximum, so only this zone closes (condensation lockout). This works like the floor overheat lockout, mirrored.
4. **Surface above the dew point**: the zone's probe (ceiling surface or supply pipe) is above `Td + 1 K`. Released at
   `Td + 2 K`. A probe that reads unknown closes the zone.
5. **No dew-point switch alarm** (section 3.5).

*Floor* zones are closed in cooling mode. *Ceiling, cool only* zones are closed in heating mode.

### 3.5 Dew-point switch

A dew-point switch (dry contact on the supply pipe) is the usual protection in ceiling cooling. It **should be wired in
hardware**, in series with the valve actuators or the circuit pump, so that it protects without the firmware. The
firmware also reads it, on a TERM input whose role is set to *Dew-point switch*:

* On a ceiling zone's own TERM input: closes that zone. In topology B the ceiling zone's TERM input is free for this.
* On a spare TERM input marked *global*: stops all cooling.

An alarm is shown on the status LED (fast blink), on the OLED and in Home Assistant.

### 3.6 Zone thermostats and wired thermostats

* **ESPHome**: the zone `thermostat` climate gets a `cool_action` as well. In cooling mode it runs on the cooling
  setpoint (separate setpoint, own comfort/night values; the schedule applies to both). In topology B the ceiling zone
  has no climate of its own: its demand is the linked floor zone's cooling demand.
* **Wired thermostats**: in cooling mode, a closed contact means "cooling wanted". This only works with thermostats
  that have their own heat/cool changeover. Otherwise the room sensor is the only demand source in cooling mode.
* **TERM role** becomes a per-zone setting: *Thermostat* (default) / *Dew-point switch* / *Unused*.

### 3.7 Heating in ceiling zones (topology A)

* No floor tempering and no floor limits in *Ceiling, heat + cool* zones. Instead there is a **surface maximum** on the
  zone's probe (default 30 °C), with the same lockout logic.
* The flow temperature in heating gets a **ceiling flow cap** (default 35 °C). If floor and ceiling zones share one
  supply without separate mixing, the lower cap of the open zone types applies.

## 4. Actuators

| Option | How | Hardware |
|---|---|---|
| **A) OpenTherm heat pump** | master status *cooling enable* (ID 0, bit 2), flow setpoint (ID 1); *cooling control signal* (ID 7) where the unit wants it | none. Whether a heat pump accepts ID 1 as the cooling flow setpoint is vendor-specific: check per model |
| **B) 3-point mixing valve** (230 V open/close) | two relays with a software interlock (never both on, 1 s dead time at reversal). The mixed flow is measured by an NTC (strap-on probe after the mixer), and a PI loop drives the valve in pulses | two relays and one NTC input. There is no spare relay on the board, so this costs a zone (or P2) |
| **C) 0–10 V valve actuator** | I²C DAC (for example GP8403) on the existing bus | a hardware change or an external module |

Notes:
* In cooling mode the mixing valve works the other way round from heating: it mixes return water into the cold supply
  to keep the flow above the dew point.
* Option B: relay wear limits the pulse rate. Use a deadband (±0.3 K), a minimum pulse (1 s), at most one pulse per
  30 s, and the valve run time as a setting (default 120 s). Check the pulse count per season against the G5NB rating.
* Heat pumps without OpenTherm usually take a heat/cool changeover as a dry contact. That costs one more relay.

The actuator is a setting: *OpenTherm* / *Mixing valve (3-point)* / *Mixing valve (0–10 V)* / *None* (the heat pump
controls its flow itself, so the controller only switches the zones).

## 5. Pumps

P1 and P2 get a **circuit** setting: *All* (default, as today) / *Floor* / *Ceiling*. In *Auto* mode a pump runs only
while a zone of its own circuit is open. Typical topology B setup: P1 = floor circuit, P2 = ceiling circuit.

## 6. Optional in both firmwares

### ESPHome (Zigbee mode)

* New package [`cooling.yaml`](../firmware/controller/), included like `coprocessor.yaml`. Without it the firmware is
  unchanged.
* The package adds:
  * the *Cool* and *Auto* heating modes and a `cooling_available` flag that the main control lambda checks
    (`if (cooling_available && cooling_active) …`);
  * per zone: zone type, room link and TERM role (selects, restored from flash), dew point, condensation lockout,
    humidity (from HA, or from `coprocessor.yaml` when both packages are included);
  * settings: margin, cooling flow range and curve, surface limits, changeover thresholds, actuator, valve run time,
    pump circuits;
  * the actuator outputs (OpenTherm cooling enable and setpoint, or the valve relays chosen by substitution).
* The control lambda in [heating-controller.yaml](../firmware/controller/heating-controller.yaml) is restructured so
  that zone opening, flow setpoint and pumps go through one function per mode. Heating behaviour stays identical.

### Thread/Matter (ESP-IDF)

* Kconfig `CONFIG_HC_COOLING` (default on) compiles the feature in. A runtime switch `cooling.enabled` in NVS turns it
  on (default off).
* `components/heating/cooling.c`: the logic in section 3, sharing the zone and flow code with heating.
* Matter: 0x0405 subscription and mapping (section 2). MQTT: `settings/cooling/*`, `zone/<n>/humidity`,
  `zone/<n>/dew_point`, lockout states.

Both firmwares use the same setting names, ranges and defaults (the generated limits header from the
[web UI plan](web-ui-plan.md)).

## 7. Web UI and API

Additions to the [web UI plan](web-ui-plan.md), section 3:

| Area | Endpoints | Content |
|---|---|---|
| Cooling | `GET/PATCH /settings/cooling` | enabled, changeover (mode, thresholds, pause), margin, flow range and curve, surface limits, actuator, valve run time |
| Zones | `GET/PATCH /zones/{n}` (extended) | zone type, room link, TERM role, cooling setpoints, humidity source |
| Pumps | `GET/PATCH /settings/pumps` (extended) | circuit per pump |
| Live state | `GET /state`, SSE (extended) | per zone: humidity, dew point, surface temperature, condensation lockout; dew-point switch alarms; cooling flow setpoint and actual |

The UI shows the cooling settings only when the firmware reports the feature (`GET /info` feature flags). The heating
curve page gets a cooling curve tab, and the dashboard zone cards get humidity, dew point and lockout.

## 8. Phases

| Phase | Deliverable | Depends on |
|---|---|---|
| 0 | Review of this plan; defaults agreed; check OpenTherm cooling support of the target heat pump | – |
| 1 | Humidity readings in both firmwares: Zigbee mapping in `coprocessor.yaml`, Matter 0x0405, HA/MQTT input; dew point per zone | 0 |
| 2 | Zone types, room link, TERM role and pump circuits (heating behaviour unchanged for *Floor* zones); ceiling heating limits | 0 |
| 3 | Cooling logic (changeover, flow setpoint, zone gating, dew-point switch), actuator A (OpenTherm) | 1, 2 |
| 4 | Actuator B (3-point valve with PI loop); actuator C only if the hardware gets a DAC | 3 |
| 5 | Web UI and API additions | 3, web UI phase 2 |
| 6 | Test on a real system: dew-point switch trip, sensor timeout, changeover, valve loop tuning | hardware |

## 9. Risks

* **Condensation damage** from a wrong reading, a stale sensor or a firmware bug. Mitigations: the hardware dew-point
  switch (section 3.5), fail-safe handling of missing data, the surface probe, and a conservative default margin.
* **Humidity sensor accuracy**: cheap sensors are often off by ±3–5 % RH, which is about ±0.7–1 K of dew point. The
  margin must cover that; a per-sensor offset setting allows calibration.
* **Sensor placement**: the humidity sensor must be in the cooled room, not next to a bathroom door or a window.
  Document this.
* **OpenTherm cooling support** varies between heat pumps and is often incomplete. Option A must be checked per model;
  option B works with any heat pump that delivers cold water.
* **Relay budget**: topology B and option B both use relays. Plan the zone assignment before installing.
* **More code paths in the control loop**: heating behaviour must not change. Keep the heating tests and compare
  behaviour before and after phase 2.

## 10. Still open

* Default cooling curve and changeover thresholds (after the first season of real data).
* Whether a heat/cool changeover relay output for non-OpenTherm heat pumps is needed often enough to be a standard
  option.
* Whether the hardware should get a DAC (0–10 V) or a spare relay in a later revision.
