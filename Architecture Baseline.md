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

## Zigbee Deployment Status

- ESP-IDF v5.5.4 and ESP Zigbee SDK v2.0.3 are the tested baseline.
- Two ESP32-C6-DevKitC-1 boards now run SiteTwin images: a Coordinator gateway and an End Device pod.
- The pod repeatedly sends the real 30-byte SiteTwin payload through custom cluster `0xFC00`; the gateway accepts it.
- The temporary health producer will be replaced by real sensor-driver records without changing the radio payload path.

Confirmed after the latest supervisor/team meeting:

- the pod development board in hand is the Espressif ESP32-C6-DevKitC-1
- the universal module concept remains a six-contact physical connector carrying supply, ground, shared I2C SDA/SCL, and two auxiliary signal positions
- analogue resistor-coded module identification is being evaluated for one auxiliary line because non-I2C modules do not have protocol-level identity
- reducing identification-divider standby current through switched/gated identification circuitry is a design objective
- the exact mux topology, GPIO allocation, port-count expansion scheme, and immediate hot-swap wake circuitry are not yet locked and must remain marked as pending until the team confirms them

Do not implement assumptions about ID-only versus ID+DATA multiplexing, a specific mux channel count, a four-versus-six-port final PCB, comparator wake circuitry, or exact resistor voltage bands until those choices are confirmed.
