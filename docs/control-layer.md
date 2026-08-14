# Alarm and Gateway-State Control Layer

Status: deterministic host-tested logic and ESP32-C6 target compilation. No
physical alarm output, final-board, deployed ThingsBoard, or motor-control
claim is made.

## Pod alarm model

Each pod evaluates its capability-targeted rules against every usable sensor
reading, including readings suppressed by the telemetry reporting policy.
Numeric high/low rules transition immediately. A `state_active_value` rule uses
the capability's `event_debounce_ms` rule for both activation and clearing.
Readings marked stale, CRC-failed, out of range, missing, or otherwise invalid
cannot change an alarm condition.

An active condition receives a stable, JSON-exact instance ID. The same ID is
used for its acknowledgement and eventual clear. A later retrigger receives a
new ID. Every transition carries the rule/capability, observed value,
threshold, quality flags, configuration revision, ruleset revision, pod/boot,
and sensor sequence evidence.

Condition, acknowledgement, and silence are separate states:

- clearing a condition ends that instance and resets its acknowledgement;
- acknowledgement is persisted and never claims that a physical output
  changed;
- silence never clears or acknowledges a condition;
- silence is volatile; every pod boot publishes `alarm_silence/reboot_reset`
  with `silenced=false`;
- current pods do not advertise a verified `shared_alarm_indicator`, so
  `silence_alarm` and `test_output` are explicitly rejected as `unsupported`.

Pod command/configuration, duplicate-command history, ruleset revision, active
conditions, stable-ID counter, and acknowledgement state use the ESP-IDF
default NVS partition under namespace `st_pod_ctrl`. Version-1 CO2-only and
version-2 capability-rule blobs are accepted and upgraded in memory before the
next successful state save. Silence is intentionally absent from NVS.

## Coordinator state engine

The Zigbee coordinator uses fixed-size storage: 8 pods, 24 sensor tracks, 24
alarm-evidence records, 16 incidents, 8 samples per trend window, and 16 queued
control events. Coordinator-local receive time is authoritative for freshness.

- each numbered development pod is registered at coordinator start, so a pod
  that never reports is still observable;
- a pod becomes stale after 90 seconds without accepted telemetry (or since
  registration if none has arrived) and a further 5-second debounce;
- recovery from stale state also requires 5 seconds of fresh evidence;
- two distinct active alarm capabilities on one pod create a multi-sensor
  incident after 5 seconds; clearing below two capabilities also requires 5
  seconds;
- incidents carry the relevant observed values, capabilities, evidence count,
  and the fixed-window trend of the primary capability;
- active alarm evidence, active incidents, and the stable-ID counter are stored
  in default NVS namespace `st_gw_state`;
- after coordinator restart, persisted active incidents are republished as
  `recovered` with reason `coordinator_restart`.

The coordinator engine is deterministic and rule-derived. INA219 and ADXL345
may supply Pod 3 evidence, but no incident can actuate or interrupt a motor.

## Publication model

Control events use schema version 1 and a fixed 112-byte Zigbee/UART payload.
Pod events publish to `sitetwin/pods/{pod_id}/control`; coordinator incidents
publish to `sitetwin/gateway/incidents`. The Pi bridge projects stable
capabilities and rule definitions as ThingsBoard attributes. Revisions, alarm
condition/acknowledgement/silence transitions, and gateway incidents are
time-varying telemetry. Command results remain correlated RPC responses rather
than telemetry.

RPC methods are `get_capabilities`, `get_rule`, `set_rule`, `ack_alarm`, and
`silence_alarm`, plus the legacy CO2 `get_config`/`set_threshold` mapping.
Duplicate, timeout, late-result, and reconnect behaviour remains the bounded,
process-local exactly-once model described in `command-transport.md`.

## Hardware and inference boundary

The frozen architectural facts are SHT41, DS18B20 Type 5, CD74HC4052M96,
GPIO3 `DATA_COMMON`, and one shared buzzer/LED low-side branch on GPIO19. This
workstream does not configure those final-board paths. GPIO19 remains untouched
until the complete output schematic establishes polarity, PWM limits, and load
limits. Universal hot-swap, Wokwi/PCB work, deployment secrets, ML-driven
control, and all motor-control/current-cut behaviour remain out of scope.
