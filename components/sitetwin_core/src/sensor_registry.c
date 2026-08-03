#include "sitetwin/sensor_registry.h"

#include <string.h>

static void copy_string(char *destination, size_t capacity, const char *source)
{
    if (capacity == 0U) {
        return;
    }

    if (source == NULL) {
        destination[0] = '\0';
        return;
    }

    strncpy(destination, source, capacity - 1U);
    destination[capacity - 1U] = '\0';
}

static void transition(st_sensor_port_t *port, st_sensor_port_state_t state, uint64_t now_ms)
{
    port->state = state;
    port->last_transition_at_ms = now_ms;
}

void st_sensor_registry_init(st_sensor_registry_t *registry, const char *pod_id, uint32_t boot_id)
{
    if (registry == NULL) {
        return;
    }

    memset(registry, 0, sizeof(*registry));
    copy_string(registry->pod_id, sizeof(registry->pod_id), pod_id);
    registry->boot_id = boot_id;
}

int st_sensor_registry_attach(st_sensor_registry_t *registry, uint8_t port_index, st_sensor_driver_t driver)
{
    st_sensor_port_t *port;

    if (registry == NULL || port_index >= ST_MAX_SENSOR_PORTS || driver.probe == NULL || driver.sample == NULL) {
        return -1;
    }

    port = &registry->ports[port_index];
    memset(port, 0, sizeof(*port));
    port->driver = driver;
    port->attached = 1U;
    port->state = ST_PORT_PROBING;
    return 0;
}

void st_sensor_registry_detach(st_sensor_registry_t *registry, uint8_t port_index, uint64_t now_ms)
{
    st_sensor_port_t *port;

    if (registry == NULL || port_index >= ST_MAX_SENSOR_PORTS) {
        return;
    }

    port = &registry->ports[port_index];
    memset(port, 0, sizeof(*port));
    port->last_transition_at_ms = now_ms;
}

static void probe_port(st_sensor_port_t *port, uint64_t now_ms)
{
    st_module_metadata_t metadata;
    st_driver_result_t result;

    memset(&metadata, 0, sizeof(metadata));
    result = port->driver.probe(port->driver.context, &metadata);

    if (result == ST_DRIVER_READY) {
        if (metadata.sample_interval_ms == 0U) {
            metadata.sample_interval_ms = 1000U;
        }
        port->metadata = metadata;
        port->next_sample_at_ms = now_ms;
        transition(port, ST_PORT_WARMING_UP, now_ms);
    } else if (result == ST_DRIVER_ERROR) {
        port->next_sample_at_ms = now_ms + 1000U;
        transition(port, ST_PORT_FAULTED, now_ms);
    } else {
        port->next_sample_at_ms = now_ms + 1000U;
        transition(port, ST_PORT_PROBING, now_ms);
    }
}

size_t st_sensor_registry_tick(st_sensor_registry_t *registry, uint64_t now_ms,
                               st_sensor_reading_t *readings, size_t reading_capacity)
{
    size_t port_index;
    size_t reading_count = 0U;

    if (registry == NULL || readings == NULL) {
        return 0U;
    }

    for (port_index = 0U; port_index < ST_MAX_SENSOR_PORTS; ++port_index) {
        st_sensor_port_t *port = &registry->ports[port_index];
        st_driver_sample_t sample;
        st_driver_result_t result;
        st_sensor_reading_t *reading;

        if (port->attached == 0U) {
            continue;
        }

        if (port->state == ST_PORT_PROBING || port->state == ST_PORT_FAULTED) {
            if (now_ms >= port->next_sample_at_ms) {
                probe_port(port, now_ms);
            }
            continue;
        }

        if (now_ms < port->next_sample_at_ms) {
            continue;
        }

        memset(&sample, 0, sizeof(sample));
        result = port->driver.sample(port->driver.context, now_ms, &sample);
        if (result == ST_DRIVER_NOT_PRESENT) {
            memset(&port->metadata, 0, sizeof(port->metadata));
            port->next_sample_at_ms = now_ms + 1000U;
            transition(port, ST_PORT_PROBING, now_ms);
            continue;
        }
        if (result == ST_DRIVER_ERROR) {
            port->next_sample_at_ms = now_ms + 1000U;
            transition(port, ST_PORT_FAULTED, now_ms);
            continue;
        }
        if (result == ST_DRIVER_RETRY) {
            port->next_sample_at_ms = now_ms + 100U;
            continue;
        }

        port->next_sample_at_ms = now_ms + port->metadata.sample_interval_ms;
        if ((sample.quality_flags & ST_QUALITY_WARMING_UP) != 0U) {
            transition(port, ST_PORT_WARMING_UP, now_ms);
        } else {
            transition(port, ST_PORT_READY, now_ms);
        }

        if (reading_count >= reading_capacity) {
            continue;
        }

        reading = &readings[reading_count++];
        memset(reading, 0, sizeof(*reading));
        copy_string(reading->pod_id, sizeof(reading->pod_id), registry->pod_id);
        copy_string(reading->sensor_id, sizeof(reading->sensor_id), port->metadata.sensor_id);
        reading->sensor_kind = port->metadata.sensor_kind;
        reading->unit = sample.unit;
        reading->sequence = ++port->next_sequence;
        reading->boot_id = registry->boot_id;
        reading->uptime_ms = now_ms;
        reading->value = sample.value;
        reading->quality_flags = sample.quality_flags;
        if ((reading->quality_flags & (ST_QUALITY_CRC_FAILED | ST_QUALITY_SENSOR_MISSING)) == 0U) {
            reading->quality_flags |= ST_QUALITY_VALID;
        }
    }

    return reading_count;
}
