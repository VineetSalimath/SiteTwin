# Pod Command and Local Actuation Foundation

Status: implemented, host-tested, and ESP32-C6 compile-tested; no physical command or actuator test has been run.

## Scope and evidence boundary

This milestone adds a shared bidirectional command path and integrates it with the Environment Pod profile. It does not validate Activity/Access or Equipment Pod wiring, sensors, GPIOs, actuators, or Zigbee behaviour. Those profiles expose no command capabilities and are marked `pending_hardware_verification` until their individual schematics and boards are checked.

The existing telemetry path remains unchanged. Commands travel in the reverse direction and terminal acknowledgements return upstream:

`ThingsBoard RPC -> Pi bridge -> HiveMQ -> Wi-Fi ESP -> framed UART -> Zigbee coordinator -> custom-cluster downlink -> Environment Pod -> command runtime/NVS/local output -> custom-cluster acknowledgement -> coordinator -> framed UART -> Wi-Fi ESP -> HiveMQ -> Pi bridge -> ThingsBoard RPC reply`

## Command contract

The portable contract is `st_command_t` in `components/sitetwin_core/include/sitetwin/command.h`. UART and Zigbee use the same fixed 64-byte little-endian payload.

| Field | Meaning |
| --- | --- |
| `command_id` | Non-zero idempotency key. The Pod persists the eight most recent results and never repeats a stored command. |
| `target_pod_id` | Stable logical target. This milestone enables only `ENV_01`. |
| `command_type` | `set_threshold`, `silence_alarm`, `test_output`, or `get_config`. |
| `target` | `co2_threshold_ppm`, `alarm`, `led`, `buzzer`, or `config`. |
| `value` | Numeric configuration value; currently used by `set_threshold`. |
| `duration_ms` | Bounded duration for silence or output test. |
| `issued_at_ms` / `expires_at_ms` | Server wall-clock audit timestamps. The Pi rejects/times out against this deadline. |
| `valid_for_ms` | Local execution window, 1-60 seconds. The Pod starts it on Zigbee receipt because it has no absolute clock. |
| `config_revision` | `set_threshold` requires current revision + 1. Other commands accept zero or current revision. |
| `source` | `thingsboard` or local maintenance. Only ThingsBoard is wired in this milestone. |

MQTT JSON uses the same names, with `value` and `duration_ms` nested inside `parameters`.

```json
{
  "schema_version": 1,
  "command_id": 101,
  "target_pod_id": "ENV_01",
  "command_type": "set_threshold",
  "target": "co2_threshold_ppm",
  "parameters": {"value": 1200, "duration_ms": 0},
  "issued_at_ms": 1786380000000,
  "expires_at_ms": 1786380010000,
  "valid_for_ms": 10000,
  "config_revision": 2,
  "source": "thingsboard"
}
```

## Acknowledgement contract and state semantics

`st_command_ack_t` is a fixed 44-byte little-endian payload.

| Field | Meaning |
| --- | --- |
| `command_id`, `pod_id` | Correlate result with request and target. |
| `status` | `queued`, `delivered`, `executed`, `rejected`, `expired`, `duplicate`, or `failed`. |
| `reason` | Stable code such as `wrong_target`, `unsupported`, `revision_conflict`, `out_of_bounds`, `queue_full`, `transport_failed`, or `timeout`. |
| `applied_config_revision` | Revision after processing. |
| `timestamp_ms` | Pod monotonic time for Pod results; gateway monotonic time for intermediate status. |
| `config_value` | Current CO2 threshold, including `get_config` results. |

State mapping:

- `queued`: Wi-Fi ESP validated MQTT and wrote a CRC-protected UART frame. It is not execution and does not close RPC.
- `delivered`: reserved for a future APS delivery-confirmation hook; deliberately not emitted now.
- `executed`, `rejected`, `expired`, `duplicate`: emitted by the Pod and terminal.
- `failed`: known transport, queue, persistence, or Pi timeout failure and terminal.

The Pi closes ThingsBoard RPC only for a terminal state. Its timer returns `failed/timeout` if no terminal Pod acknowledgement arrives.

## Environment Pod capabilities

| Command | Target | Bounds/behaviour |
| --- | --- | --- |
| `set_threshold` | `co2_threshold_ppm` | 400-5000 ppm; revision increments by one; persisted. Default 1000 ppm. |
| `silence_alarm` | `alarm` | 1-300000 ms. Mutes only buzzer; LED remains while valid CO2 is unsafe. Volatile across reboot. |
| `test_output` | `led`, `buzzer`, or `alarm` | 1-5000 ms; no raw GPIO access. A real alarm remains authoritative. |
| `get_config` | `config` | Returns current threshold and revision. |

Activity/Access and Equipment profiles have empty capability masks. Their mappings remain pending physical wiring verification and are neither flash-ready nor hardware-validated.

## Alarm safety model

