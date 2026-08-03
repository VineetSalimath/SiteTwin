# Gateway-to-Server Bring-Up

This documents the Wi-Fi/server-side ESP work: HiveMQ delivery, the Pi-side bridge, and
ThingsBoard ingestion. It is the counterpart to `SITETWIN_ZIGBEE_BRINGUP.md`, which
covers the Zigbee side up to the point where a validated 30-byte SiteTwin payload is
accepted by `st_gateway_runtime_t` on the Zigbee-side gateway.

## Confirmed

- ESP32-C6-DevKitC-1 (Wi-Fi role) connects to Wi-Fi, establishes a TLS (MQTTS, port 8883)
  connection to HiveMQ Cloud, and publishes to `sitetwin/pods/{pod_id}/telemetry`.
- The full existing `sitetwin_core` pipeline runs on this board: Zigbee payload decode,
  gateway registry resolution, gateway processor validation/deduplication, and JSON
  conversion via `st_gateway_telemetry_to_json`. This is real, tested code, not a
  simplified reimplementation.
- ESP-IDF v5.5.4, matching the Zigbee-side baseline.
- A Raspberry Pi 5 (8GB) runs ThingsBoard CE 4.3.1.3, installed natively (not Docker)
  following the official ThingsBoard Raspberry Pi guide: Java 21 (OpenJDK), PostgreSQL 16,
  in-memory queue.
- A Python bridge script (`bridge.py`) on the Pi subscribes to HiveMQ over TLS and
  forwards telemetry into ThingsBoard using the official `TBGatewayMqttClient` gateway
  API. Devices are auto-created in ThingsBoard per `pod_id` (no manual per-device
  provisioning needed).
- End-to-end delivery has been verified: locally-encoded test records (both routine
  readings and heartbeat records) travel from the Wi-Fi ESP through HiveMQ and the
  bridge script into ThingsBoard device entities, visible in the UI, with all metadata
  fields intact (see "Known data loss" below — resolved).
- `sitetwin_core` on this board is byte-identical to the version used in the
  `sitetwin-zigbee-bringup` branch (verified via `diff -rq`), so no shared-contract drift
  exists between the two boards' firmware as of this writing.
- `bridge.py` runs as a systemd service (`sitetwin-bridge.service`): starts on boot,
  restarts automatically on failure, logs to both `journalctl` and a local file.
- The physical UART hand-off is verified end to end: a Zigbee pod health frame is accepted
  by the Zigbee coordinator, carried over UART1 (GPIO4 TX → GPIO5 RX, shared GND),
  CRC-validated by the Wi-Fi ESP, encoded as JSON, published to HiveMQ, and acknowledged
  by the broker.

## Confirmed but hardcoded for now

- The Zigbee-side gateway currently synthesizes `pod_id` directly from the Zigbee short
  address (e.g. `POD_1234`) rather than through `gateway_registry` lookups. This board's
  test data source matches that convention (see "Pod/sensor naming scheme" below).
- The 30-byte SiteTwin telemetry payload consumed by the test data source
  (`gateway_pipeline.c`) is hand-constructed in firmware for testing (via
  `st_zigbee_telemetry_encode`) rather than received over UART. This exercises the same
  downstream pipeline (`gateway_runtime`, `gateway_json`) that a real UART-received
  payload would use.
- The default UART link is 115200 baud, UART1, with Zigbee gateway GPIO4 (TX) wired to
  Wi-Fi gateway GPIO5 (RX), plus shared GND. Confirm both boards' pin maps before wiring.
- HiveMQ and ThingsBoard credentials are stored in `wifi_config.h` (Wi-Fi ESP, excluded
  from git) and `pi-bridge/config.py` (bridge script, excluded from git). Both are
  plaintext local files, not a secrets manager — acceptable for the current project
  stage but not production-grade.

## Known data loss in the current pipeline

