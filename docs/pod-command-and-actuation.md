# Capability-Targeted Configuration Foundation

Status: portable implementation and target compilation only. The historical
filename is retained to preserve source-branch lineage; I2 implements no alarm
state machine and no physical actuation.

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

Command contract version 2 retains the 64-byte command and 44-byte
acknowledgement sizes. Bytes 60 and 61 now carry the capability and rule kind.
Version-1 command frames remain decodable. Legacy Environment Pod
`set_threshold` commands targeting `co2_threshold_ppm` map to the
`CO2/numeric_high_threshold` rule. Legacy `get_config` reads the same rule.

The old CO2 value/revision prefix remains in persistent state and mirrors that
rule. Host tests cover migration of a version-1 state into the version-2 rule
table and duplicate-command history. A target NVS migration adapter is not
composed in I2 and remains an integration item for the later transport layer.

## Control boundary

`silence_alarm` and `test_output` enum values remain decodable for wire
compatibility, but every I2 profile rejects them as `unsupported`. I2 has no
alarm-condition, acknowledgement, silence, output-test, LED, buzzer, or motor
state machine. GPIO19 and the shared buzzer/LED branch are never configured or
driven.

I3 composes the command/ack gateway-frame message types through the Wi-Fi
gateway, Zigbee coordinator, pod command runtime, and Pi RPC correlation layer.
This is a transport implementation only: the pod uses volatile command state,
target NVS persistence is not composed, and no deployed or physical end-to-end
claim is made. See `docs/command-transport.md`.

## Verification boundary

Host verification covers strict v1/v2 codec handling, profile capability
masks, numeric and event/state rule validation, per-rule revisions, legacy CO2
mapping, persistence migration, idempotency, reporting-policy application,
wrong-target/expiry handling, and explicit output-command rejection.

ESP32-C6 builds prove compilation only. No SCD41, SGP40, configuration command,
NVS migration, Zigbee downlink, alarm, or physical output validation is claimed.
