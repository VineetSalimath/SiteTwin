# Capability-Targeted Configuration and Logical Alarm Control

Status: portable implementation, target NVS composition, deterministic host
tests, and target compilation only. The historical filename is retained to
preserve source-branch lineage. C1 implements logical alarm state, never
physical actuation.

## Scope

I2 replaces the Environment-only global threshold model with a bounded,
versioned rule table. Every rule is keyed by:

- telemetry capability (`st_sensor_kind_t`);
- rule kind;
- typed numeric value encoded as a finite `float`;
- revision local to that capability/rule pair.

The table supports numeric sensors and state/event sensors without placing
policy inside low-level drivers.

| Rule kind | Intended capability |
| --- | --- |
| `numeric_high_threshold` / `numeric_low_threshold` | Numeric observations such as CO2, VOC, temperature, current, or vibration |
| `report_deadband` | Numeric reporting policy |
| `report_min_interval_ms` / `report_max_interval_ms` | Numeric reporting cadence |
| `event_debounce_ms` | Motion/contact event inputs |
| `event_retrigger_ms` | Motion event suppression |
| `state_active_value` | Boolean motion/contact polarity semantics |

Rules reject unsupported profile/capability combinations, non-finite values,
invalid bounds, inverted low/high or minimum/maximum pairs, and revisions that
are not exactly the stored revision plus one.

## Profile capabilities

- `POD_1`: SHT41 temperature/humidity, SCD41 CO2, SGP40 VOC Index.
- `POD_2`: BH1750 illuminance, PIR motion, reed contact.
- `POD_3`: INA219 current/voltage, ADXL345 vibration, DS18B20 temperature.

Configuration capability does not imply final universal-port discovery or
physical compatibility. Fixed profile wiring remains the only target
composition in I2.

## Wire compatibility

Command contract version 3 uses a 72-byte command and 60-byte acknowledgement
to carry an exact alarm-instance ID, capability mask, and ruleset revision.
Version-1 and version-2 command/result frames remain decodable. Legacy Environment Pod
`set_threshold` commands targeting `co2_threshold_ppm` map to the
`CO2/numeric_high_threshold` rule. Legacy `get_config` reads the same rule.

The old CO2 value/revision prefix remains in persistent state and mirrors that
rule. Host tests cover version-1 and version-2 migration into the C1 state,
including duplicate-command history. The target adapter stores command/config,
rule revision, alarm-instance counter, active conditions, and acknowledgement
state in default NVS namespace `st_pod_ctrl`.

## Control boundary

Usable sensor readings evaluate numeric high/low and state-active rules before
telemetry suppression. A condition receives a stable instance ID through its
active, acknowledged, and cleared transitions; retriggering receives a new ID.
Acknowledgement is persistent and independent of the condition. Silence is
independent and deliberately volatile: a reboot cancels it and publishes
`reboot_reset`.

Every current profile advertises `shared_alarm_indicator_verified=false`, so
`silence_alarm` and `test_output` are rejected as `unsupported`. GPIO19 and the
shared buzzer/LED branch are never configured or driven. Acknowledgement never
claims output success. Pod 3 alarms are monitoring evidence only and cannot
control or cut a motor.

C1 composes commands, results, and control-state events through the Wi-Fi
gateway, Zigbee coordinator, and Pi RPC/projection layer. See
`docs/command-transport.md` and `docs/control-layer.md`.

## Verification boundary

Host verification covers strict v1/v2/v3 codec handling, profile capability
masks, numeric and event/state rule validation, per-rule revisions, legacy CO2
mapping, persistence migration, idempotency, reporting-policy application,
condition clear/retrigger, acknowledgement recovery, state debounce, volatile
silence expiry/reboot, wrong-target/expiry handling, and explicit
output-command rejection.

ESP32-C6 builds prove compilation only. No C1 NVS migration, alarm threshold,
Zigbee control-event, ThingsBoard projection, or physical output validation is
claimed on hardware.
