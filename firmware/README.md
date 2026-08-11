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

The test suite covers module detection/removal, warm-up quality flags, change suppression and heartbeat reporting, monotonically increasing sensor sequences, event priority, latest-value replacement, PIR stabilization and duplicate suppression, ADXL345 FIFO/RMS/clipping behaviour, a compact Zigbee payload round trip, gateway node rejoin/address changes, duplicate/stale rejection, binary gateway frame integrity, canonical gateway JSON, delivery queue pressure, and a randomized stress harness.

## Pod profiles

Pod builds select one composition with `SITETWIN_POD_PROFILE`:

| Profile | Pod ID | Sensors | Prototype wiring |
| --- | --- | --- | --- |
| `environment` (default) | `ENV_01` | SHT41 temperature/humidity | I2C SDA GPIO6, SCL GPIO7 |
| `activity` | `ACT_01` | BH1750 illuminance, SR505 PIR, reed contact | I2C SDA GPIO6/SCL GPIO7, PIR GPIO1, reed GPIO0 |
| `equipment` | `EQP_01` | INA219 voltage/current, ADXL345 vibration RMS | Shared I2C SDA GPIO6/SCL GPIO7 |

The profile values and prototype pin assignments are configurable through
`menuconfig`. The Activity GPIO ISR only queues wake-ups; debounce, PIR
stabilization, and telemetry creation occur in task context. The Equipment
profile removes the mean from each ADXL345 axis before calculating vector RMS,
so mounting orientation and static gravity are excluded from the vibration
feature.

From an ESP-IDF PowerShell, compile the two new profiles with:

```powershell
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

## Firmware boundary

Pods produce typed `st_telemetry_record_t` values, never JSON. The gateway-side adapter transports those records in a versioned binary frame and converts them to JSON only at the MQTT boundary. Sensor drivers are attached to the registry through `st_sensor_driver_t`, which makes a fake driver and a future ESP-IDF driver interchangeable.

The Zigbee telemetry payload is a fixed 30-byte, little-endian record. The deployed adapter sends it through custom Zigbee cluster `0xFC00`, command `0x01`. The gateway receives and validates it through the existing runtime. Pod compositions attach physical sensor modules to the registry and send typed state, event, or derived-feature records over Zigbee.

The battery and processing split, default sensor deadbands, heartbeat rules, and future task ownership are documented in [FIRMWARE_POLICY.md](FIRMWARE_POLICY.md).

## Deploying Zigbee

Follow [SITETWIN_ZIGBEE_BRINGUP.md](SITETWIN_ZIGBEE_BRINGUP.md) to build and flash the pod and gateway images. The host test suite must continue to pass before deployment.

## Hardware validation still required

The Activity and Equipment images compile for ESP32-C6 and their portable
drivers are host-tested, but PIR and ADXL345 still require first-board
validation. Confirm SR505 polarity/retrigger behaviour and the GPIO0/GPIO1
prototype assignment, then establish an ADXL345 mounted-idle noise baseline and
a known-vibration response. DS18B20 is intentionally not part of the Equipment
image yet and must not be marked complete.
