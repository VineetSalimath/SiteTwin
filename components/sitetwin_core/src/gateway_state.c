#include "sitetwin/gateway_state.h"

#include <math.h>
#include <string.h>

static void copy_id(char *destination, size_t capacity, const char *source)
{
    size_t length = source == NULL ? 0U : strlen(source);
    if (length >= capacity) {
        length = capacity - 1U;
    }
    if (length > 0U) {
        memcpy(destination, source, length);
    }
    destination[length] = '\0';
}

void st_gateway_state_persistent_init(st_gateway_state_persistent_t *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
        state->magic = ST_GATEWAY_STATE_MAGIC;
        state->version = ST_GATEWAY_STATE_VERSION;
    }
}

int st_gateway_state_persistent_valid(const st_gateway_state_persistent_t *state)
{
    size_t index;
    uint8_t alarms = 0U;
    uint8_t incidents = 0U;

    if (state == NULL || state->magic != ST_GATEWAY_STATE_MAGIC ||
        state->version != ST_GATEWAY_STATE_VERSION ||
        state->alarm_count > ST_GATEWAY_STATE_ALARM_CAPACITY ||
        state->incident_count > ST_GATEWAY_STATE_INCIDENT_CAPACITY) {
        return 0;
    }
    for (index = 0U; index < ST_GATEWAY_STATE_ALARM_CAPACITY; ++index) {
        const st_gateway_alarm_evidence_t *alarm = &state->alarms[index];
        if (alarm->used == 0U) {
            continue;
        }
        ++alarms;
        if (alarm->pod_id[0] == '\0' || alarm->capability >= ST_SENSOR_UNKNOWN ||
            !isfinite(alarm->observed) || !isfinite(alarm->threshold) ||
            (alarm->active != 0U && alarm->alarm_instance_id == 0U)) {
            return 0;
        }
    }
    for (index = 0U; index < ST_GATEWAY_STATE_INCIDENT_CAPACITY; ++index) {
        const st_gateway_incident_state_t *incident = &state->incidents[index];
        if (incident->used == 0U) {
            continue;
        }
        ++incidents;
        if (incident->pod_id[0] == '\0' ||
            incident->incident_kind < ST_GATEWAY_INCIDENT_POD_STALE ||
            incident->incident_kind > ST_GATEWAY_INCIDENT_MULTI_SENSOR ||
            !isfinite(incident->primary_observed) ||
            !isfinite(incident->secondary_observed) || !isfinite(incident->trend) ||
            (incident->active != 0U && incident->instance_id == 0U)) {
            return 0;
        }
    }
    return alarms == state->alarm_count && incidents == state->incident_count;
}

static void queue_event(st_gateway_state_t *state, const st_control_event_t *event)
{
    if (state->event_count == ST_GATEWAY_STATE_EVENT_CAPACITY) {
        memmove(&state->events[0], &state->events[1],
                (ST_GATEWAY_STATE_EVENT_CAPACITY - 1U) * sizeof(state->events[0]));
        --state->event_count;
        ++state->event_drops;
    }
    state->events[state->event_count++] = *event;
}

static st_gateway_incident_state_t *find_incident(st_gateway_state_t *state,
                                                  const char *pod_id,
                                                  st_gateway_incident_kind_t kind,
                                                  int create)
{
    size_t index;
    st_gateway_incident_state_t *free_entry = NULL;

    for (index = 0U; index < ST_GATEWAY_STATE_INCIDENT_CAPACITY; ++index) {
        st_gateway_incident_state_t *incident = &state->persistent.incidents[index];
        if (incident->used == 0U) {
            if (free_entry == NULL) {
                free_entry = incident;
            }
        } else if (incident->incident_kind == kind &&
                   strcmp(incident->pod_id, pod_id) == 0) {
            return incident;
        }
    }
    if (!create || free_entry == NULL) {
        return NULL;
    }
    memset(free_entry, 0, sizeof(*free_entry));
    free_entry->used = 1U;
    free_entry->incident_kind = kind;
    free_entry->primary_capability = ST_SENSOR_UNKNOWN;
    free_entry->secondary_capability = ST_SENSOR_UNKNOWN;
    copy_id(free_entry->pod_id, sizeof(free_entry->pod_id), pod_id);
    ++state->persistent.incident_count;
    return free_entry;
}

