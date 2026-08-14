---
title: SCD41 Milestone 1 Assessment
project: SiteTwin
status: Implemented, host-tested, and target-compiled; physical validation pending
date: 2026-08-06
branch: feature/environment-pod-sensors
foundation: 38082d5bc481810d01d213f9fef02d2acffd2341
---

# SCD41 Milestone 1 Assessment

I2 reconciliation addendum (2026-08-14): the driver and fixed `POD_1` slot-2
composition described below are now integrated with the full I1 sensor set.
SHT41 remains the sole temperature/humidity authority. Historical GPIO4/GPIO5
or local-alarm discussion is not an active hardware contract: I2 implements no
alarm state machine or output driver, GPIO19 remains electrically gated, and
no physical SCD41 validation is claimed.

Related: [[SENSOR_RUNTIME_FOUNDATION_IMPLEMENTATION]], [[Firmware Architecture]], [[Pod and Sensor Strategy]], [[Hardware Bring-Up and Open Decisions]]

## Baseline and scope

The approved Option C foundation is retained: one physical module context owns one bounded logical CO2 adapter, which uses the existing `probe()` and `sample()` interface, registry sequence, reporting policy, priority queue, and 30-byte telemetry record. Only SCD41 CO2 concentration is exposed. SHT41 remains the authoritative temperature and relative-humidity source; the SCD41 RHT words are read and CRC-checked because they are part of the nine-byte response, but they are not published as logical channels.

The Arduino Pod 1 sketch is treated as successful hardware-validation evidence. It proved address `0x62`, readiness checking, periodic acquisition, and a useful local CO2 alarm. Production firmware replaces its blocking wake/stop/reinit sequence and monolithic loop with a non-blocking state machine. It does not automatically reinitialize, self-test, factory-reset, recalibrate, persist settings, or write calibration configuration.

No protected shared contract change is required for SCD41 acquisition. `contracts.h`, `sensor_driver.h`, `sensor_registry.h`, `pod_runtime.h`, and `reporting_policy.h` will remain unchanged in this milestone.

## Official protocol sources