- Environment sensors remain on I2C controller 0, SDA GPIO6 and SCL GPIO7.
- Alert LED is GPIO4. Passive buzzer is GPIO5 through LEDC PWM at 1 kHz.
- Only a finite SCD41 reading with `ST_QUALITY_VALID` and none of `WARMING_UP`, `STALE`, `CRC_FAILED`, `OUT_OF_RANGE`, or `SENSOR_MISSING` can change the condition.
- A valid value above threshold activates it; a later valid value at or below threshold clears it.
- Invalid data cannot start a false alarm and cannot clear an existing unsafe condition.
- Raising the threshold remotely does not clear an active alarm; the next valid local CO2 observation must confirm the clear condition.
- While unsafe, LED and buzzer follow a non-blocking one-second ON / one-second OFF cadence. Silence suppresses only buzzer.
- MQTT, UART, and Zigbee callbacks parse or enqueue; they never drive GPIOs.
- NVS stores configuration and eight command results. Output tests are recorded before activation so reboot replay cannot repeat a pulse.

## Transport and limitations

- Gateway frames add command and command-ack types while retaining the header and CRC16.
- UART1 is bidirectional at 115200: coordinator TX GPIO4 -> Wi-Fi RX GPIO5 and Wi-Fi TX GPIO4 -> coordinator RX GPIO5, with shared ground.
- Zigbee cluster `0xFC00` retains telemetry `0x01` and adds Pod command `0x02` and acknowledgement `0x03`.
- The coordinator learns the Environment Pod short address from accepted telemetry. A command before that fails; no short address is compiled in.
- Environment slots 0-3 are presented upstream as `ENV_01`. Multi-Pod stable commissioning remains future work.
- Sleepy end devices may delay or lose a downlink. No false `delivered` state is emitted. Later bench work should keep the Pod USB-powered and awake.
- The Pod lacks wall time. Pi enforces absolute RPC expiry; Pod enforces `valid_for_ms` from local receipt. Preventing execution after a long Zigbee indirect-queue delay needs network time or APS-expiry support later.
- Return UART, Zigbee downlink, GPIO, PWM, NVS reboot, and live RPC timing are compile-only, not physically verified.

## Verification status

| Area | Status | Evidence/limitation |
| --- | --- | --- |
| Command/ack codecs and gateway framing | Host-tested | Strict C11 build and binary round trips. |
| Validation/capability/expiry/revision | Host-tested | Wrong target, unsupported profile, bounds, revision and expiry covered. |
| Idempotency/persistence model | Host-tested; adapter compiled | Replay across fake reload; real NVS not run. |
| Alarm/silence/output bounds | Host-tested; adapter compiled | Quality gating, valid-only clear, cadence, 300 s silence and 5 s test. |
| Pod command/ack Zigbee | ESP32-C6 compiled only | No physical downlink. |
| Coordinator UART/Zigbee return path | ESP32-C6 compiled only | No physical bidirectional UART. |
| Wi-Fi MQTT/UART/ack path | ESP32-C6 compiled only | Built with temporary gitignored compile credentials; no broker run. |
| Pi bridge | Syntax-checked only | No live ThingsBoard/HiveMQ run on this branch. |
| Pod 2/3 | Unverified stubs | No mappings, sensors, actuation, or hardware claim. |

## Later Environment Pod bench plan

Do not begin until wiring is checked unpowered and serial ports are known.

```powershell
# Coordinator
cd firmware
idf.py -B build_gateway -DSITETWIN_DEVICE_ROLE=gateway -DIDF_TARGET=esp32c6 build
idf.py -p <COORDINATOR_PORT> -B build_gateway flash monitor

# USB-powered Environment Pod
idf.py -B build_pod -DIDF_TARGET=esp32c6 build
idf.py -p <ENVIRONMENT_POD_PORT> -B build_pod flash monitor

# Wi-Fi ESP, after creating gitignored gateway-wifi/main/wifi_config.h
cd ..\gateway-wifi
idf.py -B build -DIDF_TARGET=esp32c6 build
idf.py -p <WIFI_GATEWAY_PORT> -B build flash monitor
```

Start the Pi bridge and wait for valid `ENV_01` telemetry so the coordinator learns the short address. Then retain serial, MQTT, and UI evidence for:

1. `get_config`: `{"config_revision":0,"valid_for_ms":10000}`.
2. `set_threshold`: `{"target":"co2_threshold_ppm","value":1200,"config_revision":<current+1>,"valid_for_ms":10000}`.
3. `silence_alarm`: `{"duration_ms":10000,"config_revision":<current>,"valid_for_ms":10000}` during a real valid high-CO2 condition; LED must continue.
4. `test_output`: `{"target":"led","duration_ms":3000,"config_revision":<current>,"valid_for_ms":10000}`; test buzzer only after wiring verification.
5. Replay the same command ID at MQTT level; expect `duplicate` and no repeated output.
6. Exercise the local expiry check with `valid_for_ms: 1` while the Pod is awake; expect a Pod `expired` result or Pi timeout and verify that no output occurred. Do not use old wall-clock timestamps as a Pod-expiry test: the Pod has no wall clock and rebases the local window on receipt.
7. Send thresholds 399 and 5001; expect `rejected/out_of_bounds` and unchanged revision.
8. Lose UART/Zigbee; expect timeout/failure, not a fabricated `executed`. Restore/rejoin and watch explicitly for a late queued command: execution after the Pi deadline is the documented APS-expiry gap and must block release. Then wait for telemetry address learning and retry with a new ID.
9. Reboot Pod; use `get_config` and replay a pre-reboot ID to verify NVS configuration and idempotency.

Record exact commit, board revisions, power, wiring, timestamps, status transitions, output behaviour, and deviations. Compilation is not hardware evidence.
