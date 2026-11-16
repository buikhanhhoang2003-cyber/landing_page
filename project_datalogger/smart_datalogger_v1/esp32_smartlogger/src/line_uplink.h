#ifndef LINE_UPLINK_H
#define LINE_UPLINK_H

#include <Arduino.h>
#include "stm32_link.h"

// Initialize once; disabled until include/line_config.h enables the uplink.
void LineUplink_Init();
// Called on the Arduino loop task: copies a snapshot without waiting for HTTP.
void LineUplink_Task(uint32_t now, bool networkConnected, const SystemState &state);

#endif
