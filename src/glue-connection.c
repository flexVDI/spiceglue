/**
 * Copyright (C) 2016 flexVDI (Flexible Software Solutions S.L.)
 * Copyright (C) 2013 Iordan Iordanov
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
#include "virt-viewer-file.h"

struct _SpiceConnection {
    GObject          parent;
    SpiceSession     *session;
    SpiceMainChannel *main;
    SpiceDisplay     *display;
    SpiceAudio       *audio;
    int              channels;
    int              disconnecting;
    gboolean         enable_sound;
    void (*buffer_resize_callback)(int, int, int);
    void (*buffer_update_callback)(int, int, int, int);
    void (*disconnect_callback)(void);
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
    SPICE_DEBUG("Initializing connection %p", conn);
    conn->session = spice_session_new();

    g_signal_connect(conn->session, "channel-new",
                     G_CALLBACK(channel_new), conn);
    g_signal_connect(conn->session, "channel-destroy",
                     G_CALLBACK(channel_destroy), conn);
    g_object_ref(conn);
}

static void spice_connection_dispose(GObject * obj) {
    SpiceConnection * conn = SPICE_CONNECTION(obj);
    SPICE_DEBUG("Disposing connection %p", conn);
    g_warn_if_fail(conn->channels > 0);
    g_clear_object(&conn->session);
    g_clear_object(&conn->display);
    G_OBJECT_CLASS(spice_connection_parent_class)->dispose(obj);
}

SpiceConnection *spice_connection_new(void)
{
    return SPICE_CONNECTION(g_object_new(SPICE_CONNECTION_TYPE, NULL));
}

static void channel_event(SpiceChannel *channel, SpiceChannelEvent event,
				  gpointer data)
{
    int channel_type;
    g_object_get(channel, "channel-type", &channel_type, NULL);
    const char* channel_name = spice_channel_type_to_string(channel_type);

    SpiceConnection *conn = SPICE_CONNECTION(data);

    switch (event) {
    case SPICE_CHANNEL_OPENED:
        SPICE_DEBUG("%s channel: opened", channel_name);
        break;
    case SPICE_CHANNEL_SWITCHING:
        SPICE_DEBUG("%s channel: switching host", channel_name);
        break;
    case SPICE_CHANNEL_CLOSED:
        SPICE_DEBUG("%s channel closed", channel_name);
        spice_connection_disconnect(conn);
        break;
    case SPICE_CHANNEL_ERROR_IO:
        g_warning("%s channel: input-output error", channel_name);
        spice_connection_disconnect(conn);
        break;
    case SPICE_CHANNEL_ERROR_TLS:
    case SPICE_CHANNEL_ERROR_LINK:
    case SPICE_CHANNEL_ERROR_CONNECT:
        g_warning("%s channel: failed to connect", channel_name);
        spice_connection_disconnect(conn);
        break;
    case SPICE_CHANNEL_ERROR_AUTH:
        g_warning("%s channel: auth failure (wrong password?)", channel_name);
        spice_connection_disconnect(conn);
        break;
    default:
        /* TODO: more sophisticated error handling */
        g_warning("unknown %s channel event: %d", channel_name, event);
        break;
    }
}

