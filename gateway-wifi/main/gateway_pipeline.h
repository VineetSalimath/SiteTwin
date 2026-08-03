#ifndef GATEWAY_PIPELINE_H
#define GATEWAY_PIPELINE_H

#include <stdint.h>

#include "sitetwin/gateway_frame.h"

void gateway_pipeline_init(void);
int gateway_pipeline_send_test_record(float value);
int gateway_pipeline_send_heartbeat(void);
int gateway_pipeline_process_uart_frame(const st_gateway_frame_header_t *header,
                                        const uint8_t *payload);
uint32_t gateway_pipeline_sent_count(void);

#endif
