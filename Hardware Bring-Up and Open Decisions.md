---
title: Hardware Bring-Up and Open Decisions
project: SiteTwin
status: Active hardware integration
updated: 2026-08-17
tags:
  - sitetwin
  - hardware
  - bring-up
  - decisions
---

# Hardware Bring-Up and Open Decisions

This note is the handoff point for SiteTwin hardware-facing firmware. It
separates the locked KiCad-derived board contract from electrical evidence
that is still required before final-board control paths can be enabled.

## Confirmed hardware contract

- The pod MCU is ESP32-C6; ESP-IDF v5.5.4 and ESP Zigbee SDK v2.0.3 are the
  currently tested software baseline.
- The final universal board has four ports.
- Type 1 is SHT41 at I2C address `0x44`. Any SHT40 label is a documentation or
  Wokwi-label defect.
- Type 5 is DS18B20 with the 10 kOhm identification code. TMP36 is not the
  current Type 5 contract.
- U4 is CD74HC4052M96. It selects the non-I2C DATA path and presents
  `DATA_COMMON` to ESP32-C6 GPIO3.
- GPIO19 drives R36/Q2 (2N7002) for one shared external buzzer/LED low-side
  alarm branch. LED and buzzer are not independently controllable.
- Pod 3 is monitoring/inference only. INA219 and ADXL345 may provide evidence
  or alarm inputs, but motor-current cut and motor control are prohibited.
- The stable development identities are `POD_1`, `POD_2`, and `POD_3`.

The fixed development pods are not final-board routing evidence. In
particular, the physically verified Pod 3 DS18B20 path on GPIO0 with an
external approximately 4.7 kOhm DQ pull-up remains valid prototype work, but
the final Type 5 DATA route is through U4 to GPIO3.

## Firmware boundary

The portable drivers, logical-channel adapters, module lifecycle, registry,
reporting policy, queues, Zigbee codec, and gateway framing remain reusable.
The current I1 profiles compose fixed development sensors only.

The disabled-by-default H1/H2 profile now implements the confirmed U2/U4 select
mapping and a board-port raw ID measurement with a provisional midpoint
classifier. It always disables the temporary ID pull-up after successful or
failed acquisition. It is not composed into the fixed pod profiles. No
physical insertion/removal handler, stable-scan registry commit, DATA_COMMON
sensor acquisition, or live shared-alarm-indicator driver is enabled. The
`board_port` and `local_output` boundaries do not prove electrical behavior.

## Remaining hardware evidence required

- Calibrated ADC samples and non-overlapping acceptance bands for every ID
  resistor, including Type 5's nominal 10 kOhm code, across supply, resistor,
  temperature, connector, ADC, and mux tolerances.
- Complete hot-swap/power schematic evidence establishing active polarities,
  sequencing, settle times, interrupt behavior, and safe insertion/removal
  behavior before universal-port runtime work.
- Bench evidence for CD74HC4052M96 channel truth, enable behavior, on-resistance,
  leakage, voltage range, and DS18B20 timing/signal integrity through U4 to
  GPIO3.
- Complete Q2/R36/buzzer/LED schematic and load data establishing GPIO19 active
  polarity, safe PWM frequency/duty-cycle envelope, branch current, transistor
  dissipation, and external load limits.
- Evidence for how duplicate fixed-address I2C modules are isolated or ruled
  out on the shared universal connector bus.

Nominal part numbers and resistor codes must not be converted into invented
ADC thresholds, PWM values, or load limits.

## Current implementation status

- The 30-byte SiteTwin Zigbee telemetry path is deployed on ESP32-C6.
- The sensor runtime supports stable numbered pod identities.
- This I2 line composes SHT41, SCD41, and SGP40 for `POD_1`; BH1750, PIR, and
  reed for `POD_2`; and INA219, ADXL345, and DS18B20 for `POD_3`.
- SHT41 is the sole temperature/humidity authority. SGP40 requires recent
  valid SHT41 compensation; SCD41 temperature/humidity is not published.
- Physical final-board discovery, hot-swap, and shared-indicator behavior are
  intentionally absent.

Treat this file together with `Architecture Baseline.md` and `Firmware
Architecture.md` as the current firmware-facing hardware authority. Older
TMP36, independently controlled LED/buzzer, unfinalized DATA-mux, or motor
actuation statements are superseded.
