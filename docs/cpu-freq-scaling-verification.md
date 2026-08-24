# Dynamic CPU frequency scaling: real-hardware verification

## What this branch is

A stable checkpoint of `e2dd5ad` (dynamic CPU frequency scaling,
40MHz idle / 160MHz active, building on the already-verified
`feature/battery-lightsleep`) after real-hardware testing. Kept as its
own named branch, separate from `feature/battery-lightsleep` (light
sleep only, no frequency scaling), so that checkpoint remains available
as a clean fallback on its own if anything found later specifically
implicates frequency scaling rather than light sleep itself.

Still does NOT include the Zigbee stack's own `esp_zb_sleep_enable()`
-- that remains a separate, higher-risk step, not attempted on any
branch as of this checkpoint.

## What was actually tested

Same pod (`POD_3C60`), one continuous session, immediately confirmed
`APB_MIN: 40` in the boot log (frequency scaling actually applied, not
silently rejected):

- **Cold boot / network join.** Restored network membership normally,
  no join failures.
- **Idle periods totalling roughly a minute**, split across the
  session. `[scan-scheduler]` gap logging held steady at ~5010-5020ms
  throughout -- no visible drift or irregularity from the CPU actually
  dropping to 40MHz while idle (as opposed to step 1, where the CPU
  never left 160MHz).
- **Two separate hot-swap insert+removal cycles** on the same port,
  with a reed open/close in between. All identification, lifecycle
  transitions, and telemetry behaved identically to previous testing.
- **Ten `reed_contact` events** (sequence 3-12) across both insert
  cycles and a dedicated switch-toggling pass -- in order, no
  duplicates, no gaps.
- **One downlink `silence_alarm` RPC**, sent from ThingsBoard, executed
  correctly.

No I2C timeout, no Zigbee signal errors observed.

## What this does NOT establish

Same caveats as `feature/battery-lightsleep`: one board, on the order
of a few minutes of total observation, not an overnight soak, not all
four ports populated, no SGP40 (the timing-sensitive sensor). This
session's test coverage was somewhat broader than step 1's (two
insert/remove cycles instead of one), but still short of anything that
should be called exhaustive.
