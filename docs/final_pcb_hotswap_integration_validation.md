# Final PCB Hot-Swap Integration and Real-Hardware Validation

Status: H3/H4 integration complete and validated on the assembled final PCB.
Supersedes the "not yet composed into app_main" boundary stated in
`docs/final_pcb_hotswap_plan.md`'s H0 contract for the identification and
telemetry path specifically. Actuation (GPIO19 shared buzzer/LED) remains
out of scope -- see "Explicitly not done" below.

Software base: `feature/dynamic-pod-downlink-routing` merged with
`feature/final-pcb-hotswap` at `810eeff`, then built on directly. Final
commit referenced by this document: `9e938d3`.

## Scope

This work composes the H0/H1/H2 board-port identification skeleton
(`final_pcb_board.c`, `espidf_final_pcb_board.c` -- mux/ADC identification,
median-of-N sampling, electrical error-safety) into a complete,
end-to-end hot-swap pipeline:

```
final_pcb_board (identify)
  -> board_port_manager (debounce, stable double-scan commit, I2C
     cross-check, fault handling)
  -> hotswap_module_binding (type -> driver mapping)
       - SHT41/SCD41/SGP40/BH1750/ADXL345/INA219/DS18B20: st_module_instance_t
         into the pod's st_sensor_registry_t (same path the three fixed
         pod profiles already use)
       - PIR/REED: event-driven, outside the registry (per reed.h's own
         documented design), serviced by pod_telemetry_task's own poll
         loop and injected via st_pod_runtime_emit_event()
  -> Zigbee telemetry (st_hotswap_zigbee_sensor_slot() reuses the exact
     slot numbers gateway_identity.c's existing sensor_identity_table
     already assigns per sensor_id -- no changes needed to that shared
     table)
```

