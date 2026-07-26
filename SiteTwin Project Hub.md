---
title: SiteTwin Project Hub
aliases:
  - SiteTwin
  - SiteTwin Overview
project: SiteTwin
status: Firmware foundation implemented
date: 2026-07-26
tags:
  - sitetwin
  - hub
  - architecture
  - dissertation
---

# SiteTwin Project Hub

SiteTwin is a low-cost, modular digital twin sensing platform for indoor and workplace monitoring. The current baseline is a battery-first Zigbee sensing fabric built from three ESP32-C6 pods, a powered gateway, and a Raspberry Pi for storage, dashboards, soak testing, and ML-backed interpretation. A portable shared firmware foundation is now implemented and host-tested before hardware acquisition.

## What We Are Building

- [[SiteTwin Data Flow - Beginner Guide]] - start here for a plain-language explanation
- [[Architecture Baseline]]
- [[Pod and Sensor Strategy]]
- [[Firmware Architecture]]
- [[Gateway and Data Flow]]
- [[Validation and Implementation Plan]]
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

## Current Firmware Progress

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

Not yet implemented because it depends on hardware or final transport choices:

- real ESP-IDF I2C, GPIO, interrupt, Zigbee, and power-management adapters
- FreeRTOS task composition and deep-sleep behavior on the selected boards
- ESP Zigbee custom-cluster adapter, commissioning, MQTT batching, persistent outage buffer, and Raspberry Pi services
- real sensor characterization and final deadband tuning

## Pod Map

- [[Architecture Baseline#Pod Topology|Pod topology]]
- [[Pod and Sensor Strategy#Environment Pod|Environment Pod]]
- [[Pod and Sensor Strategy#Activity and Access Pod|Activity and Access Pod]]
- [[Pod and Sensor Strategy#Equipment Pod|Equipment Pod]]

## Next Practical Work

- add gateway MQTT batching and simulated Wi-Fi outage recovery
- design commissioning, allow-listing, and sensor-slot registration messages
- create thin ESP Zigbee SDK adapter files once the SDK is installed and version-pinned
- bring up the SHT41 board adapter when the first ESP32-C6 hardware arrives
- measure real sensor noise and tune the reporting thresholds in [[Firmware Architecture]]
- validate gateway reliability through the tests listed in [[Validation and Implementation Plan]]
