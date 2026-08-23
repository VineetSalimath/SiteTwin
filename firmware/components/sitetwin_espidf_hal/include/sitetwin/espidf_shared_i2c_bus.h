#ifndef SITETWIN_ESPIDF_SHARED_I2C_BUS_H
#define SITETWIN_ESPIDF_SHARED_I2C_BUS_H

#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#include "sitetwin/espidf_i2c_bus.h"
#include "sitetwin/sensor_hal.h"

/*
 * The final PCB's I2C lines are one shared, unmuxed bus across all four
 * ports (see the final-PCB contract: "I2C is shared and does not pass
 * through U4"), so a hot-swap pod may need to talk to any of several
 * fixed addresses (SHT41 0x44, SCD41 0x62, SGP40 0x59, BH1750 0x23,
 * ADXL345 0x53, INA219 0x40) over the lifetime of a single boot,
 * depending on what gets plugged into which port. st_espidf_i2c_device_t
 * (espidf_i2c_bus.h) is deliberately the opposite shape -- one device
 * bound to one fixed address, correct for the three existing fixed pod
 * profiles -- so it is not reused here; this is a distinct component,
 * not a modification of that one.
 *
 * NOTE ON VERIFICATION: this file depends on ESP-IDF's driver/i2c_master.h
 * and cannot be exercised by the plain-C host test suite (no ESP-IDF
 * toolchain there). It has been written to match espidf_i2c_bus.c's
 * existing patterns as closely as possible, but its correctness can only
 * be confirmed by `idf.py build` and real hardware -- treat it with more
 * scrutiny than the host-tested layers underneath it.
 */

#define ST_ESPIDF_SHARED_I2C_MAX_DEVICES 8U

typedef struct {
    uint8_t address;
    uint8_t in_use;
    i2c_master_dev_handle_t handle;
} st_espidf_shared_i2c_slot_t;

typedef struct {
    st_espidf_i2c_master_bus_t *master_bus; /* not owned; caller manages its lifetime */
    uint32_t clock_hz;
    uint32_t timeout_ms;
    st_espidf_shared_i2c_slot_t devices[ST_ESPIDF_SHARED_I2C_MAX_DEVICES];
} st_espidf_shared_i2c_bus_t;

/* master_bus must already be initialised (st_espidf_i2c_master_bus_init)
 * and must outlive this shared bus -- ownership stays with the caller
 * since the same physical bus/GPIOs might plausibly be reused elsewhere
 * in a future composition. */
esp_err_t st_espidf_shared_i2c_bus_init(st_espidf_shared_i2c_bus_t *bus,
                                        st_espidf_i2c_master_bus_t *master_bus,
                                        uint32_t clock_hz, uint32_t timeout_ms);
void st_espidf_shared_i2c_bus_deinit(st_espidf_shared_i2c_bus_t *bus);

/* st_i2c_bus_t whose write/read accept any 7-bit address per call. A
 * device handle for a given address is added lazily on first use and
 * then cached (up to ST_ESPIDF_SHARED_I2C_MAX_DEVICES distinct
 * addresses) -- it is never removed on its own, since the same fixed
 * address remains valid for that module type even if the physical
 * module is unplugged and a different unit of the same type is plugged
 * back in later. */
st_i2c_bus_t st_espidf_shared_i2c_bus(st_espidf_shared_i2c_bus_t *bus);

/* Matches st_hotswap_binding_io_t.i2c_probe's signature. Uses ESP-IDF's
 * own i2c_master_probe() rather than a zero-length transaction. */
st_hal_result_t st_espidf_shared_i2c_probe(void *context, uint8_t address);

#endif
