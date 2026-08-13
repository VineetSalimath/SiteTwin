---
title: Sensor Runtime Foundation Implementation
project: SiteTwin
status: Implemented, host/compile validated, and subsequently physically validated
date: 2026-08-06
tags:
  - sitetwin
  - firmware
  - sht41
  - sensors
  - validation
---

# Sensor Runtime Foundation Implementation

Related: [[SENSOR_PROTOTYPE_PORTING_ASSESSMENT]], [[Firmware Architecture]], [[Pod and Sensor Strategy]], [[Hardware Bring-Up and Open Decisions]]

## Outcome

Status addendum (2026-08-13): the SHT41 foundation described here was
subsequently incorporated into Pod 1 and physically exercised through the full
gateway/server path. This document retains the original 2026-08-06 milestone
evidence below, but physical validation is no longer outstanding.

The existing single-sample contract was retained through bounded logical channel adapters sharing one physical sensor context. This enables SHT41 temperature and humidity without changing the proven Zigbee, UART, JSON, or MQTT contracts.

Implemented flow:

`SHT41 physical context -> temperature/humidity logical adapters -> existing sensor registry -> existing reporting policy -> existing priority queue -> existing 30-byte Zigbee transport`

Option C was selected because it preserves existing single-output fake and real drivers, keeps independent sequence/reporting state per logical channel, avoids a shared batch-contract migration during parallel team development, and makes physical-module lifetime explicit. Independent adapters without a group owner were rejected because grouped detach/re-probe could be inconsistent. A bounded multi-observation return was deferred because it would force shared-header, fake-driver, registry, and Yicheng-driver migrations.

## Ownership and lifetime

The ESP-IDF composition owns these static objects for the pod image lifetime:

- ESP-IDF I2C device/HAL context;
- `st_sht41_t` physical sensor context;
- `st_module_instance_t` group owner;
- `st_pod_runtime_t`.

The module instance owns the bounded logical adapter objects. Registry entries contain copies of `st_sensor_driver_t` whose context pointers refer to those owned adapters. Therefore, the module instance must outlive every registry attachment.

Detach follows this order:

1. mark every channel adapter inactive;
2. reset the physical driver context;
3. detach every logical registry slot;
4. mark the module instance detached.

An old copied adapter driver returns `ST_DRIVER_NOT_PRESENT` after step 1. Reattachment activates the same stable adapter storage only after the group is attached again, so the registry never retains a pointer to stack or temporary storage.

## Shared acquisition and cache

The first due adapter starts the high-precision SHT41 command. While conversion is pending, both adapters return `ST_DRIVER_RETRY`. Once the maximum conversion interval has elapsed, the first adapter reads and validates the six-byte result; the second adapter consumes the cached result without another sensor command or read.

The physical context records:

- command/start attempt time;
- completion/acquisition time;
- cache expiry time;
- temperature and humidity from the same response;
- last valid values and their shared acquisition time;
- current failure/quality state.

A cached result is reused until `cache_validity_ms` expires. A due poll after that window starts a new physical measurement. The default development settings are a 1000 ms physical sample interval and a 250 ms cache window, both externally configurable. The reporting policy remains independently configurable through `st_reporting_policy_set_rule()`; the established development defaults remain 0.2 C and 1% RH deadbands, 30 seconds minimum reporting, and 15 minutes maximum silence.

Both fresh logical samples carry the same `acquired_at_ms`. The existing registry sequence counters remain independent because each adapter occupies its own logical registry slot. The gateway receives normal temperature and humidity records in sensor slots 0 and 1.

## Timestamp and stale-value semantics

`st_driver_sample_t` now has optional `acquired_at_ms` and `acquired_at_valid` fields while retaining the existing `probe()` and `sample()` signatures. Drivers that do not provide a timestamp remain source-compatible because the zero-initialized registry falls back to scheduler time. The fake driver now supplies its actual host-test acquisition time.

For a valid SHT41 result, the telemetry `uptime_ms` is the shared completion/acquisition time. After a later failed acquisition, both channels may carry the last finite value, but their `uptime_ms` remains the original last-valid acquisition time and quality becomes `STALE` plus the specific failure flag. It is never replaced by the fault-attempt time.

The fixed radio record has only one timestamp. The fault-attempt time remains available inside the SHT41 context through `st_sht41_last_attempt_at_ms()` but cannot be added to the radio record without a separately approved payload change. At the backend, gateway receipt time can bound when the fault report arrived, but it is not a replacement for a transmitted fault timestamp. This is the smallest compatible handling that preserves the meaning of the existing on-wire timestamp.

Before the first valid finite acquisition, CRC, range, I/O, or missing-sensor failure emits no measurement. It does not invent zero, NaN, or default environmental compensation. Once a valid acquisition exists:

