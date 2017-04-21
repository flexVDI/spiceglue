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

#ifdef G_OS_WIN32
#include <windows.h>

#define FLEXVDI_CLPBRD_WINCLASS_NAME  TEXT("FLEXVDI_CLPBRD_MSGLOOP_CLIENT")

/* 
 * In Vista and upper, applications can get WM_messages when the clipboard 
 * content changes, subscribing to them with AddClipboardFormatListener().
 * Our current Mingw does not support AddClipboardFormatListener and WM_CLIPBOARDUPDATE,
 * so we get a handle to the function and define the constant. 
 */
typedef BOOL (WINAPI *PFN_AddClipboardFormatListener) (HWND);
PFN_AddClipboardFormatListener addClipboardFormatListener = NULL;

#ifndef WM_CLIPBOARDUPDATE
#define WM_CLIPBOARDUPDATE              0x031D
#endif

static HWND hwnd = NULL; // handle to WM_message receiver window, which manages the clipboard
#endif

gboolean enableClipboardToGuest = FALSE;
gboolean enableClipboardToClient = FALSE;
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
gboolean isClipboardGrabbed = FALSE;

const int max_clipboard = 512*1024;

 /* Flag to know if guest owns the clipboard, and we should ask vdagent for data
  * or not. We don't need to distinguish if clipboard is owned by some other 
  * program in client machine or by nobody, so we don't store that information.
 */
gboolean guestOwnsClipboard = FALSE; 

/* Values for comunication beween signal receiving thread 
 * and windows message receiving thread 
 */
gpointer current_data = NULL;
GMutex data_mutex;
GCond data_cond;

//If more clipboard formats are to be supported, use CBData
typedef struct
{
    gpointer selection_data;
    guint selection_size;
    guint type;
} CBData;

/* Allocates new memory for current_data, which will need to be freed later*/
void
push_clipboard_data (gpointer data) {

  g_mutex_lock (&data_mutex);
  SPICE_DEBUG("CB: data_mutex locked in push.\n");

  if (current_data) {
    g_free(current_data);
    current_data = NULL;
  }
  // Counting on null terminated string, and not any other non null terminated data
  current_data = g_strdup(data);
  g_cond_signal (&data_cond);
  g_mutex_unlock (&data_mutex);
  SPICE_DEBUG("CB: data_mutex UNlocked in push.\n");
}

gpointer pop_clipboard_data_timed (void) {

  gint64 end_time;
  gpointer data;

  g_mutex_lock (&data_mutex);
  SPICE_DEBUG("CB: data_mutex locked in pop.\n");

    // max wait 10 seconds
  end_time = g_get_monotonic_time () + 10 * G_TIME_SPAN_SECOND;
  while (!current_data)
    if (!g_cond_wait_until (&data_cond, &data_mutex, end_time))
      {
        // timeout has passed.
		SPICE_DEBUG("CB: Timeout has passed.\n");
        g_mutex_unlock (&data_mutex);
		SPICE_DEBUG("CB: data_mutex UNlocked in pop (after timeout).\n");
        return NULL;
      }

  // There are data for us
  data = current_data;
  current_data = NULL;

  g_mutex_unlock (&data_mutex);
  SPICE_DEBUG("CB: data_mutex UNlocked in pop (after signal).\n");

  return data;
}

int SpiceGlibGlue_GrabGuestClipboard() {

    SPICE_DEBUG("CB: GrabGuestClipboard grabbing %d", VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD);
    
    if (!enableClipboardToGuest) {
        SPICE_DEBUG("CB: enableClipboardToGuest set to false. Doing nothing.");
        return 0;
    }

    SpiceDisplay *display;
    SpiceDisplayPrivate *d;
    
    display = global_display;
    if (global_display == NULL) {
        return -1;
    }

    d = SPICE_DISPLAY_GET_PRIVATE(display);
    if (d->main == NULL) {
        return -1;
    }
   
    /* Grab the guest clipboard, with just one type (text) */
    spice_main_clipboard_selection_grab(d->main, 
        /*selection */VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD,
        clipboardTypes, ntypes);
    isClipboardGrabbed = TRUE;
    return 0;
}

