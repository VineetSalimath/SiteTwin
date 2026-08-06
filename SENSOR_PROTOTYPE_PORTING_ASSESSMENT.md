# SiteTwin Sensor Prototype Porting Assessment

Status: Option C approved; implementation recorded in linked note

Date: 2026-08-06

Branch: `feature/sensor-runtime-foundation`

Base branch: `integration/end-to-end-v1`

Exact base commit: `c38230a970843d420097cfb3f302eb529c1fda91`

Implementation approval was received on 2026-08-06 for Option C, the strict
first-failure policy, and invalid-quality normalization including `STALE`.
Implementation details and verification evidence are recorded in
[[SENSOR_RUNTIME_FOUNDATION_IMPLEMENTATION]].

## Scope and approval boundary

This assessment recorded the baseline and the smallest compatible sensor-runtime proposal before implementation. The existing Arduino sketches remain useful hardware-validation prototypes and are preserved unchanged.

The initial stop point was satisfied when Option C, the optional acquisition timestamp, and the stricter validity normalization were approved. The linked implementation note records the resulting foundation and SHT41 work. Yicheng's drivers, SCD41, SGP40, ADXL345, final hot-swap electronics, and transport-contract changes remain outside this branch's approved scope.

## Baseline evidence

### Repository state

- The remote was fetched before the feature branch was created.
- Local and remote `integration/end-to-end-v1` both resolved to `c38230a970843d420097cfb3f302eb529c1fda91`, with zero divergence.
- `feature/sensor-runtime-foundation` was created directly at that commit.
- Two pre-existing user changes were present before the branch was created and were preserved: a modified `Architecture Baseline.md` and an untracked `SiteTwin_PortScaling_HotSwap_Design_Note.docx`.
- Generated build trees and managed components remain ignored and are not candidates for commit.

### Baseline validation results

| Validation | Result | Evidence boundary |
| --- | --- | --- |
| Host tests | Passed | `firmware/host_tests/run-tests.ps1`; 384 telemetry records processed; all tests passed |
| `git diff --check` | Passed | No whitespace error; Git emitted only the pre-existing LF-to-CRLF working-copy warning for `Architecture Baseline.md` |
| ESP-IDF pod target | Compiled | ESP32-C6, ESP-IDF v5.5.4, Zigbee SDK v2.0.3; `firmware/build_pod/sitetwin_firmware.bin` linked and size-checked |
| ESP-IDF coordinator target | Compiled | ESP32-C6, ESP-IDF v5.5.4, Zigbee SDK v2.0.3; `firmware/build_gateway/sitetwin_firmware.bin` linked and size-checked |
| Wi-Fi gateway target | Compiled | ESP32-C6, ESP-IDF v5.5.4; `gateway-wifi/build/gateway-wifi.bin` linked and size-checked |
| Physical sensor test | Not run | No board was flashed or wired during this assessment |
| End-to-end physical test | Not run | Compilation and host tests do not establish physical operation |

The build environment's stock `export.ps1` points to a missing default Python environment. The successful builds used the repository's cached/pinned tool paths directly and required normal access for the ESP-IDF component manager and Git/MSYS process creation. This is a reproducibility issue to document, not a firmware result.

## Prototype assessment

The prototypes validated provisional wiring, selected sensor communication, and useful local behavior. Their monolithic loops and blocking calls are appropriate for hardware validation, but not the production runtime architecture.

