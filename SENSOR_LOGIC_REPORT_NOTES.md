---
title: SiteTwin Sensor Logic Report Notes
project: SiteTwin
status: Working report note; all current fixed-pod sensor paths physically verified
date: 2026-08-13
tags:
  - sitetwin
  - firmware
  - sensors
  - validation
  - dissertation
---

# SiteTwin Sensor Logic Report Notes

This note records the logic currently used for every SiteTwin sensor, including
the relationship between the original Arduino prototypes and the production
ESP-IDF drivers. It deliberately distinguishes software completion from
physical validation. The ADXL345 end-to-end path is now verified, but a full
mounted baseline/threshold dataset is still required before this material is
treated as final vibration-threshold evidence.

## System-wide sensor pipeline

The production data path is:

```text
physical sensor
  -> portable sensor driver or event state machine
  -> logical channel / sensor registry
  -> reporting policy
  -> bounded telemetry queue
  -> 30-byte Zigbee record
  -> gateway
```

Sensor drivers contain no Zigbee, MQTT, JSON, dashboard, or machine-learning
logic. A multi-output physical sensor, such as SHT41 or INA219, performs one
physical acquisition and exposes the result through separate logical channels.
Each logical channel retains its own sensor ID, sequence number, reporting
state, and Zigbee slot.

Routine sensors use change-based reporting with a maximum heartbeat interval.
PIR and reed sensors instead emit event records immediately after a confirmed
transition. Fault handling uses explicit quality metadata rather than replacing
missing or invalid measurements with zero.

## Implementation and ownership summary

| Pod | Sensor | Production output | Implementation position | Current validation status |
| --- | --- | --- | --- | --- |
| Environment / Pod 1 | SHT41 | Temperature and relative humidity | Production driver on `feature/environment-pod-sensors` | Host-tested and target-compiled; environment hardware has been exercised, final evidence sweep pending |
| Environment / Pod 1 | SCD41 | CO2 concentration | Production driver on `feature/environment-pod-sensors` | Host-tested and target-compiled; environment hardware has been exercised, final evidence sweep pending |
| Environment / Pod 1 | SGP40 | Compensated VOC index | Production driver on `feature/environment-pod-sensors` | Host-tested and target-compiled; environment hardware has been exercised, final evidence sweep pending |
| Activity / Pod 2 | BH1750 | Illuminance | Yicheng's production driver, reused in the composed Activity image | Pod 2 reported physically complete; logs/results to consolidate later |
| Activity / Pod 2 | Reed switch | Contact open/closed events | Yicheng's production state machine, reused in the Activity image | Pod 2 reported physically complete; logs/results to consolidate later |
| Activity / Pod 2 | SR505 PIR | Motion-start events | Vineet-owned production state machine and ESP-IDF composition | Pod 2 reported physically complete; logs/results to consolidate later |
| Equipment / Pod 3 | INA219 | Bus voltage and current | Yicheng's production driver, reused in the composed Equipment image | Physically verified through Zigbee, UART, MQTT, and ThingsBoard |
| Equipment / Pod 3 | ADXL345 | Vibration RMS in g | Vineet-owned production driver and feature extraction | Physically verified through Zigbee, UART, MQTT, and ThingsBoard; full mounted threshold dataset still pending |
| Equipment / Pod 3 | DS18B20 | Surface temperature | Portable production driver plus ESP-IDF RMT 1-Wire HAL | Host-tested, target-compiled, and physically verified end to end on 2026-08-13 |

## Environment Pod sensor logic

### SHT41 temperature and relative humidity

The SHT41 is connected to the shared I2C bus at address `0x44`. The production
driver first reads the device serial number and validates both returned words
with the Sensirion CRC-8 calculation (initial value `0xFF`, polynomial `0x31`).
This makes the probe stronger than simply assuming that an ACK represents the
expected device.

Measurements use the high-precision command `0xFD`. The driver is non-blocking:
it sends the command, waits 9 ms through its state machine, and then reads one
temperature/humidity frame. The two output channels share that one acquisition
and timestamp.

