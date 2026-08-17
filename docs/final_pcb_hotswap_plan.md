# Final PCB Hot-Swap Truth and H0-H2 Plan

Status: H0 contract freeze for `feature/final-pcb-hotswap`

Hardware basis: latest KiCad-derived four-port implementation note

Software base: `integration/c1-alarm-gateway-control` at
`1c2be4b712d8e29a8225e0234611fccc509e92ec`

## Claim boundary

The final PCB hot-swap behaviour has a Wokwi-backed firmware reference and a
fixed KiCad pin/mux mapping. This branch documents those constraints and begins
translating them into the ESP-IDF board-port layer. Wokwi validates behavioural
sequencing, while assembled-PCB testing is still required for ADC windows,
comparator behaviour, I2C live-insertion robustness, power consumption, and
long-term reliability.

This H0-H2 increment is not a claim that dynamic hot-swap is complete or that
the final PCB has been physically validated.

## Fixed board contract

The MCU module is U1, an ESP32-C6-DevKitC-1. The four universal connectors are
J7, J8, J9, and J10. Each connector exposes one module-ID line, one non-I2C
DATA line, shared I2C SCL and SDA, GND, and 3V3. The schematic contact-number
ordering is not restated here because the supplied firmware contract identifies
the six nets but does not provide a reviewed connector-pin-number table.

Type 1 is SHT41 at I2C address `0x44`. Type 5 is DS18B20 with a 10 kOhm ID
code. Older SHT40 and TMP36 labels in the Wokwi source are reference-model
defects and are superseded by this contract. The existing Pod 3 DS18B20 driver
and its physically verified GPIO0 prototype route remain valid legacy/profile
work; the final-board Type-5 DATA route is U4 to GPIO3.

U2 and U4 are CD74HC4052M96 devices. U2 selects one ID line for measurement;
U4 selects one non-I2C DATA line. I2C is shared and does not pass through U4.
The board has no confirmed per-port sensor-power switch. GPIO18 only controls
the temporary precision ID pull-up.

GPIO19 is the `BUZZER` net feeding R36/Q2 and one shared external buzzer/LED
low-side branch. The pin is defined by the final profile but must not be driven
until active polarity, PWM limits, and load limits are physically verified.
LED and buzzer are not independently controllable. Pod 3 remains
monitoring/inference only; motor-current cut and motor control are prohibited.

## MCU pin map

| GPIO | U1 pad | Net | Confirmed use |
| ---: | ---: | --- | --- |
| 0 | 7 | `ID_ADC` | ADC input from U2 common X |
| 1 | 8 | `ID_MUX_SEL0` | U2 A / least-significant select bit |
| 2 | 12 | `ID_MUX_SEL1` | U2 B / most-significant select bit |
| 3 | 13 | `DATA_COMMON` | U4 common X for non-I2C DATA |
| 6 | 5 | `HOTSWAP_WAKE_1` | TS884 OUT1 input |
| 7 | 6 | `HOTSWAP_WAKE_2` | TS884 OUT2 input |
| 10 | 10 | `I2C_SDA` | Shared I2C data |
| 11 | 11 | `I2C_SCL` | Shared I2C clock |
| 18 | 23 | `ID_MOSFET_GATE` | LOW enables, HIGH disables 10 kOhm scan pull-up |
| 19 | 24 | `BUZZER` | Feature-gated shared alarm branch through R36/Q2 |
| 20 | 25 | `DATA_MUX_SEL1` | U4 B / most-significant select bit |
| 21 | 26 | `DATA_MUX_SEL0` | U4 A / least-significant select bit |
| 22 | 27 | `HOTSWAP_WAKE_3` | TS884 OUT3 input |
| 23 | 28 | `HOTSWAP_WAKE_4` | TS884 OUT4 input |

Hot-swap wake active polarity is not confirmed. H1 may define the inputs, but
H2 must not infer insertion/removal from a hard-coded comparator polarity.

## Mux and port map

Both muxes use A as the least-significant bit and B as the most-significant
bit. Firmware port indices are zero based even though board labels are one
based.

| Firmware port | Connector | A / SEL0 | B / SEL1 | U2 input | U4 input |
| ---: | --- | ---: | ---: | --- | --- |
| 0 | J7 / Port 1 | 0 | 0 | `PORT1_ID` / X0 | `PORT1_DATA` / X0 |
| 1 | J8 / Port 2 | 1 | 0 | `PORT2_ID` / X1 | `PORT2_DATA` / X1 |
| 2 | J9 / Port 3 | 0 | 1 | `PORT3_ID` / X2 | `PORT3_DATA` / X2 |
| 3 | J10 / Port 4 | 1 | 1 | `PORT4_ID` / X3 | `PORT4_DATA` / X3 |

GPIO4/GPIO5 and GPIO16/GPIO17 DATA-mux mappings are obsolete.

## Adapter ID targets

During a precision scan, the selected adapter resistor to ground and switched
10 kOhm pull-up form `V_ID = 3.3 * R_ID / (10 kOhm + R_ID)`.

