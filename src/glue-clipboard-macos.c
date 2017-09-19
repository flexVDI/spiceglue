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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>

#include "glue-service.h"
#include "glib.h"
#include "glue-spice-widget.h"
#include "glue-spice-widget-priv.h"
#include "glue-clipboard.h"

gboolean enableClipboardToGuest = FALSE;
gboolean enableClipboardToClient = FALSE;
uint32_t *guestClipboard = NULL;
uint32_t *hostClipboard = NULL;
/*  
 *   Clipboard sharing between client and guest, using spice client library
 *   and windows native API. No GTK or any other GUI framework.
 *
 *   Only utf-8 text implemented (although library supports images)
 *   ClipboardMain shared (but not primary clipboard, as windows has only the main one).
 */


/* Array with the type of data we are interested in sharing */
guint32 clipboardTypes [1] = {VD_AGENT_CLIPBOARD_UTF8_TEXT};
int ntypes = 1;

#define CB_SIZE 512 * 1024
#define CB_OWNER_NONE 0
#define CB_OWNER_GUEST 1
#define CB_OWNER_HOST 2
int clipboardOwner = CB_OWNER_NONE;
int pendingGuestData = 0;

/* Values for comunication beween signal receiving thread 
 * and windows message receiving thread 
 */
gpointer current_data = NULL;
GMutex data_mutex;

//If more clipboard formats are to be supported, use CBData
typedef struct
{
    gpointer selection_data;
    guint selection_size;
    guint type;
} CBData;

/* Allocates new memory for current_data, which will need to be freed later*/
void
push_clipboard_data (const guchar *data, guint size)
{
  g_mutex_lock (&data_mutex);
  SPICE_DEBUG("CB: data_mutex locked in push.\n");

  if (size > CB_SIZE) {
      size = CB_SIZE;
  }

  if (guestClipboard != NULL) {
      snprintf((char *) guestClipboard, size, "%s", data);
  }

  pendingGuestData = 1;

  g_mutex_unlock (&data_mutex);
  SPICE_DEBUG("CB: guestClipboard contains \"%s\"\n", guestClipboard); 
  SPICE_DEBUG("CB: data_mutex UNlocked in push.\n");
}

static gboolean grab_guest_clipboard(gpointer data)
{
    SpiceDisplay *display;
    SpiceDisplayPrivate *d;
    
    display = global_display;
    if (global_display == NULL) {
        return FALSE;
    }

    d = SPICE_DISPLAY_GET_PRIVATE(display);
    if (d->main == NULL) {
        return FALSE;
    }
   
    /* Grab the guest clipboard, with just one type (text) */
    spice_main_clipboard_selection_grab(d->main, 
        VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD,
        clipboardTypes, ntypes);
    clipboardOwner = CB_OWNER_HOST;

    return FALSE;
}

int SpiceGlibGlue_GrabGuestClipboard()
{
    SPICE_DEBUG("CB: GrabGuestClipboard grabbing %d", VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD);
    
    if (!enableClipboardToGuest) {
        SPICE_DEBUG("CB: enableClipboardToGuest set to false. Doing nothing.");
        return 0;
    }

    g_idle_add(grab_guest_clipboard, NULL);

    return 0;
}

static gboolean release_guest_clipboard(gpointer data)
{
    SpiceDisplay *display;
    SpiceDisplayPrivate *d;
    
    display = global_display;
    if (global_display == NULL) {
        return FALSE;
    }

    d = SPICE_DISPLAY_GET_PRIVATE(display);
    if (d->main == NULL) {
        return FALSE;
    }

    spice_main_clipboard_selection_release(d->main, VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD);
    clipboardOwner = CB_OWNER_NONE;

    return FALSE;
}

int SpiceGlibGlue_ReleaseGuestClipboard()
{
    SPICE_DEBUG("CB: ReleaseGuestClipboard");
    if (!enableClipboardToGuest) {
        SPICE_DEBUG("CB: enableClipboardToGuest set to false. Doing nothing.");
        return 0;
    }

    g_idle_add(release_guest_clipboard, NULL);

    return 0;
}

static gboolean clipboard_get_data(gpointer data)
{
    SpiceDisplay *display;
    SpiceDisplayPrivate *d;

    display = global_display;
    if (global_display == NULL) {
        return FALSE;
    }

    d = SPICE_DISPLAY_GET_PRIVATE(display);
    if (d->main == NULL) {
        return FALSE;
    }

    spice_main_clipboard_selection_request(d->main, 
        VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD,  
        VD_AGENT_CLIPBOARD_UTF8_TEXT);

    return FALSE;
}

int SpiceGlibGlue_ClipboardGetData()
{
    SPICE_DEBUG("CB: ClipboardGetData");

    if (clipboardOwner != CB_OWNER_GUEST) {
        SPICE_DEBUG("CB: Guest has not grabbed CB, returning false");
        return 0;
    }

    g_idle_add(clipboard_get_data, NULL);
    
    return 1;
}

int SpiceGlibGlue_ClipboardDataAvailable()
{
    SPICE_DEBUG("CB: ClipboardDataAvailable");

    g_mutex_lock (&data_mutex);
    if (pendingGuestData == 1) {
        pendingGuestData = 0;
        g_mutex_unlock (&data_mutex);
        return 1;
    }
    g_mutex_unlock (&data_mutex);
    return 0;
}