static st_gateway_pod_track_t *find_pod(st_gateway_state_t *state,
                                        const char *pod_id, int create)
{
    size_t index;
    st_gateway_pod_track_t *free_entry = NULL;
    for (index = 0U; index < ST_GATEWAY_STATE_POD_CAPACITY; ++index) {
        st_gateway_pod_track_t *pod = &state->pods[index];
        if (pod->used == 0U) {
            if (free_entry == NULL) {
                free_entry = pod;
            }
        } else if (strcmp(pod->pod_id, pod_id) == 0) {
            return pod;
        }
    }
    if (!create || free_entry == NULL) {
        return NULL;
    }
    memset(free_entry, 0, sizeof(*free_entry));
    free_entry->used = 1U;
    copy_id(free_entry->pod_id, sizeof(free_entry->pod_id), pod_id);
    return free_entry;
}

int st_gateway_state_register_pod(st_gateway_state_t *state,
                                  const char *pod_id, uint64_t now_ms)
{
    st_gateway_pod_track_t *pod;
    if (state == NULL || pod_id == NULL || pod_id[0] == '\0') {
        return -1;
    }
    pod = find_pod(state, pod_id, 1);
    if (pod == NULL) {
        return -1;
    }
    if (pod->tracked_since_ms == 0U && pod->last_seen_ms == 0U) {
        pod->tracked_since_ms = now_ms;
    }
    return 0;
}

static st_gateway_sensor_track_t *find_sensor(st_gateway_state_t *state,
                                              const st_sensor_reading_t *reading)
{
    size_t index;
    st_gateway_sensor_track_t *free_entry = NULL;
    for (index = 0U; index < ST_GATEWAY_STATE_SENSOR_CAPACITY; ++index) {
        st_gateway_sensor_track_t *sensor = &state->sensors[index];
        if (sensor->used == 0U) {
            if (free_entry == NULL) {
                free_entry = sensor;
            }
        } else if (strcmp(sensor->pod_id, reading->pod_id) == 0 &&
                   strcmp(sensor->sensor_id, reading->sensor_id) == 0) {
            return sensor;
        }
    }
    if (free_entry == NULL) {
        return NULL;
    }
    memset(free_entry, 0, sizeof(*free_entry));
    free_entry->used = 1U;
    free_entry->capability = reading->sensor_kind;
    copy_id(free_entry->pod_id, sizeof(free_entry->pod_id), reading->pod_id);
    copy_id(free_entry->sensor_id, sizeof(free_entry->sensor_id), reading->sensor_id);
    return free_entry;
}

static float sensor_trend(const st_gateway_sensor_track_t *sensor)
{
    uint8_t oldest;
    uint8_t newest;
    if (sensor == NULL || sensor->window_count < 2U) {
        return 0.0F;
    }
    oldest = sensor->window_count < ST_GATEWAY_STATE_TREND_WINDOW
                 ? 0U
                 : sensor->window_next;
    newest = sensor->window_next == 0U ? ST_GATEWAY_STATE_TREND_WINDOW - 1U
                                       : sensor->window_next - 1U;
    return (sensor->values[newest] - sensor->values[oldest]) /
           (float)(sensor->window_count - 1U);
}

static float trend_for_capability(const st_gateway_state_t *state,
                                  const char *pod_id,
                                  st_sensor_kind_t capability)
{
    size_t index;
    for (index = 0U; index < ST_GATEWAY_STATE_SENSOR_CAPACITY; ++index) {
        const st_gateway_sensor_track_t *sensor = &state->sensors[index];
        if (sensor->used != 0U && sensor->capability == capability &&
            strcmp(sensor->pod_id, pod_id) == 0) {
            return sensor_trend(sensor);
        }
    }
    return 0.0F;
}

static uint64_t make_instance_id(st_gateway_state_t *state, const char *pod_id)
{
    uint32_t hash = 2166136261U;
    size_t index;
    for (index = 0U; pod_id[index] != '\0'; ++index) {
        hash ^= (uint8_t)pod_id[index];
        hash *= 16777619U;
    }
    ++state->persistent.next_instance_counter;
    if (state->persistent.next_instance_counter == 0U) {
        state->persistent.next_instance_counter = 1U;
    }
    return ((uint64_t)(hash & 0xFFFFU) << 32U) |
           state->persistent.next_instance_counter;
}

