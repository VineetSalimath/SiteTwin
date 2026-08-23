#include "sitetwin/espidf_muxed_data_common.h"

#include <string.h>

#include "driver/gpio.h"

esp_err_t st_espidf_muxed_data_common_init(st_espidf_muxed_data_common_t *muxed,
                                           const st_board_port_ops_t *port_ops,
                                           st_onewire_bus_t real_bus, int data_common_gpio,
                                           size_t port_index)
{
    if (muxed == NULL || port_ops == NULL || port_ops->port_select_bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(muxed, 0, sizeof(*muxed));
    muxed->port_ops = port_ops;
    muxed->real_bus = real_bus;
    muxed->data_common_gpio = data_common_gpio;
    muxed->port_index = port_index;
    return ESP_OK;
}

static st_hal_result_t muxed_select(st_espidf_muxed_data_common_t *muxed)
{
    if (muxed == NULL || muxed->port_ops == NULL || muxed->port_ops->port_select_bus == NULL) {
        return ST_HAL_IO_ERROR;
    }
    return muxed->port_ops->port_select_bus(muxed->port_ops->context, muxed->port_index);
}

static st_hal_result_t muxed_reset(void *context)
{
    st_espidf_muxed_data_common_t *muxed = (st_espidf_muxed_data_common_t *)context;
    st_hal_result_t result = muxed_select(muxed);

    if (result != ST_HAL_OK) {
        return result;
    }
    if (muxed->real_bus.reset == NULL) {
        return ST_HAL_UNSUPPORTED;
    }
    return muxed->real_bus.reset(muxed->real_bus.context);
}

static st_hal_result_t muxed_write(void *context, const uint8_t *data, size_t length)
{
    st_espidf_muxed_data_common_t *muxed = (st_espidf_muxed_data_common_t *)context;
    st_hal_result_t result = muxed_select(muxed);

    if (result != ST_HAL_OK) {
        return result;
    }
    if (muxed->real_bus.write == NULL) {
        return ST_HAL_UNSUPPORTED;
    }
    return muxed->real_bus.write(muxed->real_bus.context, data, length);
}

static st_hal_result_t muxed_read(void *context, uint8_t *data, size_t length)
{
    st_espidf_muxed_data_common_t *muxed = (st_espidf_muxed_data_common_t *)context;
    st_hal_result_t result = muxed_select(muxed);

    if (result != ST_HAL_OK) {
        return result;
    }
    if (muxed->real_bus.read == NULL) {
        return ST_HAL_UNSUPPORTED;
    }
    return muxed->real_bus.read(muxed->real_bus.context, data, length);
}

st_onewire_bus_t st_espidf_muxed_onewire_bus(st_espidf_muxed_data_common_t *muxed)
{
    st_onewire_bus_t bus;

    memset(&bus, 0, sizeof(bus));
    bus.context = muxed;
    bus.reset = muxed_reset;
    bus.write = muxed_write;
    bus.read = muxed_read;
    return bus;
}

st_hal_result_t st_espidf_muxed_data_common_read_level(st_espidf_muxed_data_common_t *muxed,
                                                        uint8_t *out_level)
{
    st_hal_result_t result;
    int level;

    if (muxed == NULL || out_level == NULL) {
        return ST_HAL_IO_ERROR;
    }
    result = muxed_select(muxed);
    if (result != ST_HAL_OK) {
        return result;
    }
    if (!GPIO_IS_VALID_GPIO((gpio_num_t)muxed->data_common_gpio)) {
        return ST_HAL_IO_ERROR;
    }
    level = gpio_get_level((gpio_num_t)muxed->data_common_gpio);
    *out_level = (uint8_t)((level != 0) ? 1U : 0U);
    return ST_HAL_OK;
}