Raw values are converted with:

```text
temperature_C = -45 + 175 * raw_temperature / 65535
humidity_%RH  =  -6 + 125 * raw_humidity / 65535
```

Both measurement words must pass CRC. Temperature must remain within
`-40..125 degrees C` and humidity within `0..100 %RH`. A failed read can retain
the last valid value temporarily with `STALE` quality; repeated failures force
a new probe. The configured physical sampling interval is 1 second and the
shared acquisition cache is valid for 250 ms.

Default reporting policy:

- Temperature: `0.2 degrees C` deadband, 30-second minimum, 15-minute heartbeat.
- Relative humidity: `1 %RH` deadband, 30-second minimum, 15-minute heartbeat.

### SCD41 carbon dioxide

The SCD41 uses I2C address `0x62`. The driver uses a non-blocking command/response
state machine and a serial-number probe with CRC validation. If the sensor is
already measuring when probed, the driver safely stops periodic measurement
before reading identity and restarting the configured measurement mode.

The current Environment image uses low-power periodic measurement, which
produces an update approximately every 30 seconds. The registry checks the
data-ready status at a 1-second cadence and reads a measurement only when the
sensor reports that new data is available. CO2 is accepted over the explicit
range `1..40000 ppm`; invalid CRC or out-of-range values are rejected.

Although the SCD41 frame also contains temperature and humidity, only CO2 is
published. SHT41 remains the Environment Pod's canonical temperature/humidity
source. The default CO2 reporting rule is a `50 ppm` deadband, a 30-second
minimum interval, and a 5-minute heartbeat.

The Arduino prototype's `1000 ppm` LED/buzzer alarm was a local demonstration
rule. It is not part of the production sensor driver and should not be confused
with measurement validity or reporting policy.

### SGP40 VOC index

The SGP40 uses I2C address `0x59`. Its identity response and raw measurement
response are CRC-checked. The sensor does not directly output a finished VOC
index; it outputs a raw gas signal that must be processed by Sensirion's gas
index algorithm at a regular cadence.

The production cadence is 1 second. Before every raw measurement, the driver
obtains a recent SHT41 sample and encodes compensation values as:

```text
humidity_ticks    = round(humidity_%RH * 65535 / 100)
temperature_ticks = round((temperature_C + 45) * 65535 / 175)
```

Compensation must be no more than 2 seconds old. Unlike the prototype, the
production driver does not silently substitute `25 degrees C / 50 %RH`; stale
or unavailable compensation is explicitly represented with
`COMPENSATION_UNAVAILABLE`, and the VOC algorithm is reset when continuity is
lost. During the algorithm's conditioning period the channel reports its
warming-up state rather than presenting an unqualified result.

The accepted VOC index range is `1..500`. The default reporting rule is a
5-index-point deadband, a 30-second minimum interval, and a 5-minute heartbeat.

## Activity / Access Pod sensor logic

### BH1750 illuminance

The BH1750 uses I2C address `0x23`. Because the device has no readable serial
number, presence is established by successfully sending its idempotent
`Power On` command. Measurements use one-time high-resolution mode (`0x20`),
which lets the device return to power-down automatically after conversion.

The driver sends the measurement command, waits the datasheet's maximum
conversion bound of 180 ms without blocking the firmware task, then reads the
big-endian 16-bit result. With the default sensitivity (`MTreg = 69`):

```text
illuminance_lux = raw / 1.2
```

The maximum raw value `0xFFFF` is treated as saturation/out-of-range. The
BH1750 protocol provides no CRC, so validity relies on I2C transaction status,
range checking, stale-value handling, and reprobe after repeated failures.
Physical sampling is configured at 1 second.

Default reporting policy: `10 lux` deadband, 30-second minimum interval, and
15-minute heartbeat.

### SR505 PIR motion

The SR505 is an active-high digital sensor connected to GPIO1 in the prototype
mapping. Its production logic is event-driven rather than treating a held-high
level as a continuous stream of readings.