- **Resolved**: `bridge.py` previously forwarded only `pod_id`, `sensor_id`, and `value`
  into ThingsBoard, discarding `sequence`, `boot_id`, `uptime_ms`, and `quality_flags`.
  The bridge now forwards all of these, each prefixed with `sensor_id` (e.g.
  `SLOT_0_quality_flags`, `SLOT_0_sequence`) to avoid key collisions if a pod reports
  multiple sensors. Verified end to end.
- **Still open**: `st_gateway_telemetry_to_json` (shared firmware code, not modified on
  this board) omits the `priority` field. Downstream consumers still cannot distinguish
  routine state from event- or health-priority records. Requires a shared-code change;
  should be raised with the team rather than patched locally.
- **Still open**: the ThingsBoard telemetry timestamp is set to bridge processing time
  (wall-clock time when `bridge.py` handles the message), not a timestamp derived from
  the pod's `uptime_ms`. This is an accepted architectural gap, not an oversight: pods
  are deliberately designed without knowledge of absolute time (to avoid the battery
  cost of an RTC or periodic time sync), and `uptime_ms` is the free byproduct of a
  timer pods already need for their own reporting-policy logic. Converting `uptime_ms`
  to an absolute timestamp would require the Gateway/Server side to track each pod's
  absolute boot time — a mechanism that does not exist yet anywhere in the pipeline (not
  on the Zigbee-side gateway, not here). Not planned for the current milestone.

## Open decisions — do not hard-code further assumptions

### UART payload format between the two ESP32 boards — Resolved and verified

The UART payload is the unchanged 30-byte SiteTwin Zigbee telemetry payload. Its
originating short address, boot ID, and sequence are carried in a shared 16-byte frame
header, followed by CRC16; both boards use the same `gateway_frame.c` implementation.

The deployed one-way link is Zigbee gateway GPIO4 (UART1 TX) to Wi-Fi gateway GPIO5
(UART1 RX), with shared GND, at 115200 baud 8N1. The physical connection and complete
Zigbee-to-HiveMQ delivery path were verified on ESP32-C6-DevKitC-1 boards.

### Pod/sensor naming scheme — Resolved

Decided: standardize on the Zigbee-side gateway's technical identifier scheme
(`POD_1234`, `SLOT_0`) rather than introducing a human-readable naming/mapping layer
(`ENV_01`, `env_temperature`). Rationale: a pod's identity (tied to its Zigbee address)
is not strictly bound to its current role or sensor configuration — the same physical
board could be repurposed with a different sensor module — so encoding role semantics
into the identifier itself is misleading. If a human-readable label is needed later
(e.g. for dashboard presentation), it should be added as a separate attribute, not
baked into the identifier.

This board's test data source (`gateway_pipeline.c`) matches: `TEST_POD_ID`/
`TEST_SENSOR_ID` default to `POD_1234`/`SLOT_0`. Verified end to end — ThingsBoard
correctly auto-creates a `POD_1234` device with a `SLOT_0` telemetry key.

### Heartbeat / health record handling — Resolved

Pod health frames (`record_class = HEALTH`, `sensor_kind = UNKNOWN`, `value = 1.0`) are
sent by the Zigbee-side pod image every 15 seconds as a bring-up signal, and this
board's test data source can synthesize the same shape via the `send_heartbeat` console
command.

`bridge.py` detects `record_class == "health"` and forwards it as
`{sensor_id}_heartbeat: true` (plus `_sequence`, `_boot_id`, `_uptime_ms`) instead of
treating it as an ordinary sensor reading — this avoids a misleading telemetry point
named after an "unknown" sensor kind with a meaningless numeric value.

Note on why this exists: ThingsBoard's built-in device connectivity/inactivity tracking
does **not** require a dedicated heartbeat key — any telemetry message from a device
(gateway-connected or not) refreshes its "last activity" time, and ThingsBoard's native
Device Inactivity Alarm feature (importable as a no-code alarm rule scoped to a device
profile) can already alert on stale devices without any bridge-side logic. The
dedicated heartbeat key exists purely for dashboard readability — so a human looking at
device history sees a clearly-labeled liveness signal instead of a stray reading mixed
into a real sensor's data — not because the platform requires it for offline detection.
This distinction is worth remembering before adding more bridge-side "liveness" logic
that the platform may already provide natively. Not independently verified yet: how
ThingsBoard's native inactivity tracking behaves specifically for devices connected
through the gateway API (as opposed to devices with their own direct MQTT session) —
worth checking in the ThingsBoard UI before relying on it.