Protocol constants and timing come from the official [Sensirion SCD4x datasheet](https://sensirion.com/media/documents/48C4B7FB/64C134E7/Sensirion_SCD4x_Datasheet.pdf) and are cross-checked against Sensirion's official [embedded SCD4x driver](https://github.com/Sensirion/embedded-i2c-scd4x).

Relevant operations are:

| Operation | Command | Required handling |
| --- | ---: | --- |
| Start periodic measurement | `0x21B1` | Fixed 5-second sensor update interval |
| Start low-power periodic measurement | `0x21AC` | Approximately 30-second sensor update interval |
| Get data-ready status | `0xE4B8` | Wait at least 1 ms, read one CRC-protected word; low 11 bits nonzero means ready |
| Read measurement | `0xEC05` | Wait at least 1 ms, read three CRC-protected words; CO2 ppm is the first word |
| Stop periodic measurement | `0x3F86` | Wait 500 ms before an idle-only command |
| Get serial number | `0x3682` | Idle-only; wait at least 1 ms and validate all three words |
| Reinitialize | `0x3646` | Idle-only, 30 ms; reserved for explicit recovery policy, not ordinary startup |

Commands are 16-bit big-endian words without a trailing CRC. Every returned 16-bit data word is followed by CRC-8 using polynomial `0x31`, initial value `0xFF`, no reflection, and final XOR `0x00`. The official `0xBEEF -> 0x92` vector will be tested. CO2 conversion is the identity conversion `ppm = word[0]`. A periodic-mode CO2 value of zero or a value above the documented 40,000 ppm output range is rejected rather than used as a live reading.

## Operating-mode decision and battery implications

Milestone 1 will support two periodic modes through SCD41 configuration:

- low-power periodic, selected by default for the battery pod;
- standard periodic, selectable for demonstrations requiring the fastest sensor update.

At 3.3 V the datasheet specifies approximately 15 mA typical average current for standard 5-second periodic measurement and 3.2 mA typical for low-power 30-second periodic measurement. Both can draw up to approximately 205 mA peak, so the supply and breakout must tolerate the peak even when average power is reduced. Low-power periodic therefore reduces the sensor's stated typical average current by roughly 79% while retaining autonomous periodic measurement, readiness polling, and ASC compatibility.

The slower mode adds up to roughly one sensor update interval to threshold detection. This is acceptable as the initial battery-oriented engineering choice for non-safety indoor-air-quality monitoring: the SCD41 itself has a typical 60-second CO2 response time, and the local alarm is not a certified safety function. Standard periodic mode remains available for latency and current comparison during physical testing.

Single-shot operation is deferred. Although the datasheet specifies about 0.45 mA typical at one measurement per five minutes, each CO2 conversion takes up to five seconds, the initial single-shot after power-up should be discarded for best accuracy, ASC behavior depends on measurement cadence, power-cycling disables ASC, and five-minute operation is a poor starting point for the local alarm. Adding it now would expand state, calibration, and evidence requirements without helping the first production milestone.

The existing development reporting rule remains the proposal: 50 ppm deadband, 30-second minimum report interval, and five-minute maximum silence. These control radio reporting, not the SCD41 measurement mode, and remain subject to physical noise, latency, and current measurements.

## State machine and first-valid semantics

The portable SCD41 context will expose explicit state for diagnostics and host tests:

`UNINITIALISED -> PROBING -> STARTING -> WARMING_UP -> WAITING_DATA_READY -> READING -> READY`

Failures transition through `FAULT_REPROBE`; explicit module detach returns to `DETACHED` through the existing module-instance reset path.

`probe()` has no timestamp argument, so its multi-step serial-number sequence will be split safely across registry probe calls rather than sleeping. A fresh idle sensor receives the serial command on one call and its response is read on a later call. If an idle-only serial command is rejected because an MCU reset left the independently powered sensor in periodic mode, the driver attempts the allowed stop command, waits through subsequent non-blocking probe calls for more than 500 ms, and then retries identification. Failure of both identification and stop is treated as absence. Ordinary startup never sends `reinit`.

After starting the configured periodic mode, the driver remains in `WARMING_UP`/`WAITING_DATA_READY` and returns `ST_DRIVER_RETRY`. It emits no numeric record, zero placeholder, arbitrary default, or `VALID | WARMING_UP` record before the first CRC-valid in-range measurement. The internal state getter and existing registry port state expose that the module is warming without inventing telemetry.

Data-ready access is also split into command and response states. After its 1 ms command time, the status word is read and CRC-checked. Not-ready is a normal retry, not a fault. When ready, the driver sends `read_measurement`, waits at least 1 ms without blocking, reads all nine bytes, and validates all three CRCs before accepting CO2.

## Failure, removal, and recovery

Before the first valid measurement, command, CRC, range, missing-device, or read failures emit no measurement. After a valid finite CO2 value exists, the first later failed acquisition may return that value with its original acquisition timestamp, `STALE`, and the specific available flag:

- `CRC_FAILED` for any bad returned word;
- `OUT_OF_RANGE` for zero or a value above 40,000 ppm;
- `SENSOR_MISSING` for a confirmed absent/NACK device.

`VALID` is cleared by the existing registry normalizer. A generic bus failure has no dedicated transport quality bit, so it can be represented as `STALE` internally/on telemetry but cannot gain a new fault-reason bit without an approved contract change. Quality transitions continue to bypass deadband suppression through the existing reporting policy.

After the diagnostic stale transition, a missing sensor or repeated fault returns the registry entry to probing. Recovery repeats non-destructive identification/startup and produces a fresh `VALID` value with a new acquisition timestamp. Explicit detach first invalidates the adapter and then resets/detaches the registry slot, preserving the foundation's no-dangling-pointer lifetime rule.

## Automatic self-calibration policy

The SCD41 factory default has ASC enabled. Sensirion states that default ASC assumes exposure to approximately 400 ppm background air at least weekly and at least four hours of uninterrupted operation at a time. Whether a deployed construction-site room satisfies that assumption is not yet established.

Milestone 1 therefore preserves the sensor's existing volatile/persisted ASC setting and performs no ASC write. No `persist_settings` command is issued. A later reviewed configuration may explicitly select preserve, enable, or disable behavior, but enabling/disabling and persistence require deployment evidence and deliberate user-visible configuration rather than a hidden startup write.

## Shared I2C integration

The portable `st_i2c_bus_t` already carries a target address per transaction and needs no change. The current ESP-IDF adapter, however, creates one hardware master bus inside each device instance. Creating a second hardware bus on controller 0 would conflict with the physically verified SHT41 bus.

The smallest compatible change is an additive ESP-IDF-only split:

- one application-lifetime master-bus owner for controller 0, SDA GPIO 6, SCL GPIO 7, pull-up policy, and ESP-IDF bus handle;
- one SHT41 target/device handle for `0x44`;
- one SCD41 target/device handle for `0x62`;
- later, one SGP40 target/device handle for `0x59`.

Each target still exports an ordinary `st_i2c_bus_t`, so portable sensor code, fake buses, Option C adapters, and public driver signatures remain unchanged. The single pod task continues to serialize all sensor transactions. The existing one-device ESP-IDF initializer will remain source-compatible while the Environment Pod composition uses the new shared-owner initializer.

## Environment Pod composition

SHT41 remains in registry slots 0 and 1. SCD41 CO2 uses logical registry slot and radio sensor slot 2. This is a logical telemetry mapping, not a final physical hot-swap port or mux mapping. SGP40 is not implemented, but the shared ESP-IDF bus arrangement leaves address `0x59` available without further HAL redesign.

Expected configurable additions are:

- SCD41 enabled flag;
- I2C address, default `0x62`;
- periodic operating mode, default low-power periodic;
- internal data-ready poll interval;
- sensor ID;
- fault/re-probe policy where it is not already fixed by the registry.

The existing shared physical bus settings remain controller 0, SDA GPIO 6, SCL GPIO 7, 100 kHz, 100 ms transaction timeout, and internal pull-ups disabled. They will not be duplicated into contradictory per-sensor pin settings.

## Local CO2 alarm boundary

The SCD41 driver will not control GPIOs or contain the 1000 ppm threshold. The smallest future integration is:

`every acquired CO2 observation -> Environment Pod CO2 rule -> two bounded output requests -> ESP-IDF LED/PWM output service`

The rule state should contain only enabled state, trigger threshold, clear threshold/hysteresis, buzzer frequency, one-second phase duration, output timeout/failsafe, last valid input time, alarm state, and output phase. It must remain disabled before the first valid CO2 observation and force both outputs off when input becomes invalid/stale or its failsafe expires.

The current `st_local_output_service_t` is sufficient as an actuator boundary, but `st_pod_runtime_tick()` currently consumes registry observations internally before reporting suppression and exposes no observation callback. Driving the rule from the outbound telemetry queue would be incorrect because unchanged readings are deliberately suppressed. A later alarm implementation therefore needs review of one small additive shared-runtime hook, for example:

```c
typedef void (*st_pod_observation_callback_t)(
    void *context, const st_sensor_reading_t *reading);

void st_pod_runtime_set_observation_callback(
    st_pod_runtime_t *runtime,
    st_pod_observation_callback_t callback,
    void *context);
```

The callback would run once for every acquired observation before reporting policy, while the existing `st_pod_runtime_tick()` signature and all transport behavior remain unchanged. This protected contract change is not required for SCD41 telemetry and will not be made in Milestone 1 without separate approval. MQTT control, Zigbee downlink, reverse UART, acknowledgements, and a generic rule engine remain out of scope.

## Planned tests and evidence boundary

Host tests will cover the official CRC vector, identity CO2 conversion, command/state timing, not-ready retries, first-valid behavior, first-failure suppression, stale CRC/range/missing transitions, recovery, immediate quality-transition reporting, detach/reattach, repeated lifecycle cycles, shared ESP-IDF-independent fake-bus behavior, and SHT41 regression. The existing payload, UART CRC, JSON, gateway, queue, and 384-record stress tests remain part of the same suite.

Milestone completion requires strict host compilation, full host tests, `git diff --check`, pod build, and coordinator build. The Wi-Fi gateway will also be rebuilt because it resolves the shared components from the same repository even though no transport source is expected to change. Compilation will be reported separately from physical and end-to-end evidence.

The schematic confirms the provisional development wiring: all three sensor modules share controller 0 on GPIO 6/7; SCD41 uses `0x62`; GPIO 4 is the alarm LED; GPIO 5 is passive-buzzer PWM; onboard module pull-ups are reported and ESP internal pull-ups remain disabled. SCD41 peak supply capability, actual combined pull-up resistance, bus waveforms, module voltage/regulation, ventilation, and physical CO2 behavior remain unverified until Vineet tests the real Pod.

## Milestone 1 implementation result

The decision above was implemented without changing any protected shared contract. The production SCD41 driver uses `st_i2c_bus_t`, keeps the existing physical-module and logical-adapter interfaces, and exposes only one `ST_SENSOR_CO2_PPM` channel. A shared ESP-IDF master-bus owner now permits the physically separate SHT41 and SCD41 device handles to use controller 0 without creating the controller twice; the earlier single-device initializer remains source-compatible.

The Environment Pod composition attaches SHT41 temperature/humidity to logical slots 0/1 and SCD41 CO2 to slot 2. Low-power periodic mode is the default. Standard periodic mode is selectable. The driver performs no automatic reinitialization, factory reset, self-test, forced recalibration, ASC write, or settings persistence.

Implemented protocol and lifecycle behavior includes:

- split command/response states for serial identification, data-ready polling, and measurement reads;
- CRC validation of the serial number, data-ready word, and all three measurement words;
- no observation before the first valid, in-range CO2 result;
- preservation of the last-valid value and acquisition timestamp for one diagnostic stale transition;
- `CRC_FAILED`, `OUT_OF_RANGE`, and `SENSOR_MISSING` quality detail where the existing contract supports it;
- fault-to-probe recovery without reboot;
- clean detach/reattach adapter lifetime;
- recovery if an MCU restart finds the independently powered SCD41 already measuring;
- restart of a consumed data-ready transaction if the shared bus is temporarily busy before the measurement command is accepted.

The local CO2 alarm is intentionally not implemented. The documented observation callback remains a proposal requiring separate approval because it adds to protected `pod_runtime` API. No GPIO 4/5 actuator behavior, threshold, buzzer PWM, MQTT control, Zigbee downlink, or generic rule engine was added.

## Configuration added

| Symbol | Development default | Purpose |
| --- | ---: | --- |
| `CONFIG_SITETWIN_SCD41_ENABLED` | enabled | Include the SCD41 in the Environment Pod composition |
| `CONFIG_SITETWIN_SCD41_I2C_ADDRESS` | `0x62` | Seven-bit target address |
| `CONFIG_SITETWIN_SCD41_MODE_LOW_POWER_PERIODIC` | selected | Approximately 30-second sensor update mode |
| `CONFIG_SITETWIN_SCD41_MODE_PERIODIC` | alternative | Five-second sensor update mode |
| `CONFIG_SITETWIN_SCD41_POLL_INTERVAL_MS` | 1000 ms | Non-blocking data-ready polling interval |

The existing controller, GPIO, clock, timeout, and pull-up symbols now describe the shared Environment Pod I2C bus. Their verified defaults remain controller 0, SDA 6, SCL 7, 100 kHz, 100 ms, and internal pull-ups disabled.

CO2 reporting policy defaults already associated with `ST_SENSOR_CO2_PPM` remain a 50 ppm deadband, 30-second minimum interval, and five-minute maximum silence. They were not promoted to research conclusions and the transport record did not change.

## Validation evidence

| Evidence level | Result | Procedure / evidence |
| --- | --- | --- |
| Implemented | Passed | Portable SCD41 driver, shared ESP-IDF bus ownership, Environment Pod composition, configuration, and host fakes/tests are present on `feature/environment-pod-sensors`. |
| Host-tested | Passed | `firmware/host_tests/run-tests.ps1`; strict `-Wall -Wextra -Werror` build, all suites passed, stress harness processed 384 telemetry records. |
| Target-compiled: Pod | Passed | ESP-IDF 5.5.4 and repository-pinned Zigbee SDK 2.0.3; `build_pod/sitetwin_firmware.bin`, 0x869b0 bytes, 43% of the smallest application partition free. |
| Target-compiled: coordinator | Passed | `build_gateway/sitetwin_firmware.bin`, 0x84500 bytes, 44% free. The pre-existing coordinator-only unused `pod_cluster_init` warning remains. |
| Target-compiled: Wi-Fi gateway | Passed | Rebuilt because the gateway resolves the shared sensor component; `gateway-wifi/build/gateway-wifi.bin`, 0x115410 bytes, 46% free. |
| Physical SCD41 test | Not run | Requires Vineet to connect and flash the actual Pod. |
| End-to-end SCD41 test | Not run | No claim is made until a real SCD41 CO2 record reaches HiveMQ through the established backbone. |

`git diff --check` must be clean for milestone-owned paths before the commit. The full host suite retains the existing SHT41, 30-byte payload, UART framing, JSON, gateway, priority-queue, reporting, and stress regression coverage. No Zigbee payload, cluster/command, UART frame, canonical JSON, MQTT schema, or gateway behavior was changed.

## Build, flash, and physical test procedure

From an ESP-IDF 5.5.4 shell:

```powershell
cd firmware
idf.py -B build_pod build
idf.py -B build_pod -p <POD_COM_PORT> flash monitor
```

Expected startup messages include:

```text
Environment I2C ready on controller 0 SDA GPIO6 SCL GPIO7
SHT41 runtime attached at address 0x44
SCD41 runtime attached at address 0x62 in low-power periodic mode
```

Before flashing, confirm a common 3.3 V supply/ground arrangement suitable for the actual modules, verify that the SCD41 module and combined pull-ups are 3.3 V compatible, and confirm that the supply can tolerate the SCD41 peak current. Connect SHT41 `0x44` and SCD41 `0x62` in parallel on SDA GPIO 6 and SCL GPIO 7. Do not connect the proposed GPIO 4 LED or GPIO 5 buzzer as part of this driver-only test unless their external circuits are independently verified.

After flashing:

1. Confirm the three startup messages and absence of reset loops or I2C initialization failures.
2. Allow at least one selected sensor update interval; low-power periodic may take about 30 seconds before its first data-ready result.
3. Confirm a `Sending scd41_co2 sequence ... quality 0x...` line appears only after a real CRC-valid CO2 measurement.
4. Confirm the corresponding canonical JSON record reaches HiveMQ with CO2 in ppm and `VALID` quality.
5. Disconnect the SCD41 while leaving SHT41 powered; confirm SHT41 continues and the CO2 path reports/enters missing-reprobe behavior without reboot.
6. Reconnect the SCD41; confirm a later fresh `VALID` CO2 reading recovers automatically.
7. Record branch, commit, date, board/module details, supply, logs, MQTT evidence, observed latency, and any failure in the sensor-integration evidence record.

## Remaining limitations and assumptions

- Physical SCD41 wiring, current peaks, breakout regulation, combined pull-up strength, signal integrity, ventilation, accuracy, noise, response time, removal behavior, and recovery are not yet tested.
- Low-power periodic is an engineering default, not a final power result; both mode current and system-level battery life need measurement.
- ASC is preserved rather than configured. Deployment exposure may not satisfy its factory-default assumptions.
- The registry owns the general re-probe cadence, so exact recovery latency is scheduler-dependent.
- Generic I2C failures have no dedicated quality bit in the approved transport contract.
- Alarm integration remains pending a separately reviewed observation-boundary addition and output-service implementation.
- SGP40 is not implemented or started in this milestone.