| Sensor | Prototype source | Useful validated behaviour | Temporary limitations | Planned production driver | Logical outputs | Open hardware questions |
| --- | --- | --- | --- | --- | --- | --- |
| SHT41 | `firmware/sensor_firmwares/Pod-1.ino` | High-precision temperature/humidity; heater disabled; readings compensate SGP40 | Arduino/Adafruit dependency; blocking call inside a one-second monolithic loop; fixed provisional bus pins | Vineet-owned ESP-IDF driver behind the shared module/channel adapter layer; CRC checked; non-monolithic conversion state | Temperature in degrees Celsius; relative humidity in percent from one acquisition | Final port routing; bus isolation for duplicate addresses; pull-ups; connector assignment; power switching; confirmed GPIO/controller selection |
| SCD41 | `firmware/sensor_firmwares/Pod-1.ino` | Wake/stop/reinit/start sequence; periodic measurement; readiness check; valid CO2 state; local threshold alarm | Blocking initialization delays; one-second loop; fixed provisional pins/threshold; no production warm-up/quality integration | Future Vineet-owned readiness-driven ESP-IDF driver after SHT41 approval and validation | CO2 ppm; any additional outputs require a later explicit scope decision | Final venting/layout; port routing; shared-bus collision strategy; power-cycle/warm-up policy |
| SGP40 | `firmware/sensor_firmwares/Pod-1.ino` | SHT41 compensation when available; prototype falls back to 25 C/50% RH when SHT41 is unavailable | Silent default compensation; Arduino library/algorithm dependency; monolithic cadence; no compensation quality metadata | Future Vineet-owned compensated ESP-IDF path after SHT41; fallback only if separately approved | VOC index | Approved algorithm/library; exact cadence; explicit fallback policy; thermal/airflow placement |
| BH1750 | `firmware/sensor_firmwares/Pod_2.ino` | Continuous high-resolution light measurement at prototype address `0x23` | Polled in monolithic loop; fixed address/mode/pins; no reporting integration | Future Yicheng-owned ESP-IDF driver; not implemented on this branch | Illuminance in lux | Address strap; placement/optical window; bus routing; power policy |
| PIR | `firmware/sensor_firmwares/Pod_2.ino` | Five-second stabilization; digital motion state | Polled instead of interrupt driven; provisional GPIO; startup delay blocks all setup | Future Yicheng-owned GPIO/event driver; not implemented on this branch | Motion event/state | Sensor voltage/output levels; wake-capable GPIO; retrigger behavior; stabilization time; final connector mapping |
| Reed/contact switch | `firmware/sensor_firmwares/Pod_2.ino` | Door state; input pull-up; approximately 30 ms software debounce; local door alarm | Polled instead of interrupt driven; provisional active level/GPIO; fixed debounce and alarm behavior | Future Yicheng-owned GPIO/event driver; not implemented on this branch | Contact/door event and state | Normally-open/closed choice; wiring length/noise; pull-up; wake-capable GPIO; final active level |
| DS18B20 | `firmware/sensor_firmwares/Pod_3.ino` | Device discovery; 12-bit temperature; disconnection detection; local over-temperature contribution | Blocking conversion; index-based single-device selection; provisional 1-Wire pin; monolithic loop | Future Yicheng-owned asynchronous 1-Wire driver; not implemented on this branch | Surface temperature in degrees Celsius | Parasite versus powered mode; cable length/pull-up; ROM addressing; connector routing; power control |
| INA219 | `firmware/sensor_firmwares/Pod_3.ino` | Bus voltage, current, and power measurement | Arduino dependency; default calibration/address assumptions; one-second loop; no explicit range/quality model | Future Yicheng-owned ESP-IDF slow-path driver; not implemented on this branch | Voltage and current; power may be added later only through an approved logical-channel decision | Shunt value/rating; measurement range; address strap; high-side wiring; isolation and connector safety |
| ADXL345 | `firmware/sensor_firmwares/Pod_3.ino` | Three-axis acceleration at provisional address `0x53`; +/-16 g range; basic movement threshold and local equipment alarm | Single-sample magnitude threshold; no FIFO/interrupt path; no vibration windowing/features; one-second loop | Future Vineet-owned ESP-IDF FIFO/acquisition path plus vibration feature extraction, after SHT41 | At least vibration RMS; further features require bounded, explicit channel definitions | I2C versus SPI; FIFO interrupt GPIO; sample rate/range; mounting/orientation; clipping; bus routing; diagnostic raw-capture policy |

