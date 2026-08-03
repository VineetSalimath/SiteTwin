#include "sitetwin/gateway_registry.h"

#include <string.h>

static void copy_string(char *destination, size_t capacity, const char *source)
{
    strncpy(destination, source, capacity - 1U);
    destination[capacity - 1U] = '\0';
}

static st_gateway_node_t *find_node(st_gateway_registry_t *registry, uint16_t source_address)
{
    size_t index;

    for (index = 0U; index < ST_GATEWAY_NODE_CAPACITY; ++index) {
        if (registry->nodes[index].used != 0U &&
            registry->nodes[index].source_address == source_address) {
            return &registry->nodes[index];
        }
    }
    return NULL;
}

void st_gateway_registry_init(st_gateway_registry_t *registry)
{
    if (registry != NULL) {
        memset(registry, 0, sizeof(*registry));
    }
}

int st_gateway_registry_register_node(st_gateway_registry_t *registry, uint16_t source_address,
                                      uint64_t ieee_address, const char *pod_id)
{
    size_t index;
    st_gateway_node_t *node = NULL;
    st_gateway_node_t *free_node = NULL;
    st_gateway_node_t *source_owner = NULL;

    if (registry == NULL || source_address == 0U || ieee_address == 0U || pod_id == NULL ||
        pod_id[0] == '\0') {
        return -1;
    }

    for (index = 0U; index < ST_GATEWAY_NODE_CAPACITY; ++index) {
        st_gateway_node_t *candidate = &registry->nodes[index];

        if (candidate->used == 0U) {
            if (free_node == NULL) {
                free_node = candidate;
            }
        } else if (candidate->ieee_address == ieee_address) {
            node = candidate;
        } else if (candidate->source_address == source_address) {
            source_owner = candidate;
        }
    }

    if (source_owner != NULL && source_owner != node) {
        return -1;
    }

    if (node == NULL) {
        node = free_node;
        if (node == NULL) {
            return -1;
        }
        memset(node, 0, sizeof(*node));
        node->used = 1U;
        node->ieee_address = ieee_address;
    }
    node->source_address = source_address;
    copy_string(node->pod_id, sizeof(node->pod_id), pod_id);
    return 0;
}

int st_gateway_registry_bind_sensor(st_gateway_registry_t *registry, uint16_t source_address,
                                    uint8_t sensor_slot, const char *sensor_id)
{
    st_gateway_node_t *node;

    if (registry == NULL || sensor_slot >= ST_GATEWAY_SENSOR_SLOTS || sensor_id == NULL ||
        sensor_id[0] == '\0') {
        return -1;
    }
    node = find_node(registry, source_address);
    if (node == NULL) {
        return -1;
    }
    copy_string(node->sensor_ids[sensor_slot], sizeof(node->sensor_ids[sensor_slot]), sensor_id);
    return 0;
}

int st_gateway_registry_resolve(const st_gateway_registry_t *registry, uint16_t source_address,
                                uint8_t sensor_slot, const char **pod_id,
                                const char **sensor_id)
{
    size_t index;

    if (registry == NULL || sensor_slot >= ST_GATEWAY_SENSOR_SLOTS || pod_id == NULL ||
        sensor_id == NULL) {
        return -1;
    }
    for (index = 0U; index < ST_GATEWAY_NODE_CAPACITY; ++index) {
        const st_gateway_node_t *node = &registry->nodes[index];

        if (node->used != 0U && node->source_address == source_address &&
            node->sensor_ids[sensor_slot][0] != '\0') {
            *pod_id = node->pod_id;
            *sensor_id = node->sensor_ids[sensor_slot];
            return 0;
        }
    }
    return -1;
}
