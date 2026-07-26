#include "esp_log.h"

#include "sitetwin/pod_runtime.h"

static const char *TAG = "sitetwin";

void app_main(void)
{
    st_pod_runtime_t runtime;

    st_pod_runtime_init(&runtime, ST_POD_ENVIRONMENT, "ENV_01", 1U);
    ESP_LOGI(TAG, "SiteTwin firmware core initialised; waiting for board adapters");
}
