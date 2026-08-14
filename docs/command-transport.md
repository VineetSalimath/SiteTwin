# Bidirectional Command Transport

Status: host-tested and target-compiled transport implementation. No physical
hardware or deployed ThingsBoard result is claimed.

## Contract

The Pi bridge accepts ThingsBoard gateway RPC envelopes shaped as:

```json
{"device":"POD_1","data":{"id":8,"method":"get_config","params":{"valid_for_ms":10000}}}
```

`params` may be a JSON object or a string containing exactly one JSON object.
The bridge validates and normalises it before publishing schema-version-2
commands to `sitetwin/pods/{pod_id}/commands`. It never forwards arbitrary RPC
parameters directly to firmware.

Supported transport methods are `set_threshold`, `get_config`, `set_rule`,
`get_rule`, `silence_alarm`, and `test_output`. The latter two are carried for
wire compatibility but I2/I3 pods reject them as `unsupported`; transport
support is not alarm or output support.

## Result path

The Wi-Fi gateway converts validated JSON into the fixed 64-byte I2 command,
wraps it in the CRC-protected UART frame, and sends it to the Zigbee
coordinator. The coordinator sends Zigbee custom command `0x02` to the selected
development pod. The pod command runtime returns the fixed 44-byte result on
custom command `0x03`; the coordinator and Wi-Fi gateway forward it to
`sitetwin/pods/{pod_id}/command_results`.

The Pi bridge keys pending requests by `(pod_id, ThingsBoard request id)`,
assigns a JSON-exact downstream command ID, and keeps the explicit mapping until
completion. It sends one terminal RPC response. Repeated RPC callbacks do not republish a
pending/completed command. Repeated downstream results and late results after a
timeout do not produce a second response. A failed ThingsBoard reply is retained
and retried after reconnect. This guarantee is process-local and bounded to the
256-entry completion cache; durable cross-process exactly-once delivery is not
claimed.

## Timeout and reconnect behaviour

- `valid_for_ms` must be between 1 and 60,000 ms and is also the Pi timeout.
- A timeout returns `status=failed`, `reason=timeout`.
- An immediate MQTT publish failure returns `reason=transport_failed`.
- HiveMQ reconnect restores telemetry and command-result subscriptions.
- Paho QoS 1 owns retransmission of an accepted publish; the bridge does not
  create a second logical command on reconnect.
- Pod duplicate history makes QoS redelivery idempotent while that runtime state
  remains available.

## Telemetry and attributes

Values, sequence, uptime, and quality flags remain telemetry. Sensor kind, unit,
boot ID, and record class are latest-value device attributes. Command results
are correlated RPC responses and are never emitted as sensor telemetry.

## Current routing boundary

`POD_1`, `POD_2`, and `POD_3` are routed through the current development
network's short-address table. Zigbee short addresses are not durable hardware
identities, so this table must later be replaced by reviewed IEEE-address
provisioning. Unknown address-derived pod names are never promoted to command
routes.

No credentials, tokens, certificates, dashboards, alarm state machine,
universal-port logic, GPIO19 control, motor control, or deployment configuration
is part of this workstream.
