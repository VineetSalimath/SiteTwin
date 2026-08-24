# Base ESP-IDF power management (light sleep): real-hardware verification

## What this branch is

A stable checkpoint of `e87f05a` (base ESP-IDF power management: light
sleep + tickless idle, CPU frequency pinned at both ends, no Zigbee-
stack-level sleep negotiation) after real-hardware testing, kept
separate from:

- `final-pcb-hotswap-integration` -- the fully safe baseline, does not
  include this yet.
- `feature/battery-optimization` -- continues forward from here into
  higher-risk territory (CPU frequency scaling and/or the Zigbee
  stack's own `esp_zb_sleep_enable()`), which the real, current
  (2025-2026) failure reports cited in `e87f05a`'s own commit message
  make meaningfully more likely to hit a real problem.

This branch exists so the light-sleep result has a named, pushed,
recoverable point in history independent of whatever happens next.

## What was actually tested

One assembled final PCB pod (`POD_3C60`), one continuous test session:

- **Cold boot / network join.** Joined and restored network membership
  in well under a second (`Pod restored network as 0x3C60`), no
  different from without light sleep enabled. Does not reproduce the
  "Steering forever" permanent-join-failure pattern reported elsewhere
  against this exact SDK/chip combination.
- **~50 seconds idle**, untouched. `[scan-scheduler]` gap logging held
  steady at ~5010ms per cycle the entire time -- the existing 50ms
  FreeRTOS tick and the scan scheduler's own timing were not visibly
  disrupted by tickless idle.
- **One hot-swap insert + removal** on a single port. Identification,
  lifecycle transitions, and port-health telemetry all behaved
  identically to previous (non-light-sleep) testing.
- **Three reed-switch (door contact) open/close cycles.** All six
  `reed_contact` events sent, in order, no duplicates, no gaps.
- **One downlink RPC command** (`silence_alarm`, sent from ThingsBoard)
  received and executed correctly.

No I2C timeout, no Zigbee signal errors, and no repeat of any of the
specific failure modes found in the pre-implementation research (see
`e87f05a`'s commit message) were observed in any of the above.

## What this does NOT establish

This was one board, one session, on the order of a few minutes of
total observation time -- not an overnight soak, not all four ports
populated simultaneously, and did not include SGP40 (the sensor whose
VOC warm-up logic is timing-sensitive, per the project's own earlier
findings, and therefore the sensor most likely to be affected if light
sleep introduces any timing irregularity elsewhere). "Clean in this
test" is not the same claim as "exhaustively verified safe."

## Scope confirmed unaffected

`CONFIG_PM_ENABLE=y` / `CONFIG_FREERTOS_USE_TICKLESS_IDLE=y` are set
only in `firmware/sdkconfig.final-pcb.defaults`, which applies only to
this exact `firmware/` project build variant
(`SITETWIN_POD_PROFILE=final_pcb`). The separate `gateway-wifi/` board
has its own independent `sdkconfig.defaults` and cannot be reached by
this change regardless of build parameters used here.
