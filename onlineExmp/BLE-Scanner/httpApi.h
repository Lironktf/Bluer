/*
  BLE-Scanner - Laundry Machine Monitor

  HTTP API client for posting machine status to REST endpoint

  This file is part of BLE-Scanner.

  BLE-Scanner is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

*/

#ifndef __HTTP_API_H__
#define __HTTP_API_H__ 1

#include "config.h"

/*
   Initialize the HTTP API client
*/
void HttpApiSetup(void);

/*
   Cyclic update (handles connection management)
*/
void HttpApiUpdate(void);

/*
   Post machine status to the API
   Returns true if successful
*/
bool HttpApiPostMachineStatus(const char* machineId, bool running, bool empty);

#endif
