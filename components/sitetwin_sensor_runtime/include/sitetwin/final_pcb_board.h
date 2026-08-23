#ifndef SITETWIN_FINAL_PCB_BOARD_H
#define SITETWIN_FINAL_PCB_BOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sitetwin/board_port.h"

#define ST_FINAL_PCB_PORT_COUNT 4U
#define ST_FINAL_PCB_MAX_ID_SAMPLES 9U

typedef st_hal_result_t (*st_final_pcb_gpio_write_fn)(void *context, int gpio,
                                                       bool level);
typedef st_hal_result_t (*st_final_pcb_adc_read_fn)(void *context, int gpio,
                                                    uint16_t *raw_adc,
                                                    uint16_t *millivolts,
                                                    bool *voltage_calibrated);
typedef void (*st_final_pcb_delay_us_fn)(void *context, uint32_t delay_us);

typedef struct {
    void *context;
    st_final_pcb_gpio_write_fn gpio_write;
    st_final_pcb_adc_read_fn adc_read;
    st_final_pcb_delay_us_fn delay_us;
} st_final_pcb_io_t;

typedef struct {
    int id_adc_gpio;
    int id_mux_sel0_gpio;
    int id_mux_sel1_gpio;
    int data_mux_sel0_gpio;
    int data_mux_sel1_gpio;
    int id_mosfet_gate_gpio;
    bool id_pullup_enable_level;
    bool id_pullup_disable_level;
    uint32_t mux_settle_us;
    uint32_t id_settle_us;
    uint8_t id_sample_count;
} st_final_pcb_board_config_t;

typedef struct {
    st_final_pcb_board_config_t config;
    st_final_pcb_io_t io;
    st_board_port_ops_t ops;
} st_final_pcb_board_t;

int st_final_pcb_board_init(st_final_pcb_board_t *board,
                            const st_final_pcb_board_config_t *config,
                            const st_final_pcb_io_t *io);
const st_board_port_ops_t *st_final_pcb_board_ops(st_final_pcb_board_t *board);

st_hal_result_t st_final_pcb_port_select_bits(size_t port_index, bool *sel0,
                                               bool *sel1);
st_module_type_t st_final_pcb_classify_id_mv(uint16_t millivolts);
bool st_final_pcb_module_uses_i2c(st_module_type_t module_type);
uint8_t st_final_pcb_expected_i2c_address(st_module_type_t module_type);

#endif