static void channel_new(SpiceSession *s, SpiceChannel *channel, gpointer data)
{
    SpiceConnection *conn = data;
    int id;
    int channel_type;

    g_object_get(channel, "channel-id", &id, "channel-type", &channel_type, NULL);
    const char* channel_name = spice_channel_type_to_string(channel_type);
    conn->channels++;
    SPICE_DEBUG("new %s channel (#%d)", channel_name, id);

    if (SPICE_IS_MAIN_CHANNEL(channel)) {
        conn->main = SPICE_MAIN_CHANNEL(channel);
    }

    else if (SPICE_IS_DISPLAY_CHANNEL(channel)) {
        if (id > 0) {
            g_warning("Only one display channel supported!");
            return;
        }
        if (conn->display != NULL)
            return;
        conn->display = spice_display_new(conn->session, id);
        set_buffer_resize_callback(conn->display, conn->buffer_resize_callback);
        set_buffer_update_callback(conn->display, conn->buffer_update_callback);
    }

    else if (conn->enable_sound && SPICE_IS_PLAYBACK_CHANNEL(channel)) {
        conn->audio = spice_audio_get(s, NULL);
    }

    else if (!SPICE_IS_INPUTS_CHANNEL(channel) &&
             !SPICE_IS_PORT_CHANNEL(channel)) {
        SPICE_DEBUG("Unsupported channel type %s", channel_name);
        return;
    }

    spice_channel_connect(channel);
    g_signal_connect(channel, "channel-event", G_CALLBACK(channel_event), conn);
}

static void channel_destroy(SpiceSession *s, SpiceChannel *channel, gpointer data)
{
    SpiceConnection *conn = data;
    int id;
    int channel_type;

    g_object_get(channel, "channel-id", &id, "channel-type", &channel_type, NULL);
    const char* channel_name = spice_channel_type_to_string(channel_type);
    SPICE_DEBUG("destroy %s channel (#%d)", channel_name, id);

    conn->channels--;
    SPICE_DEBUG("Number of channels remaining: %d", conn->channels);

    g_signal_handlers_disconnect_by_data(channel, data);

    if (SPICE_IS_MAIN_CHANNEL(channel)) {
        conn->main = NULL;
    }

    if (SPICE_IS_DISPLAY_CHANNEL(channel)) {
        if (id > 0)
            return;
        if (conn->display == NULL)
            return;
        g_clear_object(&conn->display);
    }

    if (conn->enable_sound && SPICE_IS_PLAYBACK_CHANNEL(channel)) {
        conn->audio = NULL;
    }

    if (conn->channels > 0) {
        return;
    }

    g_object_unref(conn);
}

void spice_connection_connect(SpiceConnection *conn)
{
    conn->disconnecting = FALSE;
    SPICE_DEBUG("Connect Spice connection %p", conn);
    spice_session_connect(conn->session);
}

