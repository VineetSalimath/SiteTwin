#include "sitetwin/voc_index_algorithm.h"

#include <stddef.h>
#include <string.h>

#define ST_VOC_ALGORITHM_ONE_SECOND_MS 1000U
#define ST_VOC_ALGORITHM_TEN_SECONDS_MS 10000U

static int supported_sampling_interval(uint32_t sampling_interval_ms)
{
    return sampling_interval_ms == ST_VOC_ALGORITHM_ONE_SECOND_MS ||
           sampling_interval_ms == ST_VOC_ALGORITHM_TEN_SECONDS_MS;
}

int st_voc_index_algorithm_init(st_voc_index_algorithm_t *algorithm,
                                uint32_t sampling_interval_ms)
{
    if (algorithm == NULL || supported_sampling_interval(sampling_interval_ms) == 0) {
        return -1;
    }

    memset(algorithm, 0, sizeof(*algorithm));
    algorithm->sampling_interval_ms = sampling_interval_ms;
    GasIndexAlgorithm_init_with_sampling_interval(
        &algorithm->parameters,
        GasIndexAlgorithm_ALGORITHM_TYPE_VOC,
        (float)sampling_interval_ms / 1000.0F);
    return 0;
}

void st_voc_index_algorithm_reset(st_voc_index_algorithm_t *algorithm)
{
    if (algorithm == NULL) {
        return;
    }

    GasIndexAlgorithm_reset(&algorithm->parameters);
    algorithm->samples_processed = 0U;
    ++algorithm->reset_count;
}

st_voc_algorithm_result_t st_voc_index_algorithm_process(
    st_voc_index_algorithm_t *algorithm,
    uint16_t raw_signal,
    int32_t *voc_index)
{
    int32_t output = 0;

    if (algorithm == NULL || voc_index == NULL) {
        return ST_VOC_ALGORITHM_WARMING_UP;
    }

    GasIndexAlgorithm_process(&algorithm->parameters, (int32_t)raw_signal, &output);
    ++algorithm->samples_processed;
    *voc_index = output;
    return output > 0 ? ST_VOC_ALGORITHM_READY : ST_VOC_ALGORITHM_WARMING_UP;
}
