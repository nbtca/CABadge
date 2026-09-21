#ifndef CABADGE_PC_BRIDGE_H
#define CABADGE_PC_BRIDGE_H
#include "ui/badge_ui.h"
void bridge_init(badge_state_t *state);
bool bridge_open(const char *port);
void bridge_close(void);
void bridge_poll(void);
bool bridge_ready(void);
const char *bridge_status(void);
void bridge_shake(void);
bool bridge_upload(const uint8_t *pixels,size_t size);
bool bridge_snapshot(const char *json,size_t size); /* Also used by protocol regression. */
bool bridge_transport_check(void); /* Local named-pipe check; never opens a COM port. */
#endif
