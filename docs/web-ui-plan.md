# Web UI and API: plan

Status: **plan only, nothing implemented.** Goal: one web UI where everything on the controller can be configured and
monitored, backed by a fully specified REST API (OpenAPI), with a React + MUI frontend.

Decided (2026-11-11):
1. **Both firmwares** (ESP-IDF Thread/Matter and ESPHome/Zigbee) implement the API from the start.
2. **The UI is embedded** in the firmware image.
3. **English and Hungarian** UI (i18n).
4. **HTTPS with a self-signed certificate**, generated on each controller.
5. **API keys** that users can generate for scripts and AI agents (section 4).

## 1. Architecture

The controller works on its own, without Home Assistant or any other server ([firmware.md](firmware.md), section 1),
and there is no separate machine to host a backend. So the controller is the backend:

* **The OpenAPI spec is the contract** (`api/openapi.yaml`, OpenAPI 3.1). The firmware implementation, the typed
  frontend client, the development mock and the tests are all generated from it or checked against it.
* **The controller implements the API** (ESP-IDF `esp_http_server`, C) and **serves the React UI from its own
  flash**. A browser on the LAN is all that is needed.
* **No backend server.** For UI development without hardware, the browser mocks the API itself (Mock Service Worker,
  section 6): a development tool that runs inside the browser, not something to host.

```
 Browser (React + MUI, served by the controller)
   │  REST + SSE, OpenAPI v1
   └──────────────► Controller (ESP32-S3, hc_web component) ── control logic, NVS settings
                     └ MQTT stays for Home Assistant / automations
```

Long-term history is out of scope for the controller (no storage for it). The controller keeps a short history for the
UI charts (section 3). For long-term charts, use the MQTT state messages (Home Assistant's recorder, or any MQTT
logger).

## 2. Both firmwares

Both firmwares implement the same API and embed the same UI build. Each implements the API on top of its own way of
storing settings, so the control logic is not duplicated.

* **Thread/Matter firmware (ESP-IDF):** a new component `hc_web`.
  * It already has a central settings store (`hc_settings.c` / `hc_json.c`, NVS) and one command dispatcher shared by
    MQTT and the USB console (`hc_mqtt.cpp`).
  * The REST handlers become a third front end on that dispatcher. MQTT stays.
