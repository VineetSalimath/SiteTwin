---
title: SGP40 Milestone 2 Assessment
project: SiteTwin
status: Implemented, host-tested, and target-compiled; physical validation pending
date: 2026-08-06
branch: feature/environment-pod-sensors
foundation: 00d49710b6af92dc5eb3e024e8b0075ca47ef9e3
---

# SGP40 Milestone 2 Assessment

Related: [[SCD41_MILESTONE_1_ASSESSMENT]], [[SENSOR_RUNTIME_FOUNDATION_IMPLEMENTATION]], [[Firmware Architecture]], [[Pod and Sensor Strategy]]

## Scope and contract decision

Milestone 2 will add SGP40 as the third physical Environment Pod module while retaining Option C, the existing registry and reporting policy, and one unchanged SiteTwin telemetry record per logical reading. No protected shared contract change is required. The public `probe()` and `sample()` signatures, 30-byte Zigbee payload, cluster `0xFC00` command `0x01`, UART frame, canonical JSON, MQTT schema, and gateway behavior remain unchanged.

The SGP40 has two distinct signal layers:

- `SRAW_VOC` is the humidity-compensated 16-bit sensor signal used as algorithm input;
- VOC Index is the processed 1-to-500 logical SiteTwin observation.

Only VOC Index will be registered and transported because the approved contract already defines `ST_SENSOR_VOC_INDEX`/`ST_UNIT_INDEX` and has no raw-VOC kind. The most recent raw signal remains available through a driver diagnostic accessor and host tests. Adding a raw telemetry kind would be a transport-contract change without a demonstrated system requirement, so it is not proposed here.

## Official protocol and algorithm

