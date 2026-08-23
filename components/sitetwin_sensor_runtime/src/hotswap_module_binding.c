#include "sitetwin/hotswap_module_binding.h"

#include <string.h>

/* Cadence defaults for hot-swap ports. These are independent of the fixed
 * pod profiles' per-type Kconfig options (a hot-swap port isn't tied to
 * one compile-time type), and are deliberately conservative placeholders
 * -- tune later against real acquisition timing if profiling calls for
 * it. */
#define ST_HOTSWAP_SAMPLE_INTERVAL_MS 1000U
#define ST_HOTSWAP_CACHE_VALIDITY_MS 5000U
#define ST_HOTSWAP_SCD41_POLL_INTERVAL_MS 5000U
#define ST_HOTSWAP_SGP40_ALGORITHM_INTERVAL_MS 1000U
#define ST_HOTSWAP_SGP40_COMPENSATION_MAX_AGE_MS 5000U
#define ST_HOTSWAP_ADXL345_MINIMUM_WINDOW_SAMPLES 32U
/* Matches the driver's own documented ceiling: max current = 0.320 V /
 * shunt_ohms (see ina219.h). 0.1 ohm is the common breakout shunt value. */
#define ST_HOTSWAP_INA219_SHUNT_OHMS 0.1F
#define ST_HOTSWAP_INA219_MAX_CURRENT_A 3.2F
#define ST_HOTSWAP_DS18B20_RESOLUTION_BITS 12U

/* Fixed sensor_id strings, matching the existing fixed-profile naming
 * convention exactly (see app_main.c) so a type reports under the same
 * telemetry key regardless of which physical port it is plugged into --
 * this keeps existing TB Rule Chain / dashboard configuration valid. */
static const char *const kSht41TemperatureId = "sht41_temperature";
static const char *const kSht41HumidityId = "sht41_humidity";
static const char *const kScd41CO2Id = "scd41_co2";
static const char *const kSgp40VocId = "sgp40_voc";
static const char *const kBh1750IlluminanceId = "bh1750_illuminance";
static const char *const kIna219VoltageId = "ina219_voltage";
static const char *const kIna219CurrentId = "ina219_current";
static const char *const kAdxl345VibrationId = "adxl345_vibration";
static const char *const kDs18b20TemperatureId = "ds18b20_temperature";
static const char *const kPirMotionId = "pir_motion";
static const char *const kReedContactId = "reed_contact";

/* PIR/REED electrical assumptions -- both raw levels arrive over the same
 * DATA_COMMON/U4 mux as DS18B20's OneWire signal (see the final-PCB
 * contract: "U4 selects one non-I2C DATA line"). These polarity/timing
 * defaults match the fixed-profile SR505 PIR and reed prototype wiring
 * used elsewhere in this codebase; confirm against the final PCB's actual
 * signal conditioning before trusting alarm behaviour on real hardware. */
#define ST_HOTSWAP_PIR_RETRIGGER_SUPPRESSION_MS 2000U
#define ST_HOTSWAP_PIR_ACTIVE_LEVEL 1U
#define ST_HOTSWAP_REED_DEBOUNCE_MS 30U
#define ST_HOTSWAP_REED_OPEN_WHEN_RAW_HIGH 1U

int st_hotswap_module_binding_init(st_hotswap_module_binding_t *binding,
                                   const st_hotswap_binding_io_t *io,
                                   st_sensor_registry_t *registry, size_t port_count)
{
    if (binding == NULL || io == NULL || registry == NULL || port_count == 0U ||
        port_count > ST_HOTSWAP_BINDING_MAX_PORTS) {
        return -1;
    }
    memset(binding, 0, sizeof(*binding));
    binding->io = *io;
    binding->registry = registry;
    binding->port_count = port_count;
    return 0;
}

static void slot_registry_base(size_t port_index, uint8_t out_slots[ST_MODULE_MAX_CHANNELS])
{
    uint8_t base = (uint8_t)(port_index * ST_HOTSWAP_REGISTRY_SLOTS_PER_PORT);
    uint8_t i;

    for (i = 0U; i < ST_MODULE_MAX_CHANNELS; ++i) {
        out_slots[i] = (uint8_t)(base + i);
    }
}