gboolean clipboard_requestFromGuest(SpiceMainChannel *main, guint selection,
                                  guint type, gpointer user_data)
{                                 
    SPICE_DEBUG("CB: clipboard_requestFromGuest()");
    if (!enableClipboardToGuest) {
        SPICE_DEBUG("CB: enableClipboardToGuest set to false. Doing nothing.");
        return TRUE;
    }

    SpiceDisplay *display;
    SpiceDisplayPrivate *d;
    
    if (clipboardOwner != CB_OWNER_HOST) {
        SPICE_DEBUG("We do NOT have clipboard grabbed, so we won't send it.");
        return FALSE;
    }
    
    display = global_display;
    if (global_display == NULL) {
        return FALSE;
    }

    d = SPICE_DISPLAY_GET_PRIVATE(display);
    if (d->main == NULL) {
        return FALSE;
    }

    //TODO check values as spice-gtk-session.
    gchar *data = (gchar *) hostClipboard;
    SPICE_DEBUG("CB: hostClipboard 0x%x\n", hostClipboard);

    if (hostClipboard != NULL) {
        SPICE_DEBUG("CB: hostClipboard contains %s\n", hostClipboard);
    }

    if (data == NULL ) {
        SPICE_DEBUG("CB: No supported Clipboard format available\n");
        return FALSE;
    }
        
    /* Transform format (line ending, etc) before sending */ 
    gint len = 0;
    gpointer conv = NULL;
    
    /* Our clipboard is not GTK but windows. So we positively have CRLF as line break.
       But it is our (client program) responsability to give the guest the format it wants.
    */
    if (spice_main_agent_test_capability(d->main, VD_AGENT_CAP_GUEST_LINEEND_CRLF)) {
        SPICE_DEBUG("CB: Host to Guest, changing line ending\n");
        GError *err = NULL;
        
        len = strnlen(data, CB_SIZE);
        conv = spice_unix2dos((gchar*)data, len, &err); 
        if (err) {
            SPICE_DEBUG("CB: Failed to conver text line ending: %s\n", err->message);
            g_warning("Failed to convert text line ending: %s", err->message);
            g_clear_error(&err);
            goto onError;
        }
        SPICE_DEBUG("CB: Host to Guest, changing line ending: OK\n");

        len = strnlen(conv, CB_SIZE);
    } else {
        len = strnlen((const char *)data, CB_SIZE);
    }

    spice_main_clipboard_selection_notify(d->main, 
            VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD, 
            VD_AGENT_CLIPBOARD_UTF8_TEXT,
            conv ? conv : data, len);
onError:
    g_free(conv);
    return FALSE;
}

void clipboard_got_from_guest(SpiceMainChannel *main, guint selection,
                                     guint type, const guchar *data, guint size,
                                     gpointer user_data)
{
    SPICE_DEBUG("CB: clipboard_got_data  type : %d ", type);
    if (!enableClipboardToClient) {
        SPICE_DEBUG("CB: enableClipboardToClient set to false. Doing nothing.");
        return;
    }

    if (type == VD_AGENT_CLIPBOARD_NONE) {
        SPICE_DEBUG("CB: No data. Received type VD_AGENT_CLIPBOARD_NONE : size %d", size);
        return;
    }
    
    if (type == VD_AGENT_CLIPBOARD_UTF8_TEXT) {
        push_clipboard_data (data, size + 1);
    } else {
        g_warning("CB: Ignoring clipboard of unexpected type %d from guest", type);
    }
}

gboolean clipboard_grabByGuest(SpiceMainChannel *main, guint selection,
                               guint32* types, guint32 num_types,
                               gpointer user_data) {

    gint i;
    
    SPICE_DEBUG("CB: clipboard_grabByGuest(sel %d)", selection);
    if (!enableClipboardToClient) {
        SPICE_DEBUG("CB: enableClipboardToClient set to false. Doing nothing.");
        return TRUE;
    }
        
    if (selection != VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD) {
        g_warning("CB: discarded clipboard request of unsupported selection %d",selection);
        return FALSE;    
    }
    
    for (i = 0; i < num_types; i++) {
        SPICE_DEBUG("CB: checking type(%d)",types[i]);
        if (types[i] == VD_AGENT_CLIPBOARD_UTF8_TEXT){
        
            SPICE_DEBUG("CB: IT IS UTF8");

            clipboardOwner = CB_OWNER_GUEST;
        }
    }

    return TRUE;
}

gboolean clipboard_releaseByGuest(SpiceMainChannel *main, guint selection,
    guint32* types, guint32 num_types,
    gpointer user_data) {

    SPICE_DEBUG("CB: clipboard_releaseByGuest(sel %d) not implemented in this platform", selection);

}

gboolean SpiceGlibGlue_InitClipboard(
        int16_t enableClipboardToGuestP, int16_t enableClipboardToClientP,
        uint32_t *guestClipboardP, uint32_t *hostClipboardP)
{
    SPICE_DEBUG("CB SpiceGlibGlue_InitClipboard (%d, %d)", 
            enableClipboardToGuestP, enableClipboardToClientP);
    enableClipboardToGuest  = enableClipboardToGuestP;
    enableClipboardToClient = enableClipboardToClientP;

    guestClipboard = guestClipboardP;
    hostClipboard = hostClipboardP;
    SPICE_DEBUG("CB: guestClipboard 0x%x\n", guestClipboard);
    SPICE_DEBUG("CB: hostClipboard 0x%x\n", hostClipboard);
    
    return FALSE;    
}
