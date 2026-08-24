#include <stdio.h>
#include <string.h>

#include "sitetwin/fake_sensor.h"
#include "sitetwin/gateway_frame.h"
#include "sitetwin/gateway_json.h"
#include "sitetwin/gateway_processor.h"
#include "sitetwin/gateway_runtime.h"
#include "sitetwin/pod_runtime.h"
#include "sitetwin/telemetry_queue.h"
#include "sitetwin/zigbee_payload.h"

int st_run_sensor_foundation_tests(void);
int st_run_bh1750_tests(void);
int st_run_reed_tests(void);
int st_run_ina219_tests(void);
int st_run_pir_tests(void);
int st_run_adxl345_tests(void);
int st_run_ds18b20_tests(void);
int st_run_scd41_tests(void);
int st_run_sgp40_tests(void);
int st_run_command_actuation_tests(void);
int st_run_command_transport_tests(void);
int st_run_gateway_identity_tests(void);
int st_run_alarm_control_tests(void);
int st_run_gateway_state_tests(void);
int st_run_final_pcb_board_tests(void);
int st_run_board_port_manager_tests(void);
int st_run_hotswap_module_binding_tests(void);
int st_run_hotswap_scan_scheduler_tests(void);
int st_run_hotswap_zigbee_slot_tests(void);
int st_run_hotswap_integration_tests(void);
int st_run_shared_alarm_indicator_tests(void);