static int attach_sht41(st_hotswap_port_slot_t *slot, const st_hotswap_binding_io_t *io,
                        st_sensor_registry_t *registry, size_t port_index)
{
    st_sht41_config_t config;

    memset(&config, 0, sizeof(config));
    config.bus = io->i2c_bus;
    config.address = ST_SHT41_DEFAULT_ADDRESS;
    config.sample_interval_ms = ST_HOTSWAP_SAMPLE_INTERVAL_MS;
    config.cache_validity_ms = ST_HOTSWAP_CACHE_VALIDITY_MS;
    config.temperature_sensor_id = kSht41TemperatureId;
    config.humidity_sensor_id = kSht41HumidityId;

    if (st_sht41_init(&slot->driver_storage.sht41, &config) != 0 ||
        st_module_instance_init(&slot->module_instance,
                                st_sht41_module_driver(&slot->driver_storage.sht41),
                                ST_SHT41_CHANNEL_COUNT) != 0 ||
        st_module_instance_attach(&slot->module_instance, registry, slot->registry_slots,
                                  ST_SHT41_CHANNEL_COUNT) != 0) {
        return -1;
    }
    (void)port_index;
    return 0;
}

static int attach_scd41(st_hotswap_port_slot_t *slot, const st_hotswap_binding_io_t *io,
                        st_sensor_registry_t *registry, size_t port_index)
{
    st_scd41_config_t config;

    memset(&config, 0, sizeof(config));
    config.bus = io->i2c_bus;
    config.address = ST_SCD41_DEFAULT_ADDRESS;
    config.measurement_mode = ST_SCD41_MODE_PERIODIC;
    config.poll_interval_ms = ST_HOTSWAP_SCD41_POLL_INTERVAL_MS;
    config.co2_sensor_id = kScd41CO2Id;

    if (st_scd41_init(&slot->driver_storage.scd41, &config) != 0 ||
        st_module_instance_init(&slot->module_instance,
                                st_scd41_module_driver(&slot->driver_storage.scd41),
                                ST_SCD41_CHANNEL_COUNT) != 0 ||
        st_module_instance_attach(&slot->module_instance, registry, slot->registry_slots,
                                  ST_SCD41_CHANNEL_COUNT) != 0) {
        return -1;
    }
    (void)port_index;
    return 0;
}

static int attach_sgp40(st_hotswap_port_slot_t *slot, const st_hotswap_binding_io_t *io,
                        st_sensor_registry_t *registry, size_t port_index)
{
    st_sgp40_config_t config;

    memset(&config, 0, sizeof(config));
    config.bus = io->i2c_bus;
    config.address = ST_SGP40_DEFAULT_ADDRESS;
    config.algorithm_interval_ms = ST_HOTSWAP_SGP40_ALGORITHM_INTERVAL_MS;
    config.compensation_maximum_age_ms = ST_HOTSWAP_SGP40_COMPENSATION_MAX_AGE_MS;
    /* No cross-port humidity/temperature compensation source is wired up
     * yet: a hot-swap SGP40 does not know whether some other port
     * happens to hold a live SHT41 right now. Runs uncompensated (still
     * a valid, if less accurate, VOC index) until that is added. */
    config.compensation_provider = NULL;
    config.compensation_context = NULL;
    config.voc_index_sensor_id = kSgp40VocId;

    if (st_sgp40_init(&slot->driver_storage.sgp40, &config) != 0 ||
        st_module_instance_init(&slot->module_instance,
                                st_sgp40_module_driver(&slot->driver_storage.sgp40),
                                ST_SGP40_CHANNEL_COUNT) != 0 ||
        st_module_instance_attach(&slot->module_instance, registry, slot->registry_slots,
                                  ST_SGP40_CHANNEL_COUNT) != 0) {
        return -1;
    }
    (void)port_index;
    return 0;
}

