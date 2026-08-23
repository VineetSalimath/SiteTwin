#ifndef SITETWIN_HOTSWAP_MODULE_BINDING_H
#define SITETWIN_HOTSWAP_MODULE_BINDING_H

#include <stdbool.h>

#include "sitetwin/adxl345.h"
#include "sitetwin/bh1750.h"
#include "sitetwin/board_port.h"
#include "sitetwin/board_port_manager.h"
#include "sitetwin/ds18b20.h"
#include "sitetwin/ina219.h"
#include "sitetwin/module_instance.h"
#include "sitetwin/pir.h"
#include "sitetwin/reed.h"
#include "sitetwin/scd41.h"
#include "sitetwin/sensor_hal.h"
#include "sitetwin/sensor_registry.h"
#include "sitetwin/sgp40.h"
#include "sitetwin/sht41.h"

#define ST_HOTSWAP_BINDING_MAX_PORTS ST_BOARD_PORT_MANAGER_MAX_PORTS
#define ST_HOTSWAP_REGISTRY_SLOTS_PER_PORT 2U

/*
 * This layer is the "type -> driver" mapping table: it is exactly what
 * st_board_port_manager_t's attach/detach/module_uses_bus_probe/bus_probe
 * callbacks are meant to call into. It does not itself decide *whether*
 * to attach anything -- the manager already did the debounce/stable-scan
 * work before calling here.
 *
 * Two different attachment styles, matching two different existing
 * architectural decisions already made elsewhere in this codebase (not
 * decided here):
 *  - SHT41/SCD41/SGP40/BH1750/ADXL345/INA219/DS18B20 are poll-based and go
 *    through st_module_instance_t into the pod's st_sensor_registry_t, the
 *    same path the fixed pod profiles already use.
 *  - PIR/REED are event-based (see reed.h's own header comment) and are
 *    deliberately NOT put in the registry. This binding layer only owns
 *    their live st_pir_t/st_reed_debounce_t state; the pod's own poll
 *    loop is responsible for reading the port's raw DATA_COMMON level
 *    (after selecting that port on the DATA mux) and feeding it in, then
 *    calling st_pod_runtime_emit_event() on a confirmed transition.
 */

typedef union {
    st_sht41_t sht41;
    st_scd41_t scd41;
    st_sgp40_t sgp40;
    st_bh1750_t bh1750;
    st_ina219_t ina219;
    st_adxl345_t adxl345;
    st_ds18b20_t ds18b20;
} st_hotswap_registry_driver_storage_t;

typedef enum {
    ST_HOTSWAP_SLOT_EMPTY = 0,
    ST_HOTSWAP_SLOT_REGISTRY_DRIVER,
    ST_HOTSWAP_SLOT_PIR,
    ST_HOTSWAP_SLOT_REED
} st_hotswap_slot_kind_t;

typedef struct {
    st_hotswap_slot_kind_t kind;
    st_module_type_t module_type;
    st_hotswap_registry_driver_storage_t driver_storage;
    st_module_instance_t module_instance;
    uint8_t registry_slots[ST_MODULE_MAX_CHANNELS];
    st_pir_t pir;
    st_reed_debounce_t reed;
} st_hotswap_port_slot_t;

typedef struct {
    /*
     * The final PCB's I2C lines are shared across all four ports and are
     * not muxed (the contract: "I2C is shared and does not pass through
     * U4"), so every I2C-based type on every port talks over the same
     * bus at its fixed default address.
     */
    st_i2c_bus_t i2c_bus;

    /*
     * Explicit address-presence probe, deliberately decoupled from
     * i2c_bus.write/read: a presence probe is a different operation from
     * a real transaction, and backends implement it differently (a
     * zero-length transmit on some stacks, a dedicated call such as
     * ESP-IDF's i2c_master_probe() on others). ST_HAL_OK means something
     * ACKed at that address; any other result means no response or a
     * transport error, and is treated as a mismatch by the caller.
     *
     * i2c_probe_context is passed as this callback's context and is
     * independent of data_common_context below -- the two callbacks
     * naturally need different backing objects (e.g. a shared I2C bus
     * instance vs. a mux/port_ops instance), so they are not forced to
     * share one.
     */
    void *i2c_probe_context;
    st_hal_result_t (*i2c_probe)(void *context, uint8_t address);

    /*
     * Called once, at attach time, to obtain a OneWire bus for a specific
     * port (DS18B20 is the only OneWire type). The returned bus's own
     * reset/write/read implementation must select that port on the DATA
     * mux before each real transaction -- this binding layer does not do
     * that itself and does not assume the mux stays pointed at this port
     * between calls. data_common_context is this callback's own context,
     * independent of i2c_probe_context above.
     */
    void *data_common_context;
    st_onewire_bus_t (*data_common_bus_for_port)(void *context, size_t port_index);
} st_hotswap_binding_io_t;

typedef struct {
    st_hotswap_binding_io_t io;
    st_sensor_registry_t *registry;
    size_t port_count;
    st_hotswap_port_slot_t slots[ST_HOTSWAP_BINDING_MAX_PORTS];
} st_hotswap_module_binding_t;

int st_hotswap_module_binding_init(st_hotswap_module_binding_t *binding,
                                   const st_hotswap_binding_io_t *io,
                                   st_sensor_registry_t *registry, size_t port_count);

/* Matches st_board_port_manager_callbacks_t's attach/detach/
 * module_uses_bus_probe/bus_probe signatures exactly; pass &binding as
 * callbacks.context when wiring up the manager. */
int st_hotswap_module_binding_attach(void *context, size_t port_index,
                                     st_module_type_t module_type, uint64_t now_ms);
void st_hotswap_module_binding_detach(void *context, size_t port_index, uint64_t now_ms);
bool st_hotswap_module_binding_uses_bus_probe(void *context, st_module_type_t module_type,
                                              uint8_t *expected_address);
st_hal_result_t st_hotswap_module_binding_bus_probe(void *context, size_t port_index,
                                                     uint8_t expected_address);

/*
 * For the pod's own poll loop. Returns 1 and fills the out-params if
 * port_index currently holds a live PIR/REED instance to drive this tick
 * (feed a freshly-read raw DATA_COMMON level in, and on a returned event
 * call st_pod_runtime_emit_event(runtime, *out_sensor_id, ..., now_ms,
 * value)). Returns 0 if the port holds neither (empty, faulted, or a
 * registry-backed type that the sensor_registry tick loop already
 * services on its own).
 */
int st_hotswap_binding_get_pir(st_hotswap_module_binding_t *binding, size_t port_index,
                               st_pir_t **out_pir, const char **out_sensor_id);
int st_hotswap_binding_get_reed(st_hotswap_module_binding_t *binding, size_t port_index,
                                st_reed_debounce_t **out_reed, const char **out_sensor_id);

#endif
