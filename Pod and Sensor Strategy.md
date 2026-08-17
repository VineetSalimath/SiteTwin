---
title: Pod and Sensor Strategy
project: SiteTwin
status: Three fixed pod profiles implemented; H1/H2 final-board interface under bring-up
updated: 2026-08-17
tags:
  - sitetwin
  - hardware
  - sensors
  - modules
---

# Pod and Sensor Strategy

Related: [[SiteTwin Project Hub]], [[SiteTwin Data Flow - Beginner Guide]], [[Architecture Baseline]], [[Firmware Architecture]], [[Hardware Bring-Up and Open Decisions]], [[Source References]]

## Connector Standard

The final board has four 6-contact universal connectors, J7 through J10. Their
confirmed functional nets are:

1. 3V3
2. GND
3. shared I2C SDA
4. shared I2C SCL
5. auxiliary DATA / GPIO
6. auxiliary identification-capable line

The interface standardizes the electrical boundary without forcing every raw
sensor to use one native protocol. U2 selects the four resistor-coded ID lines
onto `ID_ADC`/GPIO0. U4 selects the four non-I2C DATA lines onto
`DATA_COMMON`/GPIO3. Both are CD74HC4052M96 devices. I2C remains shared on
GPIO10/GPIO11. The board does not provide confirmed per-port sensor-power
switching; GPIO18 gates only the temporary precision ID pull-up.

## Identification Strategy

Each adapter carries a resistor-coded identity. U2 selects the port and the
main board enables a switched 10 kOhm pull-up only while it acquires multiple
GPIO0 ADC samples. The H2 board-port layer implements this measurement and a
clearly provisional midpoint classifier. Final acceptance bands still require
assembled-PCB calibration.

This concept is particularly useful for modules such as PIR and reed/contact sensors that do not expose protocol-level identity. I2C devices may additionally be probed at protocol level after the port has been classified.

Important implementation constraints:

- identification voltages and guard bands must be derived from bench measurements rather than assumed values
- ADC calibration, resistor tolerance, supply variation, mux effects if used, noise, and connector resistance must be included in characterization
- the identification divider should not remain continuously energized merely to preserve an already-known identity; reducing its standby current is a design objective
- unknown or out-of-range values must remain `UNKNOWN_ID`
- the fixed U2/U4 select mapping and GPIO18 LOW-enable/HIGH-disable sequence
  must be preserved

## Hot-Swap Strategy

Hot-swap remains a requirement. The final board now fixes separate low-current
change detection and precision identification mechanisms, so the firmware
continues to separate two concepts:

1. **Identification:** determine what module is attached to a port.
2. **Change detection:** determine that a module has been inserted, removed, or replaced and trigger a re-identification operation.

The permanent 1 MOhm paths feed a TS884 whose four outputs reach GPIO6, GPIO7,
GPIO22, and GPIO23. Their active polarity still needs bench confirmation. The
H2 board layer defines those inputs without arming interrupts or inferring
presence from an unverified polarity. Full debounced rescan and registry
attach/detach belong to H3.

The sensor registry already supports logical probing, removal, fault, and
reattachment. The new board-port skeleton provides mux mapping and safe raw ID
acquisition; it does not yet provide the dynamic module manager.

## Environment Pod

Sensors:

- SHT41
- SCD41
- SGP40

Behavior:

- periodic reads
- compensation-aware processing
- careful venting and thermal layout
- suppress routine reports inside sensor-specific deadbands
- retain maximum-silence heartbeats so an unchanged room is distinguishable from a failed pod

Native interface: these planned sensors are digital I2C devices.

## Activity and Access Pod

Sensors:

- PIR
- reed or contact switch
- BH1750

Behavior:

- mostly event-driven
- interrupt-based wakeups are preferred for PIR and contact events
- debounce and clean event shaping
- never suppress motion or contact events
- sample illuminance periodically but report it using deadband and heartbeat rules

Native interfaces:

- BH1750: digital I2C
- PIR: digital GPIO/event signal
- reed/contact: digital GPIO/contact signal

## Equipment Pod

Sensors:

- ADXL345
- DS18B20
- INA219

Behavior:

- slow-sensor path for temperature and current/power measurements
- feature-based vibration path for normal operation
- local feature extraction is justified here before raw streaming
- transmit derived vibration features instead of continuous raw samples during normal operation
- reserve raw capture for explicit diagnostic commands or detected anomalies

Native interfaces:

- ADXL345: digital I2C at `0x53` on the current prototype bus
- DS18B20: digital 1-Wire on current prototype GPIO0, externally powered, with
  an approximately 4.7 kΩ DQ-to-3.3 V pull-up
- INA219: digital I2C; its internal analogue measurement is converted before the ESP receives the data

The fixed Pod 3 software composition is complete: INA219 is on slots 0/1,
ADXL345 vibration RMS is on slot 2, and DS18B20 temperature is on slot 3.
INA219, ADXL345, and DS18B20 are physically verified end to end. The DS18B20
test confirmed the powered GPIO0 probe and external DQ pull-up arrangement.
This fixed prototype composition does not itself implement automatic
universal-port identification.

On the final board DS18B20 is Type 5 with the 10 kOhm ID code. Its non-I2C
DATA path is selected by CD74HC4052M96 and reaches `DATA_COMMON` on GPIO3, so
the fixed GPIO0 prototype must not be used as final-board routing. GPIO19 is a
single shared buzzer/LED low-side branch and remains electrically gated. Pod 3
has no motor-control or motor-current-cut capability.

## Shared-Bus Constraint

Most planned sensors are I2C devices, so sharing SDA and SCL reduces GPIO demand. However, resistor-coded port identity does not solve duplicate fixed-I2C-address conflicts. If two identical fixed-address modules must be supported simultaneously, the hardware needs either an I2C switch/multiplexer or another isolation/addressing strategy. This decision remains open for the final universal-port design.

## Shared Pod Power Policy

The implemented portable firmware suppresses unchanged routine telemetry before it enters the outbound queue. This saves radio airtime, but it does not yet reduce the number of sensor measurements.

Hardware-specific power savings to validate during bring-up include:

- deep sleep between scheduled work
- GPIO interrupt wake-up for PIR, reed/contact, and accelerometer FIFO events where the final wiring permits it
- switched identification-divider power so resistor-coded ID circuitry does not draw continuously after scanning
- per-port sensor power switching where worthwhile
- sensor-native low-power or single-shot modes
- adaptive sampling only after real testing establishes how quickly each physical signal can change

Identification-divider power gating and actual sensor power gating are separate optimizations and should be measured separately.

Adaptive sampling must not be used blindly: sampling too slowly can hide a short-lived change even though reporting suppression itself is safe. The first hardware measurements should therefore characterize sensor warm-up time, noise, current draw, ADC identification stability, and useful sampling cadence separately.
