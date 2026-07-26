---
title: Validation and Implementation Plan
project: SiteTwin
status: Host validation in progress
tags:
  - sitetwin
  - validation
  - implementation
  - testing
---

# Validation and Implementation Plan

Related: [[SiteTwin Project Hub]], [[SiteTwin Data Flow - Beginner Guide]], [[Gateway and Data Flow]], [[Architecture Baseline]], [[Source References]]

## Primary Validation Risk

The main technical risk is radio coexistence in a single-SoC gateway. Zigbee coordination and Wi-Fi backhaul on one ESP32-C6 compete for the same RF path, so the single-SoC design should be treated as a formal test item rather than an assumption.

## Minimum Acceptance Tests

1. three pods reporting concurrently with every sequence gap classified as policy suppression, queue replacement, or genuine delivery loss
2. Wi-Fi access point restart with automatic recovery
3. MQTT broker restart without forcing a Zigbee reset
4. pod power-cycle during Wi-Fi activity with explainable sequence continuity
5. event bursts on the activity pod without lost door or PIR events
6. feature bursts on the equipment pod without sustained disruption elsewhere
7. a 48 to 72 hour pilot soak test
8. a multi-day soak test with quantified packet delivery, duplicates, and reboot counts

## Completed Without Hardware

The shared C firmware currently compiles with strict warnings and passes native host tests covering:

- sensor probing, warm-up, removal, failure, retry, and reattachment
- monotonically increasing sensor sequences
- event priority and latest-state queue replacement
- unchanged-value suppression using configurable deadbands
- minimum report intervals and maximum-silence heartbeats
- immediate reporting when quality flags change
- binary frame round trips and CRC corruption detection
- canonical gateway JSON output
- gateway validation, duplicate rejection, stale rejection, and reboot handling
- fixed-size Zigbee payload encoding, decoding, version rejection, and truncation rejection
- gateway IEEE-address registration, sensor-slot resolution, and short-address changes after rejoin
- gateway latest-state replacement, event-first delivery, JSON retry safety, and queue saturation
- randomized sensor churn, failures, events, queue pressure, and serialization

The latest deterministic stress run processed 384 telemetry records and ended with all tests passing. This validates logic and memory bounds on the host; it does not yet validate radio performance, timing under FreeRTOS, electrical behavior, or real battery consumption.

## Reporting Policy Validation

For each real sensor, record a stable baseline and controlled changes to determine:

- measurement noise distribution
- smallest meaningful change
- safe sampling interval
- suitable reporting deadband
- maximum acceptable event-to-dashboard latency
- energy per sample, wake cycle, and Zigbee transmission

The development thresholds in [[Firmware Architecture#Battery-Aware Reporting Policy]] should be replaced with evidence-based values from these measurements.

## Recommended BOM Anchors

- pod MCU: ESP32-C6
- gateway Zigbee SoC: ESP32-C6 or ESP32-H2
- gateway Wi-Fi SoC: ESP32-C3 or ESP32-S3
- I2C hot-swap buffer: PCA9511A
- I2C switch or mux: TCA9548A
- port load switch: TPS22919
- module EEPROM and unique ID: 24AA02UID
- minimal 1-Wire ID: DS2401

## Suggested Build Sequence

1. complete: lock the initial message contracts and quality flags
2. complete: build and stress-test the shared firmware skeleton with fake sensors
3. complete: define and host-test the compact Zigbee telemetry payload and gateway ingress pipeline
4. implement gateway MQTT batching and a simulated persistent outage buffer
5. define commissioning, allow-listing, configuration commands, and application acknowledgements
6. install and pin ESP-IDF plus ESP Zigbee SDK, then add thin custom-cluster adapters
7. bring up the environment pod and SHT41 first
8. bring up the activity and access pod
9. bring up the equipment pod
10. run single-SoC gateway feasibility testing
11. move to a dual-SoC gateway if reliability demands it
12. complete the Raspberry Pi collector and storage path
13. run soak tests and quantify failure behavior
