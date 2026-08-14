#ifndef SITETWIN_COMMAND_H
#define SITETWIN_COMMAND_H

#include <stddef.h>
#include <stdint.h>

#include "sitetwin/capability_config.h"
#include "sitetwin/contracts.h"

#define ST_COMMAND_CONTRACT_VERSION 2U
#define ST_COMMAND_LEGACY_CONTRACT_VERSION 1U
#define ST_COMMAND_PERSISTENCE_VERSION 2U
#define ST_COMMAND_WIRE_SIZE 64U
#define ST_COMMAND_ACK_WIRE_SIZE 44U
#define ST_COMMAND_HISTORY_CAPACITY 8U
#define ST_COMMAND_DEFAULT_CO2_THRESHOLD_PPM ST_CAPABILITY_DEFAULT_CO2_THRESHOLD_PPM
#define ST_COMMAND_MIN_CO2_THRESHOLD_PPM 400.0F
#define ST_COMMAND_MAX_CO2_THRESHOLD_PPM 5000.0F
#define ST_COMMAND_PERSISTENCE_MAGIC 0x5354434DU

typedef enum {
    ST_COMMAND_SET_THRESHOLD = 1,
    ST_COMMAND_SILENCE_ALARM,
    ST_COMMAND_TEST_OUTPUT,
    ST_COMMAND_GET_CONFIG,
    ST_COMMAND_SET_RULE,
    ST_COMMAND_GET_RULE
} st_command_type_t;

typedef enum {
    ST_COMMAND_TARGET_CO2_THRESHOLD = 1,
    ST_COMMAND_TARGET_ALARM,
    ST_COMMAND_TARGET_LED,
    ST_COMMAND_TARGET_BUZZER,
    ST_COMMAND_TARGET_CONFIG,
    ST_COMMAND_TARGET_CAPABILITY_RULE
} st_command_target_t;

typedef enum {
    ST_COMMAND_SOURCE_THINGSBOARD = 1,
    ST_COMMAND_SOURCE_LOCAL_MAINTENANCE
} st_command_source_t;

typedef enum {
    ST_COMMAND_STATUS_QUEUED = 1,
    ST_COMMAND_STATUS_DELIVERED,
    ST_COMMAND_STATUS_EXECUTED,
    ST_COMMAND_STATUS_REJECTED,
    ST_COMMAND_STATUS_EXPIRED,
    ST_COMMAND_STATUS_DUPLICATE,
    ST_COMMAND_STATUS_FAILED
} st_command_status_t;

typedef enum {
    ST_COMMAND_REASON_NONE = 0,
    ST_COMMAND_REASON_INVALID_SYNTAX,
    ST_COMMAND_REASON_WRONG_TARGET,
    ST_COMMAND_REASON_UNSUPPORTED,
    ST_COMMAND_REASON_EXPIRED,
    ST_COMMAND_REASON_REVISION_CONFLICT,
    ST_COMMAND_REASON_OUT_OF_BOUNDS,
    ST_COMMAND_REASON_PERSISTENCE_FAILED,
    ST_COMMAND_REASON_QUEUE_FULL,
    ST_COMMAND_REASON_TRANSPORT_FAILED,
    ST_COMMAND_REASON_TIMEOUT,
    ST_COMMAND_REASON_NOT_CONFIGURED,
    ST_COMMAND_REASON_CONFIG_FULL
} st_command_reason_t;

typedef struct {
    uint64_t command_id;
    char target_pod_id[ST_POD_ID_MAX_LEN];
    st_command_type_t command_type;
    st_command_target_t target;
    st_sensor_kind_t capability;
    st_config_rule_kind_t rule_kind;
    float value;
    uint32_t duration_ms;
    uint64_t issued_at_ms;
    uint64_t expires_at_ms;
    uint32_t valid_for_ms;
    uint32_t config_revision;
    st_command_source_t source;
} st_command_t;

typedef struct {
    uint64_t command_id;
    char pod_id[ST_POD_ID_MAX_LEN];
    st_command_status_t status;
    st_command_reason_t reason;
    uint32_t applied_config_revision;
    uint64_t timestamp_ms;
    float config_value;
} st_command_ack_t;

/* Prefix retained for migration of the version-1 CO2-only NVS blob. */
typedef struct {
    float co2_threshold_ppm;
    uint32_t revision;
} st_pod_command_config_t;

typedef struct {
    uint64_t command_id;
    st_command_status_t status;
    st_command_reason_t reason;
    uint32_t applied_config_revision;
    uint64_t timestamp_ms;
} st_command_history_entry_t;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t history_count;
    uint8_t history_next;
    uint8_t reserved;
    st_pod_command_config_t config;
    st_command_history_entry_t history[ST_COMMAND_HISTORY_CAPACITY];
    st_capability_config_t capability_config;
} st_command_persistent_state_t;

#define ST_COMMAND_PERSISTENCE_V1_SIZE offsetof(st_command_persistent_state_t, capability_config)

typedef struct {
    void *context;
    int (*load)(void *context, st_command_persistent_state_t *state);
    int (*save)(void *context, const st_command_persistent_state_t *state);
} st_command_persistence_t;

typedef struct {
    uint32_t command_mask;
    uint32_t target_mask;
    uint32_t capability_mask;
    uint8_t pending_hardware_verification;
} st_pod_capabilities_t;

typedef struct {
    char pod_id[ST_POD_ID_MAX_LEN];
    st_pod_profile_t profile;
    st_pod_capabilities_t capabilities;
    st_command_persistence_t persistence;
    st_command_persistent_state_t persistent;
} st_command_runtime_t;

int st_command_encode(const st_command_t *command, uint8_t *payload,
                      size_t capacity, size_t *length);
int st_command_decode(const uint8_t *payload, size_t length, st_command_t *command);
int st_command_ack_encode(const st_command_ack_t *ack, uint8_t *payload,
                          size_t capacity, size_t *length);
int st_command_ack_decode(const uint8_t *payload, size_t length, st_command_ack_t *ack);

st_pod_capabilities_t st_pod_capabilities(st_pod_profile_t profile);
int st_command_runtime_init(st_command_runtime_t *runtime, st_pod_profile_t profile,
                            const char *pod_id, st_command_persistence_t persistence);
int st_command_runtime_handle(st_command_runtime_t *runtime, const st_command_t *command,
                              uint64_t now_ms, st_command_ack_t *ack);
int st_command_runtime_get_rule(const st_command_runtime_t *runtime,
                                st_sensor_kind_t capability,
                                st_config_rule_kind_t rule_kind,
                                st_capability_rule_t *rule);
int st_command_runtime_apply_reporting_rules(const st_command_runtime_t *runtime,
                                             st_reporting_policy_t *policy);

const char *st_command_type_name(st_command_type_t type);
const char *st_command_target_name(st_command_target_t target);
const char *st_command_status_name(st_command_status_t status);
const char *st_command_reason_name(st_command_reason_t reason);

#endif
