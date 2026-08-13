# Yicheng Sensor Driver Plan (v4)

Owner: Yicheng (wanglrebe)
Scope: BH1750, PIR, Reed/contact switch, DS18B20, INA219
Pods: Activity/Access Pod (BH1750 + PIR + Reed), Equipment Pod slow path (DS18B20 + INA219)

> Hardware-authority addendum (2026-08-14): DS18B20 is also the final
> universal-board Type 5 sensor with the 10 kOhm ID code. The fixed Pod 3
> GPIO0/4.7 kOhm prototype described in this historical driver plan remains
> valid development evidence but is not the final-board route. Final-board
> non-I2C DATA uses CD74HC4052M96 to `DATA_COMMON`/GPIO3. GPIO19 controls one
> shared buzzer/LED low-side branch, which this sensor workstream does not
> implement. Pod 3 is monitoring/inference only; motor control or current cut
> is prohibited. These statements supersede conflicting/open hardware claims
> later in this plan.
Status: **all five planned sensor paths are now implemented.** Yicheng's
BH1750, Reed, and INA219 drivers were reused; Vineet completed the PIR
composition/state machine and the DS18B20 production path. Activity Pod
hardware validation is complete. INA219 and ADXL345 have been exercised end to
end on Pod 3. DS18B20 is host-tested, target-compiled, and physically verified
with the powered waterproof probe on 2026-08-13.
Depends on: `feature/sensor-runtime-foundation` at commit `38082d5bc481810d01d213f9fef02d2acffd2341` — pushed 2026-08-06, reviewed at this exact SHA.
This revision (v2) incorporates the actual foundation implementation. Sections
previously marked **[PENDING FOUNDATION]** have been updated below; one item
(electrical module identification) remains genuinely unresolved and is noted
as such, not as a placeholder.

Vineet's own `SENSOR_PROTOTYPE_PORTING_ASSESSMENT.md` and
`SENSOR_RUNTIME_FOUNDATION_IMPLEMENTATION.md` on that branch explicitly scope
BH1750, PIR, Reed, DS18B20, and INA219 as "Future Yicheng-owned... not
implemented on this branch" — no overlap or conflict with this plan.

---

## 1. Prototype Assessment

The hardware team's Arduino prototypes (`firmware/sensor_firmwares/Pod-1.ino`,
`Pod_2.ino`, `Pod_3.ino`) are hardware-validation prototypes, not production
firmware and not failed work. They exist to prove wiring, confirm library
compatibility, and demonstrate basic readings and local alarms on real
hardware before any ESP-IDF/SiteTwin integration was attempted.

**Pod_2.ino (Activity/Access Pod — BH1750, PIR, Reed)**
- BH1750 read via the `BH1750` Arduino library in `CONTINUOUS_HIGH_RES_MODE`,
  fixed I2C address `0x23`, polled once per second inside the display loop.
- PIR (SR505) read by raw `digitalRead` every loop iteration, no interrupt,
  no retrigger filtering. A 5-second startup stabilisation delay is present.
- Reed/door sensor read by raw `digitalRead` with `INPUT_PULLUP`, software
  debounce of ~30 ms implemented via timestamp comparison in the main loop.
  `LOW = closed`, `HIGH = open`, confirming a normally-open reed switch that
  closes when the magnet is present.
- Local alarm: door-open drives an LED + buzzer at a fixed 1 Hz toggle rate
  while the door remains open.

