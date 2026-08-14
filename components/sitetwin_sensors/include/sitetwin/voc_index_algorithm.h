#ifndef SITETWIN_VOC_INDEX_ALGORITHM_H
#define SITETWIN_VOC_INDEX_ALGORITHM_H

#include <stdint.h>

#include "sensirion_gas_index_algorithm.h"

typedef enum {
    ST_VOC_ALGORITHM_WARMING_UP = 0,
    ST_VOC_ALGORITHM_READY
} st_voc_algorithm_result_t;

typedef struct {
    GasIndexAlgorithmParams parameters;
    uint32_t sampling_interval_ms;
    uint32_t samples_processed;
    uint32_t reset_count;
} st_voc_index_algorithm_t;

int st_voc_index_algorithm_init(st_voc_index_algorithm_t *algorithm,
                                uint32_t sampling_interval_ms);
void st_voc_index_algorithm_reset(st_voc_index_algorithm_t *algorithm);
st_voc_algorithm_result_t st_voc_index_algorithm_process(
    st_voc_index_algorithm_t *algorithm,
    uint16_t raw_signal,
    int32_t *voc_index);

#endif