## Test data source module (implemented)

To avoid permanently embedding test-data generation in the production firmware path, a
separate, switchable test data source was implemented on the Wi-Fi ESP, driven by an
interactive console over UART0 (the same UART used for logging):

| Command | Effect |
|---|---|
| `send_test [value]` | Sends one routine reading (`record_class=state`, default value 21.5) through the full pipeline. |
| `send_heartbeat` | Sends one health/heartbeat record (`record_class=health`, `sensor_kind=unknown`, `value=1.0`), matching the shape the real pod firmware sends every 15s. |
| `loop_start [interval_ms]` | Starts auto-sending routine readings on a timer (default 5000ms), value varies 20.0–25.0 per call. |
| `loop_stop` | Stops the auto-send loop. |
| `status` | Prints MQTT connection state, total records sent, and whether the loop is running. |
| `uart_test` | Self-tests the UART frame parser (see "UART link" below) — does not touch HiveMQ/MQTT. |
| `help` | Lists all registered commands (built into ESP-IDF's `console` component). |

Both manual single-record triggering and automated periodic sending share the same
downstream call path a real UART-received record would use — switching from test data
to real UART input later should only require replacing the data-source layer
(`gateway_pipeline.c`'s test-record construction), not the processing pipeline itself
(`gateway_runtime`, `gateway_json`, MQTT publish).

**Usage**: after flashing, `idf.py -p <PORT> monitor` (or `flash monitor`) drops into
the `gw>` prompt once Wi-Fi and MQTT have connected. Type a command and press Enter.

### Known issues (low priority)

- At short loop intervals (confirmed reproducible at `loop_start 3000`), console log
  output can interleave with and corrupt interactive command input on the same UART,
  making it difficult to type a command (e.g. `loop_stop`) before the next log line
  prints. Not reproduced at `loop_start 5000` with the stop command entered immediately
  after starting. Root cause is believed to be UART output/input contention within
  `esp_console`, not a logic fault in `test_loop_stop` — confirmed working correctly in
  isolation. Not fixed; regarded as an acceptable limitation of a developer-only
  debugging tool. Possible fixes if this becomes a real problem: reduce log verbosity
  during loop mode, or add a non-UART stop trigger.
- `TBGatewayMqttClient`'s own reconnect behaviour (if the local ThingsBoard connection
  drops after initial connect) has not been specifically tested. Only the initial
  connection retry (`connect_tb_gateway()`) and the HiveMQ-side reconnect have been
  verified.

## UART link (implemented)

`uart_link.c`/`.h` implements the framing layer for the eventual Zigbee-side-gateway →
Wi-Fi-side-board link: a background FreeRTOS task reads bytes from UART1, reassembles
them into complete frames using the shared `gateway_frame.h` format (start marker,
16-byte header, CRC16), and hands successfully-decoded frames to a handler function.

**What is implemented and verified**: start-marker detection and resynchronization on
garbage bytes, header parsing, frame-length calculation from the header's declared
payload length, CRC16 validation via `st_gateway_frame_decode`, and multi-call buffering
(a frame can arrive across multiple UART reads). Verified via the `uart_test` console
command, which constructs a real 48-byte frame (16-byte header + 30-byte SiteTwin
Zigbee payload + 2-byte CRC) using the same encoder functions the Zigbee side would use,
and feeds it directly into the parser — bypassing the UART peripheral entirely, so this
validates the parsing logic independently of physical hardware or payload-format
uncertainty.

**Payload interpretation is implemented**: the Zigbee gateway sends the unchanged
30-byte SiteTwin Zigbee telemetry payload inside the shared gateway frame. On a valid
telemetry or health frame, `uart_link_handle_frame()` derives the bring-up IDs
`POD_<short-address>` and `SLOT_<slot>`, validates and deduplicates through
`st_gateway_runtime_t`, converts to JSON, and publishes through the existing MQTT path.

**What has NOT been tested, and should be before this is relied on for real hardware
integration**:

- Malformed input: a frame with a deliberately-corrupted CRC, a truncated frame (fewer
  bytes than the header declares), garbage bytes preceding a valid frame, and two frames
  arriving back-to-back in a single UART read (frame boundaries not aligned with buffer
  reads). The resync/discard logic exists in code but has not been exercised by any of
  these cases.
- No timeout exists if a partial frame is received and the rest never arrives (e.g. the
  far end disconnects mid-transmission) — the parser will simply wait indefinitely for
  more bytes, only recovering via the unrelated buffer-overflow-reset path if enough
  further data eventually arrives.
- The receive task itself has no health/liveness monitoring; `status` does not currently
  report whether it is still running.
- The physical UART peripheral has not been exercised at all — `uart_test` bypasses it
  by design. A physical loopback test (jumper wire from this board's TX pin to its own
  RX pin) or a real connection to the Zigbee-side board has not been performed.

The default baud rate and pins now match on both images, but their physical availability
must still be checked against the board pin maps before applying power.

The remaining milestone is physical verification: build and flash both images, wire the
one-way UART link, and confirm that a Zigbee health frame reaches MQTT and ThingsBoard.

## Repository layout

```
SiteTwin/
├── firmware/              (Zigbee-side firmware, maintained by the Zigbee owner)
├── gateway-wifi/           (this board's ESP-IDF project — Wi-Fi ESP firmware)
│   ├── components/sitetwin_core/   (shared library, byte-identical to Zigbee-side copy)
│   └── main/
│       ├── gateway-wifi.c          (Wi-Fi/MQTT/console/UART init, app_main) [core]
│       ├── mqtt_publish.h          (MQTT publish/status interface)          [core]
│       ├── uart_link.c/.h          (UART frame receive/parse framework)     [core, payload interp. pending]
│       ├── gateway_pipeline.c/.h   (ingest pipeline + test record generation) [mixed: pipeline=core, record gen=test]
│       ├── test_loop.c/.h          (periodic auto-send via esp_timer)       [test only]
│       ├── console_commands.c/.h   (interactive gw> prompt commands)        [test only, except `status`]
│       └── wifi_config.h           (credentials, gitignored)
└── pi-bridge/               (Raspberry Pi bridge script)
    ├── bridge.py
    ├── config.py             (credentials, gitignored)
    ├── config.example.py     (credential template, committed)
    └── requirements.txt
```

`[core]` = needed regardless of test vs. real UART input. `[test only]` = built to
support development/validation and expected to be replaced or removed once real UART
input is wired in. `gateway_pipeline.c` is split: its pipeline-invocation logic
(encode → ingest → JSON → publish) is reused by the eventual real UART path; its
test-record construction is not.

## Immediate next steps

1. **Blocked, pending Zigbee-side owner**: confirm UART payload contents. Separately,
   confirm baud rate and TX/RX pin assignment (does not require design discussion).
2. ~~Design and implement the switchable test data-source module~~ **Done.**
3. ~~Design the heartbeat/health record representation in `bridge.py`~~ **Done.**
4. ~~Harden `bridge.py`~~ **Done**: systemd service, HiveMQ reconnect, ThingsBoard
   connect retry, persistent logging.
5. ~~Decide whether to carry `quality_flags`/`sequence`/`boot_id` into ThingsBoard~~
   **Done.** Still open: request `priority` be added to `st_gateway_telemetry_to_json`
   upstream (shared code change, needs team input).
6. ~~Implement UART frame receive, parsing, and payload interpretation~~ **Done**.
7. Before relying on the UART link for real integration: physical loopback or real
   two-board test (not yet performed), and the untested edge cases listed under
   "UART link" above.
