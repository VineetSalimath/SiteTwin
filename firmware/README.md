# SiteTwin firmware

This is the shared ESP-IDF firmware for SiteTwin pods and the Zigbee-side gateway. The portable core is host-tested, and the ESP-IDF adapter has been deployed successfully on two ESP32-C6 boards.

## Layout

- `../components/sitetwin_core`: the canonical portable production logic, shared by both
  ESP-IDF projects through `EXTRA_COMPONENT_DIRS`.
- `components/sitetwin_fake_hal`: deterministic fake sensor drivers used only by host tests and simulations.
- `main`: the ESP-IDF composition root, including the deployed ESP Zigbee custom-cluster adapter.
- `tools/gateway.ps1` and `tools/pod.ps1`: role-specific build wrappers that create independent gateway and pod build directories.
- `host_tests`: a native GCC test suite for the contract and runtime behaviour.

## Host tests

From this directory, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\host_tests\run-tests.ps1
```

The test suite covers module detection/removal, warm-up quality flags, change suppression and heartbeat reporting, monotonically increasing sensor sequences, event priority, latest-value replacement, PIR stabilization and duplicate suppression, ADXL345 FIFO/RMS/clipping behaviour, DS18B20 CRC/signed decoding/non-blocking conversion/removal recovery, a compact Zigbee payload round trip, gateway node rejoin/address changes, duplicate/stale rejection, binary gateway frame integrity, canonical gateway JSON, delivery queue pressure, and a randomized stress harness.

## Pod profiles

Pod builds select one composition with `SITETWIN_POD_PROFILE`:

| Profile | Pod ID | Sensors | Prototype wiring |
| --- | --- | --- | --- |
| `environment` (default) | `POD_1` | SHT41 temperature/humidity | I2C SDA GPIO6, SCL GPIO7 |
| `activity` | `POD_2` | BH1750 illuminance, SR505 PIR, reed contact | I2C SDA GPIO6/SCL GPIO7, PIR GPIO1, reed GPIO0 |
| `equipment` | `POD_3` | INA219 voltage/current, ADXL345 vibration RMS, DS18B20 temperature | Shared I2C SDA GPIO6/SCL GPIO7; DS18B20 DQ GPIO0 with external 4.7 kΩ pull-up to 3.3 V |

The profile values and prototype pin assignments are configurable through
`menuconfig`. The Activity GPIO ISR only queues wake-ups; debounce, PIR
stabilization, and telemetry creation occur in task context. The Equipment
profile removes the mean from each ADXL345 axis before calculating vector RMS,
so mounting orientation and static gravity are excluded from the vibration
feature.

From an ESP-IDF PowerShell, compile each profile in its own build directory:

```powershell
idf.py -B build_environment_c6 `
    -D IDF_TARGET=esp32c6 `
    -D SITETWIN_DEVICE_ROLE=pod `
    -D SITETWIN_POD_PROFILE=environment build

idf.py -B build_activity_c6 `
    -D IDF_TARGET=esp32c6 `
    -D SITETWIN_DEVICE_ROLE=pod `
    -D SITETWIN_POD_PROFILE=activity build

idf.py -B build_equipment_c6 `
    -D IDF_TARGET=esp32c6 `
    -D SITETWIN_DEVICE_ROLE=pod `
    -D SITETWIN_POD_PROFILE=equipment build
```

Add `-p COMxx flash monitor` after `idf.py -B ...` to flash the corresponding
pod. Use a separate build directory for each profile so a stale Environment or
Activity binary cannot be flashed by mistake.

The table describes the fixed development-pod compositions in this branch.
The archived Arduino prototypes under `sensor_firmwares` are reference inputs,
not additional drivers in the ESP-IDF image; in particular, SCD41 and SGP40 are
not composed by this I1 integration.

## Final-board contract and feature gates

The hardware-team contract identifies SHT41 at address `0x44` and DS18B20 as
Type 5 with the 10 kOhm identification code. On the final universal board,
non-I2C DATA is selected by a CD74HC4052M96 and reaches `DATA_COMMON` on GPIO3.
GPIO19 drives one shared buzzer/LED low-side alarm branch; it is not two
independently controllable outputs. The firmware must never cut or control the
motor.

This branch does not implement the universal-port scanner, physical hot-swap,
DATA-mux selection, or the shared alarm indicator. Those final-board paths stay
feature-gated until the remaining electrical evidence is validated. The Pod 3
DS18B20 GPIO0/4.7 kOhm wiring below is a verified development prototype and
must not be treated as the final universal-board route.

## Firmware boundary

Pods produce typed `st_telemetry_record_t` values, never JSON. The gateway-side adapter transports those records in a versioned binary frame and converts them to JSON only at the MQTT boundary. Sensor drivers are attached to the registry through `st_sensor_driver_t`, which makes a fake driver and a future ESP-IDF driver interchangeable.

The Zigbee telemetry payload is a fixed 30-byte, little-endian record. The deployed adapter sends it through custom Zigbee cluster `0xFC00`, command `0x01`. The gateway receives and validates it through the existing runtime. Pod compositions attach physical sensor modules to the registry and send typed state, event, or derived-feature records over Zigbee.

The battery and processing split, default sensor deadbands, heartbeat rules, and future task ownership are documented in [FIRMWARE_POLICY.md](FIRMWARE_POLICY.md).

## Deploying Zigbee

Follow [SITETWIN_ZIGBEE_BRINGUP.md](SITETWIN_ZIGBEE_BRINGUP.md) to build and flash the pod and gateway images. The host test suite must continue to pass before deployment.

## Hardware validation still required

The Activity Pod has been physically exercised end to end. The Equipment Pod's
INA219 and ADXL345 have also produced canonical telemetry through Zigbee, UART,
MQTT, and ThingsBoard; an observed stationary ADXL345 feature was approximately
`0.004 g` with valid quality. A complete mounted baseline/threshold dataset is
still required before selecting an operational vibration alarm threshold.

The DS18B20 production driver is part of the Equipment image, host-tested,
compiled for ESP32-C6, and physically verified on Pod 3. The powered three-wire
probe operates on GPIO0 with an external approximately 4.7 kΩ DQ-to-3.3 V
pull-up, and its canonical temperature telemetry was observed end to end.