int SpiceGlibGlue_ReleaseGuestClipboard() {

    SPICE_DEBUG("CB: ReleaseGuestClipboard");
    if (!enableClipboardToGuest) {
        SPICE_DEBUG("CB: enableClipboardToGuest set to false. Doing nothing.");
        return 0;
    }

    SpiceDisplay *display;
    SpiceDisplayPrivate *d;
    
    display = global_display;
    if (global_display == NULL) {
        return -1;
    }

    d = SPICE_DISPLAY_GET_PRIVATE(display);
    if (d->main == NULL) {
        return -1;
    }

    spice_main_clipboard_selection_release(d->main, VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD);
    isClipboardGrabbed = FALSE;
    return 0;
}

static gboolean check_clipboard_size_limits(gint clipboard_len) {

    //g_object_get(session->priv->main, "max-clipboard", &max_clipboard, NULL);
    if (max_clipboard != -1 && clipboard_len > max_clipboard) {
        g_warning("discarded clipboard of size %d (max: %d)",
                  clipboard_len, max_clipboard);
        return FALSE;
    } else if (clipboard_len <= 0) {
        SPICE_DEBUG("discarding empty clipboard");
        return FALSE;
    }

    return TRUE;
}

gboolean clipboard_requestFromGuest(SpiceMainChannel *main, guint selection,
                                  guint type, gpointer user_data) {
                                  
    SPICE_DEBUG("CB: clipboard_requestFromGuest()");
    if (!enableClipboardToGuest) {
        SPICE_DEBUG("CB: enableClipboardToGuest set to false. Doing nothing.");
        return TRUE;
    }

    SpiceDisplay *display;
    SpiceDisplayPrivate *d;
    
    if (!isClipboardGrabbed) {
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
    gchar *data = NULL;

#ifdef WIN32
    /* Get clipboard content from Windows OS (text only)
       and copy it to data (which will be g_free'd later) */ 
    HANDLE h;

    if (!OpenClipboard(NULL)) {
        SPICE_DEBUG("Can't open clipboard");
        return FALSE;
    }

    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) // UTF16
    {
        h = GetClipboardData(CF_UNICODETEXT);
        if (h == NULL) {
            SPICE_DEBUG("CB: Clipboard is empty\n");
        }
        else {
            data = g_utf16_to_utf8 ((const gunichar2*) h, -1,
                     NULL, NULL, /*GError **error*/NULL);
            //SPICE_DEBUG("CB: Clipboard contains %s\n", data);
        }
    }
    else if (IsClipboardFormatAvailable(CF_TEXT)) // ASCII
    {
        h = GetClipboardData(CF_TEXT);
        if (h == NULL) {
            SPICE_DEBUG("CB: Clipboard is empty\n");
        }
        data = g_strdup(h);
    }
    CloseClipboard();

    if (data == NULL ) {
        SPICE_DEBUG("CB: No supported Clipboard format available\n");
        goto onError;
    }
        
    /* Transform format (line ending, etc) before sending */ 
    gint len = 0;
    gpointer conv = NULL;
    
    /* Our clipboard is not GTK but windows. So we positively have CRLF as line break.
       But it is our (client program) responsability to give the guest the format it wants.
    */
    if (!spice_main_agent_test_capability(d->main, VD_AGENT_CAP_GUEST_LINEEND_CRLF)) {
        GError *err = NULL;
        
        len = strlen(data);
        conv = spice_dos2unix((gchar*)data, len, &err);
        if (err) {
            g_warning("Failed to convert text line ending: %s", err->message);
            g_clear_error(&err);
            goto onError;
        }

        len = strlen(conv);
    } else {
        len = strlen((const char *)data);
    }
    if (!check_clipboard_size_limits(/*self,*/ len)) {
        g_free(conv);
        goto onError;
    }

    
    spice_main_clipboard_selection_notify(d->main, 
            VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD, 
            VD_AGENT_CLIPBOARD_UTF8_TEXT,
            conv ?: data, len);
    g_free(conv);
#endif
    g_free(data);
    return TRUE;
onError:
    g_free(data);
    return FALSE;
}