New ESP-IDF HAL components supporting this, distinct from the existing
fixed-address `st_espidf_i2c_bus`:
- `st_espidf_shared_i2c_bus_t`: one shared, multi-address I2C bus (the
  final PCB's I2C lines are unmuxed and shared across all four ports),
  lazily adding a device handle per address on first use.
- `st_espidf_muxed_data_common_t`: composes "select this port on the
  DATA mux, then transact" for DS18B20 (OneWire) and for the raw digital
  level PIR/REED read, since all three share the single DATA_COMMON
  physical line.

New pod profile: `ST_POD_UNIVERSAL` (capability set = every sensor kind
except UNKNOWN), since a hot-swap port may report any of the nine
supported types and the fixed profiles' narrower capability sets would
otherwise silently reject alarm-threshold configuration for types
outside whatever profile was borrowed.

## Real-hardware validation performed

Tested on the assembled final PCB, one physical unit of each of the nine
supported module types, via the full chain (Pod -> Zigbee -> coordinator
-> UART -> Pi bridge -> HiveMQ -> ThingsBoard), with the coordinator
recognising the pod as `POD_1FBA` (see "Known limitations" -- this is an
identity reuse from prior commissioning, not a new assignment).

| Type | Result |
| --- | --- |
| SHT41 | Verified: attach, steady telemetry, clean detach |
| BH1750 | Verified, including a direct type-swap from SHT41 on the same port (detach-then-attach path) |
| REED | Verified, including rapid repeated manual triggers and concurrent operation alongside an active I2C-type port |
| PIR | Verified, including its ~5s stabilisation window and concurrent operation with three other active ports |
| DS18B20 | Verified, including concurrent operation with PIR/REED sharing the same physical DATA_COMMON line through the DATA mux |
| SCD41 | Verified |
| ADXL345 | Verified after a real bug fix (see below) |
| INA219 | Verified (voltage and current channels both) |
| SGP40 | Verified after a real bug fix (see below) |

Scenarios covered beyond single-type identification:
- Insertion and removal (confirmed via the corresponding telemetry
  stream starting and stopping).
- Direct type swap on one port without an intervening empty state.
- All four ports simultaneously occupied with four different types
  across both the registry-backed and event-driven paths at once, with
  no observed interference between them.
- A full power cycle (pod unplugged and re-powered) with two attached
  modules present: both were correctly re-identified and re-attached on
  cold boot without requiring physical re-insertion, and PIR/REED event
  delivery was confirmed to still work correctly afterward.

## Real bugs found and fixed during this validation

Three bugs were found and fixed; all three were invisible from the
plain-C host test suite and only surfaced on the real ESP-IDF build
and/or real hardware.

1. **`hotswap_module_binding.c`: SGP40 attach.** `st_sgp40_init()`
   hard-rejects a NULL `compensation_provider`. Fixed by supplying a
   fixed-default-value provider (25 C / 50% RH) instead -- necessary
   because a hot-swap SGP40 cannot know whether some other port happens
   to hold a live SHT41.

2. **`hotswap_module_binding.c`: ADXL345 attach.** `st_adxl345_init()`
   rejects `minimum_window_samples > 31`; the hotswap default had been
   set to 32 (copied from the FIFO capacity constant without checking
   the driver's stricter bound). Fixed to 31.

3. **`sgp40.c` (shared driver, not hotswap-specific): VOC algorithm
   reset check.** `finish_measurement()` compared elapsed time between
   measurement cycles against `algorithm_interval_ms` with strict
   equality. `sensor_registry.c`'s scheduling (`next_sample_at_ms =
   now_ms + sample_interval_ms`, fired on `>=`) means real elapsed time
   is always the interval plus scheduling jitter, essentially never an
   exact millisecond match -- so the VOC algorithm's accumulated state
   was being reset on almost every real cycle, meaning it could never
   complete `GasIndexAlgorithm`'s 45-second initial warm-up, no matter
   how long you waited. Fixed with a +/-50% tolerance band instead of
   exact equality. **This file is shared with the fixed Environment
   profile (`POD_67C3`), which also uses SGP40 -- this is a pre-existing
   latent bug, not something introduced by hot-swap work, and it likely
   affected that profile's VOC readings too. Worth a note to whoever
   owns that profile's own validation record.**

## Port health/status events reaching TB

Motivated by a direct comparison against the hardware team's own test
`.ino`, which prints insertion/removal/error events over serial: the
firmware-side detection already existed (`board_port_manager`'s
`lifecycle`/`fault_reason` state) but had no path out of the pod beyond a
local, serial-only diagnostic log. This closes that gap end-to-end.

**Design.** New `st_pod_runtime_emit_health()` (`pod_runtime.c/.h`),
parallel to `emit_event()` but bypassing its `sensor_kind`-based
auto-classification to construct an `ST_RECORD_HEALTH`/
`ST_PRIORITY_HEALTH` record directly. `sensor_kind = ST_SENSOR_UNKNOWN`
and `unit = ST_UNIT_NONE` mark it as a status event rather than a real
reading. `app_main.c`'s existing port-transition diagnostic block now
also calls this on every lifecycle/fault-reason change, one call per
physical port, `sensor_id` = `"port0_status"`..`"port3_status"`. `value`
is encoded as `lifecycle*10 + fault_reason` (`fault_reason` is always
< 10, so this is unambiguous); documented inline at the call site.

**Layers touched, and what did *not* need to change.** Investigated all
four layers before implementing:
- Both gateway boards (`firmware/` coordinator, `gateway-wifi/`)
  **needed zero code changes** -- both already route
  `ST_GATEWAY_MESSAGE_HEALTH` through the exact same decode/publish path
  as ordinary telemetry; this was already built, just never fed by any
  pod before now.
- `gateway_identity.c` **did** need 4 new rows (correcting an earlier,
  wrong assumption): `sensor_id` is never transmitted over Zigbee at
  all -- the wire payload has no such field -- it is reconstructed
  purely from `(sensor_slot, sensor_kind)` via this table. Since every
  hot-swap health event necessarily shares `sensor_kind = UNKNOWN`,
  `sensor_slot` is the only field that can distinguish which port an
  event came from, so one row per port was required (slots 10-13,
  `"port0_status"`..`"port3_status"`). Purely additive; the existing 11
  rows are untouched. `hotswap_zigbee_slot.c`'s pod-side table extended
  to match, verified with a real round-trip through the actual
  `gateway_identity_resolve()`.
- `pi-bridge/bridge_core.py`'s `telemetry_projection()` **did** need a
  fix: it previously collapsed every health record to a bare
  `{sensor_id}_heartbeat: True`, discarding `payload['value']` entirely
  -- fine for a liveness ping, useless for a status/fault code. Now
  preserves the real value under `{sensor_id}_status` (heartbeat kept
  alongside, additive). This path had zero existing test coverage;
  added `test_health_record_preserves_value_not_just_heartbeat`.

**Real-hardware validation.** Both gateway boards were rebuilt and
reflashed with the updated `gateway_identity.c` (this had been missed
in the first pass -- a stale coordinator/Wi-Fi-gateway pair initially
produced `"unknown_slot_10"`-style fallback sensor_ids on HiveMQ,
confirming the table lookup was failing exactly as expected against the
*old* firmware, and confirming the *new* table's slot numbers were
correct once decoded from the fallback string). After reflashing both
boards, a live SHT41 insertion produced, in TB:

```
port0_status_status         72.0
port0_status_heartbeat      true
port0_status_status_sequence  2
```

`72.0` decodes as `lifecycle=7` (`FAULTED`), `fault_reason=2`
(`BUS_MISMATCH`) -- a real fault event, not a synthetic test value,
caught and correctly reported end-to-end. Whether that specific
occurrence reflected a genuine loose connection or a one-off is not
established here; what is established is that the reporting pipeline
faithfully reflects the firmware's real-time state to someone who is
not watching the serial console.

## Explicitly not done

- **GPIO19 shared buzzer/LED actuation.** Not touched. The final PCB's
  shared indicator branch (LED and buzzer physically tied together, per
  the second hardware-team `.ino` and confirmed by direct hardware
  inspection) requires reworking `command.h`/`alarm.c`'s `silence_alarm`
  vs `clear_alarm` distinction, since both now collapse to the same
  physical action (PWM to zero). That redesign was agreed but not yet
  implemented. `pod_local_output_ready` stays 0 for the `final_pcb`
  profile; the alarm/command state machine runs correctly, it just has
  no physical indicator wired up yet.
- **Provisional ID voltage bands.** `final_pcb_board.c`'s classification
  table remains the provisional, midpoint-derived bands from the H0
  contract, not a calibrated distribution from assembled-PCB
  measurements across supply/tolerance/temperature. It worked correctly
  for every unit tested here, but has not been statistically validated
  the way the contract itself says it eventually should be.
- **Pod identity.** The unit used for this validation restored Zigbee
  network membership as `0x1FBA` / `POD_1FBA` -- the same short address
  previously used by the fixed Equipment pod, from prior commissioning
  data still in flash. This validation's telemetry is therefore
  commingled with `POD_1FBA`'s prior history in any downstream
  history/dashboard. A clean `erase-flash` re-commission would give the
  hot-swap pod its own distinct identity if that separation matters.
  **Update:** an accidental gateway-wifi flash later overwrote this
  unit's zb_storage/zb_fct NVS, forcing exactly that re-commission --
  it now runs as `POD_3C60`, whitelisted in both `gateway_identity.c`'s
  `pod_identity_table` (uplink/downlink Zigbee routing) and hardcoded
  as the pod's own local `pod_id` string in `app_main.c` (used only in
  command-ack replies, not telemetry -- see "A hardcoded local pod_id
  string is fragile" below for why these two independent places both
  needed updating).
- **A hardcoded local `pod_id` string is fragile.** `pod_runtime` and
  `pod_command_runtime` are both initialised with a literal `pod_id`
  string compiled into the pod's own firmware (`"POD_3C60"` for this
  unit). Telemetry never uses this string -- the gateway reconstructs
  `pod_id` purely from the Zigbee source address via
  `gateway_identity.c`, and the wire payload doesn't carry a pod_id
  field at all -- but **command acks do embed it directly**
  (`pod_send_command_ack`), and bridge_core.py correlates a pending RPC
  request against an incoming ack's `pod_id`. If this local string and
  the gateway's `pod_identity_table` entry for the pod's real short
  address ever disagree (exactly what happened here after a
  re-commission), RPC downlink commands appear to succeed at the
  Zigbee layer but the reply comes back `uncorrelated` and the original
  TB-side request simply times out. This is not new to hot-swap --
  the three fixed pod profiles hardcode the same way -- but it is worth
  someone deciding whether to keep matching these two places by hand on
  every re-commission, or derive the local string from the pod's own
  joined short address at runtime instead.
- **Port transition logging now does double duty.** `app_main.c`'s
  `pod_telemetry_task` (final_pcb branch) logs a line on every port
  lifecycle/fault-reason transition -- originally added purely to debug
  the SGP40 issue above, it is no longer purely diagnostic: the same
  transition check now also drives `emit_health()` (see "Port
  health/status events reaching TB"). The `ESP_LOGI` call itself is
  still safe to remove independently if the serial output is no longer
  wanted; the `emit_health()` call must stay.
- **Dead code in a final_pcb build.** Several `#if ACTIVITY_BUILD /
  #elif EQUIPMENT_BUILD / #else` chains elsewhere in `app_main.c` were
  not given an explicit `final_pcb` branch, so a final_pcb build also
  compiles in (but never calls) the Environment profile's SHT41/SCD41/
  SGP40 init path. Wastes some flash; does not affect correctness.
- **Duplicate-module policy.** Two physical units of the same type
  plugged into the same pod at once is not defended against in software
  (relies on only one unit of each type existing, per earlier project
  discussion). Not exercised or re-evaluated in this validation.
