# Home Assistant

The controller runs the whole heating control on its own ([firmware](../firmware/controller/README.md)). Home Assistant
is only a **dashboard and remote control**: it shows the controller's entities and changes its setpoints, schedule
and settings. There are no control automations in Home Assistant; if it is down, the heating keeps working.

| File | What it is |
|---|---|
| [`dashboards/heating.yaml`](dashboards/heating.yaml) | "Heating" dashboard: Overview (rooms, floor, boiler, graphs), Schedule, Settings, System. Built-in cards only. |
| [`notifications.yaml`](notifications.yaml) | Optional automation: notification on floor overheat lockout, boiler fault, or the controller being offline for 10 minutes. |

## Setup

1. Flash the controller and add it in Home Assistant (Settings -> Devices & services -> ESPHome; it is usually
   discovered automatically). The controller only reads Home Assistant sensors (below); it performs no Home
   Assistant actions.
2. Create a dashboard (Settings -> Dashboards -> Add dashboard), open the raw configuration editor and paste
   `dashboards/heating.yaml`.
3. Optional: add `notifications.yaml` to your automations and set your notifier.

## What Home Assistant supplies

The controller reads these Home Assistant entities (configured in the firmware's `substitutions`):

- **Room temperature** per zone (`zoneN_room`), e.g. the temperature sensor of a Zigbee room thermostat. Only used
  by zones whose demand source is *Room sensor*. After 5 minutes without Home Assistant these values are dropped
  and only floor tempering (which always runs) heats the zone.
- **Outdoor temperature** (`outdoor_sensor`) for the heating curve. Fallbacks: the boiler's own outdoor sensor over
  OpenTherm, then a fixed value.

A Zigbee room thermostat thereby becomes a plain sensor: the setpoint lives on the controller
(`climate.heating_controller_<zone>`), set it there or on the dashboard. Zones that must not depend on Home
Assistant at all can use a wired thermostat (dry contact on the TERM inputs).

## Entity IDs

All entities belong to the ESPHome device `heating-controller` ("Heating controller"), so their IDs start with
`heating_controller_`. Per zone (`<zone>` = slug of the zone name, e.g. `living_room`):

| Entity | Meaning |
|---|---|
| `climate.heating_controller_<zone>` | Zone thermostat: setpoint, mode (Heat / Off), room temperature |
| `sensor.…_<zone>_floor_temperature`, `_floor_target`, `_room_temperature`, `_state` | Floor probe, effective floor target, room temperature used, zone state |
| `binary_sensor.…_<zone>_valve`, `_floor_overheat`, `_wired_thermostat` | Valve relay, floor lockout, TERM input |
| `select.…_<zone>_demand_source`, `_floor_type` | Room sensor / Wired thermostat / Floor only; Tiles / Laminate / No probe |
| `number.…_<zone>_floor_limit`, `_comfort_temperature`, `_night_temperature`, `_boost_temperature` | Floor limit and schedule temperatures |
| `switch.…_<zone>_night_setback`, `_evening_boost` | Schedule options |

Controller-wide: `select.…_heating_mode` (Auto / Off), `select.…_flow_mode`, `select.…_pump_1_mode`,
`select.…_pump_2_mode`, `time.…_schedule_day_start` / `_night_start` / `_boost_start` / `_boost_end`, the
heating curve numbers (`number.…_curve_*`, `_flow_maximum`, `_demand_boost_maximum`, `_manual_flow_setpoint`),
floor offsets and night floor target, boiler sensors (`sensor.…_boiler_*`, `_flow_setpoint`,
`_calculated_flow_temperature`, `_outdoor_temperature`), `binary_sensor.…_boiler_flame` / `_boiler_fault` /
`_pump_1` / `_pump_2` / `_evening_preheat`, `switch.…_hot_water`.
