// DEPRECATED - This file is no longer used
// MQTT is used instead (see mqtt.h)
// This stub exists only to satisfy Arduino IDE caching

#ifndef __HTTP_API_H__
#define __HTTP_API_H__ 1

void HttpApiSetup(void);
void HttpApiUpdate(void);
bool HttpApiPostMachineStatus(const char* machineId, bool running, bool empty);

#endif