Pod-level prototype observations are therefore retained as requirements:

- Pod 1 checks SCD41 readiness and uses periodic measurement, drives a local CO2 alarm, and uses SHT41 compensation for SGP40. Its 25 C/50% RH default is evidence of prototype resilience, not an approved production fallback.
- Pod 2 uses BH1750 continuous mode, an approximately 30 ms door debounce, PIR stabilization, and a local door alarm. Production motion/contact acquisition should be event driven where the confirmed wiring permits it.
- Pod 3 combines blocking DS18B20 conversion, INA219 slow measurements, and a single ADXL345 sample in a common one-second loop. Production vibration acquisition must be separate from the slow-sensor path and must derive features from a sample window/FIFO.

## Architecture boundaries retained

The pod pipeline remains:

`physical module -> sensor driver -> sensor registry -> pod reporting policy -> bounded priority queue -> fixed 30-byte Zigbee payload -> Zigbee transport`

The gateway pipeline remains:

`Zigbee coordinator -> CRC-framed UART -> Wi-Fi ESP -> canonical JSON -> MQTT over TLS -> laptop/Raspberry Pi backend`

Sensor drivers must not contain Zigbee, JSON, MQTT, dashboard, or ML code. The laptop remains the main development/demonstration processing host, the Raspberry Pi remains the unattended soak-test host, and pod-local rules remain operable without either host.

## Exact limitations of the current driver contract

The current interface is sound for a single-output sensor, asynchronous retry, and bounded host testing, but it has the following multi-output limitations:

1. `st_module_metadata_t` contains exactly one `sensor_id`, one `sensor_kind`, one `unit`, and one sample interval.
2. `st_sensor_driver_t.sample()` returns exactly one `st_driver_sample_t` containing one numeric value and one unit.
3. `st_sensor_port_t` owns one metadata record, one due time, one state, and one sequence counter.
4. `st_sensor_registry_tick()` can emit at most one reading per attached registry slot per tick and its normal output buffer is sized to `ST_MAX_SENSOR_PORTS`.
5. The registry's name and lifecycle currently conflate a logical sampling slot with a physical port. A multi-output physical device therefore cannot be represented as one physical port without an adapter/group layer or a contract extension.
6. A failed or missing driver result changes registry state but emits no observation. After a prior valid sample, an adapter can preserve the last finite value and mark it stale/failed; before the first valid sample, there is no clean numeric value to transmit. Using zero would be incorrect, and sending NaN would require a separately approved canonical-JSON policy.
7. The registry automatically adds `ST_QUALITY_VALID` unless `CRC_FAILED` or `SENSOR_MISSING` is set. It can therefore currently combine `VALID` with `WARMING_UP` or `OUT_OF_RANGE`. The desired validity semantics must be tested and, if changed, approved because fake-sensor tests currently expect warming-up readings to carry `VALID`.
8. `ST_MAX_SENSOR_PORTS` and `ST_REPORTING_STATE_CAPACITY` are both eight. They are bounded software capacities, not confirmed physical port counts, but multi-channel adapters consume one entry per logical channel.

The current contract can represent SHT41 cleanly without a protected-header edit if two logical channel adapters share one physical SHT41 context and acquisition cache. The limitations above still need to be documented because they affect lifecycle grouping, capacity, and first-failure semantics.

## Compatible multi-channel approaches

### Option A: independent logical channel adapters over a shared context

Each logical output implements the existing `st_sensor_driver_t`; temperature and humidity adapters point to the same SHT41 context. The first due adapter starts or completes the physical transaction, and the second consumes the same cached acquisition.

