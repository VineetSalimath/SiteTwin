#ifndef SITETWIN_SENSOR_REGISTRY_H
#define SITETWIN_SENSOR_REGISTRY_H

#include <stddef.h>
#include <stdint.h>

#include "sitetwin/contracts.h"
#include "sitetwin/sensor_driver.h"

#define ST_MAX_SENSOR_PORTS 8U

typedef enum {
    ST_PORT_EMPTY = 0,
    ST_PORT_PROBING,
    ST_PORT_WARMING_UP,
    ST_PORT_READY,
    ST_PORT_FAULTED
} st_sensor_port_state_t;

typedef struct {
    st_sensor_driver_t driver;
    st_module_metadata_t metadata;
    st_sensor_port_state_t state;
    uint32_t next_sequence;
    uint64_t next_sample_at_ms;
    uint64_t last_transition_at_ms;
    uint8_t attached;
} st_sensor_port_t;

typedef struct {
    st_sensor_port_t ports[ST_MAX_SENSOR_PORTS];
    char pod_id[ST_POD_ID_MAX_LEN];
    uint32_t boot_id;
} st_sensor_registry_t;

void st_sensor_registry_init(st_sensor_registry_t *registry, const char *pod_id, uint32_t boot_id);
int st_sensor_registry_attach(st_sensor_registry_t *registry, uint8_t port_index, st_sensor_driver_t driver);
void st_sensor_registry_detach(st_sensor_registry_t *registry, uint8_t port_index, uint64_t now_ms);
size_t st_sensor_registry_tick(st_sensor_registry_t *registry, uint64_t now_ms,
                               st_sensor_reading_t *readings, size_t reading_capacity);

#endif
