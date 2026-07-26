# SiteTwin firmware policy

## Design rule

Do work on a battery pod only when it prevents sensor power use or Zigbee airtime. Move validation,
deduplication, formatting, storage preparation, Wi-Fi retries, and cloud delivery to the powered
gateway.

## Pod policy

- Sample sensors at a rate appropriate for the physical signal. Sampling and reporting are separate.
- Always report the first valid reading after boot or sensor attachment.
- Suppress state readings that have not crossed the sensor deadband.
- Enforce a minimum report interval even when a value changes repeatedly.
- Send a heartbeat at the maximum report interval so silence never looks like a dead pod.
- Report quality-flag changes immediately, including warm-up, low battery, and sensor faults.
- Never suppress motion, contact, alarm, or health events.
- Keep telemetry binary on the pod. Do not create JSON there.
- Keep only the newest queued state for each sensor, but preserve higher-priority events.
- Use deep sleep, interrupt wake-up, and sensor shutdown in board-specific drivers once hardware exists.

## Default reporting rules

| Sensor | Deadband | Minimum interval | Heartbeat |
| --- | ---: | ---: | ---: |
| Temperature | 0.2 C | 30 s | 15 min |
| Relative humidity | 1 % | 30 s | 15 min |
| CO2 | 50 ppm | 30 s | 5 min |
| VOC index | 5 | 30 s | 5 min |
| Illuminance | 10 lux | 30 s | 15 min |
| Current | 50 mA | 10 s | 5 min |
| Voltage | 0.1 V | 10 s | 5 min |
| Vibration RMS | 0.02 g | 5 s | 1 min |

These are safe development defaults, not final scientific thresholds. Tune them using real sensor
noise measurements and the dissertation's required temporal resolution.

## Gateway policy

- Validate record identifiers, enum values, sequence numbers, and boot identifiers.
- Reject exact duplicates and stale records from the same pod, sensor, and boot session.
- Accept sequence gaps because pod-side suppression intentionally creates them.
- Convert accepted binary telemetry to JSON only at the MQTT boundary.
- Perform calibration, unit normalization, derived metrics, aggregation, timestamp mapping, and anomaly
  logic on the gateway.
- Batch routine MQTT messages and send events immediately.
- Persist an outbound retry buffer so a Wi-Fi or broker outage does not wake or burden pods.
- Track pod heartbeats, missed-heartbeat alarms, link quality, duplicate counts, and stale counts.

## Zigbee boundary

- Use a versioned fixed 30-byte telemetry payload instead of JSON or repeated identifier strings.
- Carry record class, priority, sensor kind, unit, sensor slot, quality flags, sequence, boot ID,
  uptime, and float value in explicit little-endian fields.
- Resolve the Zigbee short source address and sensor slot to gateway-owned pod and sensor names.
- Track the stable IEEE address so a node can receive a new short address after rejoining without
  losing its sensor-slot bindings.
- Keep ESP Zigbee SDK types inside a thin board adapter. The portable codec and gateway runtime must
  remain independently host-testable.

## Thread ownership

The ESP-IDF implementation remains task based. Sensor acquisition and low-power scheduling belong to
pod tasks. Zigbee receive, gateway processing, persistence, and Wi-Fi/MQTT delivery belong to separate
gateway tasks connected by bounded queues. Portable policy code contains no FreeRTOS calls, which is
why it can be tested on the development computer now and called from tasks later.
