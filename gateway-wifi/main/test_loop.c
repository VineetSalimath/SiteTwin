#include "esp_timer.h"
#include "esp_log.h"
#include "gateway_pipeline.h"
#include "test_loop.h"

static const char *TAG = "test_loop";
static esp_timer_handle_t s_timer = NULL;
static bool s_running = false;

static void timer_callback(void *arg)
{
    (void)arg;
    float value = 20.0f + (float)(esp_timer_get_time() % 500) / 100.0f;
    gateway_pipeline_send_test_record(value);
}

void test_loop_start(uint32_t interval_ms)
{
    if (s_running) {
        test_loop_stop();
    }
    const esp_timer_create_args_t args = {
        .callback = &timer_callback,
        .name = "test_loop",
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &s_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_timer, (uint64_t)interval_ms * 1000ULL));
    s_running = true;
    ESP_LOGI(TAG, "Test loop started, interval=%lu ms", (unsigned long)interval_ms);
}

void test_loop_stop(void)
{
    if (!s_running) {
        return;
    }
    esp_timer_stop(s_timer);
    esp_timer_delete(s_timer);
    s_timer = NULL;
    s_running = false;
    ESP_LOGI(TAG, "Test loop stopped");
}

bool test_loop_is_running(void)
{
    return s_running;
}