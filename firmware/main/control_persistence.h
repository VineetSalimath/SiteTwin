#ifndef SITETWIN_CONTROL_PERSISTENCE_H
#define SITETWIN_CONTROL_PERSISTENCE_H

#include "sitetwin/command.h"
#include "sitetwin/gateway_state.h"

st_command_persistence_t st_espidf_pod_command_persistence(void);
st_gateway_state_persistence_t st_espidf_gateway_state_persistence(void);

#endif