static int attach_bh1750(st_hotswap_port_slot_t *slot, const st_hotswap_binding_io_t *io,
                         st_sensor_registry_t *registry, size_t port_index)
{
    st_bh1750_config_t config;

    memset(&config, 0, sizeof(config));
    config.bus = io->i2c_bus;
    config.address = ST_BH1750_DEFAULT_ADDRESS;
    config.sample_interval_ms = ST_HOTSWAP_SAMPLE_INTERVAL_MS;
    config.cache_validity_ms = ST_HOTSWAP_CACHE_VALIDITY_MS;
    config.illuminance_sensor_id = kBh1750IlluminanceId;

    if (st_bh1750_init(&slot->driver_storage.bh1750, &config) != 0 ||
        st_module_instance_init(&slot->module_instance,
                                st_bh1750_module_driver(&slot->driver_storage.bh1750),
                                ST_BH1750_CHANNEL_COUNT) != 0 ||
        st_module_instance_attach(&slot->module_instance, registry, slot->registry_slots,
                                  ST_BH1750_CHANNEL_COUNT) != 0) {
        return -1;
    }
    (void)port_index;
    return 0;
}

static int attach_ina219(st_hotswap_port_slot_t *slot, const st_hotswap_binding_io_t *io,
                         st_sensor_registry_t *registry, size_t port_index)
{
    st_ina219_config_t config;

    memset(&config, 0, sizeof(config));
    config.bus = io->i2c_bus;
    config.address = ST_INA219_DEFAULT_ADDRESS;
    config.shunt_resistance_ohms = ST_HOTSWAP_INA219_SHUNT_OHMS;
    config.max_expected_current_a = ST_HOTSWAP_INA219_MAX_CURRENT_A;
    config.sample_interval_ms = ST_HOTSWAP_SAMPLE_INTERVAL_MS;
    config.cache_validity_ms = ST_HOTSWAP_CACHE_VALIDITY_MS;
    config.bus_voltage_sensor_id = kIna219VoltageId;
    config.current_sensor_id = kIna219CurrentId;

    if (st_ina219_init(&slot->driver_storage.ina219, &config) != 0 ||
        st_module_instance_init(&slot->module_instance,
                                st_ina219_module_driver(&slot->driver_storage.ina219),
                                ST_INA219_CHANNEL_COUNT) != 0 ||
        st_module_instance_attach(&slot->module_instance, registry, slot->registry_slots,
                                  ST_INA219_CHANNEL_COUNT) != 0) {
        return -1;
    }
    (void)port_index;
    return 0;
}

static int attach_adxl345(st_hotswap_port_slot_t *slot, const st_hotswap_binding_io_t *io,
                          st_sensor_registry_t *registry, size_t port_index)
{
    st_adxl345_config_t config;

    memset(&config, 0, sizeof(config));
    config.bus = io->i2c_bus;
    config.address = ST_ADXL345_DEFAULT_ADDRESS;
    config.range_g = 16U;
    config.rate_code = 0x0AU;
    config.minimum_window_samples = ST_HOTSWAP_ADXL345_MINIMUM_WINDOW_SAMPLES;
    config.sample_interval_ms = ST_HOTSWAP_SAMPLE_INTERVAL_MS;
    config.g_per_lsb = ST_ADXL345_DEFAULT_G_PER_LSB;
    config.vibration_sensor_id = kAdxl345VibrationId;

    if (st_adxl345_init(&slot->driver_storage.adxl345, &config) != 0 ||
        st_module_instance_init(&slot->module_instance,
                                st_adxl345_module_driver(&slot->driver_storage.adxl345),
                                ST_ADXL345_CHANNEL_COUNT) != 0 ||
        st_module_instance_attach(&slot->module_instance, registry, slot->registry_slots,
                                  ST_ADXL345_CHANNEL_COUNT) != 0) {
        return -1;
    }
    (void)port_index;
    return 0;
}

