/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/**
 * Copyright (C) 2016 flexVDI (Flexible Software Solutions S.L.)
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
 
/*
 * Functions to use clipboard sharing in spice client programs.
 */

#ifdef USE_CLIPBOARD
#ifndef _GLUE_CLIPBOARD_H
#define _GLUE_CLIPBOARD_H

#include <spice-gtk/spice-util.h>
#include <spice/vd_agent.h>

/* Callback executed when the vdagent in the guest requests the 
   clipboard content to the client.
 */
gboolean clipboard_requestFromGuest(SpiceMainChannel *main, guint selection,
                                  guint type, gpointer user_data);

gboolean clipboard_grabByGuest(SpiceMainChannel *main, guint selection,
                               guint32* types, guint32 ntypes,
                               gpointer user_data);

gboolean clipboard_releaseByGuest(SpiceMainChannel *main, guint selection,
                               guint32* types, guint32 num_types,
                               gpointer user_data);

void clipboard_got_from_guest(SpiceMainChannel *main, guint selection,
                                     guint type, const guchar *data, guint size,
                                     gpointer user_data);

#endif /* _GLUE_CLIPBOARD_H */
#endif
