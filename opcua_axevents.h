/**
 * Copyright (C) 2023 Axis Communications AB, Lund, Sweden
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _OPCUA_AXEVENTS_H_
#define _OPCUA_AXEVENTS_H_

#include <axevent.h>

gboolean axevent_setup(AXEventHandler *ehandler, const gchar *topic);
void axevent_teardown(AXEventHandler *ehandler);

#endif /* _OPCUA_AXEVENTS_H_ */
