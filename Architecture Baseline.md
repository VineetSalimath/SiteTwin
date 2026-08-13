---
title: Architecture Baseline
project: SiteTwin
status: Zigbee transport deployed; sensor bring-up next
updated: 2026-07-31
tags:
  - sitetwin
  - architecture
  - zigbee
  - gateway
---

# Architecture Baseline

Related: [[SiteTwin Project Hub]], [[SiteTwin Data Flow - Beginner Guide]], [[Gateway and Data Flow]], [[Firmware Architecture]], [[Hardware Bring-Up and Open Decisions]]

## Executive Summary

SiteTwin has converged on a modular sensing architecture with three ESP32-C6 Zigbee pods, a powered gateway, and a Raspberry Pi. Battery pods perform sensor-facing work and only enough processing to reduce sensor power or Zigbee traffic. The gateway handles protocol and delivery processing, while the Raspberry Pi handles persistence, dashboards, soak-test observability, and state interpretation. The shared portable firmware policy is implemented and passing host tests. ESP32-C6-DevKitC-1 hardware and the project sensor hardware have now been acquired, so board bring-up and hardware integration can begin.

## Pod Topology

- Environment pod for temperature, humidity, CO2, and VOC
- Activity and access pod for motion, door or contact state, and illuminance
- Equipment pod for vibration, surface temperature, and current or power

## Gateway Recommendation

Two gateway designs remain viable:

1. Single-SoC gateway: one ESP32-C6 handles Zigbee coordination and Wi-Fi or MQTT backhaul.
2. Dual-SoC gateway: one SoC is dedicated to Zigbee and one SoC is dedicated to Wi-Fi backhaul.

The recommended baseline is the dual-SoC design because coordinator reliability matters more than minimum part count, especially for long soak tests.

The gateway is assumed to have continuous power. Its optimization target is therefore reliability and controlled radio coexistence rather than minimum computation. Processing should be moved from pods to the gateway unless executing it on the pod directly avoids a sensor operation or Zigbee transmission.

## Why The Split Matters

This architecture gives the dissertation a clearer technical argument:

- battery pods stay lean and deterministic
- the gateway becomes a reliability-critical processing and delivery bridge
- the Raspberry Pi becomes the place to quantify packet loss, reboots, latency, duplicates, and state models

## Design Boundary

Pods should not emit JSON over Zigbee. They should send typed embedded telemetry through Zigbee attributes or compact records. JSON should begin once data reaches the gateway or Raspberry Pi.

Routine telemetry is change-driven rather than sample-driven. Pods suppress values inside a configured deadband, enforce a minimum transmission interval, and send a maximum-silence heartbeat. Events and quality-state changes bypass suppression. This means forward sequence gaps can be intentional and must not be treated as packet loss without additional evidence.

## Hardware Integration Status

The latest KiCad-derived hardware-team authority resolves the final-board
topology as follows:

- SHT41 is the Type 1 environmental sensor at I2C address `0x44`.
- DS18B20 is Type 5 and carries the 10 kOhm identification code.
- The four-port board uses a CD74HC4052M96 DATA mux; its common non-I2C DATA
  path is `DATA_COMMON` on ESP32-C6 GPIO3.
- ESP32-C6 GPIO19 drives one shared external buzzer/LED low-side branch. The
  two loads are not independently controllable.
- Pod 3 remains monitoring/inference only. Motor-current cut and motor control
  are prohibited.

This topology supersedes earlier TMP36, DS18B20-as-legacy-only, and
unfinalized-mux statements. The universal-port scanner, physical hot-swap, mux
control, and live alarm-output implementation remain gated pending electrical
validation. Development-profile wiring, including Pod 3 DS18B20 on GPIO0, is
not a final-board GPIO claim.

## Zigbee Deployment Status

- ESP-IDF v5.5.4 and ESP Zigbee SDK v2.0.3 are the tested baseline.
- Two ESP32-C6-DevKitC-1 boards now run SiteTwin images: a Coordinator gateway and an End Device pod.
- The pod repeatedly sends the real 30-byte SiteTwin payload through custom cluster `0xFC00`; the gateway accepts it.
- The temporary health producer will be replaced by real sensor-driver records without changing the radio payload path.

The firmware integration still requires measured identification acceptance
bands, validated hot-swap electrical behavior, and the complete GPIO19/Q2/load
evidence before those final-board paths can be enabled. Nominal component
identity does not by itself establish safe ADC thresholds, output polarity,
PWM limits, or load limits.