* **ESPHome/Zigbee firmware:** a new external component `hc_web` in `firmware/controller/components/`.
  * It maps the API onto the existing entities: the zone climates, and the `number`, `select`, `switch` and
    `text`/`datetime` settings, which are stored in flash with `restore_value`.
  * It replaces ESPHome's own `web_server`. The ESPHome native API (Home Assistant) and OTA stay.
  * Needed firmware changes:
    * **Zigbee sensor mapping at run time:** the `cp_zone1_device` … `cp_outdoor_device` substitutions become stored
      settings that the API can change.
    * **Runtime network settings:** today the Wi-Fi credentials are compile-time `secrets.yaml`, and changing them
      later needs the captive portal or Improv. They become settings the API can change.
  * It runs its own `esp_https_server` instance (ESPHome's `web_server_base` has no HTTPS).
* **Capabilities:** the parts of the API that only one firmware has are marked in the spec and reported by
  `GET /info` (`features`). The UI shows only the pages the connected controller supports; the other firmware answers
  those endpoints with `404`.

| Feature | ESP-IDF Thread/Matter | ESPHome/Zigbee |
|---|---|---|
| Zones, heating, schedule, pumps, boiler, sensors, display, time, system, auth | yes | yes |
| Network: mode, Wi-Fi credentials | yes | yes (runtime settings, new) |
| MQTT settings | yes | no (ESPHome native API instead) |
| Matter devices, Thread | yes | no |
| Zigbee devices (list, permit join, mapping) | no | yes (mapping at run time, new) |
| OTA upload through the API | yes | yes (ESPHome OTA backend) |

## 3. API (OpenAPI 3.1, `/api/v1`)

Conventions:
* JSON, with partial objects in `PATCH` requests for settings.
* The ranges and enums from the existing MQTT settings list become schema `minimum` / `maximum` / `enum`.
* Errors as RFC 9457 problem details.
* `ETag` / `If-Match` on settings, so two browsers editing at the same time can't overwrite each other.
* Live data through **Server-Sent Events**: simpler than WebSocket on the ESP, and one-way is enough because commands
  go through REST.

| Area | Endpoints | Content |
|---|---|---|
| Device | `GET /info` | model, serial, firmware, RCP and API versions, uptime, free heap/PSRAM, reset reason |
| Live state | `GET /state`, `GET /events` (SSE) | everything in the MQTT `state` and `zone/<n>/state` messages; events on change and every 10 s |
| Zones | `GET /zones`, `GET/PATCH /zones/{n}` | name, mode, target, comfort/night/boost, night setback, evening boost, demand source, floor type, floor limit, floor calibration |
| Heating | `GET/PATCH /settings/heating` | heating mode, flow mode, curve (ref, slope, offset, min, max), flow max, boost max, manual flow, floor offsets, night floor target, hot water |
| Schedule | `GET/PATCH /settings/schedule` | day start, night start, boost window |
| Pumps | `GET/PATCH /settings/pumps` | P1/P2 mode, overrun, exercise day/time |
| Boiler | `GET /boiler` | OpenTherm values: flow/return temperature, modulation, pressure, faults, outdoor sensor |
| Sensors | `GET/PATCH /settings/sensors` | NTC supply A/B, reference resistor, nominal, B constant, open/short limits, room and sensor timeouts, fallback outdoor temperature |
| Room temperatures | `PUT /zones/{n}/room-temperature`, `PUT /outdoor-temperature` | external values (as MQTT does today) |
| Network | `GET/PATCH /network`, `POST /network/wifi/scan` | mode auto/ethernet/wifi, active interface, link, addresses; Wi-Fi SSID (password write-only) |
| MQTT | `GET/PATCH /mqtt` | broker URL, user, password (write-only), base topic, connection state |
| Time | `GET/PATCH /time` | timezone (POSIX TZ, chosen from a list in the UI), NTP servers, sync source and state, RTC OSF state; `POST /time` sets the clock manually |
| Matter (ESP-IDF) | `GET /matter/devices`, `POST /matter/commission`, `PATCH/DELETE /matter/devices/{node}` | device list with last readings, commissioning (code, transport, label), mapping to zone/outdoor, remove/forget, resubscribe |
| Zigbee (ESPHome) | `GET /zigbee/devices`, `POST /zigbee/permit-join`, `PATCH /zigbee/devices/{ieee}` | device list with last readings, permit join, mapping to zone/outdoor |
| Thread (ESP-IDF) | `GET /thread` | role, channel, PAN, network name, border-router state; dataset export (admin) |
| Display | `GET/PATCH /settings/display` | OLED rotation, page time |
| History | `GET /history?series=…&from=…` | short history in PSRAM: 24 h at 1 min for about 30 series (~90 KB) |
| System | `POST /system/reboot`, `POST /system/firmware` (OTA upload), `GET /system/backup`, `POST /system/restore`, `POST /system/factory-reset`, `GET /system/logs` | |
| Auth | `POST /auth/login`, `POST /auth/logout`, `PUT /auth/password`, `GET/POST /setup` | first-run setup, session |
| API keys | `GET /auth/api-keys`, `POST /auth/api-keys`, `DELETE /auth/api-keys/{id}` | list (name, scopes, prefix, created, expires, last used), create (the secret is returned once), revoke |
| Spec | `GET /openapi.json` | the OpenAPI spec of this firmware (embedded, gzip), so scripts and AI agents can discover the API from the controller itself |
| Certificate | `GET /system/certificate`, `POST /system/certificate/regenerate`, `PUT /system/certificate` | fingerprint and validity, regenerate, upload an own certificate and key |

## 4. Security

The controller switches the heating, so the API must not be open on the LAN.

* **First-run setup**: until an admin password is set, only `/setup` works (and only from the LAN). The setup wizard
  sets the password, timezone, network and zone names.
* **Passwords** stored as PBKDF2-SHA256 (mbedTLS) in NVS; **session** as a random token in an `HttpOnly`,
  `SameSite=Strict` cookie, with expiry; login rate limiting.
* **CSRF**: `SameSite=Strict` plus a required custom header (`X-Requested-With`) on all writes. This applies to
  cookie sessions only; API keys are not sent automatically by the browser, so they need no CSRF protection.
* **API keys** for scripts, automations and AI agents:
  * **Use:** sent as `Authorization: Bearer hc_<key>`. A key is 32 random bytes (base32), shown **once** at creation.
    The controller stores only its SHA-256 hash and the first 8 characters (to recognise it in the list).
  * **Settings per key:** a name (e.g. "Claude agent"), an optional expiry date, and **scopes**.
  * **Scopes**, so an agent gets only what it needs:

    | Scope | Allows |
    |---|---|
    | `read` | all `GET` endpoints except secrets and the key list |
    | `control` | everyday operation: zone targets and modes, heating mode, pump modes, external room and outdoor temperatures |
    | `configure` | all other settings: schedule, curve, sensors, zones' configuration, network, MQTT, display, time, device mapping |
    | `admin` | password, API keys, certificate, firmware update, backup/restore, factory reset, reboot |

    `admin` cannot be given to a key by default. It needs an explicit confirmation in the UI, with a warning.
  * **Limits:** at most 10 keys (stored in NVS). Each key records when it was last used and from which IP, shown in the
    list. Requests are rate-limited per key.
  * **Lifecycle:** keys are revoked individually. Changing the admin password does not revoke keys; a factory reset
    does. Every key change is written to the log.
  * **For agents:** the UI shows a ready-made snippet with the base URL, the header, and a link to `GET /openapi.json`.
    The spec describes each endpoint, so an agent can use it as tool definitions. The certificate fingerprint is
    included, for clients that pin the self-signed certificate.
* **Write-only secrets**: Wi-Fi, MQTT and admin passwords are never returned by the API.
* **HTTPS with a self-signed certificate**, on by default (`esp_https_server`, mbedTLS):
  * Each controller generates its own ECDSA P-256 key and certificate on first boot and stores them in NVS. The
    certificate is valid for 10 years, with the hostname and the current IP addresses as subject alternative names.
  * Port 443; port 80 only redirects to HTTPS. The `Secure` cookie flag and HSTS are off, so the redirect keeps
    working after a certificate change.
  * **Trusting it:** the browser warns once per certificate. To check that the certificate is the controller's own,
    its SHA-256 fingerprint is shown on the OLED (a page in the button cycle) and on the USB console. The user
    compares it before accepting.
  * The certificate can be regenerated, or replaced with an own certificate and key (e.g. from a home CA), through
    the API.
  * Cost: one TLS session takes about 40 KB, put in PSRAM where mbedTLS allows. The number of parallel sessions is
    limited (section 5).
* The USB console stays the recovery path (password reset, factory reset), since it needs physical access.

## 5. Controller implementation

Two implementations of the same handlers' behaviour:
* `firmware/thread-matter/components/hc_web` (C, on the settings store and dispatcher);
* `firmware/controller/components/hc_web` (C++ external component, on the ESPHome entities).

They share the generated pieces: the embedded UI assets, the C header with the limits, and the certificate code where
possible.

* `esp_https_server` with `max_open_sockets` 4–5 (lwIP sockets are shared with MQTT, Matter and the border router).
* SSE for at most 2 clients.
* **UI files embedded in the firmware image** (gzip), served with `Content-Encoding: gzip` and long cache headers
  (hashed file names from Vite).
  * ESP-IDF: `EMBED_FILES` / a generated asset table.
  * ESPHome: the component's Python code generates a header with the assets from `web/dist` at compile time.
  * One OTA update then updates firmware and UI together, so the UI always matches the API version, and the partition
    table does not change.
  * Size: the UI adds an estimated 300–500 KB. The ESP-IDF app slots are 6 MB; check the ESPHome slot size and its
    headroom.
* **Request validation from the spec**: a build step generates a C header with the ranges and enums from
  `api/openapi.yaml`, so limits are defined once.
* **Memory**: request bodies limited (8 KB; firmware uploads streamed to the OTA partition), JSON with cJSON in PSRAM.

## 6. Frontend (`web/`)

* **Stack**:
  * React 19 + TypeScript + Vite + MUI (Material UI); MUI X Charts for the curve and history.
  * TanStack Query for server state; a typed client generated from the spec (`openapi-typescript` + `openapi-fetch`).
  * Forms with `react-hook-form`, with validation rules taken from the schema limits.
  * react-i18next with English and Hungarian.
* **Development without hardware**: **Mock Service Worker (MSW)** intercepts the API calls in the browser.
  * The handlers are generated from the spec and its examples, then extended with a small stateful model: zones with
    simple thermal behaviour, a schedule clock that can run fast, SSE events.
  * The mock can play either firmware (feature set from `GET /info`), so both variants of the UI are tested.
  * `npm run dev` runs the UI against the mock; `npm run dev -- --api=https://<controller-ip>` runs it against a real
    controller (Vite proxy, which accepts the self-signed certificate).
  * The same mock serves the component and end-to-end tests (Vitest, Playwright).
* Responsive (phone first: it will often be used in front of the distribution board), light and dark theme.
* Live updates from `/events`; optimistic updates on settings with rollback on error; a clear "unsaved / saved /
  error" state on every form.

Pages:

| Page | Content |
|---|---|
| Dashboard | zone cards (room/floor temperature, target, valve, lockout, source), boiler card (flow setpoint and actual, CH demand, faults), pumps, network and time status, quick heating mode switch |
| Zone | all zone settings, temperatures of the last 24 h, floor lockout state and limit |
| Schedule | global times as a day timeline, per-zone comfort/night/boost temperatures and switches in one table |
| Heating curve | the curve settings with a live chart (flow vs. outdoor temperature, current point marked), boost and preheat parameters |
| Sensors & devices | NTC calibration, room-temperature sources per zone; ESP-IDF: Matter devices (commissioning by code entry or QR scan with the phone camera, mapping, removal); ESPHome: Zigbee devices (permit join, mapping) |
| Network | Ethernet/Wi-Fi status, mode, Wi-Fi scan and credentials; ESP-IDF: MQTT and Thread network info |
| System | time and timezone, display, certificate (fingerprint, regenerate, upload), firmware update (upload, progress, reboot), backup/restore, logs, reboot, factory reset, about (versions, licenses) |
| API keys | key list (name, scopes, prefix, expiry, last used and from where), create (name, scopes, expiry; the key is shown once with a copy button and a usage snippet for scripts and AI agents), revoke |
| Setup wizard | first-run: password, timezone, network, zone names and types |

## 7. Repository layout and tooling

```
api/openapi.yaml                            the contract (+ examples), linted with Redocly CLI
web/                                        React + MUI app (Vite), MSW mock, tests
firmware/thread-matter/components/hc_web/   API for the ESP-IDF firmware + embedded UI
firmware/controller/components/hc_web/      API for the ESPHome firmware (external component) + embedded UI
```

**CI** (GitHub Actions):
* lint the spec;
* build and test `web` against the mock;
* check that the generated C limits header and the TypeScript client are up to date;
* both firmware builds with the UI embedded: ESP-IDF in the `espressif/idf` container, ESPHome with `esphome compile`.

**Bench contract tests**: Schemathesis runs against a real controller by IP, generated from the same spec.

## 8. Phases

| Phase | Deliverable | Depends on |
|---|---|---|
| 0 | `api/openapi.yaml` v1 draft with examples and the feature flags; reviewed | – |
| 1 | React UI against the MSW mock (both feature sets): dashboard, zones, schedule, heating curve, network, system, setup wizard; i18n EN/HU | 0 |
| 2a | ESP-IDF `hc_web`: HTTPS with the generated certificate, auth and setup, read and settings endpoints, SSE, embedded UI | 0, 1 |
| 2b | ESPHome `hc_web` external component, same scope as 2a; Zigbee mapping and Wi-Fi credentials as runtime settings | 0, 1 |
| 3 | Matter/Thread (ESP-IDF) and Zigbee (ESPHome) pages, history buffer, firmware upload, backup/restore, certificate page and OLED fingerprint page, API keys with scopes and the `/openapi.json` endpoint | 2a, 2b |
| 4 | Bench test on the first built controller, with each firmware: contract tests by IP, memory and socket headroom (ESP-IDF: with Matter + border router + 2 browsers over HTTPS) | hardware |

Phase 1 needs no hardware and can start right away. Phases 2a and 2b compile without hardware and can run in parallel.
The memory and socket limits are only known on a real board.

## 9. Risks

* **RAM and sockets on the S3**: Matter, the border router, MQTT and the HTTPS server all run together. Keep the server
  small (few sockets, an SSE client limit, PSRAM for TLS and JSON buffers) and measure in phase 4.
* **Two implementations of one API**: the ESPHome component and the ESP-IDF component can drift apart. Mitigations:
  the same spec, the same generated limits header, and the same contract tests run against both firmwares.
* **ESPHome internals**: the component relies on ESPHome's entity and component APIs, which change between releases.
  Pin the ESPHome version and test each upgrade.
* **API keys in the wrong hands**: a leaked key with `control` scope can change the heating. Mitigations: least-privilege
  scopes, `admin` only with confirmation, expiry dates, last-used information, revocation, rate limits.
* **Self-signed certificate UX**: users must accept a browser warning once, and again after a regeneration. The
  fingerprint on the OLED makes the check possible; the docs must explain it.
* **Settings consistency** between REST, MQTT and the console: all go through the one dispatcher; the settings store
  increments a revision number used as the `ETag`.
* **Mock drift**: the MSW mock can diverge from the firmware. The bench contract tests (phase 4) catch it, and the mock
  is generated from the same spec.
* **API versioning**: `/api/v1` is frozen once released. The embedded UI always matches its firmware's API, and
  `GET /info` reports the API version.
* **Security mistakes** on a device that controls heating: review the auth design before phase 2, and keep the USB
  console as the recovery path.

## 10. Still open

* The exact endpoint and schema design: decided in the phase 0 spec review.
* The ESPHome app partition size with the UI embedded (check before phase 2b).
