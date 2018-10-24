/**
 * Copyright (C) 2016 flexVDI (Flexible Software Solutions S.L.)
 * Copyright (C) 2010 Red Hat, Inc.
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

#include <sys/stat.h>
#include <spice-client.h>
#include "glue-connection.h"

struct _SpiceConnection {
    SpiceSession     *session;
    SpiceMainChannel *main;
    SpiceDisplay     *display;
    SpiceAudio       *audio;
    int              channels;
    int              disconnecting;
    gboolean         enable_sound;
};

G_DEFINE_TYPE(SpiceConnection, spice_connection, G_TYPE_OBJECT);

static void spice_connection_dispose(GObject * obj);

static void spice_connection_class_init(SpiceConnectionClass * class) {
    GObjectClass * object_class = G_OBJECT_CLASS(class);
    object_class->dispose = spice_connection_dispose;
}

static void channel_new(SpiceSession *s, SpiceChannel *channel, gpointer data);
static void channel_destroy(SpiceSession *s, SpiceChannel *channel, gpointer data);

static void spice_connection_init(SpiceConnection * conn) {
    conn->session = spice_session_new();

    g_signal_connect(conn->session, "channel-new",
                     G_CALLBACK(channel_new), conn);
    g_signal_connect(conn->session, "channel-destroy",
                     G_CALLBACK(channel_destroy), conn);
    g_object_ref(conn);
}

static void spice_connection_dispose(GObject * obj) {
    SpiceConnection * conn = SPICE_CONNECTION(obj);
    g_clear_object(&conn->session);
    g_clear_object(&conn->display);
    G_OBJECT_CLASS(spice_connection_parent_class)->dispose(obj);
}

SpiceConnection *spice_connection_new(void)
{
    return SPICE_CONNECTION(g_object_new(SPICE_CONNECTION_TYPE, NULL));
}

static void main_channel_event(SpiceChannel *channel, SpiceChannelEvent event,
                               gpointer data)
{
    SpiceConnection *conn = SPICE_CONNECTION(data);

    switch (event) {
    case SPICE_CHANNEL_OPENED:
    	g_message("main channel: opened");
        break;
    case SPICE_CHANNEL_SWITCHING:
        g_message("main channel: switching host");
        break;
    case SPICE_CHANNEL_CLOSED:
        /* this event is only sent if the channel was succesfully opened before */
        g_message("main channel: closed");
        spice_connection_disconnect(conn);
        break;
    case SPICE_CHANNEL_ERROR_IO:
        spice_connection_disconnect(conn);
        break;
    case SPICE_CHANNEL_ERROR_TLS:
    case SPICE_CHANNEL_ERROR_LINK:
    case SPICE_CHANNEL_ERROR_CONNECT:
        g_message("main channel: failed to connect");
        spice_connection_disconnect(conn);
        break;
    case SPICE_CHANNEL_ERROR_AUTH:
        g_warning("main channel: auth failure (wrong password?)");
        spice_connection_disconnect(conn);
        break;
    default:
        /* TODO: more sophisticated error handling */
        g_warning("unknown main channel event: %d", event);
        break;
    }
}

static void generic_channel_event(SpiceChannel *channel, SpiceChannelEvent event,
				  gpointer data)
{
    // TODO: Improve this long chain of if. There must be a function for doing this
    char* channel_name= "unknown";
    if (SPICE_IS_MAIN_CHANNEL(channel)) {
	channel_name="main";
    } else if (SPICE_IS_DISPLAY_CHANNEL(channel)) {
	channel_name="display";
    } else if (SPICE_IS_INPUTS_CHANNEL(channel)) {
	channel_name="inputs";
    } else if (SPICE_IS_PLAYBACK_CHANNEL(channel)) {
    	channel_name="audio";
    } else if (SPICE_IS_USBREDIR_CHANNEL(channel)) {
    	channel_name="usbredir";
    } else if (SPICE_IS_PORT_CHANNEL(channel)) {
    	channel_name="port";
    } else if (SPICE_IS_RECORD_CHANNEL (channel)) {
	channel_name="record";
    }

    SpiceConnection *conn = SPICE_CONNECTION(data);

    switch (event) {

    case SPICE_CHANNEL_CLOSED:
        g_warning("%s channel closed", channel_name);
        spice_connection_disconnect(conn);
        break;
    case SPICE_CHANNEL_ERROR_IO:
        spice_connection_disconnect(conn);
        break;
    case SPICE_CHANNEL_ERROR_TLS:
    case SPICE_CHANNEL_ERROR_LINK:
    case SPICE_CHANNEL_ERROR_CONNECT:
        g_message("%s channel: failed to connect", channel_name);
        spice_connection_disconnect(conn);
        break;
    default:
        /* TODO: more sophisticated error handling */
        g_warning("%s channel event: %d", channel_name, event);
        break;
    }
}

