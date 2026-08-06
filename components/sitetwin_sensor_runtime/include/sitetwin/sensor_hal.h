#ifndef SITETWIN_SENSOR_HAL_H
#define SITETWIN_SENSOR_HAL_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    ST_HAL_OK = 0,
    ST_HAL_NOT_PRESENT,
    ST_HAL_BUSY,
    ST_HAL_TIMEOUT,
    ST_HAL_IO_ERROR,
    ST_HAL_UNSUPPORTED
} st_hal_result_t;

typedef struct {
    void *context;
    st_hal_result_t (*write)(void *context, uint8_t address,
                             const uint8_t *data, size_t length);
    st_hal_result_t (*read)(void *context, uint8_t address,
                            uint8_t *data, size_t length);
} st_i2c_bus_t;

#endif