**Pod_3.ino (Equipment Pod — DS18B20, INA219; ADXL345 is Vineet's)**
- DS18B20 read via `DallasTemperature`, using the blocking
  `requestTemperatures()` call, which stalls the whole loop for the duration
  of the conversion (up to 750 ms at 12-bit resolution).
- INA219 read via `Adafruit_INA219` with no explicit calibration call —
  the library's default calibration (0.1 Ω shunt, 32 V/2 A range) is used
  implicitly. The prototype additionally reads `getPower_mW()`, which is not
  yet a value the shared contract defines (see Section 8).
- Local alarm: temperature threshold and ADXL345 motion threshold both drive
  the same LED + buzzer output.

Both prototypes are single-loop, one-second-cadence designs with no
concurrency concerns because nothing else is competing for CPU time. That
assumption does not hold in production, where PIR/reed events must be
serviced with low latency while DS18B20/INA219 acquisition and MQTT/UART
transport are also active.

---

## 2. Per-Sensor Analysis

### 2.1 BH1750 (illuminance)

**Status: implemented.** Saturation is checked on the raw 16-bit register
value (`raw >= 0xFFFF`), not on the value after conversion to lux — a
float comparison there would fail near the boundary (`65535.0f/1.2f`
rounds to slightly below the exact 54612.5 threshold), silently accepting
the sensor's own maximum output as an in-range reading.

| Field | Detail |
|---|---|
| Prototype behaviour | Continuous high-res mode, address `0x23`, polled every 1 s, no saturation/error handling beyond `lux < 0` check. |
| Required production behaviour | Switch to **One Time H-Resolution Mode** (opcode `0010_0000`) per acquisition rather than continuous mode, to avoid unnecessary current draw between samples. Each measurement: send opcode → wait up to 180 ms (per ROHM datasheet, H-Res mode typical 120 ms / max 180 ms) → read 2 bytes → device auto-returns to Power Down. Detect saturation (raw value at or near `0xFFFF` equivalent post-scaling) and reads under `S0` dark-count tolerance. Respect the "one opcode per transaction, insert STOP" restriction from the ROHM datasheet — do not chain mode-change and measurement-trigger without a STOP condition between them. **Update (v2):** the driver should be written against `st_i2c_bus_t` (`sensor_hal.h`) rather than calling ESP-IDF I2C functions directly, and composed with the existing `st_espidf_i2c_device_t` / `st_espidf_i2c_bus()` HAL from `sitetwin_espidf_hal` — this is the same bus abstraction Vineet's SHT41 driver already uses, so no new I2C plumbing needs to be written from scratch. |
| Logical outputs | `illuminance_lux` (`ST_SENSOR_ILLUMINANCE_LUX`, `ST_UNIT_LUX`) |
| Acquisition style | Scheduled, non-blocking. Trigger one-shot measurement, record `next_sample_at_ms = now + measurement_time`, poll/complete on next tick rather than blocking. No interrupt involved. |
| Reporting class | `ST_RECORD_STATE` / `ST_PRIORITY_ROUTINE`. Latest-value replacement acceptable. |
| Open hardware questions | Confirm the `ADDR/AD0` solder jumper on the board is not bridged (default address `0x23` assumed based on prototype). |
| Planned tests | Lux conversion math against known register values from the ROHM datasheet worked example; one-shot timing state transitions; saturated/invalid response handling; missing-sensor (NACK) handling; reporting deadband (10 lx, provisional) and max-silence heartbeat (15 min, provisional). |

### 2.2 PIR (motion, SR505)

| Field | Detail |
|---|---|
| Prototype behaviour | Raw `digitalRead` every loop iteration (~1 Hz effective, no interrupt), 5 s startup delay before first read. |
| Required production behaviour | GPIO interrupt → minimal ISR (flag/notify only, no logging/allocation/Zigbee/registry calls inside the ISR) → task notification → configurable retrigger suppression window → confirmed transition calls `st_pod_runtime_emit_event(ST_SENSOR_MOTION)` directly (Section 6 — not routed through registry/`module_instance`, same as Reed). **Interface and planning for this driver may proceed now.** **Production implementation and merge of this driver are blocked** until the exact physical PIR module is identified and its output polarity, retrigger behaviour, and available on-board controls are confirmed by physical inspection — see Section 3. This does not block BH1750, which has no such dependency and is implemented first, nor does it block writing this driver's header/interface now. |
| Logical outputs | Motion event (`ST_SENSOR_MOTION`, `ST_UNIT_BOOLEAN`), motion event count, last-motion monotonic timestamp. |
| Acquisition style | Interrupt-driven, event-based. No polling loop. |
| Reporting class | `ST_RECORD_EVENT` / `ST_PRIORITY_EVENT`. Must bypass routine change-suppression — motion events are never dropped as "unchanged state." |
| Open hardware questions | Module has no vendor datasheet and its exact physical identity is unconfirmed. **Update:** a schematic (Pod2_ESP32C6_Security_Light_Complete_Loop, sourced from the team chat) confirms the PIR is powered at **3.3V**, not 5V — this one item is now resolved with reasonable confidence. **Do not describe a trim-pot or trigger-mode jumper as present until physically verified** — the prototype's behaviour is consistent with either being present, but this is an assumption, not a confirmed fact. Output polarity, retrigger behaviour, and whatever controls (if any) the board actually exposes still must be confirmed by physical inspection before this driver is implemented — the schematic draws the module as a 3-pin black box and cannot show internal trim-pot/jumper state. **Writing this driver's header/interface is not blocked by this; implementing and merging it is — per the blocking prerequisite above.** |
| Planned tests | Startup suppression window; interrupt→notification latency; retrigger suppression; event counting; configurable polarity. |

### 2.3 Reed / contact switch

**Status: implemented**, as a pure debounce/confirmation state machine —
see Section 6 for the architectural revision (`emit_event()`, not a
registry-polled channel) this implementation follows.

| Field | Detail |
|---|---|
| Prototype behaviour | `INPUT_PULLUP`, raw `digitalRead`, ~30 ms software debounce via timestamp diff in the main loop. `LOW = closed`, `HIGH = open`. |
| Required production behaviour | **Implemented (v4):** `st_reed_debounce_t` — a pure debounce/confirmation state machine, not wired into `module_instance`/registry. GPIO reading and the confirmed-transition → `st_pod_runtime_emit_event(ST_SENSOR_CONTACT)` call belong to the Pod composition layer (not yet written); see Section 6 for why. Debounce window is a configurable field (`config.debounce_ms`), defaulting conceptually to the prototype's ~30 ms but not hardcoded. |
| Logical outputs | Contact/open-closed event (`ST_SENSOR_CONTACT`, `ST_UNIT_BOOLEAN`), delivered via `st_pod_runtime_emit_event()`, not a registry-polled channel. Optional open-duration calculation kept outside the debounce state machine. |
| Acquisition style | Not poll-based at all — see Section 6. The debounce state machine is fed `(timestamp, level)` samples by task-context code triggered off a GPIO interrupt; it has no `acquire()`/`sample()` of its own. |
| Reporting class | `ST_RECORD_EVENT` / `ST_PRIORITY_EVENT`. Bypasses routine suppression, same as PIR. |
| Open hardware questions | No vendor/model information — generic reed switch, no library, no datasheet. Confirmed via prototype code and internal-pullup wiring that it is normally-open (closes when magnet is present); this has not yet been cross-checked with a multimeter. |
| Planned tests | Bounce rejection; open transition; closed transition; duplicate-event suppression; event ordering/sequence continuity; configurable active polarity. |

### 2.4 DS18B20 (surface temperature)

**Status: implemented on `feature/vineet-pir-adxl345-sensors`.** The portable
driver, `st_onewire_bus_t` boundary, ESP-IDF RMT adapter, Equipment Pod slot-3
composition, and host tests are present. The ESP32-C6 Equipment target build
passes. Physical waterproof-probe validation passed on 2026-08-13.

| Field | Detail |
|---|---|
| Prototype behaviour | `DallasTemperature.requestTemperatures()` — **blocking**, stalls the calling task for up to 750 ms (12-bit default resolution) each read. `DEVICE_DISCONNECTED_C` is the only failure case handled. |
| Required production behaviour | Non-blocking conversion state machine: `IDLE → CONVERSION_STARTED → WAITING → READOUT → READY / ERROR`. Correct wait time per resolution per the ADI/Maxim datasheet: 9-bit = 93.75 ms, 10-bit = 187.5 ms, 11-bit = 375 ms, 12-bit = 750 ms. Scratchpad CRC-8 validation using polynomial `X⁸ + X⁵ + X⁴ + 1` before accepting a reading. Because the probe is confirmed three-wire (externally powered, not parasite), the driver does **not** need the strong-pullup/MOSFET switching logic the datasheet describes for parasite power — this simplifies the implementation relative to the general case. **Revised per review:** the portable protocol/state-machine driver under `sitetwin_sensors` must **not** call ESP-IDF RMT/GPIO functions directly. See Section 5 for the proposed `st_onewire_bus_t` HAL boundary — this must be proposed and reviewed before DS18B20 implementation begins, not written ad hoc alongside it. |
| Logical outputs | `surface_temperature_c` (`ST_SENSOR_TEMPERATURE_C`, `ST_UNIT_CELSIUS`) |
| Acquisition style | Scheduled, non-blocking, asynchronous conversion. Must not hold a shared I2C mutex — this driver uses 1-Wire, not I2C, so no cross-interference with INA219 acquisition on that basis, but must still not block the task the ADXL345 fast path depends on. |
| Reporting class | `ST_RECORD_STATE` / `ST_PRIORITY_ROUTINE` for normal readings; fault-state transitions (CRC failure, disconnected sensor) report immediately regardless of routine suppression. |
| Open hardware questions | Probe is an unbranded waterproof three-wire module (red/black/yellow), no vendor datasheet. Confirmed three-wire (non-parasite) by physical inspection of the connector. Not yet confirmed whether the DQ line pull-up resistor (~4.7 kΩ per datasheet reference circuit) is present on the probe's own small board or needs to be added externally. |
| Tests | Host tests pass for known-good and deliberately corrupted scratchpad CRC, negative-temperature decoding, all resolution timings, non-blocking conversion, disconnected-sensor handling, stale fallback, retry, and removal/reattachment. Physical GPIO0 probe validation passed on 2026-08-13. |

### 2.5 INA219 (bus voltage / current)

**Status: implemented** (`bus_voltage_v` + `current_ma` only — `power_mw`
untouched, per Section 8). Two implementation-level fixes worth recording:
the current-range boundary check needed a small float tolerance for the
same class of rounding reason as BH1750's saturation check; and the CNVR
"not ready" path had to be guarded against issuing a second, redundant
physical I2C read when the shared bus-voltage and current channels are
both polled within the same registry tick.

| Field | Detail |
|---|---|
| Prototype behaviour | `Adafruit_INA219::begin()` with no explicit calibration — relies on the library's implicit default (0.1 Ω shunt, 32 V / 2 A range). Also calls `getPower_mW()`, which this project's shared contract does not yet define as an output type. |
| Required production behaviour | Explicit calibration at init: `Cal = trunc(0.04096 / (Current_LSB × R_SHUNT))`, `Current_LSB = MaxExpectedCurrent / 2^15`, with `R_SHUNT` a configurable value (default 0.1 Ω, provisional pending multimeter confirmation) rather than an implicit library default. Bus voltage and current read from separate registers with the correct LSB scaling (shunt voltage 10 µV/LSB, bus voltage 4 mV/LSB after a 3-bit right shift). Check the `OVF` bit (Bus Voltage register, bit 0) on every read and map it to `ST_QUALITY_OUT_OF_RANGE`. **Update (v2):** the "two logical outputs from one physical acquisition" problem (Section 4, previously open) has a concrete, validated pattern to follow — Vineet's `st_module_instance_t` + `st_logical_channel_adapter_t` (`sitetwin_sensor_runtime`), proven with SHT41's temperature/humidity split. INA219 should follow the same shape: one `st_physical_module_driver_t` context owns the calibration/register state and performs the actual I2C transaction; two bounded `st_logical_channel_adapter_t` instances (bus_voltage, current) share that context, with the first due adapter triggering acquisition and the second consuming the cached result, matching the SHT41 "shared acquisition and cache" behaviour already implemented and host-tested. This replaces the earlier plan to design an ad hoc shared struct — reuse the existing pattern instead. |
| Logical outputs | `bus_voltage_v` (`ST_SENSOR_VOLTAGE_V`), `current_ma` (`ST_SENSOR_CURRENT_MA`). `power_mw` intentionally **not** implemented yet — see Section 8. |
| Acquisition style | Scheduled polling. Voltage and current acquisition should be treated as belonging to the same acquisition instant where possible (per TI datasheet, they are converted at different internal times, up to ~68 ms apart at 12-bit/128-sample averaging — acceptable for this application). |
| Reporting class | `ST_RECORD_STATE` / `ST_PRIORITY_ROUTINE` for both channels under normal conditions; overflow/fault conditions report immediately. |
| Open hardware questions | Board carries an Adafruit-style logo; Adafruit's own documentation confirms a 0.1 Ω, 1% shunt resistor on their INA219 breakout, which matches the prototype's implicit calibration behaviour — high confidence but not yet confirmed with a multimeter or a known board revision/purchase source. Default I2C address `0x40` assumed (A0/A1 jumpers unbridged). |
| Planned tests | Bus voltage conversion math; current conversion math against a worked calibration example; calibration-register programming; overflow/out-of-range detection via the `OVF` bit; two logical outputs derived from one acquisition; missing-device handling. |

---

## 3. Open Hardware Questions (consolidated)

**Blocking status:** the PIR question below blocks that driver's production
implementation and merge (see Section 2.2). Every other provisional parameter
in this table remains configurable and does **not** block its respective
driver's initial implementation — those drivers proceed with a documented
default and get corrected later if the physical value turns out different.

| Sensor | Question | Resolution path |
|---|---|---|
| BH1750 | Is `ADDR/AD0` jumper bridged? | Visual inspection of board / I2C scan |
| PIR | Trim-pot (hold time) and jumper (single/repeat trigger) current positions? | Physical inspection; oscilloscope/logic capture of output if needed |
| PIR | Supply/output voltage of this specific SR505 unit? | Physical inspection / datasheet of the specific batch if a source is known |
| Reed | Confirmed normally-open? Any external pull-up beyond ESP32 internal pull-up? | Multimeter continuity test |
| DS18B20 | Is the DQ pull-up resistor present on the probe's own board? | Visual inspection / continuity test |
| INA219 | Shunt resistor value confirmed at 0.1 Ω? | Multimeter measurement across the shunt |
| INA219 | Confirm board purchase source (genuine Adafruit vs. compatible clone)? | Ask hardware team / check for STEMMA QT vs. header-only variant |

None of these block writing driver code — all are exposed as configurable
parameters with the values above used as provisional defaults.

---

## 4. Compatibility Review of Vineet's Foundation

Based on `feature/sensor-runtime-foundation` as pushed and reviewed on
2026-08-06 (base commit `c38230a`):

| Item | Current state | Notes |
|---|---|---|
| Physical module representation | `st_physical_module_driver_t` (`physical_module.h`) plus `st_module_instance_t` (`module_instance.h`) — owns hardware context, channel adapters, registry slot bookkeeping, and attach/detach lifecycle. | **Resolved.** More structured than the original `st_module_metadata_t` alone; validated with SHT41. |
| Logical channel representation | `st_logical_channel_adapter_t` (`logical_channel_adapter.h`) — a bounded adapter per logical output, backed by a shared physical module. | **Resolved.** Directly reusable for INA219 (bus_voltage + current channels). |
| Multiple channels sharing one driver context | Implemented and host-tested via the module-instance/adapter pattern: first due adapter triggers acquisition, second adapter consumes the cached result (`cache_validity_ms` window). | **Resolved** — see Section 2.5 update. No ad hoc design needed. |
| Acquisition → registry flow | Unchanged; `st_sensor_registry_tick()` still calls driver `sample()`. `st_driver_sample_t` gained two *optional* fields (`acquired_at_ms`, `acquired_at_valid`); existing drivers remain source-compatible without supplying them. | Stable, backward-compatible. |
| Event record creation | Unchanged; `make_record()` classification logic untouched by this branch. | Matches PIR/Reed requirements exactly, no changes needed. |
| Quality flags | Unchanged set, but the registry now *normalizes* four flags (`CRC_FAILED`, `OUT_OF_RANGE`, `SENSOR_MISSING`, `STALE`) as automatically incompatible with `VALID`, while `BATTERY_LOW`/`MOUNTING_CHANGED` remain compatible informational flags. | Worth relying on this normalization rather than re-implementing the VALID-clearing logic inside each of the five drivers. |
| Reporting rule configuration | Unchanged. | Stable. |
| Removal / reattachment | `st_module_instance_detach()` implements a defined four-step sequence (deactivate adapters → reset physical context → detach registry slots → mark instance detached), host-tested across 32 attach/detach cycles. | **Resolved at the software-lifecycle level.** Detection is still via failed communication or explicit detach — **no electrical hot-swap detection exists yet** (`board_port.h` defines the interface shape only; GPIOs/mux/ADC-ID logic remain unimplemented). This part of the original open question is genuinely still open, not just pending — treat any electrical module-ID design as future work, not something to design around now. |
| Driver registration without a large switch | Unchanged, still function-pointer based. | Stable. |

**Summary:** every item needed to write and unit-test all five drivers is now
resolved and has a concrete, validated pattern to follow — including the
INA219 multi-channel case, which previously had no concrete design. The one
genuinely open item is electrical module identification (hot-swap detection
circuitry), which is out of scope for all five drivers at this stage since
none of the current Pods are the "universal pluggable" design.

---

## 5. Proposed File Structure

**Update (v2):** file locations below now follow the pattern established by
Vineet's SHT41 driver rather than a standalone `firmware/main/sensors/`
folder, so the five drivers live alongside SHT41 as siblings in the shared
sensor-implementation component, and can depend on `sitetwin_sensor_runtime`
and `sitetwin_espidf_hal` directly:

```
components/sitetwin_sensors/
├── include/sitetwin/
│   ├── sht41.h            (Vineet, existing)
│   ├── bh1750.h
│   ├── pir.h
│   ├── reed.h
│   ├── ds18b20.h
│   └── ina219.h
└── src/
    ├── sht41.c             (Vineet, existing)
    ├── bh1750.c             — st_i2c_bus_t-based, single-channel adapter
    ├── pir.c                — pure debounce/confirmation state machine (Section 6), no adapter
    ├── reed.c                — pure debounce/confirmation state machine (Section 6), no adapter, implemented
    ├── ds18b20.c              — portable non-blocking 1-Wire driver
    └── ina219.c              — st_i2c_bus_t-based, TWO-channel module_instance

firmware/main/
├── activity_access_pod.c / .h   (composes BH1750 + PIR + Reed, event vs slow path)
└── equipment_pod_slow.c / .h    (composes DS18B20 + INA219; integrates with
                                   Vineet's ADXL345 fast path at the pod level,
                                   not inside these drivers)
```

DS18B20 is 1-Wire, not I2C, so it does not fit the existing `st_i2c_bus_t`
abstraction. The approved separation is now implemented with a small
`st_onewire_bus_t` HAL in `sensor_hal.h`:

```
portable DS18B20 protocol/state machine (sitetwin_sensors)
    → st_onewire_bus_t   (reset/presence, write-bit/byte, read-bit/byte)
    → sitetwin_espidf_hal's Espressif RMT-backed 1-Wire implementation
```

This keeps CRC generation, scratchpad decoding, and the conversion-state
machine fully host-testable against a fake `st_onewire_bus_t`; none of that
logic requires real hardware or the ESP-IDF toolchain to verify.

Each driver exposes only `st_driver_result_t probe(...)` and
`st_driver_result_t sample(...)` per `sensor_driver.h` (surfaced through a
`st_logical_channel_adapter_t` for BH1750/PIR/Reed/DS18B20, and through two
adapters sharing one `st_module_instance_t` for INA219) — no Zigbee, JSON, or
MQTT calls inside any driver file.

---

## 6. PIR / Reed Event Delivery (Revised — supersedes the original queue design)

**Revision note:** the original draft of this section proposed routing both
PIR and Reed through a driver-internal bounded event queue drained by
`sensor_registry`, requiring both to be wired into
`module_instance`/`logical_channel_adapter` like BH1750/INA219. While
implementing Reed, reading `pod_runtime.c` directly surfaced
`st_pod_runtime_emit_event()` — a push-based API Vineet already built
specifically for `ST_SENSOR_MOTION`/`ST_SENSOR_CONTACT` that bypasses the
poll-based registry model entirely. That is the actual intended path for
both sensors; the queue design below is retained only as a record of what
was superseded, not as the current design.

**Current design:** Reed and PIR (both implemented) are
each split into two layers:

1. **Portable debounce/confirmation logic** — pure state machine, no GPIO
   HAL, no dependency on `sitetwin_core`/`sitetwin_sensor_runtime`. Lives in
   `sitetwin_sensors` alongside the other drivers, fully host-testable.
   Reed's version is `st_reed_debounce_t` (Section 5).
2. **GPIO read + `emit_event()` call** — ESP-IDF/hardware-specific glue,
   belongs to the Pod composition layer (`activity_access_pod.c`, not yet
   written), not to the portable driver.

The ISR design below still applies to layer 2 — it governs how a GPIO edge
gets from an interrupt into task context safely, which is unchanged by
which downstream API (queue vs. `emit_event()`) ultimately consumes the
confirmed transition.

**ISR (IRAM-resident, per GPIO pin, installed via `gpio_isr_handler_add`)**
- Records no timestamp, no edge count, no GPIO level — no data of any kind.
- Performs no registry, reporting-policy, Zigbee, UART, telemetry, logging,
  or heap-allocating call.
- Sends a task notification using an ISR-safe FreeRTOS primitive
  (`xTaskNotifyFromISR` with a per-pin bit in the notification value, e.g.
  bit 0 = PIR, bit 1 = Reed, using `eSetBits` so multiple pending pins are
  never lost even if the task hasn't woken yet), then
  `portYIELD_FROM_ISR()` if a higher-priority task was woken.

**Task context (existing pod telemetry/tick task)**
- Blocks on `xTaskNotifyWait(0, ULONG_MAX, &notified_bits, timeout)`, where
  `timeout` is bounded by the next scheduled slow-sensor sample time
  (BH1750/DS18B20/INA219), so the loop still ticks on schedule even with no
  GPIO activity.
- On wake, for each bit set in `notified_bits`: **reads the GPIO level now**,
  and **records the timestamp now**, both entirely in task context — the ISR
  never touches either value.
- Runs `st_reed_debounce_update()` (or the equivalent PIR state machine)
  against that task-context timestamp and level. Only a **confirmed**
  transition calls `st_pod_runtime_emit_event()` directly — there is no
  separate queue to drain; `emit_event()` already pushes onto the standard
  telemetry outbound queue that every other sensor's readings flow through.

**Event semantics (unchanged by the revision above)**

*PIR:* a confirmed PIR record means **motion-start** — the transition from
"no motion detected" to "motion detected," not a continuous active-state
report and not a raw per-retrigger pulse. While motion remains continuously
detected, the retrigger-suppression window prevents additional motion-start
records from being emitted; the motion-event **count** (tracked separately,
per Section 2.2's logical outputs) increments on every confirmed motion-start
regardless of suppression, so downstream consumers can distinguish "one long
motion episode" from "several short ones" even though only the first
transition in each episode produces a record. The suppression window itself
is: once a motion-start record is emitted, no further motion-start record is
emitted until the PIR output returns to its inactive level for at least the
configured suppression duration — i.e. suppression is retrigger-collapsing,
not a fixed dead-time unrelated to the sensor's actual state.

*Reed:* every confirmed open→closed and closed→open transition is emitted as
its own `emit_event()` call, in the order it was confirmed. Multiple genuine
transitions that occur before the task drains its notification queue are
**not** reduced to "only the latest level" — `st_reed_debounce_update()` must
be called once per confirmed transition, each producing its own
`emit_event()` call. Three distinct layers are kept conceptually separate:
**raw edges** (every GPIO interrupt, collapsed by debounce and never
individually visible above the driver), **debounced state** (the reed
switch's current confirmed open/closed level, tracked inside
`st_reed_debounce_t`), and **emitted event sequence** (the ordered stream of
`emit_event()` calls that actually reaches the registry).

---

## 7. Driver Implementation Order

1. BH1750 (simplest I2C driver, validates the acquisition-scheduling pattern).
   **Approved to begin now.** Requirements: use `st_i2c_bus_t`; one-time
   high-resolution mode; non-blocking trigger/wait/read state machine;
   configurable address and timing; no transport calls inside the driver;
   host tests and pod target build. **Stop after this step** — do not begin
   Reed/PIR integration — and report the exact resulting commit SHA before
   proceeding.
2. Physical verification of BH1750 on real hardware
3. **Physically confirm the exact PIR module (voltage, output polarity,
   retrigger behaviour, available controls) — blocking prerequisite before
   step 5, per Section 2.2 / Section 3. Does not block step 4 (Reed).**
4. Reed/contact (interrupt + debounce pattern from Section 6, no I2C complexity)
5. PIR (same interrupt pattern as reed, adds retrigger suppression, only after
   step 3 is complete)
6. Activity/Access Pod composition (BH1750 + PIR + Reed, event vs. slow path,
   door-alarm local rule preserved behind the driver/rule-logic boundary)
7. Complete: `st_onewire_bus_t` HAL boundary reviewed and implemented.
8. Complete: DS18B20 non-blocking driver, host tests, RMT adapter, Equipment
   composition, ESP32-C6 target build, and physical probe test.
9. INA219 voltage/current (introduces calibration-register programming)
10. Propose INA219 `power_mw` contract addition — only after voltage/current
    is implemented and confirmed working, not before
11. Equipment Pod slow-path composition, integrated with Vineet's ADXL345 path
12. Power and reporting-rate tuning across both pods

---

## 8. Required Shared-Interface Changes

Only one shared-contract change is currently anticipated:

**Proposal: add `ST_SENSOR_POWER_MW` and `ST_UNIT_MILLIWATT`**
1. *Current limitation*: `contracts.h` defines voltage (V) and current (mA)
   kinds/units but no power kind/unit.
2. *Proposed change*: add `ST_SENSOR_POWER_MW` to `st_sensor_kind_t` and
   `ST_UNIT_MILLIWATT` to `st_unit_t`.
3. *Affected sensors*: INA219 only.
4. *Compatibility impact*: additive enum values, no change to existing
   readings' representation.
5. *Required tests*: unit test confirming the new kind/unit round-trip
   through JSON conversion, same as existing kinds.
6. *Zigbee payload impact*: none — the 30-byte payload's fixed layout does
   not change; only which `sensor_kind`/`unit` values are considered valid.

This proposal will be sent to Vineet before implementing `power_mw`, per the
shared-contract change process. Bus voltage and current will be implemented
first using existing approved types, independent of this proposal.

---

## 9. Test Plan Summary

Per-sensor test lists are in Section 2. Integration-level tests, run after
individual driver tests pass:

- Event priority: PIR/Reed events are never suppressed by routine
  change-suppression logic.
- Routine latest-state replacement: BH1750/DS18B20/INA219 unsent readings
  are correctly replaced by newer ones, not queued indefinitely.
- Quality-state reporting: no sensor ever reports a valid value alongside a
  conflicting failure flag; no zero is emitted as a placeholder for a missing
  reading.
- No regression in Zigbee payload encoding, UART framing, or JSON output
  caused by adding these five drivers.

Build/verification commands (per `INTEGRATION_VALIDATION.md`):
```
firmware/host_tests/run-tests.ps1
idf.py build   (pod build; gateway-wifi build only if shared contracts change)
```

Results will be reported per driver as: host-tested / target-compiled /
physically tested / end-to-end physically verified — not collapsed into a
single "done" status.
