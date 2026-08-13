---
title: Hardware Bring-Up and Open Decisions
project: SiteTwin
status: Active hardware integration
updated: 2026-08-13
tags:
  - sitetwin
  - hardware
  - bring-up
  - decisions
---

# Hardware Bring-Up and Open Decisions

This note is the handoff point for Codex and the project team as SiteTwin moves from host-tested portable firmware to physical hardware. It intentionally separates confirmed facts from architecture choices that still require team approval.

## Confirmed

- Physical hardware has been acquired.
- The pod development board in hand is the Espressif ESP32-C6-DevKitC-1.
- SiteTwin retains the three logical pod profiles: environment, activity/access, and equipment.
- The universal-module concept uses a six-contact physical interface carrying VCC, GND, shared SDA/SCL, and two auxiliary signal contacts.
- Most selected sensor ICs are digital I2C devices; PIR and reed/contact are digital GPIO-style signals; DS18B20 uses 1-Wire; ADXL345 supports I2C/SPI.
- A resistor-coded analogue identification mechanism is being evaluated so non-I2C modules can be identified independently of their native data protocol.
- Identification circuitry should be duty-cycled or otherwise gated so that a voltage-divider ID path does not impose unnecessary continuous battery drain.
- The existing portable C sensor-driver abstraction, registry, reporting policy, telemetry queues, Zigbee codec, and gateway core remain valid and should be preserved while hardware-specific adapters are added.
- Two ESP32-C6-DevKitC-1 boards have formed a SiteTwin Zigbee network and repeatedly delivered the real 30-byte SiteTwin payload from pod to gateway.
- ESP-IDF v5.5.4 and ESP Zigbee SDK v2.0.3 are the currently tested software baseline.
- The three pod identities are standardized as `POD_1`, `POD_2`, and `POD_3`.
- Pod 2 and all three Pod 3 sensor paths have been physically exercised through
  Zigbee and MQTT. DS18B20 operates on GPIO0 with the external DQ pull-up and
  produces valid temperature telemetry.

## Confirmed Firmware Direction

The hardware layer should hide board-specific operations behind small interfaces so portable policy does not depend on muxes, ADC registers, I2C transactions, or GPIO wiring.

Likely board-layer responsibilities include:

- initialize GPIO/I2C/ADC
- read a port's identification signal
- enable/disable ID measurement power
- detect insertion/removal events or request periodic re-scan
- instantiate the correct sensor driver after identification
- provide real `probe()` and `sample()` implementations
- attach/detach drivers from the existing sensor registry

The first straightforward real-sensor bring-up remains SHT41 over I2C because it can exercise the path from real sensor driver through the already-tested portable core.

## Decisions Still Open — Do Not Hard-Code Yet

### Port count and expansion

The team is considering a four-port baseline with an upgrade path, but the final physical port count and exact reserved GPIO budget are not yet confirmed. Do not make the firmware assume four, six, eight, or sixteen physical ports. Keep configurable compile-time/runtime limits where practical.

### Identification multiplexing

Several designs have been discussed:

- dedicated ID ADC per port
- multiplex ID lines only onto one ADC
- multiplex both ID and auxiliary DATA lines

No topology is finalized. The software should therefore model `port_index -> identify/read` rather than expose mux-channel assumptions to the sensor registry.

### Power gating

The objective of disconnecting the ID voltage-divider path outside active scans is agreed in principle. The exact implementation — MOSFET topology versus dedicated low-Iq load switch, shared versus per-port control — remains an electrical design decision.

Do not conflate ID-divider power gating with switching the main sensor VCC. These solve different power problems.

### Immediate hot-swap detection

Identification and change detection are separate functions. Possible change-detection approaches include periodic re-scan, GPIO/interrupt-based detection, comparator-based detection, or an interrupt-capable GPIO expander. The previously drafted shared comparator/diode-OR wake proposal is not yet a locked design and should not be implemented without review.

### I2C address collisions

Shared SDA/SCL is attractive for GPIO efficiency, but two identical fixed-address I2C modules on different universal ports cannot be distinguished by resistor ID alone. The final design must decide whether duplicate identical modules are a requirement and, if so, use an I2C switch/multiplexer or another isolation/address-management strategy.

### ADC ID bands

Do not assign final sensor-ID voltages or large arbitrary tolerance windows yet. The mapping must be characterized on real hardware using calibrated ADC readings and measured variation from:

- resistor tolerances
- 3.3 V rail variation
- ADC conversion/calibration error
- temperature
- connector/contact resistance
- noise
- any analogue mux on-resistance/leakage

Use multiple ADC samples and robust classification bands only after bench data exists.

## Immediate Bring-Up Sequence

1. Complete: pin ESP-IDF v5.5.4 and ESP Zigbee SDK v2.0.3, then prove pod-to-gateway SiteTwin telemetry on ESP32-C6 boards.
2. Complete for current prototypes: GPIO/I2C mappings and real sensor compositions for Pods 1-3.
3. Complete: real SHT41, SCD41, SGP40, BH1750, PIR, reed, INA219, and ADXL345 paths implemented and exercised.
4. Complete: physically validate the DS18B20 on Pod 3 using GPIO0 and an external approximately 4.7 kΩ DQ pull-up to 3.3 V.
5. Characterize sensor current, warm-up, noise, and practical sample cadence.
6. Prototype the resistor-coded ID circuit on a breadboard and capture ADC distributions for each proposed ID resistor.
7. After team confirmation, implement the selected port/mux/power-gating abstraction.
8. Add physical insertion/removal testing only after the identification path is stable.
9. Run multi-pod soak tests and collect report-ready evidence.

## Guidance for Codex

Treat this file plus `Architecture Baseline.md` and `Pod and Sensor Strategy.md` as the current architecture source of truth. Do not infer that discussion-stage mux, six-port, comparator, or exact pin-allocation proposals are approved. Ask before changing portable core contracts solely to accommodate one unconfirmed hardware topology.