Protocol values and timing come from the official [Sensirion SGP40 datasheet](https://sensirion.com/media/documents/296373BB/6203C5DF/Sensirion_Gas_Sensors_Datasheet_SGP40.pdf) and are cross-checked against the official [Sensirion Raspberry Pi SGP40 driver](https://github.com/Sensirion/raspberry-pi-i2c-sgp40). VOC Index processing will vendor the unmodified floating-point implementation from Sensirion's official [gas-index-algorithm repository](https://github.com/Sensirion/gas-index-algorithm), revision `2ef9f13d225e8a0dedd3ff42dd229af4dbb1aae4`, under its BSD-3-Clause licence.

Relevant protocol operations are:

| Operation | Command | Required handling |
| --- | ---: | --- |
| Measure raw VOC | `0x260F` plus RH word/CRC and temperature word/CRC | Wait 30 ms; read one word plus CRC |
| Read serial number | `0x3682` | Wait 1 ms; read and CRC-check three words |
| Built-in self-test | `0x280E` | 320 ms; not run during ordinary startup |
| Turn heater off | `0x3615` | Explicit stop/detach policy only; not part of every continuous sample |

RH ticks are `RH% * 65535 / 100`; temperature ticks are `(degrees C + 45) * 65535 / 175`. Inputs are range-checked before conversion and each command argument receives Sensirion CRC-8, polynomial `0x31`, initial value `0xFF`. No default `0x8000`/`0x6666` compensation words will be substituted.

## Compensation boundary

The SGP40 low-level protocol driver must not know SHT41 commands or representation. A small sensor-specific provider callback will supply one bounded compensation snapshot:

```c
typedef struct {
    float temperature_c;
    float humidity_percent;
    uint64_t acquired_at_ms;
} st_sgp40_compensation_t;

typedef int (*st_sgp40_compensation_provider_t)(
    void *context,
    uint64_t now_ms,
    st_sgp40_compensation_t *compensation);
```

The Environment Pod provider will call a new read-only SHT41 accessor. It succeeds only when both SHT41 channels come from the same fresh, currently valid physical acquisition. Stale, missing, CRC-failed, out-of-range, expired, or never-acquired SHT41 data is rejected.

Before the first valid VOC Index, unavailable compensation produces no numeric observation. After a valid VOC Index exists, an interruption may produce the last-valid index with its original timestamp, `STALE`, and `ST_QUALITY_COMPENSATION_UNAVAILABLE`. The algorithm is reset before processing resumes because its recursive state requires an uninterrupted fixed cadence. This reset returns the channel to warm-up; no arbitrary environmental compensation is ever sent.

The SHT41-to-SGP40 provider and SGP40-specific API are additive sensor APIs, not changes to `contracts.h`, `sensor_driver.h`, `sensor_registry.h`, `pod_runtime.h`, or `reporting_policy.h`.

## Operating mode, cadence, and battery implications

The initial production mode will be continuous measurement at a one-second raw-signal and algorithm cadence. The datasheet specifies a typical 2.6 mA average operating current at 3.3 V, compared with 34 microamps typical idle current. Continuous operation is selected because Sensirion specifies a one-second VOC Index input cadence, the official algorithm defaults to and is tested at one second, and continuous operation avoids introducing the separate two-measurement 170 ms heater duty-cycle sequence before physical validation.

The protocol transaction remains non-blocking: issue the compensated measurement command, return to the scheduler, then read after at least 30 ms. The driver will anchor successive measurement commands to the required cadence and account for the registry's bounded retry phase so successfully processed raw samples remain one second apart. A missed acquisition, compensation interruption, or hard I2C fault resets volatile algorithm state rather than feeding irregular samples as though they were continuous.

Sensirion documents optional duty-cycled modes with lower power, but they add heater sequencing and have different evidence requirements. They are deferred until continuous-mode response, current, and stability are physically measured. The one-second cadence and continuous heater are development choices, not final battery results.

## Warm-up and algorithm state

The official algorithm returns zero during its 45-second initial blackout and 1-to-500 afterward. Zero will be treated as algorithm warm-up, not a valid VOC Index. Before readiness, the internal driver state and registry warming state are available for diagnostics but no misleading numeric telemetry is emitted.

Algorithm state is kept in RAM inside a processing object separate from the I2C protocol state. It is reinitialized on boot, detach, reattachment, compensation interruption, missed cadence, or acquisition failure. No algorithm state is written to NVS and no previously learned state is restored. Persistence may be evaluated later, but it is not approved in this milestone.

The planned lifecycle is:

`UNINITIALISED -> PROBING -> WAITING_COMPENSATION -> MEASURING -> ALGORITHM_WARMING -> READY`

Failures transition through `COMPENSATION_UNAVAILABLE` or `FAULT_REPROBE`; explicit detach returns to `DETACHED`. Recovery requires fresh SHT41 compensation and a new uninterrupted algorithm blackout before fresh `VALID` VOC Index output.

## Environment Pod integration

The existing shared ESP-IDF master bus will add an SGP40 device handle at configurable address `0x59`. Logical registry slots remain:

- slot 0: SHT41 temperature;
- slot 1: SHT41 relative humidity;
- slot 2: SCD41 CO2;
- slot 3: SGP40 VOC Index.

This is a logical telemetry mapping, not final physical port or mux wiring. SHT41 acquisition remains first in registry order, so an updated environment snapshot is available before the SGP40 slot is serviced in the same runtime tick where practical.

The existing provisional VOC reporting policy remains a 5-point deadband, 30-second minimum interval, and five-minute maximum silence. Sampling continues every second independently of reporting suppression, and quality transitions bypass deadband through the current policy.

## Planned host evidence

Tests will cover:

- official CRC vectors and compensation tick conversions;
- exact compensated command bytes and the absence of default fallback words;
- serial-number probe and measurement timing states;
- unavailable, stale, expired, invalid, and recovered SHT41 compensation;
- raw response CRC failure and missing sensor handling;
- one-second algorithm feeding and the 45-second blackout;
- no zero/misleading VOC Index before readiness;
- first valid VOC Index and raw/index distinction;
- stale-last-valid quality transitions;
- algorithm reset after cadence or compensation interruption;
- detach/reattach and repeated lifecycle stress;
- no regression to SHT41, SCD41, reporting, queue, payload, UART, JSON, gateway, and the existing stress harness.

Milestone completion requires strict host compilation with `-Wall -Wextra -Werror`, complete host and stress tests, `git diff --check`, Pod and coordinator builds, and a Wi-Fi gateway rebuild because it resolves the shared portable sensor component. Physical and end-to-end SGP40 validation will remain explicitly untested until Vineet flashes the Pod.

## Implemented result

Milestone 2 implements the decision above without changing the protected shared contracts. The SGP40 owns one physical driver context and one bounded logical adapter for `ST_SENSOR_VOC_INDEX`. Its protocol state machine uses `st_i2c_bus_t`; compensation crosses a narrow callback boundary backed by a read-only SHT41 snapshot accessor. The official algorithm is held in a separate processing object, so I2C protocol, compensation sourcing, VOC processing, reporting, and transport remain distinct.

The vendored Sensirion files are from gas-index-algorithm revision `2ef9f13d225e8a0dedd3ff42dd229af4dbb1aae4`, algorithm version 3.2.0. Only repository line endings were normalised; algorithm logic and declarations were not changed. `UPSTREAM.md` records provenance and the BSD-3-Clause licence is retained beside the source.

The implemented state flow is:

`UNINITIALISED -> PROBING -> WAITING_COMPENSATION -> MEASURING -> ALGORITHM_WARMING -> READY`

Unavailable compensation and acquisition failures produce no invented first value. Once a valid index exists, the driver may return that last-valid value with its original acquisition timestamp and the relevant stale/failure quality flags. Recovery resets the recursive VOC algorithm and requires a fresh blackout period before a new valid index is emitted.

## Development configuration

| Setting | Current default | Kconfig symbol |
| --- | ---: | --- |
| SGP40 enabled | yes when SHT41 is enabled | `CONFIG_SITETWIN_SGP40_ENABLED` |
| I2C address | `0x59` | `CONFIG_SITETWIN_SGP40_I2C_ADDRESS` |
| VOC algorithm cadence | 1 second | `CONFIG_SITETWIN_SGP40_INTERVAL_1S` |
| Optional cadence | 10 seconds | `CONFIG_SITETWIN_SGP40_INTERVAL_10S` |
| Maximum compensation age | 2000 ms | `CONFIG_SITETWIN_SGP40_COMPENSATION_MAX_AGE_MS` |
| VOC deadband | 5 index points | existing reporting default |
| Minimum report interval | 30 seconds | existing reporting default |
| Maximum silence | 5 minutes | existing reporting default |

The ten-second option is supported by the official algorithm API but is not the physically validated default. Neither cadence is presented as a final battery optimisation result.

## Validation evidence

Evidence date: 2026-08-06. Branch: `feature/environment-pod-sensors`. Foundation commit: `00d49710b6af92dc5eb3e024e8b0075ca47ef9e3`.

| Evidence level | Result |
| --- | --- |
| Implemented | SGP40 compensated protocol, volatile official VOC algorithm, SHT41 snapshot provider, logical adapter, registry composition, configuration, and host tests |
| Host-tested | Passed strict C11 compilation with `-Wall -Wextra -Werror`; complete host suite passed; stress harness processed 384 telemetry records |
| Target-compiled | ESP32-C6 Pod passed with ESP-IDF v5.5.4 and repository-pinned Zigbee SDK v2.0.3; binary size `0x88b30`, 42% partition space free |
| Coordinator-compiled | Passed; binary size `0x84500`, 44% partition space free |
| Wi-Fi gateway-compiled | Passed; binary size `0x115410`, 46% partition space free |
| Physically tested | Not tested |
| End-to-end physically verified | Not tested |

The Pod and coordinator builds each report one pre-existing role-specific unused-function warning (`gateway_cluster_init` in the Pod build and `pod_cluster_init` in the coordinator build). These are outside the sensor milestone and do not fail either build.

Host coverage includes CRC, compensation conversions and ranges, exact command bytes, non-blocking measurement timing, algorithm blackout and first-valid behaviour, fixed cadence, unavailable and expired compensation, no invented value, stale-last-valid timestamps and quality, CRC and missing-device recovery, detach/reattach, repeated lifecycle stress, SHT41 regression, reporting, queue, fixed payload, UART framing, canonical JSON, and gateway regression tests.

## Build and flash commands

Use the repository's ESP-IDF v5.5.4 environment and pinned Zigbee SDK:

```powershell
cd firmware
idf.py -B build_pod build
idf.py -B build_pod -p <POD_PORT> flash monitor
```

Coordinator and Wi-Fi gateway compile checks use their existing project configurations and build directories:

```powershell
cd firmware
idf.py -B build_gateway build

cd gateway-wifi
idf.py build
```

Expected Pod startup messages include:

```text
Environment I2C ready on controller 0 (SDA GPIO 6, SCL GPIO 7)
SHT41 runtime attached at address 0x44
SCD41 runtime attached at address 0x62 in low-power periodic mode
SGP40 runtime attached at address 0x59 with 1000 ms VOC algorithm cadence
```

No valid `sgp40_voc_index` transmission is expected during the initial Sensirion algorithm blackout. With fresh SHT41 compensation and uninterrupted one-second sampling, the first index should appear after approximately 45 to 50 seconds; scheduling and network reporting can add small observable delay.

## Physical wiring assumptions

- SHT41, SCD41, and SGP40 share ESP32-C6 I2C controller 0, SDA GPIO 6, and SCL GPIO 7.
- The SGP40 breakout is powered at a compatible 3.3 V level and shares ground with the Pod.
- The configured SGP40 address is `0x59`.
- ESP32-C6 internal I2C pull-ups remain disabled; the sensor modules are reported to provide onboard pull-ups.
- The effective combined pull-up resistance, bus capacitance, breakout regulator/level-shifter behaviour, supply integrity, and enclosure airflow have not yet been measured.

## Physical test procedure

1. Inspect power, ground, SDA, and SCL wiring with the Pod unpowered; confirm the breakout is 3.3 V compatible.
2. Flash the Pod build and monitor serial output. Confirm SHT41, SCD41, and SGP40 attach without preventing the other sensors from running.
3. Confirm fresh SHT41 temperature and humidity readings first, then leave the Pod sampling continuously for at least 60 seconds.
4. Verify a valid `sgp40_voc_index` record traverses Zigbee, coordinator UART, gateway JSON, MQTT, and HiveMQ without schema changes.
5. Use a safe, controlled consumer VOC source at a distance and observe directionally responsive index behaviour; do not apply liquid, solvent, or damaging concentrations directly to the sensor.
6. Disconnect or invalidate SHT41. Confirm no substitute 25 C / 50% RH command is used, the VOC result becomes stale/compensation-unavailable after a prior valid value, and no new result is represented as fully valid.
7. Reattach SHT41 and confirm the VOC algorithm restarts its blackout before returning to fresh valid output.
8. Disconnect SGP40 and confirm SHT41 and SCD41 continue operating. Reattach SGP40 and confirm automatic probe, warm-up, and recovery without reboot.
9. Record branch, commit, hardware revision, date, procedure, serial/MQTT logs, observed values, failures, and evidence paths.

## Remaining limitations and assumptions

- No physical SGP40 or end-to-end SGP40 result is claimed by this milestone.
- Continuous operation has a datasheet typical current of about 2.6 mA at 3.3 V; duty-cycled low-power operation remains unimplemented pending physical measurements.
- Algorithm state is volatile. A reboot, detach, compensation interruption, missed cadence, or hard read fault causes a new blackout. No NVS persistence was added.
- Raw VOC is diagnostic-only and is not transported because no approved telemetry kind exists for it.
- The one-second transaction phasing currently accounts for the registry's 100 ms in-progress retry. A future scheduler redesign should make this timing relationship explicit rather than silently changing it.
- Automatic self-test and heater-off are not used during normal startup; factory reset and calibration writes are not implemented.
- Local CO2 alarm behaviour is unchanged and remains independent of the SGP40 driver.
- Remote configuration transport, NVS policy, hot-swap electronics, and all new transport contracts remain out of scope.

## Compatibility confirmation

No Zigbee payload, custom cluster or command, UART frame, canonical JSON, MQTT schema, gateway behaviour, queue contract, registry API, reporting-policy API, fake-driver interface, or protected shared sensor contract was changed. Each accepted VOC Index remains one ordinary fixed 30-byte SiteTwin telemetry record.
