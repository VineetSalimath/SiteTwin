#ifndef SITETWIN_GATEWAY_JSON_H
#define SITETWIN_GATEWAY_JSON_H

#include <stddef.h>

#include "sitetwin/contracts.h"

int st_gateway_telemetry_to_json(const st_telemetry_record_t *record, char *json,
                                 size_t json_capacity);

#endif
