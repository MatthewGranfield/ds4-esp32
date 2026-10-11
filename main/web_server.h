#pragma once
#include "usb_hid_ds4.h"

void web_server_init(void);
void web_controller_state_snapshot(ds4_action_t *out);
