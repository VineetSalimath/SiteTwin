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
- End-to-end delivery has been verified: a locally-encoded test record travels from the
  Wi-Fi ESP through HiveMQ and the bridge script into a ThingsBoard device entity,
  visible in the UI, including all metadata fields (see "Known data loss" below —
  resolved).
- `sitetwin_core` on this board is byte-identical to the version used in the
  `sitetwin-zigbee-bringup` branch (verified via `diff -rq`), so no shared-contract drift
  exists between the two boards' firmware as of this writing.
- `bridge.py` runs as a systemd service (`sitetwin-bridge.service`): starts on boot,
  restarts automatically on failure, logs to both `journalctl` and a local file.

## Confirmed but hardcoded for now

- The Zigbee-side gateway currently synthesizes `pod_id` directly from the Zigbee short
  address (e.g. `POD_1234`) rather than through `gateway_registry` lookups. This board's
  test data source now matches that convention (see "Pod/sensor naming scheme" below).
- The 30-byte SiteTwin telemetry payload consumed by this board is currently
  hand-constructed in firmware for testing (via `st_zigbee_telemetry_encode`) rather
  than received over UART. This exercises the same downstream pipeline
  (`gateway_runtime`, `gateway_json`) that a real UART-received payload would use.
- HiveMQ and ThingsBoard credentials are stored in `wifi_config.h` (Wi-Fi ESP, excluded
  from git) and `pi-bridge/config.py` (bridge script, excluded from git). Both are
  plaintext local files, not a secrets manager — acceptable for the current project
  stage but not production-grade.

## Known data loss in the current pipeline

Tracing a single record end to end originally surfaced several places where information
was dropped rather than carried forward. Status as of this writing:

- **Resolved**: `bridge.py` previously forwarded only `pod_id`, `sensor_id`, and `value`
  into ThingsBoard, discarding `sequence`, `boot_id`, `uptime_ms`, and `quality_flags`.
  The bridge now forwards all of these, each prefixed with `sensor_id` (e.g.
  `SLOT_0_quality_flags`, `SLOT_0_sequence`) to avoid key collisions if a pod reports
  multiple sensors. Verified end to end: all five telemetry keys appear correctly in
  ThingsBoard's Latest Telemetry panel for a test device.
- **Still open**: `st_gateway_telemetry_to_json` (shared firmware code, not modified on
  this board) omits the `priority` field. Downstream consumers still cannot distinguish
  routine state from event- or health-priority records. This requires a change to shared
  code and should be raised with the team rather than patched locally.
- **Still open**: the ThingsBoard telemetry timestamp is set to bridge processing time
  (wall-clock time when `bridge.py` handles the message), not a timestamp derived from
  the pod's `uptime_ms`. No uptime-to-wall-clock mapping exists yet on either board.

## Open decisions — do not hard-code further assumptions

### UART payload format between the two ESP32 boards — Blocked, pending confirmation

`SITETWIN_ZIGBEE_BRINGUP.md` confirms the Zigbee-side gateway is responsible for Zigbee
reception and validation only, and that "the planned gateway-to-server ESP remains
responsible for the later UART frame and JSON conversion." This places JSON conversion
on this board, consistent with the current implementation. However, the exact contents
of the UART frame payload are not yet finalized. The strongest available evidence
(the `gateway_runtime_ingest_zigbee_source` function signature, which accepts a raw
30-byte Zigbee payload plus a source address) suggests the UART payload will likely be
the validated 30-byte SiteTwin payload plus its originating short address, but this has
not been confirmed with the Zigbee-side owner and should not be hard-coded into a real
UART receive path yet.

**Decision for now**: defer real UART implementation until this is confirmed. Continue
building out everything downstream of it (this board's own test data source, bridge
hardening, ThingsBoard-side data handling) against synthetic data in the meantime, since
all of that work is reusable once the real UART path lands — only the data-source layer
will need to change.

### Pod/sensor naming scheme — Resolved

Decided: standardize on the Zigbee-side gateway's technical identifier scheme
(`POD_1234`, `SLOT_0`) rather than introducing a human-readable naming/mapping layer
(`ENV_01`, `env_temperature`). Rationale: a pod's identity (tied to its Zigbee address)
is not strictly bound to its current role or sensor configuration — the same physical
board could be repurposed with a different sensor module — so encoding role semantics
into the identifier itself is misleading. If a human-readable label is needed later
(e.g. for dashboard presentation), it should be added as a separate attribute, not
baked into the identifier.

This board's test data source (`gateway_pipeline.c`) has been updated to match:
`TEST_POD_ID`/`TEST_SENSOR_ID` now default to `POD_1234`/`SLOT_0`. Verified end to end —
ThingsBoard correctly auto-creates a `POD_1234` device with a `SLOT_0` telemetry key.