1. The first 5 seconds after startup are a stabilization period. Activity seen
   during this interval is never emitted as real motion.
2. A post-warm-up sample establishes the baseline state.
3. Only an inactive-to-active edge creates a `motion-start` event.
4. Repeated samples while the output remains high are suppressed.
5. The sensor must return inactive before it can be armed for another motion
   episode.
6. A configurable 1-second retrigger-suppression interval collapses implausibly
   rapid duplicate starts.

The GPIO ISR performs no logging, timing, telemetry, or sensor processing. It
only queues the pin number through an ISR-safe FreeRTOS primitive. GPIO level
sampling, stabilization, edge confirmation, event counting, and telemetry
creation all occur in task context. After stabilization, the task blocks until
an edge instead of continuously polling the pin.

PIR output is an immediate `ST_RECORD_EVENT` with boolean value `1`, sensor kind
`MOTION`, and Activity Pod slot 1. Routine deadband/heartbeat suppression is not
applied to events.

### Reed/contact switch

The reed switch uses GPIO0 with the internal pull-up enabled. The provisional
mapping is raw high = open and raw low = closed. The first observation seeds the
known state without inventing a transition from an unknown state.

Every raw level change starts or restarts a 30 ms debounce timer. A state is
confirmed only if it remains unchanged for the complete debounce interval and
differs from the previously confirmed state. Repeated observations of an
already confirmed state are ignored. Both open-to-closed and closed-to-open
transitions therefore produce exactly one ordered contact event.

As with PIR, the ISR is wake-only and all GPIO reads, timestamps, debounce logic,
and event creation occur in task context. Reed output is an immediate boolean
`CONTACT` event on Activity Pod slot 2; open is represented as `1` and closed as
`0`.

## Equipment Pod sensor logic

### INA219 voltage and current

The INA219 uses I2C address `0x40` and shares the Equipment Pod bus with the
ADXL345. The production configuration assumes the current breakout's
provisional `0.1 ohm` shunt and a maximum expected current of `3.2 A`. These
values must remain configurable because the shunt fitted to the physical board
ultimately determines the valid current range.

Calibration follows the INA219 equations:

```text
Current_LSB_A = maximum_expected_current_A / 32768
Calibration   = trunc(0.04096 / (Current_LSB_A * shunt_resistance_ohm))
```

The calibration register is written during probe. The driver uses triggered
shunt-and-bus conversion rather than leaving the ADC free-running. It configures
32 V bus range, +/-320 mV shunt range, and 12-bit bus/shunt conversion. After a
2 ms non-blocking wait, it also checks the hardware conversion-ready (`CNVR`)
bit before accepting the result.

Bus voltage is decoded by discarding the three status bits and applying the
4 mV LSB:

```text
bus_voltage_V = (bus_register >> 3) * 0.004
```

Current is a signed two's-complement register:

```text
current_mA = signed_current_register * Current_LSB_mA
```

The overflow (`OVF`) bit produces `OUT_OF_RANGE` quality. Voltage and current
share one acquisition and timestamp, exposed through logical slots 0 and 1.
Power is intentionally not published because the current telemetry contract has
no approved power sensor kind/unit.

Default reporting policy:

- Voltage: `0.1 V` deadband, 10-second minimum, 5-minute heartbeat.
- Current: `50 mA` deadband, 10-second minimum, 5-minute heartbeat.

### ADXL345 vibration RMS

The ADXL345 uses I2C address `0x53`. Probe succeeds only when register `0x00`
contains device ID `0xE5`. The driver then places the device in standby while
configuring:

- Full-resolution, `+/-16 g` range (`DATA_FORMAT = 0x0B`).
- 100 Hz output data rate (`BW_RATE = 0x0A`).
- FIFO stream mode with a minimum window of 16 samples.
- Measurement mode after configuration is complete.

The original Arduino prototype processed one sample using:

```text
motion = abs(sqrt(x^2 + y^2 + z^2) - 9.81 m/s^2)
```