- Required type changes: none in the shared core; two sensor-specific adapter contexts are added.
- Registry impact: consumes two registry slots; current registry code is unchanged.
- Hot-swap impact: an external owner must attach/detach both adapters together; the registry itself does not know they form one module.
- Sequence behavior: independent monotonically increasing sequences per logical channel, matching gateway deduplication by logical sensor ID.
- Memory cost: approximately one additional `st_sensor_port_t` (roughly 100 bytes on a 32-bit target, toolchain-layout dependent) plus one reporting state for the second channel; the physical SHT41 acquisition buffer is shared.
- Test impact: add shared-acquisition and grouped lifecycle tests; existing tests remain unchanged.
- Fake sensors: fully compatible.
- 30-byte payload: fully compatible; one existing record per channel.
- Yicheng impact: none; existing single-output drivers remain valid.
- Merge risk: low for the protected core, but ad-hoc attach/detach grouping is easy to implement inconsistently.

### Option B: bounded multi-observation driver result

One physical driver poll returns a bounded batch of logical observations. This models multi-output hardware directly but changes protected contracts and every registry driver call site.

Illustrative complete signature set for this option (not recommended for the first milestone):

```c
#define ST_DRIVER_MAX_OBSERVATIONS 4U

typedef struct {
    char sensor_id[ST_SENSOR_ID_MAX_LEN];
    st_sensor_kind_t sensor_kind;
    st_unit_t unit;
} st_channel_metadata_t;

typedef struct {
    char module_uid[ST_MODULE_UID_MAX_LEN];
    uint32_t sample_interval_ms;
    uint8_t channel_count;
    st_channel_metadata_t channels[ST_DRIVER_MAX_OBSERVATIONS];
} st_module_metadata_t;

typedef struct {
    uint8_t channel_index;
    float value;
    uint32_t quality_flags;
} st_driver_observation_t;

typedef struct {
    uint64_t acquired_at_ms;
    uint8_t observation_count;
    st_driver_observation_t observations[ST_DRIVER_MAX_OBSERVATIONS];
} st_driver_observation_batch_t;

typedef struct {
    void *context;
    st_driver_result_t (*probe)(void *context, st_module_metadata_t *metadata);
    st_driver_result_t (*sample)(void *context, uint64_t now_ms,
                                 st_driver_observation_batch_t *batch);
} st_sensor_driver_t;
```

Registry state would also need per-channel sequences:

```c
uint32_t next_sequence[ST_DRIVER_MAX_OBSERVATIONS];
```

- Required type changes: `contracts.h`, `sensor_driver.h`, `sensor_registry.h`, and registry/runtime implementation; output-buffer sizing must account for `physical slots * observations` without allocating an unbounded array.
- Registry impact: direct physical-module semantics and bounded fan-out, but more complex partial-capacity handling and per-channel metadata validation.
- Hot-swap impact: naturally grouped because one registry entry owns one physical module.
- Sequence behavior: must be per logical channel, not one shared counter, so suppressed or failed channels remain independently explainable.
- Memory cost: fixed channel metadata and sequence arrays are paid by every registry entry, including single-output modules; at a bound of four, this is roughly 100-150 additional bytes per entry depending on alignment.
- Test impact: migrate all fake drivers and every registry test; add batch bounds, partial invalid channel, capacity exhaustion, and per-channel sequence tests.
- Fake sensors: source incompatible until adapted.
- 30-byte payload: compatible after fan-out; each observation still becomes one existing record.
- Yicheng impact: forces changes or compatibility wrappers in parallel driver work.
- Merge risk: highest because shared headers and common tests change while two owners are developing drivers.

### Option C: explicit physical-module group plus existing logical adapters

Add a small module/group layer above the unchanged registry. One physical driver/context owns probe, acquisition state, cached results, and removal status. Bounded logical adapters expose those results through the existing `st_sensor_driver_t`. A module manager records which physical port owns which logical registry slots and attaches/detaches the group together.