### Heartbeat / health record handling — Still open

Pod health frames (`record_class = HEALTH`, `sensor_kind = UNKNOWN`, `value = 1.0`) are
already being sent by the Zigbee-side pod image every 15 seconds as a bring-up signal.
These should not be silently dropped once real UART integration begins — they are a
meaningful liveness signal — but forwarding them into ThingsBoard as an ordinary
telemetry key named after an "unknown" sensor kind would be misleading. A dedicated
representation (e.g. a `last_seen` or heartbeat-specific field) is planned but not yet
designed or implemented. Full validation is blocked on real UART data; a synthetic
health record could be used to validate the design in the meantime.

## Test data source module (implemented)

To avoid permanently embedding test-data generation in the production firmware path, a
separate, switchable test data source was implemented on the Wi-Fi ESP:

- **`gateway_pipeline.c`**: owns the `st_gateway_runtime_t` instance, registers a test
  node in the gateway registry, and exposes `gateway_pipeline_send_test_record(value)`
  — encodes a record, runs it through the full ingest → validate → JSON pipeline, and
  publishes it over MQTT. Tracks a running sent-count.
- **`test_loop.c`**: wraps an `esp_timer` to call the above periodically, with a
  configurable interval. Values vary slightly per call (20.0–25.0 range) rather than
  repeating a fixed value.
- **`console_commands.c`** + ESP-IDF's `console` component: exposes an interactive
  `gw>` prompt over the same UART used for logging, with commands `send_test [value]`,
  `loop_start [interval_ms]`, `loop_stop`, and `status`.

Both manual single-record triggering and automated periodic sending share the same
downstream call path a real UART-received record would use — switching from test data
to real UART input later should only require replacing the data-source layer, not the
processing pipeline.

### Known issues (low priority)

- At short loop intervals (confirmed reproducible at `loop_start 3000`), console log
  output can interleave with and corrupt interactive command input on the same UART,
  making it difficult to type a command (e.g. `loop_stop`) before the next log line
  prints. Not reproduced at `loop_start 5000` with the stop command entered immediately
  after starting. Root cause is believed to be UART output/input contention within
  `esp_console`, not a logic fault in `test_loop_stop` — confirmed working correctly in
  isolation. Possible fixes if this becomes a real problem: reduce log verbosity during
  loop mode, or add a non-UART stop trigger (e.g. a physical button or a timeout-based
  auto-stop). Not fixed; regarded as an acceptable limitation of a developer-only
  debugging tool.
- `TBGatewayMqttClient`'s own reconnect behaviour (if the local ThingsBoard connection
  drops after initial connect) has not been specifically tested. Only the initial
  connection retry (`connect_tb_gateway()`) and the HiveMQ-side reconnect have been
  verified.

## Repository layout

```
SiteTwin-main/
├── firmware/              (Zigbee-side firmware, maintained by the Zigbee owner)
├── gateway-wifi/           (this board's ESP-IDF project — Wi-Fi ESP firmware)
│   ├── components/sitetwin_core/   (shared library, byte-identical to Zigbee-side copy)
│   └── main/
│       ├── gateway-wifi.c          (Wi-Fi/MQTT/console init, app_main)
│       ├── gateway_pipeline.c/.h   (test record generation + full ingest pipeline)
│       ├── test_loop.c/.h          (periodic auto-send via esp_timer)
│       ├── console_commands.c/.h   (interactive gw> prompt commands)
│       └── wifi_config.h           (credentials, gitignored)
└── pi-bridge/               (Raspberry Pi bridge script)
    ├── bridge.py
    ├── config.py             (credentials, gitignored)
    ├── config.example.py     (credential template, committed)
    └── requirements.txt
```

## Immediate next steps

1. **Blocked, pending Zigbee-side owner**: confirm UART payload contents.
2. ~~Design and implement the switchable test data-source module~~ **Done.**
3. Design the heartbeat/health record representation in `bridge.py`. Cannot be fully
   verified until real UART data is available; may implement against synthetic health
   records in the meantime.
4. ~~Harden `bridge.py`~~ **Done**: systemd service (auto-start, auto-restart on
   failure), HiveMQ reconnect handling, ThingsBoard connect retry, persistent logging.
5. ~~Decide whether to carry `quality_flags`/`sequence`/`boot_id` into ThingsBoard~~
   **Done** — see "Known data loss" above. Still open: request `priority` be added to
   `st_gateway_telemetry_to_json` upstream (shared code change, needs team input).
6. Only after (1) is resolved: implement real UART reception on this board using
   `gateway_frame_decode`.