void clipboard_got_from_guest(SpiceMainChannel *main, guint selection,
                                     guint type, const guchar *data, guint size,
                                     gpointer user_data) {

    SPICE_DEBUG("CB: clipboard_got_data  type : %d ", type);
    if (!enableClipboardToClient) {
        SPICE_DEBUG("CB: enableClipboardToClient set to false. Doing nothing.");
        return;
    }

    gchar *conv = NULL;
    if (type == VD_AGENT_CLIPBOARD_NONE) {
        SPICE_DEBUG("CB: No data. Received type VD_AGENT_CLIPBOARD_NONE : size %d", size);
        push_clipboard_data (NULL);
        return;
    }
    
    if (type == VD_AGENT_CLIPBOARD_UTF8_TEXT) {
#ifdef G_OS_WIN32
        /* Received text is UTF-8, with the line-ending of the guest.
         * Here we convert the line-ending to windowsstyle if necessary. */
        
        if (!spice_main_agent_test_capability(main, VD_AGENT_CAP_GUEST_LINEEND_CRLF)) {
            SPICE_DEBUG("CB: Change Line ending");
            GError *err = NULL;

            conv = spice_unix2dos((gchar*)data, size, &err);
            if (err) {
                g_warning("Failed to convert text line ending: %s", err->message);
                g_clear_error(&err);
                goto end;
            }
            //size = strlen(conv);
        } else {
            SPICE_DEBUG("CB: Set null terminator");
            conv = g_new(char, size+1);
            memcpy(conv, data, size);
            conv[size] = 0;
        }
#endif
        push_clipboard_data (conv?(gpointer)conv:data);
        SPICE_DEBUG("CB: clipboard got %s", conv);
        g_free(conv);
    } else {
        g_warning("CB: Ignoring clipboard of unexpected type %d from guest", type);
    }
    
end:
    g_free(conv);
}

gboolean clipboard_grabByGuest(SpiceMainChannel *main, guint selection,
                               guint32* types, guint32 num_types,
                               gpointer user_data) {

    gboolean sth_grabbed = FALSE;
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
            
            if (!OpenClipboard(hwnd)) {
                return false;
            }
            sth_grabbed= TRUE;
            guestOwnsClipboard = TRUE;
            EmptyClipboard();
            SetClipboardData(CF_UNICODETEXT, NULL);
            SPICE_DEBUG("CB: ClipboardData Set to null WM_RENDERFORMAT should come");
        }
    }// end for
    if (sth_grabbed) {
        CloseClipboard();
    } else {
        SPICE_DEBUG("CB: Guest only requested unsupported types of clipboard. Not setting.");
    }
    
    return TRUE;
}


gboolean clipboard_releaseByGuest(SpiceMainChannel *main, guint selection,
                               guint32* types, guint32 num_types,
                               gpointer user_data) {

    gboolean sth_grabbed = FALSE;
    gint i;
    
    SPICE_DEBUG("CB: clipboard_releaseByGuest(sel %d)", selection);
    if (!enableClipboardToClient) {
        SPICE_DEBUG("CB: enablelClipboardToClient set to false. Doing nothing.");
        return TRUE;
    }
    
    if (selection != VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD) {
        g_warning("CB: discarded clipboard request of unsupported selection %d",selection);
        return FALSE;    
    }

    if (!guestOwnsClipboard) {
        SPICE_DEBUG("CB: guest already does not own clipboard. Doing nothing.");
        return TRUE;
    }
    guestOwnsClipboard = FALSE;
    
    return TRUE;
}

/* 
 * Code block to receive windows messages.
 * Create a window so that we receive WM_RENDERFORMAT messages.
 * We need them to know when some local application asks
 * for the content of the clipboard.
 * G_IO paraphernalia is just to ensure a message pump is started.
 */
