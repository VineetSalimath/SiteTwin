---
title: Firmware Architecture
project: SiteTwin
status: Hardware Zigbee bring-up complete
tags:
  - sitetwin
  - firmware
  - esp-idf
  - telemetry
---

# Firmware Architecture

Related: [[SiteTwin Project Hub]], [[SiteTwin Data Flow - Beginner Guide]], [[Architecture Baseline]], [[Pod and Sensor Strategy]], [[Gateway and Data Flow]]

For a non-technical walkthrough of the same architecture, start with [[SiteTwin Data Flow - Beginner Guide]].

## Shared Codebase

The software structure is one shared ESP-IDF codebase with pod profiles, not four unrelated firmware projects. The portable core has now been implemented in C without ESP-IDF dependencies, allowing its behavior to be compiled and stress-tested on the development computer before hardware is available.

## Implemented Portable Core

The current firmware contains:

- typed `st_sensor_reading_t` and `st_telemetry_record_t` contracts
- a generic `probe` and `sample` sensor-driver interface
- an eight-port sensor registry with probing, warm-up, ready, faulted, removal, and reattachment behavior
- a 32-record priority queue that replaces stale state records but protects higher-priority events
- a pod runtime that converts sensor readings into state, feature, or event telemetry
- a pod reporting policy for change suppression and heartbeats
- gateway binary framing with CRC16
- gateway-side JSON conversion
- a gateway processor that validates records and rejects duplicates and stale sequences
- a fixed 30-byte Zigbee telemetry codec with explicit little-endian fields
- a gateway node registry that maps IEEE and short addresses plus sensor slots to canonical IDs
- a gateway runtime with ingress processing, a 64-record priority delivery queue, and JSON handoff
- fake sensor drivers and native host tests

Current reading flow:

`sensor driver -> sensor registry -> pod reporting policy -> priority queue -> 30-byte Zigbee codec -> ESP Zigbee custom command -> gateway ingress`

Current gateway flow:

`ESP Zigbee custom-command callback -> payload decode -> validation/deduplication -> delivery queue -> UART frame -> server/Wi-Fi ESP -> JSON -> MQTT adapter (future)`

## Deployed Zigbee Bring-Up

The ESP-IDF composition root now has a real ESP Zigbee SDK v2.0.3 adapter. The same source produces
separate images in `build_pod` and `build_gateway`: the pod is a Zigbee End Device and the gateway is
a Zigbee Coordinator. The pod sends the implemented fixed 30-byte SiteTwin payload through custom
cluster `0xFC00`, command `0x01`; the gateway validates it with the existing portable runtime.

This was deployed and observed on two ESP32-C6-DevKitC-1 boards. The pod restored its network and
sent periodic SiteTwin health frames; the gateway logged `ingress result 0` for each frame. The
temporary health record is a bring-up source only. Real sensor drivers will replace it without
changing the queue, codec, or Zigbee transport.

## Common Task Model

The ESP-IDF deployment remains task based using FreeRTOS. The portable core does not create tasks itself; future composition code will call it from these common tasks:

- pod manager
- acquisition
- processing
- telemetry

The equipment pod also gets a dedicated vibration acquisition path. This separation keeps fast vibration acquisition from blocking slower environmental sensors or radio delivery.

## Signaling Model

- use queues for records
- use task notifications for simple wakeups from PIR, reed, or ADXL345 FIFO interrupts
- keep I2C ownership centralized instead of spreading bus access across many tasks

## Internal Data Contracts

Two core records should anchor the firmware:

- `sensor_reading_t` for raw or normalized sensor observations
- `telemetry_record_t` for pod-level radio payloads

Important fields include:

- `sensor_id`
- `sequence`
- `uptime_ms`
- `boot_id`
- `capability_bitmap`
- `quality_flags`