#define EXPECT(condition)                                                                        \
    do {                                                                                         \
        if (!(condition)) {                                                                      \
            fprintf(stderr, "Expectation failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

typedef struct {
    char sensor_id[ST_SENSOR_ID_MAX_LEN];
    uint32_t last_sequence;
    uint8_t used;
} sequence_tracker_slot_t;

static st_telemetry_record_t make_state_record(const char *sensor_id, uint32_t sequence)
{
    st_telemetry_record_t record;

    memset(&record, 0, sizeof(record));
    record.record_class = ST_RECORD_STATE;
    record.priority = ST_PRIORITY_ROUTINE;
    strcpy(record.reading.pod_id, "POD_1");
    strcpy(record.reading.sensor_id, sensor_id);
    record.reading.sequence = sequence;
    record.reading.sensor_kind = ST_SENSOR_TEMPERATURE_C;
    record.reading.unit = ST_UNIT_CELSIUS;
    record.reading.quality_flags = ST_QUALITY_VALID;
    return record;
}

static st_telemetry_record_t make_event_record(const char *sensor_id, uint32_t sequence,
                                               st_sensor_kind_t kind)
{
    st_telemetry_record_t record = make_state_record(sensor_id, sequence);

    record.record_class = ST_RECORD_EVENT;
    record.priority = ST_PRIORITY_EVENT;
    record.reading.sensor_kind = kind;
    record.reading.unit = ST_UNIT_BOOLEAN;
    return record;
}

static uint32_t next_random(uint32_t *state)
{
    uint32_t value;

    value = *state;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    if (value == 0U) {
        value = 0xA5A5A5A5U;
    }
    *state = value;
    return value;
}

static uint32_t random_range(uint32_t *state, uint32_t limit)
{
    return limit == 0U ? 0U : next_random(state) % limit;
}

static int track_sequence(sequence_tracker_slot_t *slots, size_t slot_capacity,
                          const st_telemetry_record_t *record)
{
    size_t index;
    size_t free_slot = slot_capacity;

    for (index = 0U; index < slot_capacity; ++index) {
        sequence_tracker_slot_t *slot = &slots[index];

        if (slot->used == 0U) {
            if (free_slot == slot_capacity) {
                free_slot = index;
            }
            continue;
        }
        if (strcmp(slot->sensor_id, record->reading.sensor_id) == 0) {
            if (record->reading.sequence <= slot->last_sequence) {
                return -1;
            }
            slot->last_sequence = record->reading.sequence;
            return 0;
        }
    }

    if (free_slot == slot_capacity) {
        return -1;
    }

    slots[free_slot].used = 1U;
    strcpy(slots[free_slot].sensor_id, record->reading.sensor_id);
    slots[free_slot].last_sequence = record->reading.sequence;
    return 0;
}

static int validate_serialised_record(const st_telemetry_record_t *record, uint16_t source_address)
{
    char json[512];
    uint8_t frame[768];
    size_t frame_length = 0U;
    const uint8_t *decoded_payload = NULL;
    st_gateway_frame_header_t header;
    st_gateway_frame_header_t decoded_header;
    size_t json_length;

    EXPECT(st_gateway_telemetry_to_json(record, json, sizeof(json)) == 0);
    json_length = strlen(json);

    header.version = ST_GATEWAY_FRAME_VERSION;
    header.message_type = ST_GATEWAY_MESSAGE_TELEMETRY;
    header.payload_length = (uint16_t)json_length;
    header.source_address = source_address;
    header.boot_id = record->reading.boot_id;
    header.sequence = record->reading.sequence;

    EXPECT(st_gateway_frame_encode(&header, (const uint8_t *)json, frame, sizeof(frame),
                                   &frame_length) == 0);
    EXPECT(st_gateway_frame_decode(frame, frame_length, &decoded_header, &decoded_payload) == 0);
    EXPECT(decoded_header.version == header.version);
    EXPECT(decoded_header.message_type == header.message_type);
    EXPECT(decoded_header.payload_length == header.payload_length);
    EXPECT(decoded_header.source_address == header.source_address);
    EXPECT(decoded_header.boot_id == header.boot_id);
    EXPECT(decoded_header.sequence == header.sequence);
    EXPECT(memcmp(decoded_payload, json, json_length) == 0);
    return 0;
}

static int test_registry_warmup_and_hot_swap(void)
{
    st_pod_runtime_t runtime;
    st_fake_sensor_t sensor;
    st_telemetry_record_t record;

    st_pod_runtime_init(&runtime, ST_POD_ENVIRONMENT, "POD_1", 42U);
    st_fake_sensor_init(&sensor, "sht41_temperature", "module-001", ST_SENSOR_TEMPERATURE_C,
                        ST_UNIT_CELSIUS, 1U, 20.0F);
    sensor.warm_until_ms = 500U;
    EXPECT(st_sensor_registry_attach(&runtime.registry, 0U, st_fake_sensor_driver(&sensor)) == 0);

    st_pod_runtime_tick(&runtime, 0U);
    EXPECT(st_telemetry_queue_count(&runtime.outbound) == 0U);
    st_pod_runtime_tick(&runtime, 1U);
    EXPECT(st_pod_runtime_next_telemetry(&runtime, &record) == 0);
    EXPECT(record.reading.sequence == 1U);
    EXPECT((record.reading.quality_flags & ST_QUALITY_WARMING_UP) != 0U);
    EXPECT((record.reading.quality_flags & ST_QUALITY_VALID) != 0U);

    sensor.present = false;
    st_pod_runtime_tick(&runtime, 2U);
    st_pod_runtime_tick(&runtime, 1002U);
    EXPECT(runtime.registry.ports[0].state == ST_PORT_PROBING);

    sensor.present = true;
    st_pod_runtime_tick(&runtime, 2002U);
    st_pod_runtime_tick(&runtime, 2003U);
    EXPECT(st_pod_runtime_next_telemetry(&runtime, &record) == 0);
    EXPECT(record.reading.sequence == 2U);
    EXPECT((record.reading.quality_flags & ST_QUALITY_WARMING_UP) == 0U);
    EXPECT(runtime.registry.ports[0].state == ST_PORT_READY);
    return 0;
}

static int test_delivery_policy(void)
{
    st_telemetry_queue_t queue;
    st_telemetry_record_t routine;
    st_telemetry_record_t replacement;
    st_telemetry_record_t event;
    st_telemetry_record_t output;

    st_telemetry_queue_init(&queue);
    routine = make_state_record("sht41_temperature", 1U);
    replacement = make_state_record("sht41_temperature", 2U);
    EXPECT(st_telemetry_queue_push(&queue, &routine) == 0);
    EXPECT(st_telemetry_queue_push(&queue, &replacement) == 0);
    EXPECT(st_telemetry_queue_count(&queue) == 1U);

    event = make_state_record("door_contact", 1U);
    event.record_class = ST_RECORD_EVENT;
    event.priority = ST_PRIORITY_EVENT;
    event.reading.sensor_kind = ST_SENSOR_CONTACT;
    EXPECT(st_telemetry_queue_push(&queue, &event) == 0);
    EXPECT(st_telemetry_queue_pop(&queue, &output) == 0);
    EXPECT(output.record_class == ST_RECORD_EVENT);
    EXPECT(st_telemetry_queue_pop(&queue, &output) == 0);
    EXPECT(output.reading.sequence == 2U);
    return 0;
}

static int test_gateway_frame_round_trip(void)
{
    const uint8_t payload[] = {0x10U, 0x20U, 0x30U};
    uint8_t frame[32];
    size_t frame_length = 0U;
    const uint8_t *decoded_payload = NULL;
    st_gateway_frame_header_t header = {
        .version = ST_GATEWAY_FRAME_VERSION,
        .message_type = ST_GATEWAY_MESSAGE_TELEMETRY,
        .payload_length = sizeof(payload),
        .source_address = 0x1234U,
        .boot_id = 0xAABBCCDDU,
        .sequence = 77U,
    };
    st_gateway_frame_header_t decoded_header;

    EXPECT(st_gateway_frame_encode(&header, payload, frame, sizeof(frame), &frame_length) == 0);
    EXPECT(frame_length == ST_GATEWAY_FRAME_HEADER_SIZE + sizeof(payload) + ST_GATEWAY_FRAME_CRC_SIZE);
    EXPECT(st_gateway_frame_decode(frame, frame_length, &decoded_header, &decoded_payload) == 0);
    EXPECT(decoded_header.source_address == header.source_address);
    EXPECT(decoded_header.boot_id == header.boot_id);
    EXPECT(decoded_header.sequence == header.sequence);
    EXPECT(memcmp(decoded_payload, payload, sizeof(payload)) == 0);
    frame[5] ^= 0xFFU;
    EXPECT(st_gateway_frame_decode(frame, frame_length, &decoded_header, &decoded_payload) != 0);
    return 0;
}

static int test_gateway_json_contract(void)
{
    char json[512];
    st_telemetry_record_t record = make_state_record("sht41_temperature", 7U);

    record.reading.boot_id = 42U;
    record.reading.uptime_ms = 1234U;
    record.reading.value = 21.5F;
    EXPECT(st_gateway_telemetry_to_json(&record, json, sizeof(json)) == 0);
    EXPECT(strcmp(json,
                  "{\"schema_version\":1,\"pod_id\":\"POD_1\",\"sensor_id\":\"sht41_temperature\","
                  "\"sensor_kind\":\"temperature_c\",\"record_class\":\"state\",\"sequence\":7,"
                  "\"boot_id\":42,\"uptime_ms\":1234,\"value\":21.500,\"unit\":\"celsius\","
                  "\"quality_flags\":1}") == 0);
    return 0;
}

static int test_pod_reporting_policy(void)
{
    st_pod_runtime_t runtime;
    st_fake_sensor_t sensor;
    st_telemetry_record_t record;
    st_reporting_rule_t rule = {
        .deadband = 0.5F,
        .minimum_interval_ms = 3000U,
        .maximum_interval_ms = 5000U,
    };

    st_pod_runtime_init(&runtime, ST_POD_ENVIRONMENT, "POD_1", 42U);
    EXPECT(st_reporting_policy_set_rule(&runtime.reporting, ST_SENSOR_TEMPERATURE_C, rule) == 0);
    st_fake_sensor_init(&sensor, "env_temperature", "module-temp-001",
                        ST_SENSOR_TEMPERATURE_C, ST_UNIT_CELSIUS, 1000U, 20.0F);
    sensor.increment_per_sample = 0.1F;
    EXPECT(st_sensor_registry_attach(&runtime.registry, 0U, st_fake_sensor_driver(&sensor)) == 0);

    st_pod_runtime_tick(&runtime, 0U);
    st_pod_runtime_tick(&runtime, 1U);
    EXPECT(st_pod_runtime_next_telemetry(&runtime, &record) == 0);
    EXPECT(record.reading.value == 20.0F);

    st_pod_runtime_tick(&runtime, 1001U);
    st_pod_runtime_tick(&runtime, 2001U);
    st_pod_runtime_tick(&runtime, 3001U);
    st_pod_runtime_tick(&runtime, 4001U);
    EXPECT(st_telemetry_queue_count(&runtime.outbound) == 0U);

    st_pod_runtime_tick(&runtime, 5001U);
    EXPECT(st_pod_runtime_next_telemetry(&runtime, &record) == 0);
    EXPECT(record.reading.uptime_ms == 5001U);

    sensor.value = 25.0F;
    st_pod_runtime_tick(&runtime, 6001U);
    st_pod_runtime_tick(&runtime, 7001U);
    EXPECT(st_telemetry_queue_count(&runtime.outbound) == 0U);
    st_pod_runtime_tick(&runtime, 8001U);
    EXPECT(st_pod_runtime_next_telemetry(&runtime, &record) == 0);
    EXPECT(record.reading.value >= 25.0F);

    sensor.warm_until_ms = 10000U;
    st_pod_runtime_tick(&runtime, 9001U);
    EXPECT(st_pod_runtime_next_telemetry(&runtime, &record) == 0);
    EXPECT((record.reading.quality_flags & ST_QUALITY_WARMING_UP) != 0U);
    EXPECT(runtime.reporting.readings_seen == 10U);
    EXPECT(runtime.reporting.readings_reported == 4U);
    EXPECT(runtime.reporting.readings_suppressed == 6U);
    return 0;
}

static int test_gateway_processing_policy(void)
{
    st_gateway_processor_t processor;
    st_telemetry_record_t record = make_state_record("env_temperature", 10U);

    record.reading.boot_id = 42U;
    st_gateway_processor_init(&processor);
    EXPECT(st_gateway_processor_ingest(&processor, &record) == ST_GATEWAY_RECORD_ACCEPTED);
    EXPECT(st_gateway_processor_ingest(&processor, &record) == ST_GATEWAY_RECORD_DUPLICATE);

    record.reading.sequence = 9U;
    EXPECT(st_gateway_processor_ingest(&processor, &record) == ST_GATEWAY_RECORD_STALE);
    record.reading.sequence = 12U;
    EXPECT(st_gateway_processor_ingest(&processor, &record) == ST_GATEWAY_RECORD_ACCEPTED);

    record.reading.boot_id = 43U;
    record.reading.sequence = 1U;
    EXPECT(st_gateway_processor_ingest(&processor, &record) == ST_GATEWAY_RECORD_ACCEPTED);
    record.reading.boot_id = 42U;
    record.reading.sequence = 13U;
    EXPECT(st_gateway_processor_ingest(&processor, &record) == ST_GATEWAY_RECORD_STALE);
    record.reading.boot_id = 43U;
    record.reading.sensor_id[0] = '\0';
    EXPECT(st_gateway_processor_ingest(&processor, &record) == ST_GATEWAY_RECORD_INVALID);

    EXPECT(processor.accepted == 3U);
    EXPECT(processor.duplicates == 1U);
    EXPECT(processor.stale == 2U);
    EXPECT(processor.invalid == 1U);
    return 0;
}

static int test_zigbee_payload_round_trip(void)
{
    uint8_t payload[ST_ZIGBEE_TELEMETRY_PAYLOAD_SIZE];
    size_t payload_length = 0U;
    uint8_t sensor_slot = 0U;
    st_telemetry_record_t input = make_state_record("env_temperature", 0x12345678U);
    st_telemetry_record_t output;

    input.reading.boot_id = 0xA1B2C3D4U;
    input.reading.uptime_ms = 0x0102030405060708ULL;
    input.reading.value = -12.75F;
    input.reading.quality_flags = ST_QUALITY_VALID | ST_QUALITY_BATTERY_LOW;

    EXPECT(st_zigbee_telemetry_encode(&input, 3U, payload, sizeof(payload),
                                      &payload_length) == 0);
    EXPECT(payload_length == ST_ZIGBEE_TELEMETRY_PAYLOAD_SIZE);
    EXPECT(st_zigbee_telemetry_decode(payload, payload_length, "POD_1", "env_temperature",
                                      &output, &sensor_slot) == 0);
    EXPECT(sensor_slot == 3U);
    EXPECT(output.record_class == input.record_class);
    EXPECT(output.priority == input.priority);
    EXPECT(output.reading.sensor_kind == input.reading.sensor_kind);
    EXPECT(output.reading.unit == input.reading.unit);
    EXPECT(output.reading.sequence == input.reading.sequence);
    EXPECT(output.reading.boot_id == input.reading.boot_id);
    EXPECT(output.reading.uptime_ms == input.reading.uptime_ms);
    EXPECT(output.reading.value == input.reading.value);
    EXPECT(output.reading.quality_flags == input.reading.quality_flags);
    EXPECT(strcmp(output.reading.pod_id, "POD_1") == 0);
    EXPECT(strcmp(output.reading.sensor_id, "env_temperature") == 0);

    EXPECT(st_zigbee_telemetry_decode(payload, payload_length - 1U, "POD_1",
                                      "env_temperature", &output, &sensor_slot) != 0);
    payload[0] = ST_ZIGBEE_PAYLOAD_VERSION + 1U;
    EXPECT(st_zigbee_telemetry_decode(payload, payload_length, "POD_1", "env_temperature",
                                      &output, &sensor_slot) != 0);
    return 0;
}

static int test_gateway_runtime_pipeline(void)
{
    st_gateway_runtime_t runtime;
    st_telemetry_record_t state = make_state_record("env_temperature", 1U);
    st_telemetry_record_t event = make_event_record("door_contact", 1U, ST_SENSOR_CONTACT);
    st_telemetry_record_t output;
    uint8_t payload[ST_ZIGBEE_TELEMETRY_PAYLOAD_SIZE];
    size_t payload_length = 0U;
    char json[512];
    size_t index;

    state.reading.boot_id = 42U;
    state.reading.uptime_ms = 1000U;
    state.reading.value = 20.0F;
    event.reading.boot_id = 42U;
    event.reading.uptime_ms = 1100U;
    event.reading.value = 1.0F;

    st_gateway_runtime_init(&runtime);
    EXPECT(st_zigbee_telemetry_encode(&state, 0U, payload, sizeof(payload),
                                      &payload_length) == 0);
    EXPECT(st_gateway_runtime_ingest_zigbee(&runtime, payload, payload_length, "POD_1",
                                            "env_temperature") == ST_GATEWAY_INGRESS_ACCEPTED);
    EXPECT(st_gateway_runtime_ingest_zigbee(&runtime, payload, payload_length, "POD_1",
                                            "env_temperature") == ST_GATEWAY_INGRESS_DUPLICATE);

    state.reading.sequence = 2U;
    state.reading.uptime_ms = 2000U;
    state.reading.value = 21.0F;
    EXPECT(st_zigbee_telemetry_encode(&state, 0U, payload, sizeof(payload),
                                      &payload_length) == 0);
    EXPECT(st_gateway_runtime_ingest_zigbee(&runtime, payload, payload_length, "POD_1",
                                            "env_temperature") == ST_GATEWAY_INGRESS_ACCEPTED);
    EXPECT(st_gateway_runtime_pending(&runtime) == 1U);

    state.reading.sequence = 1U;
    EXPECT(st_zigbee_telemetry_encode(&state, 0U, payload, sizeof(payload),
                                      &payload_length) == 0);
    EXPECT(st_gateway_runtime_ingest_zigbee(&runtime, payload, payload_length, "POD_1",
                                            "env_temperature") == ST_GATEWAY_INGRESS_STALE);

    EXPECT(st_zigbee_telemetry_encode(&event, 1U, payload, sizeof(payload),
                                      &payload_length) == 0);
    EXPECT(st_gateway_runtime_ingest_zigbee(&runtime, payload, payload_length, "POD_2",
                                            "door_contact") == ST_GATEWAY_INGRESS_ACCEPTED);
    EXPECT(st_gateway_runtime_pending(&runtime) == 2U);
    EXPECT(st_gateway_runtime_next_json(&runtime, json, sizeof(json)) == 0);
    EXPECT(strstr(json, "\"record_class\":\"event\"") != NULL);
    EXPECT(st_gateway_runtime_next_json(&runtime, json, 8U) != 0);
    EXPECT(st_gateway_runtime_pending(&runtime) == 1U);
    EXPECT(st_gateway_runtime_next_record(&runtime, &output) == 0);
    EXPECT(output.reading.sequence == 2U);
    EXPECT(output.reading.value == 21.0F);

    payload[0] = ST_ZIGBEE_PAYLOAD_VERSION + 1U;
    EXPECT(st_gateway_runtime_ingest_zigbee(&runtime, payload, payload_length, "POD_2",
                                            "door_contact") == ST_GATEWAY_INGRESS_INVALID);

    st_gateway_runtime_init(&runtime);
    event.reading.boot_id = 99U;
    for (index = 0U; index < ST_GATEWAY_DELIVERY_CAPACITY; ++index) {
        event.reading.sequence = (uint32_t)index + 1U;
        event.reading.uptime_ms = index;
        EXPECT(st_zigbee_telemetry_encode(&event, 1U, payload, sizeof(payload),
                                          &payload_length) == 0);
        EXPECT(st_gateway_runtime_ingest_zigbee(&runtime, payload, payload_length, "POD_2",
                                                "door_contact") == ST_GATEWAY_INGRESS_ACCEPTED);
    }
    event.reading.sequence = ST_GATEWAY_DELIVERY_CAPACITY + 1U;
    EXPECT(st_zigbee_telemetry_encode(&event, 1U, payload, sizeof(payload),
                                      &payload_length) == 0);
    EXPECT(st_gateway_runtime_ingest_zigbee(&runtime, payload, payload_length, "POD_2",
                                            "door_contact") == ST_GATEWAY_INGRESS_DROPPED);
    EXPECT(st_gateway_runtime_pending(&runtime) == ST_GATEWAY_DELIVERY_CAPACITY);
    EXPECT(runtime.delivery_drops == 1U);
    return 0;
}

static int test_gateway_registry_and_source_ingress(void)
{
    st_gateway_runtime_t runtime;
    st_telemetry_record_t state = make_state_record("env_temperature", 1U);
    st_telemetry_record_t output;
    uint8_t payload[ST_ZIGBEE_TELEMETRY_PAYLOAD_SIZE];
    size_t payload_length = 0U;
    const uint64_t ieee_address = 0x00124B0001ABCDEFULL;

    state.reading.boot_id = 42U;
    state.reading.uptime_ms = 1000U;
    state.reading.value = 20.0F;
    st_gateway_runtime_init(&runtime);

    EXPECT(st_gateway_registry_register_node(&runtime.registry, 0x1234U, ieee_address,
                                             "POD_1") == 0);
    EXPECT(st_gateway_registry_bind_sensor(&runtime.registry, 0x1234U, 0U,
                                           "env_temperature") == 0);
    EXPECT(st_zigbee_telemetry_encode(&state, 0U, payload, sizeof(payload),
                                      &payload_length) == 0);
    EXPECT(st_gateway_runtime_ingest_zigbee_source(&runtime, 0x1234U, payload,
                                                   payload_length) ==
           ST_GATEWAY_INGRESS_ACCEPTED);
    EXPECT(st_gateway_runtime_ingest_zigbee_source(&runtime, 0x1234U, payload,
                                                   payload_length) ==
           ST_GATEWAY_INGRESS_DUPLICATE);

    EXPECT(st_gateway_registry_register_node(&runtime.registry, 0x2345U, ieee_address,
                                             "POD_1") == 0);
    EXPECT(st_gateway_runtime_ingest_zigbee_source(&runtime, 0x1234U, payload,
                                                   payload_length) ==
           ST_GATEWAY_INGRESS_INVALID);
    state.reading.sequence = 2U;
    state.reading.uptime_ms = 2000U;
    EXPECT(st_zigbee_telemetry_encode(&state, 0U, payload, sizeof(payload),
                                      &payload_length) == 0);
    EXPECT(st_gateway_runtime_ingest_zigbee_source(&runtime, 0x2345U, payload,
                                                   payload_length) ==
           ST_GATEWAY_INGRESS_ACCEPTED);
    EXPECT(st_gateway_registry_register_node(&runtime.registry, 0x2345U,
                                             ieee_address + 1U, "ENV_02") != 0);
    EXPECT(st_gateway_runtime_next_record(&runtime, &output) == 0);
    EXPECT(output.reading.sequence == 2U);
    EXPECT(strcmp(output.reading.pod_id, "POD_1") == 0);
    EXPECT(strcmp(output.reading.sensor_id, "env_temperature") == 0);
    return 0;
}

static int test_stress_runtime_and_gateway(void)
{
    st_pod_runtime_t runtime;
    st_fake_sensor_t temperature_sensor;
    st_fake_sensor_t humidity_sensor;
    st_fake_sensor_t co2_sensor;
    st_telemetry_record_t record;
    sequence_tracker_slot_t trackers[16] = {0};
    uint32_t rng = 0xC0FFEE11U;
    uint64_t now_ms = 0U;
    size_t drained_records = 0U;
    size_t saw_event_records = 0U;
    size_t saw_warming_records = 0U;
    size_t iteration;
    st_telemetry_queue_t pressure_queue;
    size_t saw_pressure_events = 0U;
    size_t pressure_push_successes = 0U;
    size_t pressure_push_drops = 0U;
    const char *pressure_sensor_ids[] = {
        "pressure_temp",
        "pressure_humidity",
        "pressure_co2",
        "pressure_motion",
    };

    printf("Running stress harness...\n");

    st_pod_runtime_init(&runtime, ST_POD_ENVIRONMENT, "POD_1", 99U);

    st_fake_sensor_init(&temperature_sensor, "env_temperature", "module-temp-001",
                        ST_SENSOR_TEMPERATURE_C, ST_UNIT_CELSIUS, 250U, 20.0F);
    temperature_sensor.increment_per_sample = 0.05F;
    temperature_sensor.warm_until_ms = 120U;

    st_fake_sensor_init(&humidity_sensor, "env_humidity", "module-rh-001",
                        ST_SENSOR_RELATIVE_HUMIDITY_PERCENT, ST_UNIT_PERCENT, 500U, 45.0F);
    humidity_sensor.increment_per_sample = 0.10F;
    humidity_sensor.warm_until_ms = 180U;

    st_fake_sensor_init(&co2_sensor, "env_co2", "module-co2-001", ST_SENSOR_CO2_PPM, ST_UNIT_PPM,
                        1000U, 600.0F);
    co2_sensor.increment_per_sample = 1.0F;
    co2_sensor.warm_until_ms = 250U;

    EXPECT(st_sensor_registry_attach(&runtime.registry, 0U, st_fake_sensor_driver(&temperature_sensor)) == 0);
    EXPECT(st_sensor_registry_attach(&runtime.registry, 1U, st_fake_sensor_driver(&humidity_sensor)) == 0);
    EXPECT(st_sensor_registry_attach(&runtime.registry, 2U, st_fake_sensor_driver(&co2_sensor)) == 0);

    for (iteration = 0U; iteration < 5000U; ++iteration) {
        size_t drain_budget = 3U;
        st_telemetry_record_t record;

        now_ms += 1U + random_range(&rng, 20U);

        if (random_range(&rng, 48U) == 0U) {
            temperature_sensor.present = !temperature_sensor.present;
            if (temperature_sensor.present) {
                temperature_sensor.warm_until_ms = now_ms + 100U + random_range(&rng, 400U);
            }
        }
        if (random_range(&rng, 60U) == 0U) {
            humidity_sensor.present = !humidity_sensor.present;
            if (humidity_sensor.present) {
                humidity_sensor.warm_until_ms = now_ms + 150U + random_range(&rng, 500U);
            }
        }
        if (random_range(&rng, 72U) == 0U) {
            co2_sensor.present = !co2_sensor.present;
            if (co2_sensor.present) {
                co2_sensor.warm_until_ms = now_ms + 200U + random_range(&rng, 600U);
            }
        }

        if (temperature_sensor.present && random_range(&rng, 16U) == 0U) {
            temperature_sensor.fail_next_sample = true;
        }
        if (humidity_sensor.present && random_range(&rng, 20U) == 0U) {
            humidity_sensor.fail_next_sample = true;
        }
        if (co2_sensor.present && random_range(&rng, 24U) == 0U) {
            co2_sensor.fail_next_sample = true;
        }

        if (random_range(&rng, 14U) == 0U) {
            st_sensor_kind_t event_kind = random_range(&rng, 2U) == 0U ? ST_SENSOR_MOTION
                                                                       : ST_SENSOR_CONTACT;
            const char *event_sensor_id = event_kind == ST_SENSOR_MOTION ? "pir_motion"
                                                                          : "door_contact";
            EXPECT(st_pod_runtime_emit_event(&runtime, event_sensor_id, event_kind, now_ms,
                                             1.0F) == 0);
        }

        st_pod_runtime_tick(&runtime, now_ms);

        while (drain_budget > 0U && st_pod_runtime_next_telemetry(&runtime, &record) == 0) {
            EXPECT(record.reading.pod_id[0] != '\0');
            EXPECT(record.reading.sensor_id[0] != '\0');
            EXPECT((record.reading.quality_flags & ST_QUALITY_VALID) != 0U);
            EXPECT(track_sequence(trackers, 16U, &record) == 0);
            EXPECT(validate_serialised_record(&record, 0x1234U) == 0);
            drained_records++;
            if (record.record_class == ST_RECORD_EVENT) {
                saw_event_records++;
            }
            if ((record.reading.quality_flags & ST_QUALITY_WARMING_UP) != 0U) {
                saw_warming_records++;
            }
            --drain_budget;
        }
    }

    while (st_pod_runtime_next_telemetry(&runtime, &record) == 0) {
        EXPECT(record.reading.pod_id[0] != '\0');
        EXPECT(record.reading.sensor_id[0] != '\0');
        EXPECT((record.reading.quality_flags & ST_QUALITY_VALID) != 0U);
        EXPECT(track_sequence(trackers, 16U, &record) == 0);
        EXPECT(validate_serialised_record(&record, 0x1234U) == 0);
        drained_records++;
        if (record.record_class == ST_RECORD_EVENT) {
            saw_event_records++;
        }
        if ((record.reading.quality_flags & ST_QUALITY_WARMING_UP) != 0U) {
            saw_warming_records++;
        }
    }

    EXPECT(drained_records > 200U);
    EXPECT(saw_event_records > 0U);
    EXPECT(saw_warming_records > 0U);

    st_telemetry_queue_init(&pressure_queue);
    for (iteration = 0U; iteration < 5000U; ++iteration) {
        st_telemetry_record_t pressure_record;
        uint32_t sequence = (uint32_t)iteration + 1U;
        size_t sensor_index = iteration % 4U;

        if (iteration % 7U == 0U) {
            pressure_record = make_event_record(pressure_sensor_ids[sensor_index], sequence,
                                                sensor_index == 3U ? ST_SENSOR_MOTION
                                                                  : ST_SENSOR_CONTACT);
            saw_pressure_events++;
        } else {
            pressure_record = make_state_record(pressure_sensor_ids[sensor_index], sequence);
            pressure_record.reading.sensor_kind = sensor_index == 0U ? ST_SENSOR_TEMPERATURE_C
                                                                     : sensor_index == 1U
                                                                           ? ST_SENSOR_RELATIVE_HUMIDITY_PERCENT
                                                                           : ST_SENSOR_CO2_PPM;
            pressure_record.reading.unit = sensor_index == 0U ? ST_UNIT_CELSIUS
                                                              : sensor_index == 1U ? ST_UNIT_PERCENT
                                                                                   : ST_UNIT_PPM;
        }

        if (st_telemetry_queue_push(&pressure_queue, &pressure_record) == 0) {
            pressure_push_successes++;
        } else {
            pressure_push_drops++;
        }
        EXPECT(st_telemetry_queue_count(&pressure_queue) <= ST_TELEMETRY_QUEUE_CAPACITY);

        if (iteration % 37U == 0U) {
            EXPECT(st_telemetry_queue_pop(&pressure_queue, &record) == 0);
            EXPECT(validate_serialised_record(&record, 0x4321U) == 0);
        }
    }

    while (st_telemetry_queue_pop(&pressure_queue, &record) == 0) {
        EXPECT(validate_serialised_record(&record, 0x4321U) == 0);
    }

    EXPECT(saw_pressure_events > 0U);
    EXPECT(pressure_push_successes > 0U);
    EXPECT(pressure_push_drops > 0U);
    printf("Stress harness processed %zu telemetry records.\n", drained_records);
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_registry_warmup_and_hot_swap();
    failures += test_delivery_policy();
    failures += test_gateway_frame_round_trip();
    failures += test_gateway_json_contract();
    failures += test_pod_reporting_policy();
    failures += test_gateway_processing_policy();
    failures += test_zigbee_payload_round_trip();
    failures += test_gateway_runtime_pipeline();
    failures += test_gateway_registry_and_source_ingress();
    failures += st_run_sensor_foundation_tests();
    failures += st_run_bh1750_tests();
    failures += st_run_reed_tests();
    failures += st_run_ina219_tests();
    failures += st_run_pir_tests();
    failures += st_run_adxl345_tests();
    failures += st_run_ds18b20_tests();
    failures += st_run_scd41_tests();
    failures += st_run_sgp40_tests();
    failures += st_run_command_actuation_tests();
    failures += st_run_command_transport_tests();
    failures += st_run_gateway_identity_tests();
    failures += st_run_alarm_control_tests();
    failures += st_run_gateway_state_tests();
    failures += st_run_final_pcb_board_tests();
    failures += st_run_board_port_manager_tests();
    failures += st_run_hotswap_module_binding_tests();
    failures += st_run_hotswap_scan_scheduler_tests();
    failures += st_run_hotswap_zigbee_slot_tests();
    failures += st_run_hotswap_integration_tests();
    failures += st_run_shared_alarm_indicator_tests();
    failures += test_stress_runtime_and_gateway();

    if (failures != 0) {
        fprintf(stderr, "%d SiteTwin host test(s) failed.\n", failures);
        return 1;
    }

    printf("All SiteTwin host tests passed.\n");
    return 0;
}
