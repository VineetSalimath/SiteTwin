# SiteTwin firmware

This is the shared ESP-IDF firmware for SiteTwin pods and the Zigbee-side gateway. The portable core is host-tested, and the ESP-IDF adapter has been deployed successfully on two ESP32-C6 boards.

## Layout

- `components/sitetwin_core`: portable production logic with no ESP-IDF or board-driver dependencies.
- `components/sitetwin_fake_hal`: deterministic fake sensor drivers used only by host tests and simulations.
- `main`: the ESP-IDF composition root, including the deployed ESP Zigbee custom-cluster adapter.
- `tools/gateway.ps1` and `tools/pod.ps1`: role-specific build wrappers that create independent gateway and pod build directories.
- `host_tests`: a native GCC test suite for the contract and runtime behaviour.

## Host tests

From this directory, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\host_tests\run-tests.ps1
```

The test suite covers module detection/removal, warm-up quality flags, change suppression and heartbeat reporting, monotonically increasing sensor sequences, event priority, latest-value replacement, a compact Zigbee payload round trip, gateway node rejoin/address changes, duplicate/stale rejection, binary gateway frame integrity, canonical gateway JSON, delivery queue pressure, and a randomized stress harness.

## Firmware boundary

Pods produce typed `st_telemetry_record_t` values, never JSON. The gateway-side adapter transports those records in a versioned binary frame and converts them to JSON only at the MQTT boundary. Sensor drivers are attached to the registry through `st_sensor_driver_t`, which makes a fake driver and a future ESP-IDF driver interchangeable.

The Zigbee telemetry payload is a fixed 30-byte, little-endian record. The deployed adapter sends it through custom Zigbee cluster `0xFC00`, command `0x01`. The gateway receives and validates it through the existing runtime. The current pod emits a development health record every 15 seconds until real sensor adapters are connected.

The battery and processing split, default sensor deadbands, heartbeat rules, and future task ownership are documented in [FIRMWARE_POLICY.md](FIRMWARE_POLICY.md).

## Deploying Zigbee

Follow [SITETWIN_ZIGBEE_BRINGUP.md](SITETWIN_ZIGBEE_BRINGUP.md) to build and flash the pod and gateway images. The host test suite must continue to pass before deployment.

## Next hardware-dependent increment

When the first ESP32-C6 and environment sensors arrive, add an ESP-IDF I2C adapter that implements `probe` and `sample` for SHT41 first. The core, packet layout, queue behaviour, and tests remain unchanged.