void spice_connection_disconnect(SpiceConnection *conn)
{
    if (conn == NULL || conn->disconnecting)
        return;
    conn->disconnecting = TRUE;
    SPICE_DEBUG("Disconnect Spice connection %p", conn);
    spice_session_disconnect(conn->session);
    if (conn->disconnect_callback != NULL)
        conn->disconnect_callback();
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

void spice_session_setup_from_vv(VirtViewerFile *file, SpiceConnection *conn, gboolean enable_audio)
{
    SPICE_DEBUG("spice_session_setup_from_vv");

    g_return_if_fail(VIRT_VIEWER_IS_FILE(file));
    g_return_if_fail(SPICE_IS_SESSION(conn->session));

    if (virt_viewer_file_is_set(file, "host")) {
        gchar *val = virt_viewer_file_get_host(file);
        g_object_set(G_OBJECT(conn->session), "host", val, NULL);
        g_free(val);
    }

    if (virt_viewer_file_is_set(file, "port")) {
        gchar *val = g_strdup_printf("%d", virt_viewer_file_get_port(file));
        g_object_set(G_OBJECT(conn->session), "port", val, NULL);
        g_free(val);
    }
    if (virt_viewer_file_is_set(file, "tls-port")) {
        gchar *val = g_strdup_printf("%d", virt_viewer_file_get_tls_port(file));
        g_object_set(G_OBJECT(conn->session), "tls-port", val, NULL);
        g_free(val);
    }
    if (virt_viewer_file_is_set(file, "password")) {
        gchar *val = virt_viewer_file_get_password(file);
        g_object_set(G_OBJECT(conn->session), "password", val, NULL);
        g_free(val);
    }

    if (virt_viewer_file_is_set(file, "tls-ciphers")) {
        gchar *val = virt_viewer_file_get_tls_ciphers(file);
        g_object_set(G_OBJECT(conn->session), "ciphers", val, NULL);
        g_free(val);
    }

    if (virt_viewer_file_is_set(file, "ca")) {
        gchar *ca = virt_viewer_file_get_ca(file);
        g_return_if_fail(ca != NULL);

        GByteArray *ba = g_byte_array_new_take((guint8 *)ca, strlen(ca) + 1);
        g_object_set(G_OBJECT(conn->session), "ca", ba, NULL);
        g_byte_array_unref(ba);
    }

    if (virt_viewer_file_is_set(file, "host-subject")) {
        gchar *val = virt_viewer_file_get_host_subject(file);
        g_object_set(G_OBJECT(conn->session), "cert-subject", val, NULL);
        g_free(val);
    }

    if (virt_viewer_file_is_set(file, "proxy")) {
        gchar *val = virt_viewer_file_get_proxy(file);
        g_object_set(G_OBJECT(conn->session), "proxy", val, NULL);
        g_free(val);
    }

    if (virt_viewer_file_is_set(file, "enable-smartcard")) {
        g_object_set(G_OBJECT(conn->session),
                     "enable-smartcard", virt_viewer_file_get_enable_smartcard(file), NULL);
    }

    if (virt_viewer_file_is_set(file, "enable-usbredir")) {
        g_object_set(G_OBJECT(conn->session),
                     "enable-usbredir", virt_viewer_file_get_enable_usbredir(file), NULL);
    }

    if (virt_viewer_file_is_set(file, "color-depth")) {
        g_object_set(G_OBJECT(conn->session),
                     "color-depth", virt_viewer_file_get_color_depth(file), NULL);
    }

    if (virt_viewer_file_is_set(file, "disable-effects")) {
        gchar **disabled = virt_viewer_file_get_disable_effects(file, NULL);
        g_object_set(G_OBJECT(conn->session), "disable-effects", disabled, NULL);
        g_strfreev(disabled);
    }

    if (virt_viewer_file_is_set(file, "enable-usb-autoshare")) {
        //gboolean enabled = virt_viewer_file_get_enable_usb_autoshare(file);
        //SpiceGtkSession *gtk = spice_gtk_session_get(conn->session);
        //g_object_set(G_OBJECT(gtk), "auto-usbredir", enabled, NULL);
    }

    if (virt_viewer_file_is_set(file, "secure-channels")) {
        gchar **channels = virt_viewer_file_get_secure_channels(file, NULL);
        g_object_set(G_OBJECT(conn->session), "secure-channels", channels, NULL);
        g_strfreev(channels);
    }

    if (virt_viewer_file_is_set(file, "disable-channels")) {
        //DEBUG_LOG("FIXME: disable-channels is not supported atm");
    }

    g_object_set(conn->session, "enable-audio", enable_audio, NULL);
    conn->enable_sound = enable_audio;
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
#ifndef SPICEGLUE_DISABLE_POWER
    if (conn->main != NULL)
        spice_main_power_event_request(conn->main, powerEvent);
#endif
}

void spice_connection_set_buffer_resize_callback(SpiceConnection *conn,
             void (*buffer_resize_callback)(int, int, int)) {
    SPICE_DEBUG("spice_connection_set_buffer_resize_callback");
    conn->buffer_resize_callback = buffer_resize_callback;
}

void spice_connection_set_buffer_update_callback(SpiceConnection *conn,
             void (*buffer_update_callback)(int, int, int, int)) {
    SPICE_DEBUG("spice_connection_set_buffer_update_callback");
    conn->buffer_update_callback = buffer_update_callback;
}

void spice_connection_set_disconnect_callback(SpiceConnection *conn,
             void (*disconnect_callback)(void)) {
    SPICE_DEBUG("spice_connection_set_disconnect_callback");
    conn->disconnect_callback = disconnect_callback;
}