static void channel_new(SpiceSession *s, SpiceChannel *channel, gpointer data)
{
    SpiceConnection *conn = data;
    int id;

    g_object_get(channel, "channel-id", &id, NULL);
    conn->channels++;
    SPICE_DEBUG("new channel (#%d)", id);

    if (SPICE_IS_MAIN_CHANNEL(channel)) {
        SPICE_DEBUG("new main channel");
        conn->main = SPICE_MAIN_CHANNEL(channel);
        g_signal_connect(channel, "channel-event",
                         G_CALLBACK(main_channel_event), conn);
    }

    if (SPICE_IS_DISPLAY_CHANNEL(channel)) {
        if (id > 0) {
            g_warning("Only one display channel supported!");
            return;
        }
        if (conn->display != NULL)
            return;
        SPICE_DEBUG("new display channel (#%d)", id);
        conn->display = spice_display_new(conn->session, id);

        g_signal_connect(channel, "channel-event",
                         G_CALLBACK(generic_channel_event), conn);

        spice_channel_connect(channel);
    }

    if (SPICE_IS_INPUTS_CHANNEL(channel)) {
        SPICE_DEBUG("new inputs channel");
        g_signal_connect(channel, "channel-event",
                         G_CALLBACK(generic_channel_event), conn);
    }

    if (conn->enable_sound && SPICE_IS_PLAYBACK_CHANNEL(channel)) {
        SPICE_DEBUG("new audio channel");
        conn->audio = spice_audio_get(s, NULL);
        g_signal_connect(channel, "channel-event",
                         G_CALLBACK(generic_channel_event), conn);
    }

    if (SPICE_IS_PORT_CHANNEL(channel)) {
        spice_channel_connect(channel);
    }
}

static void channel_destroy(SpiceSession *s, SpiceChannel *channel, gpointer data)
{
    SPICE_DEBUG("channel_destroy called");

    SpiceConnection *conn = data;
    int id;

    g_object_get(channel, "channel-id", &id, NULL);
    if (SPICE_IS_MAIN_CHANNEL(channel)) {
        SPICE_DEBUG("zap main channel");
        conn->main = NULL;
    }

    if (SPICE_IS_DISPLAY_CHANNEL(channel)) {
        if (id > 0)
            return;
        if (conn->display == NULL)
            return;
        SPICE_DEBUG("zap display channel (#%d)", id);
        g_clear_object(&conn->display);
    }

    if (conn->enable_sound && SPICE_IS_PLAYBACK_CHANNEL(channel)) {
        SPICE_DEBUG("zap audio channel");
    }

    conn->channels--;
    if (conn->channels > 0) {
        SPICE_DEBUG("Number of channels: %d", conn->channels);
        return;
    }

    g_object_unref(conn);
}

void spice_connection_connect(SpiceConnection *conn)
{
    conn->disconnecting = FALSE;
    spice_session_connect(conn->session);
}

void spice_connection_disconnect(SpiceConnection *conn)
{
    if (conn->disconnecting)
        return;
    conn->disconnecting = TRUE;
    spice_session_disconnect(conn->session);
}

/* Saver config parameters to session Object*/
void spice_connection_setup(SpiceConnection *conn, const char *host,
			 const char *port,
			 const char *tls_port,
			 const char *ws_port,
			 const char *password,
			 const char *ca_file,
			 const char *cert_subj,
             gboolean enable_sound) {

    SPICE_DEBUG("spice_session_setup host=%s, ws_port=%s, port=%s, tls_port=%s", host, ws_port, port, tls_port);
    g_return_if_fail(SPICE_IS_SESSION(conn->session));

    if (host)
        g_object_set(conn->session, "host", host, NULL);
    // If we receive "-1" for a port, we assume the port is not set.
    if (port && strcmp (port, "-1") != 0)
        g_object_set(conn->session, "port", port, NULL);
    if (tls_port && strcmp (tls_port, "-1") != 0)
        g_object_set(conn->session, "tls-port", tls_port, NULL);
    if (ws_port && strcmp (ws_port, "-1") != 0)
        g_object_set(conn->session, "ws-port", ws_port, NULL);
    if (password)
        g_object_set(conn->session, "password", password, NULL);
    if (ca_file)
        g_object_set(conn->session, "ca-file", ca_file, NULL);
    if (cert_subj)
        g_object_set(conn->session, "cert-subject", cert_subj, NULL);
    conn->enable_sound = enable_sound;
}

SpiceDisplay *spice_connection_get_display(SpiceConnection *conn)
{
    return conn->display;
}

int spice_connection_get_num_channels(SpiceConnection *conn)
{
    return conn->channels;
}

void spice_connection_power_event_request(SpiceConnection *conn, int powerEvent)
{
    if (conn->main != NULL)
        spice_main_power_event_request(conn->main, powerEvent);
}