static int persist(st_gateway_state_t *state)
{
    return state->persistence.save == NULL
               ? 0
               : state->persistence.save(state->persistence.context,
                                         &state->persistent);
}

static void emit_incident(st_gateway_state_t *state,
                          const st_gateway_incident_state_t *incident,
                          st_control_transition_t transition,
                          st_control_reason_t reason, uint64_t now_ms)
{
    st_control_event_t event;
    memset(&event, 0, sizeof(event));
    event.event_kind = ST_CONTROL_EVENT_GATEWAY_INCIDENT;
    event.transition = transition;
    event.reason = reason;
    event.active = incident->active;
    event.instance_id = incident->instance_id;
    event.timestamp_ms = now_ms;
    event.capability = incident->primary_capability;
    event.secondary_capability = incident->secondary_capability;
    event.observed = incident->primary_observed;
    event.secondary_observed = incident->secondary_observed;
    event.trend = incident->trend;
    event.evidence_count = incident->evidence_count;
    event.sequence = ++state->event_sequence;
    copy_id(event.pod_id, sizeof(event.pod_id), incident->pod_id);
    copy_id(event.sensor_id, sizeof(event.sensor_id),
            incident->incident_kind == ST_GATEWAY_INCIDENT_POD_STALE
                ? "gateway_pod_stale"
                : "gateway_multi_sensor");
    queue_event(state, &event);
}

static int transition_incident(st_gateway_state_t *state,
                               st_gateway_incident_state_t *incident,
                               int active, st_control_reason_t reason,
                               uint64_t now_ms)
{
    st_gateway_state_persistent_t before = state->persistent;
    uint64_t cleared_id = incident->instance_id;
    incident->active = active ? 1U : 0U;
    if (active) {
        incident->instance_id = make_instance_id(state, incident->pod_id);
    }
    if (persist(state) != 0) {
        state->persistent = before;
        return -1;
    }
    if (!active) {
        incident->instance_id = cleared_id;
    }
    emit_incident(state, incident,
                  active ? ST_CONTROL_TRANSITION_ACTIVE
                         : ST_CONTROL_TRANSITION_CLEARED,
                  reason, now_ms);
    if (!active) {
        incident->instance_id = 0U;
    }
    return 0;
}

int st_gateway_state_init(st_gateway_state_t *state,
                          st_gateway_state_persistence_t persistence,
                          uint64_t now_ms)
{
    size_t index;
    if (state == NULL) {
        return -1;
    }
    memset(state, 0, sizeof(*state));
    state->persistence = persistence;
    st_gateway_state_persistent_init(&state->persistent);
    if (persistence.load != NULL) {
        st_gateway_state_persistent_t loaded;
        memset(&loaded, 0, sizeof(loaded));
        if (persistence.load(persistence.context, &loaded) == 0 &&
            st_gateway_state_persistent_valid(&loaded)) {
            state->persistent = loaded;
        }
    }
    for (index = 0U; index < ST_GATEWAY_STATE_INCIDENT_CAPACITY; ++index) {
        const st_gateway_incident_state_t *incident =
            &state->persistent.incidents[index];
        if (incident->used != 0U && incident->active != 0U) {
            (void)st_gateway_state_register_pod(state, incident->pod_id,
                                                now_ms);
            emit_incident(state, incident, ST_CONTROL_TRANSITION_RECOVERED,
                          ST_CONTROL_REASON_COORDINATOR_RESTART, now_ms);
        }
    }
    return 0;
}

int st_gateway_state_ingest_telemetry(st_gateway_state_t *state,
                                      const st_telemetry_record_t *record,
                                      uint64_t now_ms)
{
    st_gateway_sensor_track_t *sensor;
    st_gateway_pod_track_t *pod;
    if (state == NULL || record == NULL || record->reading.pod_id[0] == '\0' ||
        record->reading.sensor_id[0] == '\0' ||
        record->reading.sensor_kind > ST_SENSOR_UNKNOWN ||
        !isfinite(record->reading.value)) {
        return -1;
    }
    pod = find_pod(state, record->reading.pod_id, 1);
    sensor = find_sensor(state, &record->reading);
    if (pod == NULL || sensor == NULL) {
        return -1;
    }
    if (pod->tracked_since_ms == 0U && pod->last_seen_ms == 0U) {
        pod->tracked_since_ms = now_ms;
    }
    pod->last_seen_ms = now_ms;
    pod->stale_candidate_since_ms = 0U;
    sensor->last_seen_ms = now_ms;
    sensor->quality_flags = record->reading.quality_flags;
    sensor->capability = record->reading.sensor_kind;
    sensor->values[sensor->window_next] = record->reading.value;
    sensor->window_next = (uint8_t)((sensor->window_next + 1U) %
                                    ST_GATEWAY_STATE_TREND_WINDOW);
    if (sensor->window_count < ST_GATEWAY_STATE_TREND_WINDOW) {
        ++sensor->window_count;
    }
    return 0;
}

