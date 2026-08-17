#ifndef SITETWIN_ESPIDF_FINAL_PCB_BOARD_H
#define SITETWIN_ESPIDF_FINAL_PCB_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"

#include "sitetwin/final_pcb_board.h"

typedef struct {
    st_final_pcb_board_t portable_board;
    adc_oneshot_unit_handle_t adc_handle;
    adc_cali_handle_t adc_calibration;
    adc_unit_t adc_unit;
    adc_channel_t adc_channel;
    uint8_t calibration_scheme;
    bool calibration_active;
    bool initialized;
} st_espidf_final_pcb_board_t;

esp_err_t st_espidf_final_pcb_board_init(st_espidf_final_pcb_board_t *board);
void st_espidf_final_pcb_board_deinit(st_espidf_final_pcb_board_t *board);
const st_board_port_ops_t *st_espidf_final_pcb_board_ops(
    st_espidf_final_pcb_board_t *board);

#endif
