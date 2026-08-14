#include "control_persistence.h"

#include <string.h>

#include "nvs.h"

#define ST_POD_NVS_NAMESPACE "st_pod_ctrl"
#define ST_POD_NVS_KEY "state"
#define ST_GATEWAY_NVS_NAMESPACE "st_gw_state"
#define ST_GATEWAY_NVS_KEY "state"

static int pod_load(void *context, st_command_persistent_state_t *state)
{
    nvs_handle_t handle;
    size_t length = 0U;
    esp_err_t result;
    (void)context;

    if (state == NULL || nvs_open(ST_POD_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return -1;
    }
    result = nvs_get_blob(handle, ST_POD_NVS_KEY, NULL, &length);
    if (result != ESP_OK ||
        (length != ST_COMMAND_PERSISTENCE_V1_SIZE &&
         length != ST_COMMAND_PERSISTENCE_V2_SIZE &&
         length != sizeof(*state))) {
        nvs_close(handle);
        return -1;
    }
    memset(state, 0, sizeof(*state));
    result = nvs_get_blob(handle, ST_POD_NVS_KEY, state, &length);
    nvs_close(handle);
    return result == ESP_OK ? 0 : -1;
}

static int pod_save(void *context, const st_command_persistent_state_t *state)
{
    nvs_handle_t handle;
    esp_err_t result;
    (void)context;

    if (state == NULL || nvs_open(ST_POD_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return -1;
    }
    result = nvs_set_blob(handle, ST_POD_NVS_KEY, state, sizeof(*state));
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result == ESP_OK ? 0 : -1;
}

static int gateway_load(void *context, st_gateway_state_persistent_t *state)
{
    nvs_handle_t handle;
    size_t length = sizeof(*state);
    esp_err_t result;
    (void)context;

    if (state == NULL ||
        nvs_open(ST_GATEWAY_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return -1;
    }
    memset(state, 0, sizeof(*state));
    result = nvs_get_blob(handle, ST_GATEWAY_NVS_KEY, state, &length);
    nvs_close(handle);
    return result == ESP_OK && length == sizeof(*state) ? 0 : -1;
}

static int gateway_save(void *context, const st_gateway_state_persistent_t *state)
{
    nvs_handle_t handle;
    esp_err_t result;
    (void)context;

    if (state == NULL ||
        nvs_open(ST_GATEWAY_NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return -1;
    }
    result = nvs_set_blob(handle, ST_GATEWAY_NVS_KEY, state, sizeof(*state));
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    nvs_close(handle);
    return result == ESP_OK ? 0 : -1;
}

st_command_persistence_t st_espidf_pod_command_persistence(void)
{
    st_command_persistence_t persistence = {NULL, pod_load, pod_save};
    return persistence;
}

st_gateway_state_persistence_t st_espidf_gateway_state_persistence(void)
{
    st_gateway_state_persistence_t persistence = {NULL, gateway_load, gateway_save};
    return persistence;
}
