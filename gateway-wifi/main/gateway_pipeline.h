#ifndef GATEWAY_PIPELINE_H
#define GATEWAY_PIPELINE_H

#include <stdint.h>

void gateway_pipeline_init(void);
int gateway_pipeline_send_test_record(float value);
int gateway_pipeline_send_heartbeat(void);
uint32_t gateway_pipeline_sent_count(void);

#endif