- CRC failure produces the last values with `STALE | CRC_FAILED`;
- range failure produces the last values with `STALE | OUT_OF_RANGE`;
- the first disappearance produces the last values with `STALE | SENSOR_MISSING`;
- a repeated disappearance stops emitting cached values and returns both channels to probe state;
- recovery emits fresh values with `VALID` and clears stale/failure flags.

Quality transitions bypass numeric deadband suppression through the existing reporting policy.

## Quality classification

The registry normalizes the following flags as incompatible with `ST_QUALITY_VALID`:

- `ST_QUALITY_CRC_FAILED`;
- `ST_QUALITY_OUT_OF_RANGE`;
- `ST_QUALITY_SENSOR_MISSING`;
- `ST_QUALITY_STALE`.

Compatible informational flags do not by themselves invalidate the associated measurement:

- `ST_QUALITY_BATTERY_LOW`;
- `ST_QUALITY_MOUNTING_CHANGED` when the measurement remains physically meaningful.

Context-dependent flags require the sensor/processing policy to decide whether the numeric value remains valid:

- `ST_QUALITY_WARMING_UP`: current fake-sensor behavior permits a valid but warming observation;
- `ST_QUALITY_COMPENSATION_UNAVAILABLE`: valid only under a separately approved fallback; no SGP40 fallback is implemented here;
- `ST_QUALITY_CLIPPED`: may be useful as a bounded/event observation but must be excluded from ordinary quantitative inference unless the processing policy explicitly supports clipping.

The normalizer clears `VALID` for the four unconditionally incompatible flags and preserves every other flag. Host tests exercise contradictory input such as `VALID | CRC_FAILED | BATTERY_LOW` and prove that invalid `VALID` is removed while informational metadata remains.

## Backend and ML requirement

Live backend state and future ML inference must exclude records whose `VALID` bit is clear. In particular, `STALE`, `CRC_FAILED`, `OUT_OF_RANGE`, and `SENSOR_MISSING` values are diagnostic continuity records, not new live observations. An offline model may consume them only through an explicitly designed missingness/fault feature path. Cached values must not silently enter ordinary live inference as fresh sensor data.

## SHT41 protocol implementation