static st_gateway_alarm_evidence_t *find_alarm(st_gateway_state_t *state,
                                               const st_control_event_t *event,
                                               int create)
{
    size_t index;
    st_gateway_alarm_evidence_t *free_entry = NULL;
    st_gateway_alarm_evidence_t *reusable_entry = NULL;
    for (index = 0U; index < ST_GATEWAY_STATE_ALARM_CAPACITY; ++index) {
        st_gateway_alarm_evidence_t *alarm = &state->persistent.alarms[index];
        if (alarm->used == 0U) {
            if (free_entry == NULL) {
                free_entry = alarm;
            }
        } else if (alarm->active == 0U && reusable_entry == NULL) {
            reusable_entry = alarm;
        } else if (alarm->alarm_instance_id == event->instance_id &&
                   strcmp(alarm->pod_id, event->pod_id) == 0) {
            return alarm;
        }
    }
    if (!create || (free_entry == NULL && reusable_entry == NULL)) {
        return NULL;
    }
    if (free_entry == NULL) {
        free_entry = reusable_entry;
    }
    if (free_entry->used != 0U) {
        memset(free_entry, 0, sizeof(*free_entry));
        free_entry->used = 1U;
        free_entry->alarm_instance_id = event->instance_id;
        copy_id(free_entry->pod_id, sizeof(free_entry->pod_id), event->pod_id);
        return free_entry;
    }
    memset(free_entry, 0, sizeof(*free_entry));
    free_entry->used = 1U;
    free_entry->alarm_instance_id = event->instance_id;
    copy_id(free_entry->pod_id, sizeof(free_entry->pod_id), event->pod_id);
    ++state->persistent.alarm_count;
    return free_entry;
}

int st_gateway_state_ingest_control(st_gateway_state_t *state,
                                    const st_control_event_t *event,
                                    uint64_t now_ms)
{
    st_gateway_alarm_evidence_t *alarm;
    st_gateway_state_persistent_t before;
    if (state == NULL || event == NULL ||
        event->event_kind != ST_CONTROL_EVENT_ALARM_CONDITION ||
        event->instance_id == 0U || event->pod_id[0] == '\0') {
        return -1;
    }
    before = state->persistent;
    alarm = find_alarm(state, event, event->active != 0U);
    if (alarm == NULL) {
        return event->active == 0U ? 0 : -1;
    }
    alarm->capability = event->capability;
    alarm->observed = event->observed;
    alarm->threshold = event->threshold;
    alarm->active = event->active;
    (void)st_gateway_state_register_pod(state, event->pod_id, now_ms);
    if (persist(state) != 0) {
        state->persistent = before;
        return -2;
    }
    return 0;
}

static uint8_t active_alarm_evidence(st_gateway_state_t *state,
                                     const char *pod_id,
                                     st_gateway_incident_state_t *incident)
{
    size_t index;
    uint8_t count = 0U;
    incident->primary_capability = ST_SENSOR_UNKNOWN;
    incident->secondary_capability = ST_SENSOR_UNKNOWN;
    incident->primary_observed = 0.0F;
    incident->secondary_observed = 0.0F;
    for (index = 0U; index < ST_GATEWAY_STATE_ALARM_CAPACITY; ++index) {
        const st_gateway_alarm_evidence_t *alarm = &state->persistent.alarms[index];
        if (alarm->used == 0U || alarm->active == 0U ||
            strcmp(alarm->pod_id, pod_id) != 0) {
            continue;
        }
        if ((count > 0U && alarm->capability == incident->primary_capability) ||
            (count > 1U && alarm->capability == incident->secondary_capability)) {
            continue;
        }
        if (count == 0U) {
            incident->primary_capability = alarm->capability;
            incident->primary_observed = alarm->observed;
        } else if (count == 1U) {
            incident->secondary_capability = alarm->capability;
            incident->secondary_observed = alarm->observed;
        }
        ++count;
    }
    incident->evidence_count = count;
    incident->trend = trend_for_capability(state, pod_id,
                                           incident->primary_capability);
    return count;
}

