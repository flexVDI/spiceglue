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

#ifndef _ANDROID_SPICY_H
#define _ANDROID_SPICY_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <glib-object.h>
#include "glue-spice-widget.h"

#define SPICE_CONNECTION_TYPE (spice_connection_get_type())
G_DECLARE_FINAL_TYPE(SpiceConnection, spice_connection, SPICE, CONNECTION, GObject)

void spice_connection_setup(SpiceConnection *conn, const char *host,
			 const char *port,
			 const char *tls_port,
			 const char *ws_port,
			 const char *password,
			 const char *ca_file,
			 const char *cert_subj,
             gboolean enable_sound);

SpiceConnection *spice_connection_new(void);
void spice_connection_connect(SpiceConnection *conn);
void spice_connection_disconnect(SpiceConnection *conn);
SpiceDisplay *spice_connection_get_display(SpiceConnection *conn);
int spice_connection_get_num_channels(SpiceConnection *conn);
void spice_connection_power_event_request(SpiceConnection *conn, int powerEvent);
void spice_connection_set_buffer_resize_callback(SpiceConnection *conn,
             void (*buffer_resize_callback)(int, int, int));
void spice_connection_set_buffer_update_callback(SpiceConnection *conn,
             void (*buffer_update_callback)(int, int, int, int));
void spice_connection_set_disconnect_callback(SpiceConnection *conn,
             void (*disconnect_callback)(void));

#endif /* _ANDROID_SPICY_H */
