#ifndef SITETWIN_ESPIDF_I2C_BUS_H
#define SITETWIN_ESPIDF_I2C_BUS_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#include "sitetwin/sensor_hal.h"

typedef struct {
    int controller;
    int sda_gpio;
    int scl_gpio;
    uint8_t address;
    uint32_t clock_hz;
    uint32_t timeout_ms;
    bool enable_internal_pullups;
} st_espidf_i2c_device_config_t;

typedef struct {
    int controller;
    int sda_gpio;
    int scl_gpio;
    bool enable_internal_pullups;
} st_espidf_i2c_master_bus_config_t;

typedef struct {
    st_espidf_i2c_master_bus_config_t config;
    i2c_master_bus_handle_t handle;
} st_espidf_i2c_master_bus_t;

typedef struct {
    uint8_t address;
    uint32_t clock_hz;
    uint32_t timeout_ms;
} st_espidf_i2c_target_config_t;

typedef struct {
    st_espidf_i2c_device_config_t config;
    st_espidf_i2c_master_bus_t owned_bus;
    st_espidf_i2c_master_bus_t *master_bus;
    i2c_master_dev_handle_t device_handle;
    uint8_t owns_master_bus;
} st_espidf_i2c_device_t;

esp_err_t st_espidf_i2c_master_bus_init(
    st_espidf_i2c_master_bus_t *bus,
    const st_espidf_i2c_master_bus_config_t *config);
void st_espidf_i2c_master_bus_deinit(st_espidf_i2c_master_bus_t *bus);
esp_err_t st_espidf_i2c_device_init_on_bus(
    st_espidf_i2c_device_t *device,
    st_espidf_i2c_master_bus_t *bus,
    const st_espidf_i2c_target_config_t *config);

/* Source-compatible one-device convenience initializer. */
esp_err_t st_espidf_i2c_device_init(st_espidf_i2c_device_t *device,
                                    const st_espidf_i2c_device_config_t *config);
void st_espidf_i2c_device_deinit(st_espidf_i2c_device_t *device);
st_i2c_bus_t st_espidf_i2c_bus(st_espidf_i2c_device_t *device);

#endif
