#ifndef GATEWAY_COMMAND_JSON_H
#define GATEWAY_COMMAND_JSON_H

#include <stddef.h>

#include "sitetwin/command.h"

int gw_command_json_parse(const char *topic, const char *payload,
                          st_command_t *command);
int gw_command_result_json(const st_command_ack_t *ack, char *json,
                           size_t json_capacity);

#endif
