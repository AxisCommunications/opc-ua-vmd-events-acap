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

#include <axevent.h>
#include <axparameter.h>
#include <libgen.h>
#include <pthread.h>

#include "opcua_axevents.h"
#include "opcua_common.h"
#include "opcua_open62541.h"

static AXEventHandler *ehandler;
static AXParameter *axparameter = NULL;
static gboolean ehandler_running = false;
static guint uaport = 0;
static pthread_t ua_server_thread_id;
static UA_Boolean ua_server_running = false;
static UA_Server *uaserver = NULL;

static void open_syslog(const char *app_name)
{
    openlog(app_name, LOG_PID, LOG_LOCAL4);
}

static void close_syslog(void)
{
    LOG_I("Exiting!");
}

static gboolean launch_ua_server(const guint serverport)
{
    assert(NULL == uaserver);
    assert(1024 <= serverport && 65535 >= serverport);
    assert(!ua_server_running);

    // Create an OPC UA server
    LOG_I("%s/%s: Create UA server with port %u", __FILE__, __FUNCTION__, serverport);
    ua_server_init(serverport);

    ua_server_running = true;
    LOG_I("%s/%s: Starting UA server ...", __FILE__, __FUNCTION__);
    if (!ua_server_start(&ua_server_thread_id, &ua_server_running))
    {
        LOG_E("%s/%s: Failed to start UA server", __FILE__, __FUNCTION__);
        return FALSE;
    }

    return TRUE;
}

static void shutdown_ua_server(void)
{
    assert(ua_server_running);
    assert(NULL != uaserver);

    ua_server_running = false;
    LOG_I("%s/%s: Stop UA server ...", __FILE__, __FUNCTION__);
    pthread_join(ua_server_thread_id, NULL);
    LOG_I("%s/%s: Delete UA server ...", __FILE__, __FUNCTION__);
    UA_Server_run_shutdown(uaserver);
    UA_Server_delete(uaserver);
    uaserver = NULL;
}

static void port_callback(const gchar *name, const gchar *value, void *data)
{
    (void)data;

    /* Translate parameter value to number; atoi can handle NULL */
    int newport = atoi(value);
    if (1024 > newport || 65535 < newport)
    {
        LOG_E("%s/%s: Axparam illegal value for %s: '%s'", __FILE__, __FUNCTION__, name, value);
        return;
    }

    if (uaport == newport)
    {
        LOG_I("%s/%s: Axparam callback - no port change %s: '%s'", __FILE__, __FUNCTION__, name, value);
        return;
    }

    uaport = newport;
    LOG_I("%s/%s: Axparam '%s' is %u", __FILE__, __FUNCTION__, name, uaport);

    if (ua_server_running)
    {
        shutdown_ua_server();
    }

    (void)launch_ua_server(uaport);
}

static void evtsource_callback(const gchar *name, const gchar *value, void *data)
{
    (void)data;
    assert(NULL != ehandler);
    assert(NULL != value);

    if (ehandler_running)
    {
        axevent_teardown(ehandler);
    }

    LOG_I("%s/%s: Setting up axevent monitor for '%s'...", __FILE__, __FUNCTION__, value);
    if (!axevent_setup(ehandler, value))
    {
        LOG_E("%s/%s: Failed to setup axevent subscription", __FILE__, __FUNCTION__);
    }

    ehandler_running = TRUE;
}

static gboolean setup_param(const gchar *name, AXParameterCallback callbackfn)
{
    GError *error = NULL;
    gchar *value = NULL;

    assert(NULL != name);
    assert(NULL != axparameter);
    assert(NULL != callbackfn);

    if (!ax_parameter_register_callback(axparameter, name, callbackfn, NULL, &error))
    {
        LOG_E("%s/%s: failed to register %s callback", __FILE__, __FUNCTION__, name);
        if (NULL != error)
        {
            LOG_E("%s/%s: %s", __FILE__, __FUNCTION__, error->message);
            g_error_free(error);
        }
        return FALSE;
    }

    if (!ax_parameter_get(axparameter, name, &value, &error))
    {
        LOG_E("%s/%s: failed to get %s parameter", __FILE__, __FUNCTION__, name);
        if (NULL != error)
        {
            LOG_E("%s/%s: %s", __FILE__, __FUNCTION__, error->message);
            g_error_free(error);
        }
        return FALSE;
    }

    callbackfn(name, value, NULL);
    g_free(value);

    return TRUE;
}

static gboolean setup_params(const char *appname)
{
    GError *error = NULL;

    assert(NULL != appname);
    assert(NULL == axparameter);
    axparameter = ax_parameter_new(appname, &error);
    if (NULL != error)
    {
        LOG_E("%s/%s: ax_parameter_new failed (%s)", __FILE__, __FUNCTION__, error->message);
        g_error_free(error);
        return FALSE;
    }

    if (!setup_param("port", port_callback))
    {
        ax_parameter_free(axparameter);
        return FALSE;
    }

    if (!setup_param("eventsource", evtsource_callback))
    {
        ax_parameter_free(axparameter);
        return FALSE;
    }

    return TRUE;
}

static void init_signals(void)
{
    struct sigaction sa;

    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGPIPE);
    sa.sa_flags = 0;
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);
}

int main(int argc, char **argv)
{
    int ret = 0;

    init_signals();

    char *app_name = basename(argv[0]);
    open_syslog(app_name);

    // Main loop
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);

    // Axevent handler
    ehandler = ax_event_handler_new();
    if (NULL == ehandler)
    {
        LOG_E("%s/%s: Failed to setup axevent handler", __FILE__, __FUNCTION__);
        ret = 1;
        goto exit_ehandler;
    }

    /*
     * Setup parameters
     * will also launch OPC UA server and setup AxEvent Subscription
     */
    LOG_I("%s/%s: Setup axparameters", __FILE__, __FUNCTION__);
    if (!setup_params(app_name))
    {
        LOG_E("%s/%s: Failed to setup axparameters", __FILE__, __FUNCTION__);
        ret = 1;
        goto exit_param;
    }

    // Ready
    LOG_I("%s/%s: Ready", __FILE__, __FUNCTION__);
    g_main_loop_run(loop);

    /*
     * Cleanup and controlled shutdown
     */
exit_param:
    LOG_I("%s/%s: Free axparameter handler ...", __FILE__, __FUNCTION__);
    ax_parameter_free(axparameter);
exit_ehandler:
    LOG_I("%s/%s: Unsubscribe from axevents ...", __FILE__, __FUNCTION__);
    axevent_teardown(ehandler);
    ax_event_handler_free(ehandler);

    LOG_I("%s/%s: Shut down UA server ...", __FILE__, __FUNCTION__);
    shutdown_ua_server();

    LOG_I("%s/%s: Unreference main loop ...", __FILE__, __FUNCTION__);
    g_main_loop_unref(loop);

    LOG_I("%s/%s: Closing syslog ...", __FILE__, __FUNCTION__);
    close_syslog();

    return ret;
}
