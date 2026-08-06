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
    st_espidf_i2c_device_config_t config;
    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t device_handle;
} st_espidf_i2c_device_t;

esp_err_t st_espidf_i2c_device_init(st_espidf_i2c_device_t *device,
                                    const st_espidf_i2c_device_config_t *config);
void st_espidf_i2c_device_deinit(st_espidf_i2c_device_t *device);
st_i2c_bus_t st_espidf_i2c_bus(st_espidf_i2c_device_t *device);

#endif
