---
title: SiteTwin Project Hub
aliases:
  - SiteTwin
  - SiteTwin Overview
project: SiteTwin
status: Three fixed pod profiles integrated; final-board controls gated
updated: 2026-08-14
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
- [[docs/control-layer|Alarm and Gateway-State Control Layer]]
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

The three numbered fixed-development profiles now use reconciled ESP-IDF
sensor compositions: SHT41, SCD41, and SGP40 on Pod 1; BH1750, PIR, and reed
on Pod 2; and INA219, ADXL345, and DS18B20 on Pod 3. I2 adds portable,
capability-targeted versioned configuration. I3 adds deterministic RPC
correlation and the bidirectional Pi/MQTT/UART/Zigbee command transport. C1
adds rule-derived alarm conditions, independent persisted acknowledgement,
volatile silence semantics, target NVS composition, and a bounded coordinator
freshness/multi-sensor incident engine. Physical outputs, dashboards, and
ML-driven control remain unchanged and gated.

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
- exact command/result correlation with timeout, duplicate, late-result, and
  reconnect handling
- stable alarm instance transitions with rule/evidence and clear/retrigger
- independent acknowledgement and silence state, including reboot reset
- pod rule/configuration and alarm acknowledgement persistence in NVS
- bounded gateway freshness, trend, multi-sensor evidence, incident transitions,
  and coordinator-restart recovery
- ThingsBoard attribute/telemetry separation for control state
- deterministic fake sensors and a randomized stress harness

For a step-by-step explanation of how these pieces call each other, see [[SiteTwin Data Flow - Beginner Guide]].

Not yet implemented or hardware-validated:

- controlled vibration threshold characterization
- assembled-PCB calibration and H3 composition of the implemented H1/H2
  ADC/module-identification and CD74HC4052M96 mux board-port layer
- live GPIO19 shared buzzer/LED control, pending electrical validation
- physical validation of C1 NVS migration and control-event publication
- ThingsBoard dashboard/rule-chain validation for the C1 projections
- deep-sleep behavior, MQTT batching, and persistent outage buffering
- real sensor characterization and final deadband tuning
- final universal-port runtime and immediate hot-swap behavior; the board topology is locked, but measured electrical behavior remains a prerequisite

## Pod Map

- [[Architecture Baseline#Pod Topology|Pod topology]]
- [[Pod and Sensor Strategy#Environment Pod|Environment Pod]]
- [[Pod and Sensor Strategy#Activity and Access Pod|Activity and Access Pod]]
- [[Pod and Sensor Strategy#Equipment Pod|Equipment Pod]]

## Next Practical Work

- collect a report-ready multi-condition vibration dataset for Pod 3
- collect controlled idle/normal/induced ADXL345 datasets before selecting a threshold
- characterize ADC behavior, resistor-ID tolerances, sensor noise, warm-up time, current draw, and useful sampling cadence on real hardware
- validate the locked universal-port ID, mux, hot-swap, and shared-indicator
  electrical behavior before enabling or composing the H1/H2 layer
- add gateway MQTT batching and simulated Wi-Fi outage recovery
- design commissioning, allow-listing, and sensor-slot registration messages
- validate gateway reliability through the tests listed in [[Validation and Implementation Plan]]
