---
title: Pod and Sensor Strategy
project: SiteTwin
status: Firmware policy implemented
tags:
  - sitetwin
  - hardware
  - sensors
  - modules
---

# Pod and Sensor Strategy

Related: [[SiteTwin Project Hub]], [[SiteTwin Data Flow - Beginner Guide]], [[Architecture Baseline]], [[Firmware Architecture]], [[Source References]]

## Connector Standard

The current universal module interface is a 6-pin connector:

1. GND
2. SW_PWR
3. SDA
4. SCL
5. DETECT or INT
6. ID or 1-Wire

The main idea is to standardize the electrical interface, not force every raw sensor to speak the same native protocol.

## Hot-Swap Strategy

The most robust module-port design includes:

- a per-port load switch
- an I2C hot-swap buffer
- an I2C switch or multiplexer

This reduces the chance that one inserted or faulty module corrupts the shared bus.

## Identification Strategy

Recommended identification options in increasing sophistication:

1. resistor-coded ID for very early prototypes
2. EEPROM metadata for flexible modules
3. EEPROM plus unique ID for the best MVP balance
4. 1-Wire unique serial ID for minimal traceability hardware
5. secure element only if anti-cloning becomes a real requirement

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

## Activity and Access Pod

Sensors:

- PIR
- reed or contact switch
- BH1750

Behavior:

- mostly event-driven
- interrupt-based wakeups
- debounce and clean event shaping
- never suppress motion or contact events
- sample illuminance periodically but report it using deadband and heartbeat rules

## Equipment Pod

Sensors:

- ADXL345
- DS18B20
- INA219

Behavior:

- slow-sensor path for temperature and current
- feature-based vibration path for normal operation
- local feature extraction is justified here before raw streaming
- transmit derived vibration features instead of continuous raw samples during normal operation
- reserve raw capture for explicit diagnostic commands or detected anomalies

## Module Classes

- Passive native modules: SHT41, SCD41, SGP40, BH1750, INA219
- MCU-assisted modules: PIR, reed or contact, DS18B20, future oddball sensors
- Edge-processing smart module later if needed: ADXL345

## Shared Pod Power Policy

The implemented portable firmware now suppresses unchanged routine telemetry before it enters the outbound queue. This saves radio airtime, but it does not yet reduce the number of sensor measurements. Hardware-specific power savings will be added in the ESP-IDF board layer through:

- deep sleep between scheduled work
- GPIO interrupt wake-up for PIR, reed, and FIFO events
- per-port load-switch control for sensors that can be fully powered down
- sensor-native low-power or single-shot modes
- adaptive sampling only after real testing establishes how quickly each physical signal can change

Adaptive sampling must not be used blindly: sampling too slowly can hide a short-lived change even though reporting suppression itself is safe. The first hardware measurements should therefore characterize sensor warm-up time, noise, current draw, and useful sampling cadence separately.
