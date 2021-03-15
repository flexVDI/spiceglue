/**
 * Copyright (C) 2016 flexVDI (Flexible Software Solutions S.L.)
 * Copyright (C) 2013 Iordan Iordanov
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#define GLUE_SERVICE_C

#include <stdbool.h>

#ifdef ANDROID
#include <android/log.h>
#else
#include <stdlib.h>
#include <stdio.h>
#endif

#include "glue-service.h"

#include "glue-spice-widget.h"
#include "glue-connection.h"

#include "glib.h"
#if defined(PRINTING) || defined(SSO)
#include "flexvdi-port.h"
#endif
#ifdef PRINTING
#include "glue-printing.h"
#endif
#ifdef SSO
#include "glue-sso.h"
#endif

#ifdef USBREDIR
#include "usb-glue.h"
#endif

static int32_t logVerbosity;
void (*log_callback)(int8_t *);

#ifdef ANDROID
void androidLog (const gchar *log_domain, GLogLevelFlags log_level,
		 const gchar *message, gpointer user_data) {
    android_LogPriority p = ANDROID_LOG_SILENT;
    if (log_level & G_LOG_LEVEL_ERROR) {
        p = ANDROID_LOG_ERROR;
    } else if (log_level & G_LOG_LEVEL_CRITICAL) {
        p = ANDROID_LOG_ERROR;
    } else if (log_level & G_LOG_LEVEL_WARNING) {
        p = ANDROID_LOG_WARN;
    } else if (log_level & G_LOG_LEVEL_MESSAGE) {
        p = ANDROID_LOG_INFO;
    } else if (log_level & G_LOG_LEVEL_INFO) {
        p = ANDROID_LOG_INFO;
    } else if (log_level & G_LOG_LEVEL_DEBUG) {
        p = ANDROID_LOG_DEBUG;
    }
    __android_log_print(p, "spice-glue", "%s", message);
}
#else

#include <stdlib.h>
static FILE *logfile = NULL;

void logToFile (const gchar *log_domain, GLogLevelFlags log_level,
		const gchar *message, gpointer user_data)
{
    GDateTime *now;
    gchar *dateTimeStr;

    if (logfile == NULL) {
        const gchar *basePath = g_getenv("FLEXVDICLIENT_LOGDIR");
        gchar path[4096];

        sprintf(path, "%sflexVDIClient-lib.log", basePath);
        logfile = fopen (path, "a");

        if (logfile == NULL) {
            fprintf (stderr, "Rerouted to console: %s\n", message);
            return;
        }
    }
    char* levelStr = "UNKNOWN";
    if (log_level & G_LOG_LEVEL_ERROR) {
        levelStr = "ERROR";
    } else if (log_level & G_LOG_LEVEL_CRITICAL) {
        levelStr = "CRITICAL";
    } else if (log_level & G_LOG_LEVEL_WARNING) {
        levelStr = "WARNING";
    } else if (log_level & G_LOG_LEVEL_MESSAGE) {
        levelStr = "MESSAGE";
    } else if (log_level & G_LOG_LEVEL_INFO) {
        levelStr = "INFO";
    } else if (log_level & G_LOG_LEVEL_DEBUG) {
        levelStr = "DEBUG";
    }

    now = g_date_time_new_now_local();
    dateTimeStr = g_date_time_format(now, "%Y-%m-%d %T");

    fprintf (logfile, "%s,%03d %s %s-%s\n", dateTimeStr,
	     g_date_time_get_microsecond(now) / 1000, levelStr,
         log_domain, message);

    g_date_time_unref(now);
    g_free(dateTimeStr);
    fflush(logfile);
}
#endif

void logHandler (const gchar *log_domain, GLogLevelFlags log_level,
		const gchar *message, gpointer user_data)
{
    gboolean doLog = FALSE;
    gboolean isNopoll;

    if (logVerbosity >= 0 &&
        log_level & G_LOG_LEVEL_ERROR
    ) {
        doLog = TRUE;
    }
    isNopoll = (log_domain && strcmp("nopoll", log_domain) == 0);
    if (logVerbosity >= 1
        && (log_level & G_LOG_LEVEL_CRITICAL
            || log_level & G_LOG_LEVEL_WARNING)
        && !isNopoll
    ) {
        doLog = TRUE;
    }
    if (logVerbosity >= 2
        && log_level & G_LOG_LEVEL_MESSAGE
        && !isNopoll
    ) {
        doLog = TRUE;
    }
    if (logVerbosity >= 3 && !isNopoll ) {
        doLog = TRUE;
    }
    if (logVerbosity == 4) {
        doLog = TRUE;
    }

    if (doLog) {
#ifdef ANDROID
        androidLog(log_domain, log_level, message, user_data);
#else
        logToFile(log_domain, log_level, message, user_data);
#endif
    }
    if (log_callback != NULL) {
        log_callback((int8_t *)message);
    }
}

void SpiceGlibGlue_InitializeLogging(int32_t verbosityLevel)
{
    SPICE_DEBUG("SpiceGlibGlue_InitializeLogging() ini");
    logVerbosity = verbosityLevel;

    if (verbosityLevel >= 3) {
        spice_util_set_debug(TRUE);
    }
    g_log_set_default_handler (logHandler, NULL);
    SPICE_DEBUG("Logging initialized.");
}

static SpiceConnection *mainconn = NULL;

SpiceDisplay* global_display() {
    return mainconn != NULL ? spice_connection_get_display(mainconn) : NULL;
}

void SpiceGlibGlue_MainLoop(void)
{
    GMainLoop *mainloop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(mainloop);
    g_main_loop_unref(mainloop);
#if defined(PRINTING) || defined(SSO)
    flexvdi_cleanup();
#endif
}

static gboolean disconnect1()
{
    SPICE_DEBUG("SpiceGlibGlue_Disconnect\n");
    spice_connection_disconnect(mainconn);
    g_clear_object(&mainconn);
    return FALSE;
}

void SpiceGlibGlue_Disconnect(void)
{
    g_timeout_add_full(G_PRIORITY_HIGH, 0,
                       disconnect1,
                       NULL, NULL);
}

int16_t SpiceGlibGlue_Connect(char* host,
			      char* port, char* tls_port, char* ws_port,
			      char* password,
			      char* ca_file, char* cert_subj,
			      int32_t enable_sound)
{
    int result = 0;

    SPICE_DEBUG("SpiceClientConnect session_setup");

    mainconn = spice_connection_new();
    spice_connection_setup(mainconn, host,
			port, tls_port, ws_port,
			password,
			ca_file, cert_subj, enable_sound);

#if defined(PRINTING) || defined(SSO)
    flexvdi_port_register_session(mainconn->session);
#endif
#if defined(PRINTING)
    onConnectGuestFollowMePrinting();
#endif

    SPICE_DEBUG("SpiceClientConnect connection_connect");

    spice_connection_connect(mainconn);
#if defined(USBREDIR)
	usb_glue_register_session(mainconn->session);
#endif
    SPICE_DEBUG("SpiceClientConnect exit");

    return result;
}

int16_t SpiceGlibGlue_isConnected() {
    return (mainconn != NULL && spice_connection_get_num_channels(mainconn) > 3);
}

int16_t SpiceGlibGlue_getNumberOfChannels() {
    if (mainconn == NULL) {
        return 0;
    } else {
        return spice_connection_get_num_channels(mainconn);
    }
}

void SpiceGlibGlueInitializeGlue()
{
#ifdef PRINTING
    initializeFollowMePrinting();
#endif
#ifdef SSO
    initializeSSO();
#endif
}


void SpiceGlibGlueSetDisplayBuffer(uint32_t *display_buffer,
				   int32_t width, int32_t height)
{
    SPICE_DEBUG("SpiceGlibGlueSetDisplayBuffer");
    spice_display_set_display_buffer(display_buffer, width, height);
}

/**
 * Params: width, height
 *  IN:
 *  OUT:
 * Returns true if current buffer has changed and has not been copied, since
 * the last call to SpiceGlibGlueLockDisplayBuffer (not to this function), FALSE otherwise.
 **/
