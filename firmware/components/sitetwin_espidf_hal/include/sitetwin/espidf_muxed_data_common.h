#ifndef SITETWIN_ESPIDF_MUXED_DATA_COMMON_H
#define SITETWIN_ESPIDF_MUXED_DATA_COMMON_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "sitetwin/board_port.h"
#include "sitetwin/sensor_hal.h"

/*
 * DS18B20, PIR, and REED all share the single DATA_COMMON physical line
 * (final-PCB contract: "U4 selects one non-I2C DATA line"), so reading
 * any of them requires selecting that port on the DATA mux immediately
 * before the real electrical transaction -- not once at attach time,
 * since a different port's DATA_COMMON sensor may be serviced in
 * between polls. This component owns exactly that "select, then
 * transact" composition; the underlying OneWire driver
 * (espidf_onewire_bus.c) and the raw GPIO read beneath it stay
 * completely unaware of muxing.
 *
 * OPEN VERIFICATION ITEM: this assumes reading DATA_COMMON's raw digital
 * level via plain gpio_get_level() (the PIR/REED path) behaves correctly
 * while the same physical pin is also, at other times, driven by the
 * ESP-IDF onewire_bus driver (the DS18B20 path) -- i.e. that the
 * onewire_bus driver does not hold the pin in a state that would make a
 * plain level read meaningless when a different port is currently
 * selected on the mux. This has not been confirmed against ESP-IDF's
 * onewire_bus driver internals or real hardware; verify before trusting
 * PIR/REED readings on a build that also uses DS18B20.
 *
 * NOTE ON VERIFICATION: like espidf_shared_i2c_bus, this depends on
 * ESP-IDF headers and cannot be exercised by the host test suite in this
 * sandbox -- confirm with idf.py build + real hardware.
 */

typedef struct {
    const st_board_port_ops_t *port_ops; /* for port_select_bus -- not owned */
    st_onewire_bus_t real_bus;           /* the actual GPIO3 OneWire bus -- not owned */
    int data_common_gpio;                /* GPIO3 (DATA_COMMON), for the raw-level PIR/REED path */
    size_t port_index;                   /* which port this instance is bound to */
} st_espidf_muxed_data_common_t;

esp_err_t st_espidf_muxed_data_common_init(st_espidf_muxed_data_common_t *muxed,
                                           const st_board_port_ops_t *port_ops,
                                           st_onewire_bus_t real_bus, int data_common_gpio,
                                           size_t port_index);

/* Returns an st_onewire_bus_t whose reset/write/read each select
 * muxed->port_index on the DATA mux immediately before delegating to
 * muxed->real_bus. Pass this directly as a DS18B20 driver's config.bus. */
st_onewire_bus_t st_espidf_muxed_onewire_bus(st_espidf_muxed_data_common_t *muxed);

/* For the pod's own poll loop driving PIR/REED: selects muxed->port_index
 * on the DATA mux, then reads DATA_COMMON's current raw digital level (0
 * or 1). */
st_hal_result_t st_espidf_muxed_data_common_read_level(st_espidf_muxed_data_common_t *muxed,
                                                        uint8_t *out_level);

#endif
