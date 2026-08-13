---
title: SiteTwin Project Hub
aliases:
  - SiteTwin
  - SiteTwin Overview
project: SiteTwin
status: Three pod profiles integrated and physically validated
updated: 2026-08-13
tags:
  - sitetwin
  - hub
  - architecture
  - dissertation
---

# SiteTwin Project Hub

SiteTwin is a low-cost, modular digital twin sensing platform for indoor and workplace monitoring. The current baseline is a battery-first Zigbee sensing fabric built from three ESP32-C6 pods, a powered gateway, and a Raspberry Pi for storage, dashboards, soak testing, and ML-backed interpretation. The portable shared firmware foundation is implemented and host-tested. Physical hardware has now been acquired, so the project is moving from hardware-independent validation into board bring-up and integration.

## What We Are Building

- [[SiteTwin Data Flow - Beginner Guide]] - start here for a plain-language explanation
- [[Architecture Baseline]]
- [[Pod and Sensor Strategy]]
- [[Firmware Architecture]]
- [[Gateway and Data Flow]]
- [[Validation and Implementation Plan]]
- [[Hardware Bring-Up and Open Decisions]]
- [[Source References]]

## Locked Storyline

The project is no longer "one smart node with every sensor attached." The stronger dissertation narrative is a sensing fabric with a clear energy and processing split:

- battery-pod work is limited to sensing and processing that directly reduces sensor power or Zigbee airtime
- the powered gateway handles validation, duplicate rejection, stale-record rejection, JSON conversion, buffering, Wi-Fi retries, and MQTT delivery
- the Raspberry Pi handles long-term persistence, dashboards, soak-test analysis, and ML-backed interpretation

## Core Design Decisions

1. The system uses three logical pods: environment, activity and access, and equipment.
2. Zigbee is the pod network, while the Raspberry Pi is the long-run observability and compute layer.
3. A dual-SoC gateway is the recommended baseline because Zigbee coordination and Wi-Fi backhaul on one ESP32-C6 share the same 2.4 GHz radio.
4. Sensor modules should attach through a standardized electrical interface rather than forcing every sensor to share the same native protocol.
5. Pods should publish typed telemetry records over Zigbee, while JSON should start at the gateway or Raspberry Pi boundary.
6. Sampling and reporting are separate decisions: a pod may sample a sensor but suppress transmission when the value has not changed meaningfully.
7. Sequence gaps are allowed and expected because suppressed readings still consume sensor sequence numbers.
8. The acquired pod development board is the Espressif ESP32-C6-DevKitC-1.

## Current Firmware Progress

### Latest deployed milestone

The three numbered pod profiles now use real production sensor compositions.
Pod 2 and the INA219/ADXL345 paths on Pod 3 have produced canonical telemetry
through Zigbee, the UART gateway, MQTT, and ThingsBoard. Downstream RPC has also
been demonstrated through the Pi bridge, including physical LED actuation.
ESP-IDF v5.5.4 and ESP Zigbee SDK v2.0.3 remain the tested baseline. The
DS18B20 path is host-tested, target-compiled, and physically verified end to
end on Pod 3.

Implemented and passing host tests:

- shared telemetry contracts and quality flags
- generic sensor driver interface and hot-swap sensor registry
- sensor probing, warm-up, retry, fault, removal, and reattachment states
- priority telemetry queue with latest-state replacement
- configurable deadbands, minimum report intervals, and maximum-silence heartbeats
- immediate forwarding of events and quality-state changes
- binary gateway frame with CRC16 and canonical gateway-side JSON
- gateway validation and duplicate or stale sequence rejection
- fixed 30-byte Zigbee telemetry encoding and decoding
- gateway node registry with IEEE-address rejoin and short-address remapping
- gateway ingress, priority delivery queue, and JSON handoff pipeline
- deterministic fake sensors and a randomized stress harness

For a step-by-step explanation of how these pieces call each other, see [[SiteTwin Data Flow - Beginner Guide]].

Not yet implemented or hardware-validated:

- controlled vibration threshold characterization
- ADC/module-identification and power-management adapters for the final universal-port hardware
- deep-sleep behavior, MQTT batching, and persistent outage buffering
- real sensor characterization and final deadband tuning
- final universal-port ID multiplexing and immediate hot-swap detection circuitry; these remain team decisions and must not be treated as locked architecture

## Pod Map

- [[Architecture Baseline#Pod Topology|Pod topology]]
- [[Pod and Sensor Strategy#Environment Pod|Environment Pod]]
- [[Pod and Sensor Strategy#Activity and Access Pod|Activity and Access Pod]]
- [[Pod and Sensor Strategy#Equipment Pod|Equipment Pod]]

## Next Practical Work

- collect a report-ready multi-condition vibration dataset for Pod 3
- collect controlled idle/normal/induced ADXL345 datasets before selecting a threshold
- characterize ADC behavior, resistor-ID tolerances, sensor noise, warm-up time, current draw, and useful sampling cadence on real hardware
- confirm the final universal-port ID multiplexing, power-gating, and hot-swap architecture with the team before implementing it
- add gateway MQTT batching and simulated Wi-Fi outage recovery
- design commissioning, allow-listing, and sensor-slot registration messages
- validate gateway reliability through the tests listed in [[Validation and Implementation Plan]]
