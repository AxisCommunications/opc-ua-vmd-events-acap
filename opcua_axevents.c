/**
 * Copyright (C) 2023, Axis Communications AB, Lund, Sweden
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

#include <assert.h>
#include <axevent.h>

#include "opcua_axevents.h"
#include "opcua_common.h"
#include "opcua_open62541.h"

/*
 * - Example AXEVENT for VMD 4 Alarm - Any Profile
 * <MESSAGE > [tnsaxis:topic2 (Camera1ProfileANY) = 'Camera1ProfileANY' (VMD 4: Any Profile)]
 * <MESSAGE > [tnsaxis:topic1 (VMD) = 'VMD' (Video Motion Detection)]
 * <MESSAGE > [tnsaxis:topic0 = 'CameraApplicationPlatform']
 * <MESSAGE > [active = '1'] {onvif-data} {property-state}
 *
 * - Example AXEVENT for Fence Guard Alarm - Any Profile
 * <MESSAGE > [tnsaxis:topic2 (Camera1ProfileANY) = 'Camera1ProfileANY' (Fence Guard: Any Profile)]
 * <MESSAGE > [tnsaxis:topic1 (FenceGuard) = 'FenceGuard' (Fence Guard)]
 * <MESSAGE > [tnsaxis:topic0 = 'CameraApplicationPlatform']
 * <MESSAGE > [active = '1'] {onvif-data} {property-state}
 *
 * - Example AXEVENT for Motion Guard Alarm - Any Profile
 * <MESSAGE > [tnsaxis:topic2 (Camera1ProfileANY) = 'Camera1ProfileANY' (Motion Guard: Any Profile)]
 * <MESSAGE > [tnsaxis:topic1 (MotionGuard) = 'MotionGuard' (Motion Guard)]
 * <MESSAGE > [tnsaxis:topic0 = 'CameraApplicationPlatform']
 * <MESSAGE > [active = '1'] {onvif-data} {property-state}
 *
 * - Example AXEVENT for Loitering Guard Alarm - Any Profile
 * <MESSAGE > [tnsaxis:topic2 (Camera1ProfileANY) = 'Camera1ProfileANY' (Loitering Guard: Any Profile)]
 * <MESSAGE > [tnsaxis:topic1 (LoiteringGuard) = 'LoiteringGuard' (Loitering Guard)]
 * <MESSAGE > [tnsaxis:topic0 = 'CameraApplicationPlatform']
 * <MESSAGE > [active = '1'] {onvif-data} {property-state}
 *
 */
#define AXEV_CTX_DISCOVERY "axevents-discovery"
#define AXEV_TNSAXIS_TOPIC0 "CameraApplicationPlatform"
#define AXEV_ACTIVE "active"

static guint subid = 0;

static void axevent_sub_callback(guint id, AXEvent *event, void *data)
{
    const AXEventKeyValueSet *key_value_set;
    gboolean active;
    gchar *label;
    gint value;

    (void)id;

    // Check for the discovery payload
    value = strcmp(AXEV_CTX_DISCOVERY, (char *)data);
    if (0 != value)
    {
        goto free;
    }

    // Handle the event
    key_value_set = ax_event_get_key_value_set(event);

    if (!ax_event_key_value_set_get_boolean(key_value_set, AXEV_ACTIVE, NULL, &active, NULL))
    {
        LOG_E("%s/%s: Failed to get '%s' value from axevent", __FILE__, __FUNCTION__, AXEV_ACTIVE);
        goto free;
    }

    if (!ax_event_key_value_set_get_string(key_value_set, "topic2", "tnsaxis", &label, NULL))
    {
        LOG_E("%s/%s: Failed to get label value from axevent", __FILE__, __FUNCTION__);
        goto free;
    }

    // Inform the OPC UA server of the received axevent
    ua_server_axevent_process(label, active);

free:
    // Free the received event, n.b. AXEventKeyValueSet should not be freed
    // since it's owned by the event system until unsubscribing
    ax_event_free(event);
}

static guint axevent_subscribe(AXEventHandler *ehandler, const gchar *evtsource)
{
    assert(NULL != ehandler);
    assert(NULL != evtsource);

    AXEventKeyValueSet *key_value_set;
    guint id = 0;

    key_value_set = ax_event_key_value_set_new();

    // Setup axevent subscription data
    if (!ax_event_key_value_set_add_key_values(
            key_value_set,
            NULL,
            "topic0",
            "tnsaxis",
            AXEV_TNSAXIS_TOPIC0,
            AX_VALUE_TYPE_STRING,
            "topic1",
            "tnsaxis",
            evtsource,
            AX_VALUE_TYPE_STRING,
            AXEV_ACTIVE,
            NULL,
            NULL,
            AX_VALUE_TYPE_BOOL,
            NULL))
    {
        LOG_E(
            "%s/%s: Failed to discover '%s' axevents with '%s' keyword",
            __FILE__,
            __FUNCTION__,
            evtsource,
            AXEV_ACTIVE);
    }

    // Subscribe and connect a callback function
    if (ax_event_handler_subscribe(
            ehandler,                                     // event handler
            key_value_set,                                // key value set
            &id,                                          // subscription id
            (AXSubscriptionCallback)axevent_sub_callback, // callback function
            AXEV_CTX_DISCOVERY,                           // user data
            NULL))                                        // GError
    {
        LOG_I(
            "%s/%s: Succesfully subscribed to '%s' axevent '%s' with id %d",
            __FILE__,
            __FUNCTION__,
            evtsource,
            AXEV_ACTIVE,
            id);
    }
    else
    {
        LOG_E("%s/%s: Failed to subsribe to '%s' axevent %s", __FILE__, __FUNCTION__, evtsource, AXEV_ACTIVE);
    }

    // Cleanup
    ax_event_key_value_set_free(key_value_set);

    // Return subscription id
    return id;
}

gboolean axevent_setup(AXEventHandler *ehandler, const gchar *topic)
{
    assert(NULL != ehandler);

    // Discover axevent alarms
    subid = axevent_subscribe(ehandler, topic);
    if (0 == subid)
    {
        LOG_E("%s/%s: Cannot subscribe to axevent '%s' for topic '%s'", __FILE__, __FUNCTION__, AXEV_ACTIVE, topic);
        return FALSE;
    }
    return TRUE;
}

void axevent_teardown(AXEventHandler *ehandler)
{
    assert(NULL != ehandler);

    LOG_I("%s/%s: Unsubscribing from axevent with id %u", __FILE__, __FUNCTION__, subid);
    ax_event_handler_unsubscribe(ehandler, subid, NULL);
}
