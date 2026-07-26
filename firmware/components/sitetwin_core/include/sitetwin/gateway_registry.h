#ifndef SITETWIN_GATEWAY_REGISTRY_H
#define SITETWIN_GATEWAY_REGISTRY_H

#include <stdint.h>

#include "sitetwin/contracts.h"

#define ST_GATEWAY_NODE_CAPACITY 16U
#define ST_GATEWAY_SENSOR_SLOTS 8U

typedef struct {
    uint16_t source_address;
    uint64_t ieee_address;
    char pod_id[ST_POD_ID_MAX_LEN];
    char sensor_ids[ST_GATEWAY_SENSOR_SLOTS][ST_SENSOR_ID_MAX_LEN];
    uint8_t used;
} st_gateway_node_t;

typedef struct {
    st_gateway_node_t nodes[ST_GATEWAY_NODE_CAPACITY];
} st_gateway_registry_t;

void st_gateway_registry_init(st_gateway_registry_t *registry);
int st_gateway_registry_register_node(st_gateway_registry_t *registry, uint16_t source_address,
                                      uint64_t ieee_address, const char *pod_id);
int st_gateway_registry_bind_sensor(st_gateway_registry_t *registry, uint16_t source_address,
                                    uint8_t sensor_slot, const char *sensor_id);
int st_gateway_registry_resolve(const st_gateway_registry_t *registry, uint16_t source_address,
                                uint8_t sensor_slot, const char **pod_id,
                                const char **sensor_id);

#endif
