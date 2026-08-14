# Bidirectional Command Transport

Status: host-tested and target-compiled transport implementation. No physical
hardware or deployed ThingsBoard result is claimed.

## Contract

The Pi bridge accepts ThingsBoard gateway RPC envelopes shaped as:

```json
{"device":"POD_1","data":{"id":8,"method":"get_config","params":{"valid_for_ms":10000}}}
```

`params` may be a JSON object or a string containing exactly one JSON object.
The bridge validates and normalises it before publishing schema-version-3
commands to `sitetwin/pods/{pod_id}/commands`. It never forwards arbitrary RPC
parameters directly to firmware.

Supported transport methods are `set_threshold`, `get_config`, `set_rule`,
`get_rule`, `get_capabilities`, `ack_alarm`, `silence_alarm`, and
`test_output`. `ack_alarm` targets one exact active alarm instance and changes
only its persisted acknowledgement state. Current pods do not advertise a
verified shared alarm indicator, so `silence_alarm` and `test_output` are
explicitly rejected as `unsupported`; transport support is not physical-output
support.

## Result path

The Wi-Fi gateway converts validated JSON into the fixed 72-byte C1 command,
wraps it in the CRC-protected UART frame, and sends it to the Zigbee
coordinator. The coordinator sends Zigbee custom command `0x02` to the selected
development pod. The pod command runtime returns the fixed 60-byte result on
custom command `0x03`; the coordinator and Wi-Fi gateway forward it to
`sitetwin/pods/{pod_id}/command_results`.

Version-1 and version-2 64-byte commands and 44-byte results remain decodable.
The v3 extension carries an exact alarm-instance ID, capability mask, and
ruleset revision without changing the legacy CO2 mapping.

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
boot ID, and record class are latest-value device attributes. Capabilities and
rule definitions are attributes; revision changes, alarm transitions,
acknowledgement/silence state, and gateway incidents are telemetry. Command
results are correlated RPC responses and are never emitted as sensor telemetry.

Pod control events use Zigbee custom command `0x04`, the CRC-protected UART
`CONTROL_EVENT` frame, and `sitetwin/pods/{pod_id}/control`. Coordinator
incidents use the same fixed control event and publish to
`sitetwin/gateway/incidents`.

## Current routing boundary

`POD_1`, `POD_2`, and `POD_3` are routed through the current development
network's short-address table. Zigbee short addresses are not durable hardware
identities, so this table must later be replaced by reviewed IEEE-address
provisioning. Unknown address-derived pod names are never promoted to command
routes.

No credentials, tokens, certificates, dashboards, universal-port logic,
GPIO19 control, motor control, or deployment configuration is part of this
workstream. See `control-layer.md` for logical alarm and incident semantics.