static int attach_ds18b20(st_hotswap_port_slot_t *slot, const st_hotswap_binding_io_t *io,
                          st_sensor_registry_t *registry, size_t port_index)
{
    st_ds18b20_config_t config;

    if (io->data_common_bus_for_port == NULL) {
        return -1;
    }
    memset(&config, 0, sizeof(config));
    config.bus = io->data_common_bus_for_port(io->context, port_index);
    config.resolution_bits = ST_HOTSWAP_DS18B20_RESOLUTION_BITS;
    config.sample_interval_ms = ST_HOTSWAP_SAMPLE_INTERVAL_MS;
    config.cache_validity_ms = ST_HOTSWAP_CACHE_VALIDITY_MS;
    config.temperature_sensor_id = kDs18b20TemperatureId;

    if (st_ds18b20_init(&slot->driver_storage.ds18b20, &config) != 0 ||
        st_module_instance_init(&slot->module_instance,
                                st_ds18b20_module_driver(&slot->driver_storage.ds18b20),
                                ST_DS18B20_CHANNEL_COUNT) != 0 ||
        st_module_instance_attach(&slot->module_instance, registry, slot->registry_slots,
                                  ST_DS18B20_CHANNEL_COUNT) != 0) {
        return -1;
    }
    return 0;
}

int st_hotswap_module_binding_attach(void *context, size_t port_index,
                                     st_module_type_t module_type, uint64_t now_ms)
{
    st_hotswap_module_binding_t *binding = (st_hotswap_module_binding_t *)context;
    st_hotswap_port_slot_t *slot;
    int result;

    if (binding == NULL || port_index >= binding->port_count) {
        return -1;
    }
    slot = &binding->slots[port_index];
    memset(slot, 0, sizeof(*slot));
    slot_registry_base(port_index, slot->registry_slots);

    switch (module_type) {
    case ST_MODULE_TYPE_PIR: {
        st_pir_config_t pir_config;

        memset(&pir_config, 0, sizeof(pir_config));
        pir_config.stabilization_ms = ST_PIR_DEFAULT_STABILIZATION_MS;
        pir_config.retrigger_suppression_ms = ST_HOTSWAP_PIR_RETRIGGER_SUPPRESSION_MS;
        pir_config.active_level = ST_HOTSWAP_PIR_ACTIVE_LEVEL;
        pir_config.sensor_id = kPirMotionId;
        if (st_pir_init(&slot->pir, &pir_config, now_ms) != 0) {
            return -1;
        }
        slot->kind = ST_HOTSWAP_SLOT_PIR;
        slot->module_type = module_type;
        return 0;
    }
    case ST_MODULE_TYPE_REED: {
        st_reed_config_t reed_config;

        memset(&reed_config, 0, sizeof(reed_config));
        reed_config.debounce_ms = ST_HOTSWAP_REED_DEBOUNCE_MS;
        reed_config.open_when_raw_high = ST_HOTSWAP_REED_OPEN_WHEN_RAW_HIGH;
        if (st_reed_debounce_init(&slot->reed, &reed_config) != 0) {
            return -1;
        }
        slot->kind = ST_HOTSWAP_SLOT_REED;
        slot->module_type = module_type;
        return 0;
    }
    case ST_MODULE_TYPE_SHT41:
        result = attach_sht41(slot, &binding->io, binding->registry, port_index);
        break;
    case ST_MODULE_TYPE_SCD41:
        result = attach_scd41(slot, &binding->io, binding->registry, port_index);
        break;
    case ST_MODULE_TYPE_SGP40:
        result = attach_sgp40(slot, &binding->io, binding->registry, port_index);
        break;
    case ST_MODULE_TYPE_BH1750:
        result = attach_bh1750(slot, &binding->io, binding->registry, port_index);
        break;
    case ST_MODULE_TYPE_INA219:
        result = attach_ina219(slot, &binding->io, binding->registry, port_index);
        break;
    case ST_MODULE_TYPE_ADXL345:
        result = attach_adxl345(slot, &binding->io, binding->registry, port_index);
        break;
    case ST_MODULE_TYPE_DS18B20:
        result = attach_ds18b20(slot, &binding->io, binding->registry, port_index);
        break;
    case ST_MODULE_TYPE_UNKNOWN:
    case ST_MODULE_TYPE_EMPTY:
    default:
        return -1; /* the port manager should never ask us to attach these */
    }

    if (result != 0) {
        memset(slot, 0, sizeof(*slot));
        return -1;
    }
    slot->kind = ST_HOTSWAP_SLOT_REGISTRY_DRIVER;
    slot->module_type = module_type;
    return 0;
}

