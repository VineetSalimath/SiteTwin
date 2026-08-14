# SiteTwin Pi Bridge

Copy `config.example.py` to ignored `config.py` and provide deployment values
locally. Do not commit that file, broker certificates, tokens, or passwords.

The live bridge forwards HiveMQ telemetry to ThingsBoard and separates channel
attributes from time-series readings. It also translates ThingsBoard
gateway-device RPC requests into the versioned SiteTwin command contract and
waits for a correlated terminal result before replying.

Examples of accepted RPC method/parameter pairs:

```text
get_config {"valid_for_ms":10000}
set_threshold {"value":1200,"config_revision":2,"valid_for_ms":10000}
get_capabilities {"valid_for_ms":10000}
get_rule {"capability":"co2_ppm","rule_kind":"numeric_high_threshold"}
set_rule {"capability":"contact","rule_kind":"event_debounce_ms","value":50,"config_revision":1}
ack_alarm {"alarm_instance_id":123456,"valid_for_ms":10000}
silence_alarm {"alarm_instance_id":123456,"duration_ms":10000,"valid_for_ms":10000}
```

ThingsBoard may deliver `params` as an object or as a JSON-encoded string; both
forms are normalised. Invalid parameters receive `rejected/invalid_syntax`
without publishing downstream.

The bridge also subscribes to `sitetwin/pods/+/control` and
`sitetwin/gateway/incidents`. Capability and rule definitions are sent as
ThingsBoard attributes. Rule revisions, alarm condition/acknowledgement/silence
transitions, and gateway incidents are sent as telemetry. Gateway incidents
are projected under logical device `GATEWAY_1`. Current pods reject
`silence_alarm` and `test_output` as `unsupported` because no verified physical
shared indicator is advertised.

Run the deterministic bridge tests without broker credentials:

```powershell
python -m unittest discover -s pi-bridge\tests -v
python -m py_compile pi-bridge\bridge.py pi-bridge\bridge_core.py
```

See `docs/command-transport.md` for correlation, timeout, reconnect, and current
routing limitations, and `docs/control-layer.md` for alarm/incident state
semantics.
