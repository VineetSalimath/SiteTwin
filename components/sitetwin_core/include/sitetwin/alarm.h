#ifndef SITETWIN_ALARM_H
#define SITETWIN_ALARM_H

#include <stddef.h>
#include <stdint.h>

#include "sitetwin/capability_config.h"
#include "sitetwin/control_event.h"

#define ST_ALARM_PERSISTENCE_MAGIC 0x5354414CU
#define ST_ALARM_PERSISTENCE_VERSION 1U
#define ST_ALARM_CONDITION_CAPACITY 16U
#define ST_ALARM_EVENT_CAPACITY 32U

typedef struct {
    uint64_t instance_id;
    float threshold;
    float last_observed;
    uint32_t quality_flags;
    uint32_t config_revision;
    st_sensor_kind_t capability;
    st_config_rule_kind_t rule_kind;
    uint8_t used;
    uint8_t active;
    uint8_t acknowledged;
    uint8_t reserved;
} st_alarm_condition_state_t;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t condition_count;
    uint8_t reserved[2];
    uint32_t next_instance_counter;
    uint32_t ruleset_revision;
    st_alarm_condition_state_t conditions[ST_ALARM_CONDITION_CAPACITY];
} st_alarm_persistent_state_t;

typedef int (*st_alarm_persist_fn)(void *context);

typedef struct {
    char pod_id[ST_POD_ID_MAX_LEN];
    const st_capability_config_t *config;
    st_alarm_persistent_state_t *persistent;
    uint32_t capability_mask;
    uint32_t boot_id;
    uint32_t event_sequence;
    uint64_t silenced_instance_id;
    uint64_t candidate_since_ms[ST_ALARM_CONDITION_CAPACITY];
    uint8_t candidate_active[ST_ALARM_CONDITION_CAPACITY];
    uint8_t candidate_valid[ST_ALARM_CONDITION_CAPACITY];
    uint8_t shared_alarm_indicator_verified;
    uint8_t silence_active;
    st_alarm_persist_fn persist;
    void *persist_context;
    st_control_event_t events[ST_ALARM_EVENT_CAPACITY];
    size_t event_count;
    uint32_t event_drops;
} st_alarm_runtime_t;

void st_alarm_persistent_state_init(st_alarm_persistent_state_t *state);
int st_alarm_persistent_state_valid(const st_alarm_persistent_state_t *state,
                                    st_pod_profile_t profile);
int st_alarm_runtime_init(st_alarm_runtime_t *runtime, const char *pod_id,
                          const st_capability_config_t *config,
                          st_alarm_persistent_state_t *persistent,
                          uint32_t capability_mask, uint32_t boot_id,
                          uint8_t shared_alarm_indicator_verified,
                          st_alarm_persist_fn persist, void *persist_context,
                          uint64_t now_ms);
int st_alarm_runtime_rule_changed(st_alarm_runtime_t *runtime,
                                  const st_capability_rule_t *rule,
                                  uint64_t now_ms);
int st_alarm_runtime_ingest(st_alarm_runtime_t *runtime,
                            const st_sensor_reading_t *reading,
                            uint64_t now_ms);
int st_alarm_runtime_acknowledge(st_alarm_runtime_t *runtime,
                                 uint64_t instance_id, uint64_t now_ms);
/* No duration_ms -- silence now lasts until the condition genuinely clears
 * (or is re-silenced/re-triggered), not a timed auto-expiry. The physical
 * LED and the TB dashboard both keep showing the unresolved condition
 * throughout, so there is no "forgotten silent alarm" risk that a timeout
 * would have been guarding against. */
int st_alarm_runtime_silence(st_alarm_runtime_t *runtime,
                             uint64_t instance_id, uint64_t now_ms);
/* Force-clears any currently-active condition(s) for this capability --
 * called when the sensor providing that capability has just been
 * physically detached from a hot-swap port, so a stale "active" alarm
 * does not linger with no data source left to ever clear it naturally.
 * Also resets any in-flight debounce state for those conditions, so a
 * stale candidate transition cannot resume against whatever value a
 * future re-attach happens to read first. Emits a normal alarm-cleared
 * control event tagged ST_CONTROL_REASON_SENSOR_DETACHED (distinct from
 * a real ST_CONTROL_REASON_RULE_CLEARED) for each condition it clears.
 * The rule configuration itself is untouched -- if a same-type sensor is
 * re-attached and a fresh reading arrives, st_alarm_runtime_ingest()
 * evaluates it normally, with no separate "re-enable" step needed.
 * Returns the number of conditions cleared (0 if none were active for
 * this capability), or -1/-2 on the same failure terms as ingest(). */
int st_alarm_runtime_suspend_capability(st_alarm_runtime_t *runtime,
                                        st_sensor_kind_t capability,
                                        uint64_t now_ms);
void st_alarm_runtime_tick(st_alarm_runtime_t *runtime, uint64_t now_ms);
int st_alarm_runtime_next_event(st_alarm_runtime_t *runtime,
                                st_control_event_t *event);
int st_alarm_runtime_condition_active(const st_alarm_runtime_t *runtime,
                                      uint64_t instance_id);
/* True if ANY condition is currently active, regardless of instance_id --
 * used to drive the shared physical LED indicator, which represents "is
 * there an unresolved condition on this pod" rather than any one specific
 * instance. */
int st_alarm_runtime_any_active(const st_alarm_runtime_t *runtime);

#endif