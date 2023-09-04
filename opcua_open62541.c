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

#include <open62541/server_config_default.h>
#include <pthread.h>

#include "opcua_common.h"
#include "opcua_open62541.h"

static UA_Server *server;

static void *run_ua_server(void *running)
{
    assert(NULL != server);
    assert(NULL != running);

    LOG_I("%s/%s: Starting UA server ...", __FILE__, __FUNCTION__);
    UA_StatusCode status = UA_Server_run(server, running);
    LOG_I("%s/%s: UA Server exit status: %s", __FILE__, __FUNCTION__, UA_StatusCode_name(status));
    UA_Server_delete(server);
    return NULL;
}

void ua_server_init(const UA_UInt16 port)
{
    assert(NULL == server);
    server = UA_Server_new();
    assert(NULL != server);
    assert(1024 <= port && 65535 >= port);
    UA_ServerConfig_setMinimal(UA_Server_getConfig(server), port, NULL);
}

bool ua_server_start(pthread_t *thread_id, UA_Boolean *running)
{
    assert(NULL != server);
    assert(NULL != thread_id);
    assert(NULL != running);

    int result = pthread_create(thread_id, NULL, run_ua_server, (void *)running);

    if (0 != result)
    {
        LOG_E("%s/%s: Failed to create thread (%s)", __FILE__, __FUNCTION__, strerror(result));
        return false;
    }
    LOG_I("%s/%s: OPC UA Server thread created!", __FILE__, __FUNCTION__);

    return true;
}

static void ua_server_add_status(char *label, UA_Boolean state)
{
    assert(NULL != server);
    assert(NULL != label);

    // Define attributes
    UA_VariableAttributes attr = UA_VariableAttributes_default;

    UA_Variant_setScalar(&attr.value, &state, &UA_TYPES[UA_TYPES_BOOLEAN]);

    attr.description = UA_LOCALIZEDTEXT("en-US", label);
    attr.displayName = UA_LOCALIZEDTEXT("en-US", label);
    attr.dataType = UA_TYPES[UA_TYPES_BOOLEAN].typeId;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ;

    // Add the variable node to the information model
    UA_NodeId node_id = UA_NODEID_STRING(1, label);
    UA_QualifiedName name = UA_QUALIFIEDNAME(1, label);
    UA_NodeId parent_node_id = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId parent_ref_node_id = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
    UA_Server_addVariableNode(
        server,
        node_id,
        parent_node_id,
        parent_ref_node_id,
        name,
        UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
        attr,
        NULL,
        NULL);
}

static void ua_server_update_status(char *label, UA_Boolean state)
{
    assert(NULL != server);

    UA_Variant newvalue;
    UA_Variant_setScalar(&newvalue, &state, &UA_TYPES[UA_TYPES_BOOLEAN]);
    UA_NodeId currentNodeId = UA_NODEID_STRING(1, label);
    UA_Server_writeValue(server, currentNodeId, newvalue);
}

void ua_server_axevent_process(char *label, UA_Boolean state)
{
    assert(NULL != server);

    UA_NodeId reqNodeId = UA_NODEID_STRING(1, label);
    UA_NodeId resNodeId;

    // Check if a node with this label already exists on the OPC UA node store
    UA_StatusCode ret = UA_Server_readNodeId(server, reqNodeId, &resNodeId);
    if (UA_STATUSCODE_GOOD == ret) // FOUND
    {
        // Update the node
        LOG_I(
            "%s/%s: OPC UA updating node '%s' alarm with status '%s' ",
            __FILE__,
            __FUNCTION__,
            label,
            state ? "true" : "false");
        ua_server_update_status(label, state);
    }
    else // NOT FOUND
    {
        // Create a new node
        LOG_I(
            "%s/%s: OPC UA adding node '%s' alarm with status '%s' ",
            __FILE__,
            __FUNCTION__,
            label,
            state ? "true" : "false");
        ua_server_add_status(label, state);
    }

    // clear
    UA_NodeId_clear(&resNodeId);
}