| Module | Nominal R_ID | Measured R_ID | Nominal V_ID | Expected I2C address |
| --- | ---: | ---: | ---: | ---: |
| SHT41 | 2.2 kOhm | 2.13 kOhm | 595 mV | `0x44` |
| SCD41 | 3.3 kOhm | 3.21 kOhm | 819 mV | `0x62` |
| PIR | 4.7 kOhm | 4.59 kOhm | 1055 mV | n/a |
| SGP40 | 6.8 kOhm | 6.61 kOhm | 1336 mV | `0x59` |
| DS18B20 | 10 kOhm | 9.78 kOhm | 1650 mV | n/a |
| BH1750 | 15 kOhm | 14.59 kOhm | 1980 mV | `0x23` |
| Magnitude/reed | 22 kOhm | 21.4 kOhm | 2269 mV | n/a |
| ADXL345 | 39 kOhm | 38.2 kOhm | 2627 mV | `0x53` |
| INA219 | 82 kOhm | 80 kOhm | 2941 mV | `0x40` |
| Empty | not fitted | n/a | 3300 mV | n/a |

H2 may use midpoint-derived provisional bands to make the classifier testable.
Those bands must be named and logged as provisional. Values outside the outer
accepted range are `UNKNOWN_ID`; they must never be silently assigned to the
nearest module. Final bands require assembled-PCB distributions across supply,
component tolerance, ADC calibration, temperature, mux effects, noise, and
connector resistance.

## Scan and stable-commit sequence

The intended H3 manager sequence is:

1. On startup, or after a comparator transition, mark a complete four-port
   rescan pending.
2. Debounce/settle, then mask wake handling for the scan. A transition is not
   itself counted as an insertion or removal.
3. Drive GPIO18 LOW to enable the temporary 10 kOhm precision pull-up.
4. For every port, select U2, settle, acquire multiple ID ADC samples, filter
   them, and classify the result.
5. For a classified I2C module, probe its expected address on GPIO10/GPIO11.
   A missing or mismatched response is a fault, not a valid attachment.
6. Repeat/confirm as needed and commit registry changes only after the final
   scan state is stable.
7. Drive GPIO18 HIGH on every success and error path, then re-arm the wake
   inputs using bench-confirmed/configured polarity.
8. Attach only supported, known modules. On removal, detach their logical
   channels and suppress subsequent normal sampling. Never acquire an unknown
   or unsupported module as if it were valid.

The permanent 1 MOhm path and TS884 comparators provide low-current change
detection. They are distinct from the temporary 10 kOhm precision scan path.

## Fault model

| State/fault | Required response |
| --- | --- |
| `EMPTY` | Detach any previous module and schedule no normal samples |
| `UNKNOWN_ID` | Preserve raw/voltage diagnostic data; do not acquire |
| I2C NACK/mismatch | Mark fault; do not pretend the ID alone proves validity |
| Unstable/bouncing | Rescan; commit only the stable final state |
| Unsupported known module | Mark unsupported; do not fake telemetry |
| Removed module | Detach logical channels and suppress later normal samples |
| ADC/HAL error | Disable the precision pull-up and retain a fault result |

The shared un-muxed I2C bus cannot distinguish two identical fixed-address
modules by port. Supporting or forbidding duplicates remains an explicit H3
policy/hardware decision.

## Wokwi reference inspection

The named project was inspected read-only on 2026-08-17. It currently contains
`sketch.ino`, `diagram.json`, `port-front-end.chip.json`, and
`port-front-end.chip.c`. Its wiring matches the fixed GPIO map above and its
behavioural sketch demonstrates startup scanning, ID/DATA mux selection,
switched pull-up sequencing, four change inputs, address cross-checking,
sampling scheduling, and scan-versus-service-timer separation.

The Wokwi labels still say SHT40 and TMP36. They are not the hardware authority:
firmware must use SHT41 and DS18B20. No `simulations/wokwi` source snapshot is
present in this repository, so the web project is not yet a versioned build
input. The inspected source SHA-256 fingerprints are:

| Wokwi file | SHA-256 |
| --- | --- |
| `sketch.ino` | `39bbd1f77df7f5720ab37ebaaac92ce328228ed664ff0a832bac187d78c74eb6` |
| `diagram.json` | `209ab69674ca0b720af9430f8d658ce53c8008f3932edb0d38e85ddf3e278a10` |
| `port-front-end.chip.json` | `91779983eb9268397d21fb9fe66d0ef6fe6fb548d25f067aec9d49dc30acc56f` |
| `port-front-end.chip.c` | `da879609af4eb5be0776f72d9f8740b79358c2b57143cb3f1d8bdcbe339bbb4b` |

Wokwi is behavioural evidence only. It does not validate TS884 tolerance,
DMP2035U behaviour, CD74HC4052 leakage/on-resistance, ESD/surge immunity, live
I2C insertion, battery life, RF behaviour, or unrestricted industrial
hot-swapping.

## H0-H2 boundary and later work

H0 freezes this contract. H1 adds a disabled-by-default final-PCB Kconfig
profile with the confirmed pins. H2 adds testable ID/DATA select mapping and a
board-port skeleton that can take a filtered raw ID measurement while always
returning the scan pull-up to its disabled state. Per-port power returns
unsupported. GPIO19 remains untouched.

H3/H4 remain later work: ISR/debounce scheduling, stable double-scan commit,
I2C cross-check integration, dynamic driver/module construction, registry
attach/detach, DATA_COMMON digital/ADC/1-Wire integration, duplicate-address
policy, insertion/removal telemetry, and physical calibration/validation.

## Validation plan

Before enabling final-board discovery, record measured raw ADC and calibrated
millivolt distributions for every adapter and empty state. Verify U2/U4 channel
truth and settle time, GPIO18 safe startup and enable/disable sequencing, TS884
polarity and bounce behaviour, DS18B20 signal integrity through U4, shared-I2C
live insertion and duplicate behaviour, and GPIO19/Q2/load limits separately.
Run deterministic host mapping/sequencing/classifier tests and the full existing
host/target build regressions for every software increment.