The portable driver follows the official [Sensirion SHT4x datasheet](https://sensirion.com/media/documents/33FD6951/6555C40E/Sensirion_Datasheet_SHT4x.pdf):

- high-precision measurement command `0xFD`;
- 9 ms scheduled wait, covering the documented 8.3 ms maximum;
- heater commands are neither exposed nor issued;
- CRC-8 polynomial `0x31`, initialization `0xFF`, no reflection/final XOR;
- temperature conversion `-45 + 175 * raw / 65535` C;
- humidity conversion `-6 + 125 * raw / 65535` %RH;
- temperature operating-range check from -40 C to 125 C;
- physical humidity-range check from 0% to 100% RH;
- serial-number command `0x89` for probe/module identity.

The I2C address is configurable; the development default is `0x44`. Controller, SDA, SCL, clock, timeout, internal pull-up selection, sample interval, and cache window are Kconfig options. No Arduino or Adafruit dependency is used.

## Task and bus safety

The portable physical-module interface requires serialized `probe`, `acquire`, `read_channel`, and `reset` calls. The current ESP-IDF composition satisfies this by making the single pod telemetry/acquisition task the only caller of `st_pod_runtime_tick()` and the only owner of the SHT41/module context. Adapter calls are sequential inside one registry tick.

The ESP-IDF I2C driver owns hardware transaction synchronization. The portable context intentionally contains no FreeRTOS mutex. If a later architecture permits another task to access the same physical module, the composition must place one shared lock around the complete acquire/read operation; locking only individual I2C calls would still permit state/cache races.

ESP-IDF v5.5 can return `ESP_ERR_INVALID_STATE` for a synchronous transaction that received a NACK. On that error only, the HAL performs an address probe to distinguish `NOT_PRESENT` from a different controller-state failure; it does not treat every controller error as removal.

## Hot-swap preparation and limitations

Hardware-contract update (2026-08-14): the final four-port board now fixes
DS18B20 as Type 5 with the 10 kOhm ID code, CD74HC4052M96 as the non-I2C DATA
mux, and GPIO3 as `DATA_COMMON`. These facts supersede the earlier statement
that the mux topology was open, but they do not supply a runtime
implementation. GPIO19 is one shared buzzer/LED low-side branch and remains
unimplemented pending polarity, PWM, and load validation. Motor actuation is
not a SiteTwin pod capability.

The board-intent interface defines port count, presence detection, power, module-ID read, bus selection/enable/disable, and fault-clear operations without defining GPIOs, mux arithmetic, resistor bands, power polarity, or final port count. The physical lifecycle enum records the planned states, but no unconfirmed electrical detection logic is implemented.

Current removal is detected through failed sensor communication or explicit
module-instance detach. It is not immediate electrical hot-swap detection.
Mux selection, calibrated ADC-ID bands, power switching, duplicate-address
handling, and change-interrupt behavior are not implemented by this milestone.

## Host verification coverage

Host tests cover:

- datasheet CRC example and conversion endpoints;
- first failure without an invented value;
- one measurement command/read shared by both logical channels;
- identical channel acquisition timestamps;
- a new command after cache expiry;
- independent logical sequences/reporting state;
- CRC and out-of-range stale continuity;
- missing-sensor stale transition followed by re-probe;
- recovery to fresh valid observations;
- immediate quality-transition reporting with unchanged numeric values;
- removal, old-adapter invalidation, reattachment, and 32 attach/detach cycles;
- normalization of all four invalid flags;
- preservation of compatible informational flags;
- existing queue, 30-byte Zigbee, CRC16 UART, JSON, gateway, and randomized stress tests.

## Development wiring assumptions

Physical SHT41 validation has not yet been performed by this implementation task. The default configuration follows the existing Pod 1 hardware-validation prototype and must be verified before energizing final hardware:

| SHT41 connection | ESP32-C6-DevKitC-1 development assumption |
| --- | --- |
| VDD/VIN | 3.3 V, subject to the actual breakout-board labeling |
| GND | GND |
| SDA | GPIO 6 |
| SCL | GPIO 7 |
| Address | `0x44` |

The default configuration does not enable the ESP32-C6 internal pull-ups. Confirm that the breakout/module supplies suitable pull-ups or fit external pull-ups appropriate to the measured bus before testing. Do not assume final connector, mux, power-control, or PCB GPIO assignments from this development wiring.

## Build and flash commands

Use ESP-IDF v5.5.4 with the repository-pinned Zigbee dependency, then configure the development pins if they differ:

```powershell
cd firmware
idf.py -B build_pod -DSITETWIN_DEVICE_ROLE=pod menuconfig
idf.py -B build_pod -DSITETWIN_DEVICE_ROLE=pod build
idf.py -p COM_POD -B build_pod flash monitor
```

Coordinator:

```powershell
cd firmware
idf.py -B build_gateway -DSITETWIN_DEVICE_ROLE=gateway build
idf.py -p COM_COORDINATOR -B build_gateway flash monitor
```

Replace `COM_POD` and `COM_COORDINATOR` with the verified ports. Do not commit generated `sdkconfig`, build, managed-component, binary, or local-secret files.

## Physical verification procedure

1. Confirm the exact SHT41 breakout/module part, voltage labeling, address, and pull-ups with power disconnected.
2. Wire 3.3 V, ground, SDA GPIO 6, and SCL GPIO 7 only if they match the configured development setup.
3. Build/flash the coordinator and confirm it forms or restores the SiteTwin Zigbee network.
4. Build/flash the pod and confirm the `SHT41 runtime ready` log shows the intended controller, pins, and address.
5. Confirm the pod joins the coordinator.
6. Observe pod logs for `sht41_temperature` and `sht41_humidity` sends and coordinator logs for accepted slot 0 and slot 1 telemetry.
7. Compare temperature and humidity against a plausible ambient reference; record raw logs without treating agreement as calibration proof.
8. Hold conditions steady and verify routine reports are suppressed while the 15-minute heartbeat remains possible.
9. Introduce a controlled temperature/humidity change and verify deadband/minimum-interval behavior.
10. Disconnect the SHT41 with power removed, then repeat under an approved hot-swap-safe bench method; verify stale/missing transition and re-probe without invented zero values.
11. Reattach and verify fresh `VALID` records resume with new acquisition timestamps.
12. Run the full pod-to-coordinator-to-UART-to-Wi-Fi-to-MQTT path and record that separately as physical end-to-end evidence.

Do not claim physical or end-to-end validation until these steps have actually been performed and logs/evidence have been retained.

## Compatibility statement

No changes were made to:

- `st_sensor_reading_t` or `st_telemetry_record_t`;
- the fixed 30-byte Zigbee payload layout;
- custom cluster `0xFC00` or command `0x01`;
- CRC16 UART framing;
- canonical JSON fields;
- MQTT schema;
- gateway processing/deduplication;
- Yicheng-owned final sensor drivers.

The fake-driver constructor and `probe()`/`sample()` signatures remain compatible. The only shared sample-result extension is the optional acquisition timestamp used before the registry creates the unchanged telemetry record.

## Evidence record

| Milestone | Branch | Commit | Date | Hardware | Procedure | Result | Limitation | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Option C foundation and SHT41 | `feature/sensor-runtime-foundation` | Exact foundation commit recorded in Git history and final handoff | 2026-08-06 | Host compiler and ESP32-C6 target build only | Host suite; whitespace check; ESP-IDF pod/coordinator/Wi-Fi builds | Host suite passed (384 stress records); pod, coordinator, and Wi-Fi gateway images compiled | No physical SHT41 or physical end-to-end run | Host-test output, ignored build logs/artifacts, this note |
