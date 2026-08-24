# `silence_alarm` semantics: no timed auto-expiry

## What changed

`silence_alarm` no longer accepts or validates a `duration_ms` parameter.
Silencing an alarm now lasts until the underlying condition itself clears,
the same instance is silenced/re-triggered, or the pod reboots -- never on
a timer. This differs from the previously discussed timed-silence contract.

This is a deliberate design decision (commit `5a82e09`, 21 August 2026), not
an oversight and not something forced by any hardware limitation. It was
made while the LED and buzzer still ran on independent GPIOs (GPIO4/GPIO5) --
at that point, silencing just the buzzer while leaving the LED on as a
visual-only indicator was fully achievable in hardware. The decision to drop
the timer was made anyway, for reasons independent of that later hardware
change.

## Why

1. **Timed silence produced no real benefit here.** The only thing a
   silence timeout protects against is someone forgetting an alarm is still
   silenced. ThingsBoard continues to show the underlying condition as
   unresolved for as long as it remains unresolved, regardless of silence
   state -- silencing never removes visibility into the fact that a problem
   still exists. With that risk already covered elsewhere, a timer added no
   safety value.
2. **Timed silence actively hurt the real usage pattern.** In practice,
   fixing whatever tripped an alarm rarely takes less time than any
   reasonable timeout. A short timer meant the alarm would re-fire
   mid-troubleshooting, repeatedly, which in real testing was disruptive
   enough to be counterproductive rather than merely inconvenient. A long
   timer avoids that but converges on the same practical outcome as no
   timer at all, while adding an arbitrary duration value that has to be
   chosen and justified for no corresponding benefit.

In short: for this system, a timed-silence design has a real cost (repeated
disruption, or an arbitrary long value that behaves like "no timer" anyway)
and no offsetting benefit, because the visibility problem a timer exists to
guard against is already solved independently by ThingsBoard's persistent
display of unresolved conditions.

## Later, unrelated hardware constraint

On the final PCB, GPIO19 drives the LED and buzzer as one shared PWM output
-- physically, there is no "buzzer off, LED still on" intermediate state.
As a consequence, `silence_alarm` and `clear_alarm` now produce an identical
physical result (PWM output stops). This is a separate, later hardware fact
about the final PCB, not the reason the timer was removed -- the removal
predates this constraint and would have been the same decision even without
it. The distinction between "user silenced this" and "condition genuinely
cleared" is preserved above the firmware layer (bridge/ThingsBoard event
logging), not in the physical output.