- Required type changes: new headers/types only; no edits to the five protected shared headers.
- Registry impact: unchanged; registry indices are treated as logical slots by the new manager, not as mux channels.
- Hot-swap impact: grouped attach/detach and re-probe are explicit while physical identification/change detection remain outside the generic registry.
- Sequence behavior: independent per logical channel through the current registry, while both SHT41 observations retain one cached acquisition time.
- Memory cost: similar to Option A plus a small bounded module-manager entry and adapter contexts; no per-entry multi-channel tax on single-output drivers.
- Test impact: additive host tests for the new grouping layer; existing fake-sensor and transport tests remain source compatible.
- Fake sensors: unchanged; optional grouped fake modules can be added without migrating existing fakes.
- 30-byte payload: fully compatible; one existing telemetry record per logical adapter.
- Yicheng impact: none for current drivers; the grouping layer is available later for INA219 without forcing adoption now.
- Merge risk: lowest because additions are isolated and protected shared files remain unchanged.

## Recommended minimal design

Recommend Option C for the SHT41 milestone. It applies the preferred model:

`physical SHT41 -> one shared driver context -> temperature and humidity logical adapters -> one existing telemetry record per adapter`

The smallest proposed architecture change is additive: introduce a bounded physical-module/logical-adapter component and a board-port abstraction, then implement SHT41 against those additions. Do not modify `contracts.h`, `sensor_driver.h`, `sensor_registry.h`, `pod_runtime.h`, or `reporting_policy.h` for this milestone.

One shared behavior correction is also proposed in `sensor_registry.c`: an observation carrying `CRC_FAILED`, `OUT_OF_RANGE`, or `SENSOR_MISSING` must not also carry `VALID`. Warming-up observations may remain valid, preserving the current fake-sensor behavior. This is a source-only rule change with no type or function signature change, but it still requires approval before implementation.

### Proposed additive signatures

The following signatures are the proposed review contract for new files. Names may be adjusted only through review; none change a current shared type.

```c
#define ST_MODULE_MAX_CHANNELS 4U
#define ST_MODULE_ID_DATA_MAX 16U

typedef enum {
    ST_HAL_OK = 0,
    ST_HAL_NOT_PRESENT,
    ST_HAL_BUSY,
    ST_HAL_TIMEOUT,
    ST_HAL_IO_ERROR,
    ST_HAL_UNSUPPORTED
} st_hal_result_t;

typedef struct {
    uint8_t data[ST_MODULE_ID_DATA_MAX];
    uint8_t length;
} st_module_identity_t;

typedef struct {
    char sensor_id[ST_SENSOR_ID_MAX_LEN];
    st_sensor_kind_t sensor_kind;
    st_unit_t unit;
} st_logical_channel_metadata_t;

typedef struct {
    char module_uid[ST_MODULE_UID_MAX_LEN];
    uint32_t sample_interval_ms;
    uint8_t channel_count;
    st_logical_channel_metadata_t channels[ST_MODULE_MAX_CHANNELS];
} st_physical_module_metadata_t;

typedef struct {
    void *context;
    st_driver_result_t (*probe)(void *context, st_physical_module_metadata_t *metadata);
    st_driver_result_t (*acquire)(void *context, uint64_t now_ms);
    st_driver_result_t (*read_channel)(void *context, uint8_t channel_index,
                                       st_driver_sample_t *sample);
} st_physical_module_driver_t;

typedef struct {
    st_physical_module_driver_t *module;
    uint8_t channel_index;
} st_logical_channel_adapter_t;

int st_logical_channel_adapter_init(st_logical_channel_adapter_t *adapter,
                                    st_physical_module_driver_t *module,
                                    uint8_t channel_index);
st_sensor_driver_t st_logical_channel_adapter_driver(st_logical_channel_adapter_t *adapter);
```

The board port interface expresses intent and exposes no mux arithmetic, resistor bands, pin assignments, polarity, or final port count:

