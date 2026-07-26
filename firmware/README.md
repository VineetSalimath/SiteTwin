# SiteTwin firmware

This is the shared ESP-IDF firmware foundation for the SiteTwin pods. It is deliberately split so the scheduling, hot-swap registry, delivery policy, and telemetry contracts can be tested on a development machine before physical hardware is available.

## Layout

- `components/sitetwin_core`: portable production logic with no ESP-IDF or board-driver dependencies.
- `components/sitetwin_fake_hal`: deterministic fake sensor drivers used only by host tests and simulations.
- `main`: the thin ESP-IDF composition root. Real I2C, GPIO, Zigbee, and power-management adapters will be registered here as hardware arrives.
- `host_tests`: a native GCC test suite for the contract and runtime behaviour.

## Host tests

From this directory, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\host_tests\run-tests.ps1
```

The test suite covers module detection/removal, warm-up quality flags, change suppression and heartbeat reporting, monotonically increasing sensor sequences, event priority, latest-value replacement, a compact Zigbee payload round trip, gateway node rejoin/address changes, duplicate/stale rejection, binary gateway frame integrity, canonical gateway JSON, delivery queue pressure, and a randomized stress harness.

## Firmware boundary

Pods produce typed `st_telemetry_record_t` values, never JSON. The gateway-side adapter transports those records in a versioned binary frame and converts them to JSON only at the MQTT boundary. Sensor drivers are attached to the registry through `st_sensor_driver_t`, which makes a fake driver and a future ESP-IDF driver interchangeable.

The Zigbee telemetry payload is a fixed 30-byte, little-endian record. Human-readable pod and sensor identifiers are held by the gateway registry rather than repeated over the radio. A future ESP Zigbee custom-cluster callback only needs to pass the source short address and received payload to `st_gateway_runtime_ingest_zigbee_source`.

The battery and processing split, default sensor deadbands, heartbeat rules, and future task ownership are documented in [FIRMWARE_POLICY.md](FIRMWARE_POLICY.md).

## Next hardware-dependent increment

When the first ESP32-C6 and environment sensors arrive, add an ESP-IDF I2C adapter that implements `probe` and `sample` for SHT41 first. The core, packet layout, queue behaviour, and tests remain unchanged.