int16_t SpiceGlibGlueIsDisplayBufferUpdated(int32_t width, int32_t height)
{
    return global_display() != NULL && spice_display_is_display_buffer_updated(global_display(), width, height);
}

/**
 * Locks the glue_display_buffer, so that we can safely call
 * SpiceGlibGlueSetDisplayBuffer()
 * Params: *width, *height
 *  IN: don't care
 *  OUT: size of display used by the spice-client-lib: Real guest display, and what the
 * Returns true if current buffer has changed and has not been copied, since
 * the last call to SpiceGlibGlueLockDisplayBuffer, FALSE otherwise.
 **/
int16_t SpiceGlibGlueLockDisplayBuffer(int32_t *width, int32_t *height)
{
    return spice_display_lock_display_buffer(width, height);
}

void SpiceGlibGlueUnlockDisplayBuffer()
{
    spice_display_unlock_display_buffer();
}

int16_t SpiceGlibGlueGetCursorPosition(int32_t* x, int32_t* y)
{
    if (global_display() == NULL) {
	    return -1;
    } else return spice_display_get_cursor_position(global_display(), x, y);
}

int32_t SpiceGlibGlue_SpiceKeyEvent(int16_t isDown, int32_t hardware_keycode)
{
    if (global_display() == NULL) {
        return -1;
    } else return spice_display_key_event(global_display(), isDown, hardware_keycode);
}

/* GSourcefunc */
static gboolean sendPowerEvent1(int16_t powerEvent)
{
    if (mainconn != NULL)
        spice_connection_power_event_request(mainconn, powerEvent);
    return FALSE;
}

/**
 * Sends a power event to the machine connected by SPICE.
 * Params:
 *  IN: powerEvent. One of the values of SpicePowerEvent defined
 *     in spice-protocol enums.h
 **/
void SpiceGlibGlue_SendPowerEvent(int16_t powerEvent) {
    g_timeout_add_full(G_PRIORITY_HIGH, 0,
                       (GSourceFunc)sendPowerEvent1,
                       (gpointer)powerEvent, NULL);
}

void SpiceGlibGlue_SetLogCallback(void (*cl_log_callback)(int8_t *)) {
    SPICE_DEBUG("SpiceGlibGlue_SetLogCallback()");
    log_callback = cl_log_callback;
}

void SpiceGlibGlue_SetBufferResizeCallback(void (*buffer_resize_callback)(int, int, int)) {
    SPICE_DEBUG("SpiceGlibGlue_SetBufferResizeCallback");
    if (mainconn != NULL)
        spice_connection_set_buffer_resize_callback(mainconn, buffer_resize_callback);
}

void SpiceGlibGlue_SetBufferUpdateCallback(void (*buffer_update_callback)(int, int, int, int)) {
    SPICE_DEBUG("SpiceGlibGlue_SetBufferUpdateCallback");
    if (mainconn != NULL)
        spice_connection_set_buffer_update_callback(mainconn, buffer_update_callback);
}

void SpiceGlibGlue_SetBufferDisconnectCallback(void (*disconnect_callback)(void)) {
    SPICE_DEBUG("SpiceGlibGlue_SetBufferDisconnectCallback");
    if (mainconn != NULL)
        spice_connection_set_disconnect_callback(mainconn, disconnect_callback);
}
