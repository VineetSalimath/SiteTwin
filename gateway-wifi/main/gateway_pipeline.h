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

/* Handles an incoming downstream command message from HiveMQ (topic:
 * sitetwin/pods/{pod_id}/commands). Since the Zigbee downlink (gateway to
 * pod) does not exist yet, this does NOT forward the command over UART --
 * it logs it and publishes a simulated ack to command_acks, honestly
 * labeled status="simulated" so it's never confused with a real execution
 * result once the real downlink exists. */
void gateway_pipeline_process_command(const char *topic, const char *payload);

#endif