void st_hotswap_module_binding_detach(void *context, size_t port_index, uint64_t now_ms)
{
    st_hotswap_module_binding_t *binding = (st_hotswap_module_binding_t *)context;
    st_hotswap_port_slot_t *slot;

    if (binding == NULL || port_index >= binding->port_count) {
        return;
    }
    slot = &binding->slots[port_index];
    if (slot->kind == ST_HOTSWAP_SLOT_REGISTRY_DRIVER) {
        st_module_instance_detach(&slot->module_instance, binding->registry, now_ms);
    }
    memset(slot, 0, sizeof(*slot));
}

bool st_hotswap_module_binding_uses_bus_probe(void *context, st_module_type_t module_type,
                                              uint8_t *expected_address)
{
    (void)context;
    if (expected_address == NULL) {
        return false;
    }
    switch (module_type) {
    case ST_MODULE_TYPE_SHT41:
        *expected_address = ST_SHT41_DEFAULT_ADDRESS;
        return true;
    case ST_MODULE_TYPE_SCD41:
        *expected_address = ST_SCD41_DEFAULT_ADDRESS;
        return true;
    case ST_MODULE_TYPE_SGP40:
        *expected_address = ST_SGP40_DEFAULT_ADDRESS;
        return true;
    case ST_MODULE_TYPE_BH1750:
        *expected_address = ST_BH1750_DEFAULT_ADDRESS;
        return true;
    case ST_MODULE_TYPE_ADXL345:
        *expected_address = ST_ADXL345_DEFAULT_ADDRESS;
        return true;
    case ST_MODULE_TYPE_INA219:
        *expected_address = ST_INA219_DEFAULT_ADDRESS;
        return true;
    default:
        return false;
    }
}

st_hal_result_t st_hotswap_module_binding_bus_probe(void *context, size_t port_index,
                                                     uint8_t expected_address)
{
    st_hotswap_module_binding_t *binding = (st_hotswap_module_binding_t *)context;

    (void)port_index;
    if (binding == NULL || binding->io.i2c_bus.write == NULL) {
        return ST_HAL_IO_ERROR;
    }
    /* Zero-length write is the standard I2C "is anything ACKing this
     * address" probe -- it does not require knowing the device's
     * register protocol. */
    return binding->io.i2c_bus.write(binding->io.i2c_bus.context, expected_address, NULL, 0U);
}

int st_hotswap_binding_get_pir(st_hotswap_module_binding_t *binding, size_t port_index,
                               st_pir_t **out_pir, const char **out_sensor_id)
{
    st_hotswap_port_slot_t *slot;

    if (binding == NULL || port_index >= binding->port_count || out_pir == NULL ||
        out_sensor_id == NULL) {
        return 0;
    }
    slot = &binding->slots[port_index];
    if (slot->kind != ST_HOTSWAP_SLOT_PIR) {
        return 0;
    }
    *out_pir = &slot->pir;
    *out_sensor_id = kPirMotionId;
    return 1;
}

int st_hotswap_binding_get_reed(st_hotswap_module_binding_t *binding, size_t port_index,
                                st_reed_debounce_t **out_reed, const char **out_sensor_id)
{
    st_hotswap_port_slot_t *slot;

    if (binding == NULL || port_index >= binding->port_count || out_reed == NULL ||
        out_sensor_id == NULL) {
        return 0;
    }
    slot = &binding->slots[port_index];
    if (slot->kind != ST_HOTSWAP_SLOT_REED) {
        return 0;
    }
    *out_reed = &slot->reed;
    *out_sensor_id = kReedContactId;
    return 1;
}