```c
typedef struct {
    void *context;
    size_t (*port_count)(void *context);
    st_hal_result_t (*port_detect_present)(void *context, size_t port_index, bool *present);
    st_hal_result_t (*port_power_set)(void *context, size_t port_index, bool enabled);
    st_hal_result_t (*port_read_module_id)(void *context, size_t port_index,
                                           st_module_identity_t *identity);
    st_hal_result_t (*port_select_bus)(void *context, size_t port_index);
    st_hal_result_t (*port_enable_bus)(void *context, size_t port_index);
    st_hal_result_t (*port_disable_bus)(void *context, size_t port_index);
    st_hal_result_t (*port_clear_fault)(void *context, size_t port_index);
} st_board_port_ops_t;
```

The physical lifecycle belongs in the new module manager, not in mux-specific code or the existing sensor registry:

```c
typedef enum {
    ST_MODULE_EMPTY = 0,
    ST_MODULE_PRESENT_UNIDENTIFIED,
    ST_MODULE_POWER_SETTLING,
    ST_MODULE_IDENTIFYING,
    ST_MODULE_PROBING,
    ST_MODULE_WARMING_UP,
    ST_MODULE_ACTIVE,
    ST_MODULE_FAULTED,
    ST_MODULE_REMOVED
} st_module_lifecycle_state_t;

typedef struct st_module_manager st_module_manager_t;

void st_module_manager_init(st_module_manager_t *manager, st_board_port_ops_t board);
void st_module_manager_tick(st_module_manager_t *manager, st_sensor_registry_t *registry,
                            uint64_t now_ms);
int st_module_manager_attach_driver(st_module_manager_t *manager, size_t physical_port,
                                    st_physical_module_driver_t driver,
                                    const uint8_t *registry_slots, size_t slot_count);
void st_module_manager_remove(st_module_manager_t *manager, st_sensor_registry_t *registry,
                              size_t physical_port, uint64_t now_ms);
```

No final detection logic is proposed. A compile-safe fake board can return `ST_HAL_UNSUPPORTED` until hardware identification and change detection are approved.

The portable I2C boundary used by SHT41 is also additive:

```c
typedef struct {
    void *context;
    st_hal_result_t (*write)(void *context, uint8_t address,
                             const uint8_t *data, size_t length);
    st_hal_result_t (*read)(void *context, uint8_t address,
                            uint8_t *data, size_t length);
} st_i2c_bus_t;

typedef struct {
    st_i2c_bus_t bus;
    uint8_t address;
    uint32_t sample_interval_ms;
    const char *temperature_sensor_id;
    const char *humidity_sensor_id;
} st_sht41_config_t;

typedef struct st_sht41 st_sht41_t;

int st_sht41_init(st_sht41_t *sensor, const st_sht41_config_t *config);
st_physical_module_driver_t st_sht41_module_driver(st_sht41_t *sensor);
```

The complete proposed validity-rule change has no public signature. Its implementation rule is:

```c
#define ST_QUALITY_INVALID_MASK \
    (ST_QUALITY_CRC_FAILED | ST_QUALITY_OUT_OF_RANGE | ST_QUALITY_SENSOR_MISSING | \
     ST_QUALITY_STALE)

if ((reading->quality_flags & ST_QUALITY_INVALID_MASK) == 0U) {
    reading->quality_flags |= ST_QUALITY_VALID;
} else {
    reading->quality_flags &= ~ST_QUALITY_VALID;
}
```

Required migration tests are: the existing warming-up sample remains `VALID`; CRC-failed, out-of-range, and missing samples are not `VALID`; a transition back to an in-range sample restores `VALID`; payload encoding, UART framing, and JSON for existing valid records remain unchanged.

The ESP-IDF composition root supplies the selected I2C controller and pins. The portable SHT41 code does not hard-code them. Initial mode is high precision; the heater command is not exposed and remains disabled.

### Migration path