That approach is useful as a simple movement demonstration, but it is sensitive
to single-sample noise and assumes a fixed gravity magnitude. The production
driver instead reads a FIFO window and removes the mean of each axis. For `N`
samples, with each raw axis converted using approximately `0.0039 g/LSB`:

```text
mean_x = sum(x_i) / N
mean_y = sum(y_i) / N
mean_z = sum(z_i) / N

vibration_RMS_g = sqrt(
    sum((x_i - mean_x)^2 +
        (y_i - mean_y)^2 +
        (z_i - mean_z)^2) / N
)
```

The implementation uses the algebraically equivalent form:

```text
RMS^2 = E[x^2 + y^2 + z^2] - mean_x^2 - mean_y^2 - mean_z^2
```

Removing the axis means removes static gravity, constant mounting orientation,
and constant DC offset. The remaining value represents changing acceleration
energy across all three axes rather than motion in one chosen direction.

The FIFO contains at most 32 samples. At 100 Hz, the configured 16-to-32-sample
window represents approximately the latest 160-to-320 ms of vibration. The
firmware currently calculates this snapshot every 5 seconds; it does **not**
claim to integrate vibration continuously across the entire 5-second interval.
If fewer than 16 samples are available, acquisition is retried without emitting
a result.

Raw values at approximately `+4095` or `-4096` indicate the 12-bit measurement
limit and add `CLIPPED` quality because the true acceleration may exceed the
captured value. The output is a derived `FEATURE` record in g on Equipment Pod
slot 2.

Default reporting policy: `0.02 g` deadband, 5-second minimum interval, and
1-minute heartbeat. No machine-running or fault threshold is hard-coded. A
threshold must be derived from the mounted idle, normal-operation, and abnormal
or induced-vibration measurements gathered during physical validation.

### DS18B20 surface temperature

The production DS18B20 driver is portable and talks through
`st_onewire_bus_t`; the ESP32-C6 adapter uses Espressif's RMT-backed 1-Wire
component. The current prototype assumes one externally powered three-wire
probe on GPIO0. It reads and CRC-validates the 64-bit ROM, requires family code
`0x28`, and derives a stable module UID from that ROM.

The driver writes the selected 9-to-12-bit resolution and then uses a
non-blocking state machine: reset/presence, `Skip ROM`, `Convert T`, wait for the
resolution-specific conversion duration, then reset, `Read Scratchpad`, and
decode. Conversion bounds are 94, 188, 375, and 750 ms for 9, 10, 11, and
12 bits respectively. The Equipment image uses 12-bit resolution and a
1-second physical sampling interval.

Scratchpad byte 8 must match the Dallas/Maxim CRC-8 over bytes 0-to-7
(reflected polynomial `0x8C`). The signed 16-bit temperature word is divided by
16, preserving negative values, and must fall within `-55..125 degrees C`.
The canonical output is `POD_3/ds18b20_temperature`, state class, degrees
Celsius, on slot 3.

On a CRC or transport failure the driver can report the last valid value as
`STALE` with the relevant fault quality. Missing-presence responses produce
`SENSOR_MISSING`, and repeated failures return the module to probing so a
removed/reconnected probe can recover. Host tests cover CRC vectors, negative
temperature, every conversion duration, non-blocking behaviour, stale fallback,
and removal/reattachment. The ESP32-C6 Equipment image builds successfully.
Physical validation of the powered waterproof probe passed on 2026-08-13 using
GPIO0 and an external approximately 4.7 kΩ pull-up from DQ to 3.3 V. Valid
temperature readings responded to physical temperature change and reached the
canonical gateway/server path as `POD_3/ds18b20_temperature`.

## DS18B20 physical verification record

- Date: 2026-08-13.
- Pod: `POD_3` Equipment profile on ESP32-C6.
- Connection: three-wire externally powered probe; VCC 3.3 V, GND common,
  DQ GPIO0, external approximately 4.7 kΩ DQ-to-3.3 V pull-up.
- Firmware result: the production driver detected the probe and emitted valid
  `temperature_c` state records in Celsius.