The implemented contract also carries pod ID, sensor kind, unit, record class, priority, and numeric value. The current contract does not yet contain a capability bitmap; module capabilities can be added when the Zigbee payload and module metadata format are finalized.

## Quality Flags

Measurements should carry explicit validity state, not just values. Important flags include:

- `VALID`
- `WARMING_UP`
- `STALE`
- `CRC_FAILED`
- `OUT_OF_RANGE`
- `SENSOR_MISSING`
- `COMPENSATION_UNAVAILABLE`
- `BATTERY_LOW`
- `CLIPPED`
- `MOUNTING_CHANGED`

This is especially important for SCD41 warm-up, SGP40 compensation status, and ADXL345 clipping or mount changes.

## Per-Pod Runtime Shape

- Environment pod: periodic scheduler, humidity and temperature first, then compensated VOC, then CO2 on its own cadence
- Activity and access pod: interrupt-driven PIR and reed path, with BH1750 on a slower periodic cadence
- Equipment pod: slow path for DS18B20 and INA219, fast path for ADXL345 FIFO and derived vibration features

## Battery-Aware Reporting Policy

Sampling and radio reporting are intentionally separate. The pod must still sample often enough to detect a change, but it only queues a routine transmission when one of these conditions is true:

- it is the first reading after boot or attachment
- the value crosses the configured deadband and the minimum interval has elapsed
- the maximum-silence interval has elapsed, creating a heartbeat
- quality flags change, such as entering or leaving warm-up or low-battery state
- the record is an event or health message, which bypasses suppression

Development defaults are:

| Sensor | Deadband | Minimum report interval | Heartbeat |
| --- | ---: | ---: | ---: |
| Temperature | 0.2 C | 30 s | 15 min |
| Relative humidity | 1% | 30 s | 15 min |
| CO2 | 50 ppm | 30 s | 5 min |
| VOC index | 5 | 30 s | 5 min |
| Illuminance | 10 lux | 30 s | 15 min |
| Current | 50 mA | 10 s | 5 min |
| Voltage | 0.1 V | 10 s | 5 min |
| Vibration RMS | 0.02 g | 5 s | 1 min |

These are initial engineering values, not final experimental values. They must be tuned from sensor noise, application responsiveness, and battery-life measurements on real hardware.

## Sequence Number Meaning

The sensor registry increments a sequence number whenever a reading is produced. The reporting policy may then suppress that reading. Therefore, a gap in received sequence numbers does not automatically mean packet loss. The gateway and Raspberry Pi must combine sequence gaps with policy knowledge, acknowledgements, retry counters, and link metrics before classifying loss.

## Remaining Firmware Work

- real board drivers for I2C, GPIO, PIR or reed interrupts, and ADXL345 FIFO
- sensor-to-runtime adapters that queue real readings in place of the development health record
- gateway IEEE/pod/sensor-slot registry provisioning rather than the current bring-up labels
- UART forwarding from the Zigbee gateway ESP to the server/Wi-Fi ESP
- commissioning allow-listing, application acknowledgement, retry policy, and production security policy
- watchdog and stack-margin measurement on the final workload
- deep sleep and per-port sensor power switching
- gateway MQTT batching and persistent retry storage
- remote configuration of reporting rules

## Zigbee Payload Boundary

The implemented telemetry payload is exactly 30 bytes. It carries protocol version, record class,
priority, sensor kind, unit, sensor slot, quality flags, sequence, boot ID, 64-bit uptime, and a
32-bit float value. Pod and sensor names are not repeated over the radio. The powered gateway maps a
stable node IEEE address and its current Zigbee short address to a pod ID, then maps the payload's
sensor slot to a sensor ID.

This format can be transported by a manufacturer-specific ZCL custom command while standard Zigbee
clusters remain available for commissioning and basic device metadata. The codec is intentionally
independent of Espressif SDK types because the ESP Zigbee SDK v2.x APIs have changed and should be
isolated in a thin adapter pinned to one tested SDK release.