1. Add the new portable module, adapter, board-intent, and I2C interfaces with fake host implementations.
2. Wrap existing fake single-output drivers unchanged; prove existing tests still pass.
3. Add a grouped fake module with two channels and prove one acquisition produces two readings with the same acquisition time and independent sequences.
4. Implement SHT41 CRC/conversion/state logic behind `st_physical_module_driver_t`.
5. Add the ESP-IDF I2C adapter with controller/pin/address configuration in the pod composition layer.
6. Attach the two SHT41 logical adapters to two configured registry/radio sensor slots.
7. Run the existing reporting policy with configurable development defaults: 0.2 C and 1% RH deadbands, 30 s minimum interval, 15 min heartbeat.
8. Replace only the temporary pod health producer at the composition boundary after local SHT41/runtime tests pass.
9. Re-run host, pod, coordinator, and Wi-Fi gateway builds plus transport/JSON regression tests.

If the selected logical-channel count later exceeds the existing registry/reporting capacities, propose a separate bounded capacity change with measured memory impact. Do not interpret the current limit of eight as the final physical port count.

## SHT41 invalid-data policy requiring approval

The proposed driver never substitutes zero for an invalid measurement.

- CRC failure: set `ST_QUALITY_CRC_FAILED`; do not update the cached valid acquisition.
- Out-of-range conversion: set `ST_QUALITY_OUT_OF_RANGE`; do not update the cached valid acquisition.
- After at least one valid sample: the adapter may emit the last finite value with `ST_QUALITY_STALE` plus the failure flag so reporting policy can transmit the quality transition without changing JSON or radio formats.
- Before the first valid sample: emit no telemetry value; retain the fault in module/driver state and local diagnostics. A later first valid sample reports immediately.

This avoids zero and avoids non-finite JSON. If the project requires transmission of a first-read failure with no numeric value, the existing JSON/telemetry value contract is insufficient. Options such as NaN-on-radio and JSON `null`, or a distinct health record, require a separate compatibility analysis and approval because they affect canonical downstream semantics.

## Local behavior boundary

The prototypes establish local-alarm requirements, not final thresholds. The proposed boundary is:

`sensor observation -> small pod-specific local rule function -> output request -> LED/buzzer output service`

Thresholds belong in pod configuration, never the low-level sensor driver. No generic rule engine is proposed. SHT41 foundation work should define only a small output-request interface and a fake output sink; the Environment Pod CO2 rule is implemented later with SCD41, not inside SHT41.

## Open hardware questions

The following remain deliberately unresolved:

- final physical port count and logical-channel capacity relationship;
- final ESP32-C6 I2C controller, SDA/SCL pins, bus speed, pull-ups, and GPIO assignments;
- connector pin mapping and whether every module receives switched main power;
- module-ID electrical scheme, resistor values, ADC bands, classification guard bands, and ID-divider gating;
- insertion/removal change detector and any wake circuit;
- mux/switch type, channel mapping, ID-only versus ID-plus-data multiplexing, and bus enable polarity;
- whether duplicate fixed-address I2C modules must operate simultaneously and how they will be isolated;
- port power-switch polarity, settling time, and fault feedback;
- SHT41 placement, venting, self-heating influence, and verified wiring on the final board;
- exact physical mapping from logical registry/radio sensor slots to attached module channels.

None of these is encoded in the proposed portable interfaces.

## Planned file structure after approval

```text
components/
  sitetwin_sensor_runtime/
    include/sitetwin/board_port.h
    include/sitetwin/i2c_bus.h
    include/sitetwin/logical_channel_adapter.h
    include/sitetwin/module_manager.h
    include/sitetwin/physical_module.h
    include/sitetwin/local_output.h
    src/logical_channel_adapter.c
    src/module_manager.c
  sitetwin_sensors/
    include/sitetwin/sht41.h
    src/sht41.c
firmware/
  components/sitetwin_espidf_hal/
    include/sitetwin/espidf_i2c_bus.h
    src/espidf_i2c_bus.c
  main/
    environment_pod.c
firmware/host_tests/
  test_sht41.c
  test_sensor_module_runtime.c
```

