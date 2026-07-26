---
title: Gateway and Data Flow
project: SiteTwin
status: Core policy implemented
tags:
  - sitetwin
  - gateway
  - raspberry-pi
  - mqtt
---

# Gateway and Data Flow

Related: [[SiteTwin Project Hub]], [[SiteTwin Data Flow - Beginner Guide]], [[Architecture Baseline]], [[Firmware Architecture]], [[Validation and Implementation Plan]]

For a basic explanation of how one reading travels through the whole system, see [[SiteTwin Data Flow - Beginner Guide]].

## Gateway Role

The gateway is not battery constrained, so it should absorb processing that does not need to happen before Zigbee transmission. It terminates Zigbee records, validates and deduplicates them, creates canonical JSON, buffers outages, and publishes to the Raspberry Pi over MQTT. If the gateway is split across two SoCs, the internal handoff should be a framed binary UART protocol rather than console text.

## Processing Allocation

The pod only performs work that can reduce sensor power or Zigbee airtime: deadband checking, minimum interval enforcement, heartbeat scheduling, event shaping, and selected vibration feature extraction.

The gateway should perform:

- identifier, enum, boot ID, sequence, and numeric-value validation
- duplicate and stale-record rejection
- calibration and unit normalization where raw pod transmission is more efficient
- timestamp mapping from pod uptime to gateway time
- JSON creation and schema evolution
- routine-message batching and immediate event forwarding
- Wi-Fi and MQTT retries with persistent buffering
- derived metrics, anomaly preparation, and gateway health reporting

Validation, node and sensor-slot resolution, duplicate or stale rejection, priority queueing, and JSON handoff are implemented in the portable firmware core. MQTT batching and persistent storage remain gateway-runtime work.

## Recommended Internal Envelope

Suggested UART frame fields:

- start marker
- version
- message type
- payload length
- source address
- boot ID
- sequence
- payload
- CRC16

The shared firmware currently implements this versioned envelope and verifies its CRC16 during host tests.

This framed envelope is intended for the internal UART connection in a dual-SoC gateway. It is
separate from the pod Zigbee payload.

## Zigbee Telemetry Payload

Pod telemetry uses a fixed 30-byte binary payload containing:

- payload version
- record class and delivery priority
- sensor kind, unit, and sensor slot
- quality flags
- sequence and boot ID
- 64-bit pod uptime
- 32-bit floating-point value

The Zigbee source short address is supplied by the receive callback. The gateway registry links it to
a stable IEEE address and pod ID, while the sensor slot resolves to a canonical sensor ID. If a node
rejoins with a new short address, its IEEE identity preserves the existing slot bindings.

## MQTT Topic Structure

Suggested topic families:

- `sitetwin/pods/ENV_01/telemetry`
- `sitetwin/pods/ACT_01/events`
- `sitetwin/pods/EQP_01/features`
- `sitetwin/pods/+/health`
- `sitetwin/gateway/status`
- `sitetwin/commands/ENV_01/config`
- `sitetwin/commands/EQP_01/raw_capture`

## JSON Boundary

The gateway-to-Pi boundary is the right place to produce canonical JSON envelopes. That keeps the radio side compact and typed while making downstream storage and dashboards easier to evolve.

The canonical JSON serializer is implemented and tested. Pods do not build JSON.

## Gateway Ingress Policy

For each pod and sensor, the gateway tracks the current boot ID and highest accepted sequence number:

- the first valid record is accepted
- a repeated sequence in the same boot is classified as a duplicate
- a lower sequence in the same boot is classified as stale
- a new boot ID starts a new sequence session
- a delayed record from the immediately previous boot is rejected as stale
- forward sequence gaps are accepted because pod reporting suppression intentionally creates gaps

The gateway also rejects empty or unsafe identifiers, invalid enum values, sequence zero, boot ID zero, and non-finite numeric values before JSON or MQTT processing.

## Raspberry Pi Services

The Raspberry Pi should host four lightweight services:

1. gateway bridge or MQTT collector
2. storage service for JSONL and SQLite
3. state or ML service
4. dashboard or API service

## Buffering Policy

When Wi-Fi drops:

- retain alerts, door events, and latest health first
- allow routine environment readings to overwrite stale unsent copies
- drop or abort large raw vibration captures before higher-value messages

The in-memory 64-record priority delivery queue is implemented. Persistent outage storage is still planned and should ensure a gateway reset does not erase important queued events.