static void tick_pod(st_gateway_state_t *state, st_gateway_pod_track_t *pod,
                     uint64_t now_ms)
{
    st_gateway_incident_state_t *stale = find_incident(
        state, pod->pod_id, ST_GATEWAY_INCIDENT_POD_STALE, 1);
    st_gateway_incident_state_t *multi = find_incident(
        state, pod->pod_id, ST_GATEWAY_INCIDENT_MULTI_SENSOR, 1);
    uint64_t freshness_reference_ms;
    int should_stale;
    uint8_t evidence;

    if (stale == NULL || multi == NULL) {
        return;
    }
    freshness_reference_ms = pod->last_seen_ms != 0U ? pod->last_seen_ms
                                                     : pod->tracked_since_ms;
    should_stale = now_ms >= freshness_reference_ms &&
                   now_ms - freshness_reference_ms >=
                       ST_GATEWAY_FRESHNESS_TIMEOUT_MS;
    if (should_stale && stale->active == 0U) {
        if (pod->stale_candidate_since_ms == 0U) {
            pod->stale_candidate_since_ms = now_ms;
        } else if (now_ms - pod->stale_candidate_since_ms >=
                   ST_GATEWAY_STATE_DEBOUNCE_MS) {
            stale->evidence_count = 1U;
            (void)transition_incident(state, stale, 1,
                                      ST_CONTROL_REASON_STALE_DATA, now_ms);
            pod->stale_candidate_since_ms = 0U;
        }
        pod->fresh_candidate_since_ms = 0U;
    } else if (!should_stale && stale->active != 0U) {
        if (pod->fresh_candidate_since_ms == 0U) {
            pod->fresh_candidate_since_ms = now_ms;
        } else if (now_ms - pod->fresh_candidate_since_ms >=
                   ST_GATEWAY_STATE_DEBOUNCE_MS) {
            (void)transition_incident(state, stale, 0,
                                      ST_CONTROL_REASON_STALE_DATA, now_ms);
            pod->fresh_candidate_since_ms = 0U;
        }
        pod->stale_candidate_since_ms = 0U;
    }

    evidence = active_alarm_evidence(state, pod->pod_id, multi);
    if (evidence >= 2U && multi->active == 0U) {
        if (pod->multi_candidate_since_ms == 0U) {
            pod->multi_candidate_since_ms = now_ms;
        } else if (now_ms - pod->multi_candidate_since_ms >=
                   ST_GATEWAY_STATE_DEBOUNCE_MS) {
            (void)transition_incident(state, multi, 1,
                                      ST_CONTROL_REASON_MULTI_SENSOR, now_ms);
            pod->multi_candidate_since_ms = 0U;
        }
        pod->multi_clear_since_ms = 0U;
    } else if (evidence < 2U && multi->active != 0U) {
        if (pod->multi_clear_since_ms == 0U) {
            pod->multi_clear_since_ms = now_ms;
        } else if (now_ms - pod->multi_clear_since_ms >=
                   ST_GATEWAY_STATE_DEBOUNCE_MS) {
            (void)transition_incident(state, multi, 0,
                                      ST_CONTROL_REASON_MULTI_SENSOR, now_ms);
            pod->multi_clear_since_ms = 0U;
        }
        pod->multi_candidate_since_ms = 0U;
    }
}

void st_gateway_state_tick(st_gateway_state_t *state, uint64_t now_ms)
{
    size_t index;
    if (state == NULL) {
        return;
    }
    for (index = 0U; index < ST_GATEWAY_STATE_POD_CAPACITY; ++index) {
        if (state->pods[index].used != 0U) {
            tick_pod(state, &state->pods[index], now_ms);
        }
    }
}

int st_gateway_state_next_event(st_gateway_state_t *state,
                                st_control_event_t *event)
{
    if (state == NULL || event == NULL || state->event_count == 0U) {
        return -1;
    }
    *event = state->events[0];
    if (state->event_count > 1U) {
        memmove(&state->events[0], &state->events[1],
                (state->event_count - 1U) * sizeof(state->events[0]));
    }
    --state->event_count;
    return 0;
}