The exact CMake split may be simplified during implementation, but portable driver logic remains free of ESP-IDF types and ESP-IDF board code remains outside `sitetwin_core`.

## Test plan after approval

### Portable SHT41 tests

- CRC success for each returned word;
- CRC failure on temperature and humidity independently;
- raw temperature conversion;
- raw relative-humidity conversion;
- conversion boundaries and clamping/range rejection;
- no invalid reading represented as zero;
- high-precision command and non-blocking conversion/retry state;
- two logical observations from one physical acquisition with the same acquisition timestamp;
- configurable interval and address;
- heater remains disabled;
- missing sensor, failed read, retry, removal, grouped detach, reattachment, and re-probe.

### Runtime/reporting tests

- first valid temperature and humidity reported;
- unchanged values suppressed independently;
- 0.2 C and 1% RH deadband crossings;
- 30 s minimum interval;
- 15 min maximum-silence report;
- immediate quality-state transition after a prior valid value;
- independent per-channel sequences;
- queue replacement/priority behavior under two-channel production;
- registry output-capacity handling with grouped modules;
- local output request works while gateway/backend is absent.

### Compatibility tests

- existing fake sensors compile and pass unchanged;
- warming-up fake samples retain `VALID`, while CRC-failed, out-of-range, and missing samples do not;
- Zigbee payload size remains exactly 30 bytes and codec round trips pass;
- custom cluster remains `0xFC00` and command path is unchanged;
- CRC16 UART framing round trips and corruption rejection pass;
- canonical JSON golden test remains byte-for-byte unchanged for valid readings;
- Wi-Fi gateway/MQTT schema code compiles unchanged;
- pod, coordinator, and Wi-Fi gateway ESP-IDF targets compile;
- physical SHT41 test and complete end-to-end test are reported separately from compilation.

## Compatibility conclusion and approval gate

The recommended Option C and validity correction do not change:

- `st_sensor_reading_t` or `st_telemetry_record_t`;
- the fixed 30-byte Zigbee payload or custom cluster `0xFC00`;
- the CRC16 UART frame;
- canonical JSON fields or MQTT schema;
- gateway processing, deduplication, or address mapping;
- existing fake-sensor signatures;
- Yicheng's current driver API or owned implementation files.

The source-only validity correction changes only the contradictory combination of `VALID` with an invalidating quality flag; it adds no field and changes no wire encoding. Option C adds one existing telemetry record per logical SHT41 channel. Temperature and humidity use separate configured sensor slots and independent sequences; their values originate from one shared physical acquisition and are cached together. Sampling remains separate from reporting.

Approval is requested for Option C, the source-only invalid-quality mask, and the first-failure policy above. No SHT41, module manager, HAL, local-output, or shared-core implementation should begin until that approval is given. SCD41, SGP40, ADXL345, and all Yicheng-owned final drivers remain out of scope until the SHT41 path is working and reviewed.

## Sensor-integration evidence record

| Milestone | Branch | Commit SHA | Date | Hardware | Test procedure | Observed result | Failure/limitation | Evidence/log location |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Prototype-to-production assessment and baseline | `feature/sensor-runtime-foundation` | Recorded by the documentation commit in Git history and the handoff report | 2026-08-06 | No hardware used in this assessment; earlier prototypes used project sensor hardware | Fetch/branch verification; host suite; `git diff --check`; pod/coordinator/Wi-Fi ESP-IDF builds; source review | Host suite passed; three targets compiled; production boundary and options documented | No flashing, physical SHT41, or end-to-end physical validation; local tool export path mismatch documented | Console output from this work session; this assessment; ignored ESP-IDF build logs/artifacts |

Future milestones must append branch, exact commit, date, hardware, procedure, observed result, limitations, and durable evidence location. Compilation must never be recorded as physical validation.