static gboolean recv_windows_message (GIOChannel  *channel,
		      GIOCondition cond,
		      gpointer    data)
{
  GIOError error;
  MSG msg;
  guint nb;

  SPICE_DEBUG("CB: recv_windows_message()");
  while (1)
    {
      error = g_io_channel_read (channel, &msg, sizeof (MSG), &nb);
      
      if (error != G_IO_ERROR_NONE)
   	  {
	      if (error == G_IO_ERROR_AGAIN)
	        continue;
	  }
      break;
    }
/*    g_print ("gio-test: ...Windows message for %#x: %d,%d,%d\n",
	   msg.hwnd, msg.message, msg.wParam, msg.lParam)  */
  return TRUE;
}

/* GSourceFunc
 * Requests text content of the clipboard to the vdagent of the guest */
static gboolean onRenderFormat_text() {

    SpiceDisplay *display;
    SpiceDisplayPrivate *d;

    display = global_display;
    if (global_display == NULL) {
        return -1;
    }

    d = SPICE_DISPLAY_GET_PRIVATE(display);
    if (d->main == NULL) {
        return -1;
    }
    
    spice_main_clipboard_selection_request(d->main, 
            VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD,  
            VD_AGENT_CLIPBOARD_UTF8_TEXT);
    return G_SOURCE_REMOVE;
    
}

static void showBytes(char* name, char* bytes) {

    gint numBytes =strlen(bytes)+1;
    gint i = 0;
    for (i=0; i< numBytes; i++) {
        SPICE_DEBUG("CB %s[%d]: 0x%02X", name, i, bytes[i]);
    }
}
/*
 * Some application on the local system requests us the content of the clipboard,
 * in some format we grabbed (UTF8). And now we have to put that content in the
 * Clipboard.
 */
void OnRenderFormat(UINT wparam) {

    SPICE_DEBUG("CB OnRenderFormat()");
    if (!enableClipboardToClient) {
        SPICE_DEBUG("CB: enableClipboardToClient set to false. Doing nothing.");
        return;
    }

    if (!guestOwnsClipboard) {
        SPICE_DEBUG("CB: Guest does NOT own clipboard. Not setting client clipboard data.");
        return;
    }

    if (wparam == CF_UNICODETEXT) {
        g_timeout_add_full(G_PRIORITY_HIGH, 0,
               onRenderFormat_text,
               NULL, NULL);
/* https://msdn.microsoft.com/en-us/library/windows/desktop/ms649016(v=vs.85).aspx */

        // Allocate a buffer for the text. 
        HGLOBAL hglb;
        LPTSTR  lptstr;
        char* receivedContent = pop_clipboard_data_timed();
        //showBytes("receivedContent", receivedContent);
        
        //Convert received utf8 (spice standard) to required utf16 (windows standard)
        glong winTItems=-1;
        gunichar2* winText = g_utf8_to_utf16(receivedContent, -1,
                 NULL, &winTItems, /*GError **error*/NULL);
        g_free(receivedContent);

        glong winTSize = (winTItems+1) * sizeof(gunichar2);
        
        hglb = GlobalAlloc(GMEM_MOVEABLE, winTSize);
        if (hglb == NULL) {
            g_warning("CB: Could not allocate memory for clipboard.");
            return;
        }

        lptstr = GlobalLock(hglb); 
        memcpy(lptstr, winText, winTSize);
        
        g_free(winText);
        GlobalUnlock(hglb);

        // Place the handle on the clipboard.
        if (SetClipboardData(CF_UNICODETEXT, hglb) == NULL) {
            g_warning("CB: SetClipboardData() Failed");
        };
    } else {
        g_warning("CB: Requested format %06X  is not supported (nor grabbed)", wparam);
    }
}                              