- Response test: temperature changed in the expected direction when the probe
  was warmed and returned toward ambient afterward.
- Transport result: canonical `POD_3/ds18b20_temperature` telemetry completed
  the Zigbee, UART, MQTT, and server path.
- Limitation: exact min/mean/max temperature values were not retained in this
  note; this record establishes functional physical validation, not a calibrated
  accuracy experiment.

## Shared quality and failure behaviour

Where supported, production drivers distinguish:

- `CRC_FAILED`: the transport succeeded but sensor integrity validation failed.
- `OUT_OF_RANGE`: a decoded value or hardware overflow is outside the approved
  range.
- `SENSOR_MISSING`: the physical device no longer responds.
- `STALE`: the last valid finite value is retained temporarily after a failed
  acquisition.
- `WARMING_UP`: the device or algorithm is not ready to provide a mature value.
- `COMPENSATION_UNAVAILABLE`: a compensated measurement lacks sufficiently
  recent environmental input.
- `CLIPPED`: the accelerometer reached its representable raw limit.

Failure values are never fabricated as zero. Drivers reprobe after loss or
repeated failures, while the registry prevents conflicting failure flags from
being presented as an ordinary valid observation.

## ADXL345 physical verification record

The first end-to-end verification was completed on 2026-08-11. The retained
gateway log proves a valid stationary feature reached the Wi-Fi gateway and was
acknowledged by MQTT. A longer controlled dataset is still required for a
defensible application threshold.

### Test configuration

- Date/time: 2026-08-11
- ESP32-C6 / pod identifier: `POD_3`, observed Zigbee short address `0x304E`
- Firmware commit: `56764e1` plus the working Equipment image under test
- ADXL345 breakout and address strap:
- Supply voltage:
- Mounting position and attachment method:
- Equipment/test source:
- Gateway short address:

### Measured results

| Condition | Window count | RMS minimum (g) | RMS mean (g) | RMS maximum (g) | Samples/window | Quality flags |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| Sensor stationary on bench | At least 2 retained records | 0.004 | 0.004 | 0.004 | At least 16 by driver acceptance rule | `VALID` (`1`) |
| Sensor mounted, equipment off |  |  |  |  |  |  |
| Equipment normal operation |  |  |  |  |  |  |
| Controlled tap/shake |  |  |  |  |  |  |

### Acceptance evidence

- Device ID `0xE5` successfully detected: passed indirectly; the production driver cannot enter normal sampling without it.
- Repeated windows contain at least 16 samples: passed indirectly; the driver retries rather than emitting below this bound.
- Mounted idle RMS is stable enough to establish a baseline: not yet measured as a controlled dataset.
- Controlled vibration produces a repeatable increase above idle: qualitatively exercised, but exact values were not retained in the evidence log.
- No unexpected `CLIPPED` flag during observed stationary operation: passed (`quality_flags=1`).
- Slot 2 telemetry reaches Zigbee, UART, and MQTT: passed; canonical record was `POD_3/adxl345_vibration` and MQTT acknowledgements were observed.
- Proposed operational/fault threshold and justification: pending measured data.

## Documentation sweep after ADXL345 verification

The first reconciliation was performed on 2026-08-11. The following documents
must continue to distinguish end-to-end transport verification from the still
incomplete controlled vibration threshold experiment:

- `firmware/README.md`
- `INTEGRATION_VALIDATION.md`
- `Hardware Bring-Up and Open Decisions.md`
- `Pod and Sensor Strategy.md`
- `Firmware Architecture.md`
- `SENSOR_PROTOTYPE_PORTING_ASSESSMENT.md`
- `YICHENG_SENSOR_DRIVER_PLAN.md`
- `Source References.md`
- `SiteTwin Project Hub.md`
- this note

The sweep should replace outdated planned/future language, preserve the
distinction between host tests, target compilation, physical sensor validation,
and end-to-end gateway validation, and include the actual commit IDs and
measured ADXL345 baseline rather than inferred values.