void OnRenderAllFormats(HWND hwnd) {
    if (OpenClipboard(hwnd)) {
        OnRenderFormat(CF_UNICODETEXT);
        CloseClipboard();
    }
}
static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{

    SPICE_DEBUG("CB  wnd_proc message: %06X ", message);
    switch (message) {
        case WM_RENDERFORMAT:
            OnRenderFormat(wparam);
            break;
        case WM_RENDERALLFORMATS:
            OnRenderAllFormats(hwnd);
            break;
        case WM_DESTROYCLIPBOARD:
            SPICE_DEBUG("CB WM_DESTROYCLIPBOARD. Doing nothing.");
            break;
        case WM_CLIPBOARDUPDATE:
            SPICE_DEBUG("CB WM_CLIPBOARDUPDATE.");
            if ( GetClipboardOwner() != hwnd) {
                SPICE_DEBUG("CB Another application grabbed the client clipboard. Grabbing guest cb.");
                guestOwnsClipboard = FALSE;
                SpiceGlibGlue_GrabGuestClipboard();
            }
            break;
        default:
            SPICE_DEBUG("CB wnd_proc case default");
    }
    return DefWindowProc(hwnd, message, wparam, lparam);
}

/* 
 * Create a window object that listens to windows events.
 * Starts a windows message pump.
 * If AddClipboardFormatListener is available (Vista and later), call it.
 * Current mingw64 does not contains it, so we get a reference to it in run time).
 *
 * This function can not be called from spice-glib-client mainloop thread, which
 * seems to absorb messages.
 */
gboolean SpiceGlibGlue_InitClipboard(
        int16_t enableClipboardToGuestP, int16_t enableClipboardToClientP) {
        
    SPICE_DEBUG("CB SpiceGlibGlue_InitClipboard (%d, %d)", 
            enableClipboardToGuestP, enableClipboardToClientP);
    enableClipboardToGuest  = enableClipboardToGuestP;
    enableClipboardToClient = enableClipboardToClientP;

    if (!enableClipboardToGuest && !enableClipboardToClient) return;
    
    WNDCLASS wcls;
    
    /* create a hidden window that will receive windows messages. */
    memset(&wcls, 0, sizeof(wcls));
    wcls.lpfnWndProc = wnd_proc;
    wcls.lpszClassName = FLEXVDI_CLPBRD_WINCLASS_NAME;
    if (!RegisterClass(&wcls)) {
        DWORD e = GetLastError();
        g_warning("RegisterClass failed , %ld", (long)e);
        return FALSE;
    }
    hwnd = CreateWindow(FLEXVDI_CLPBRD_WINCLASS_NAME,
                              NULL, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
    if (!hwnd) {
        DWORD e = GetLastError();
        g_warning("CreateWindow failed: %ld", (long)e);
        goto failed_unreg;
    }
    
    /* Create a message loop. 
     * Something else may have started the message pump for us,
     * but it is better not to depend on it.
     */
    GIOChannel *windows_messages_channel = g_io_channel_win32_new_messages((gsize)hwnd);
    g_io_add_watch(windows_messages_channel, G_IO_IN, recv_windows_message,0);

    if (enableClipboardToGuest) {
        addClipboardFormatListener = (PFN_AddClipboardFormatListener) GetProcAddress (GetModuleHandle 
        ("user32.dll"), "AddClipboardFormatListener");
        
        if (addClipboardFormatListener != NULL && addClipboardFormatListener (hwnd)) {
            SPICE_DEBUG("CB: addClipboardFormatListener succeded");
        } else {
            g_warning("CB: addClipboardFormatListener() Failed");
        }
    }
    
    return FALSE;

 failed_unreg:
    UnregisterClass(FLEXVDI_CLPBRD_WINCLASS_NAME, NULL);
    
    return FALSE;    
}

gboolean SpiceGlibGlue_FinalizeWindowsClipboardEvents() {

    if (hwnd) {
        DestroyWindow(hwnd);
        UnregisterClass(FLEXVDI_CLPBRD_WINCLASS_NAME, NULL);
    }
    return FALSE;    
}

/* 
 * End of code block to receive windows messages. 
 */