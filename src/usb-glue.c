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

#include <stdio.h>
#include <string.h>

#include "glue-service.h"
#include "glib.h"
#include "usb-glue.h"
#include "usb-device-widget.h"
#ifdef USBREDIR

#ifdef G_OS_WIN32
#include <windows.h>

#define FLEXVDI_MSGLOOP_WINCLASS_NAME  TEXT("FLEXVDI_MSGLOOP_CLIENT")

HWND hwnd = NULL;
#endif


SpiceUsbDeviceWidget *usbWidget = NULL;

/* Temporary storage for the list of UsbDeviceInfo
 * Safe to be called by client program thread 
 * - usbDevices: full list
 * - device: not yet retrieved list.
 */
GSList *devices;
GSList *device;

#ifdef G_OS_WIN32
static gboolean recv_windows_message (GIOChannel  *channel,
		      GIOCondition cond,
		      gpointer    data)
{
  GIOError error;
  MSG msg;
  guint nb;
  
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
  
  return TRUE;
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    /* Don care about the event. Just forward */
    return DefWindowProc(hwnd, message, wparam, lparam);
}

/* Called from glib mainloop thread */
static gboolean startMessageLoop() {

    WNDCLASS wcls;
    
    /* create a hidden window */
    memset(&wcls, 0, sizeof(wcls));
    wcls.lpfnWndProc = wnd_proc;
    wcls.lpszClassName = FLEXVDI_MSGLOOP_WINCLASS_NAME;
    if (!RegisterClass(&wcls)) {
        DWORD e = GetLastError();
        g_warning("RegisterClass failed , %ld", (long)e);
        return FALSE;
    }
    hwnd = CreateWindow(FLEXVDI_MSGLOOP_WINCLASS_NAME,
                              NULL, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
    if (!hwnd) {
        DWORD e = GetLastError();
        g_warning("CreateWindow failed: %ld", (long)e);
        goto failed_unreg;
    }
    
    /* Create a message loop */
    GIOChannel *windows_messages_channel = g_io_channel_win32_new_messages((gsize)hwnd);
    g_io_add_watch(windows_messages_channel, G_IO_IN, recv_windows_message,0);
    return FALSE;

 failed_unreg:
    UnregisterClass(FLEXVDI_MSGLOOP_WINCLASS_NAME, NULL);
    
    return FALSE;    
}

void SpiceGlibGlue_InitWindowsEvents() {

    g_timeout_add_full(G_PRIORITY_HIGH, 0,
                       startMessageLoop,
                       NULL, NULL);
}

static gboolean finalizeMessageWindow() {
    if (hwnd) {
        DestroyWindow(hwnd);
        UnregisterClass(FLEXVDI_MSGLOOP_WINCLASS_NAME, NULL);
    }
    return FALSE;    
}

void SpiceGlibGlue_FinalizeWindowsEvents() {
    g_timeout_add_full(G_PRIORITY_HIGH, 0,
                       finalizeMessageWindow,
                       NULL, NULL);
}

#endif

void usb_glue_register_session(SpiceSession* session) {

    usbWidget = g_object_new(SPICE_TYPE_USB_DEVICE_WIDGET,
            "session", session, 
            NULL);
}

void SpiceGlibGlue_GetUsbDeviceList() {

    g_debug(" %s:%d:%s()", __FILE__, __LINE__, __func__);
    if (!usbWidget) {
        g_error("Requested Usb device list  before initialization");
    }

    devices = spice_usb_device_widget_get_devices(usbWidget);
    device = devices;
    SPICE_DEBUG("USB: SpiceGlibGlueGetUsbDeviceList() END");
}

SpiceUsbDevice* SpiceGlibGlue_GetNextUsbDevice(char* devName, char* devId, 
        int32_t* isShared, int32_t* isEnabled, int32_t* opPending) {

    g_debug(" %s:%d:%s()", __FILE__, __LINE__, __func__);

    // When we get past the end of the list, free the list
    if (!device) {
        SPICE_DEBUG("USB: No more devices.");
        devName[0]= '\0';

        if (devices) {
            g_slist_free_full(devices, g_free);
            devices= NULL;
        }
        return (void *)NULL;
    } else {
        UsbDeviceInfo *dev = device->data;
        strncpy(devName, dev->name, MAX_USB_DEVICE_NAME_SIZE);
        strncpy(devId, dev->id, MAX_USB_DEVICE_ID_SIZE);
        *isShared = dev->isShared;
        *isEnabled = dev->isEnabled;
        *opPending = dev->isOpPending;
        SPICE_DEBUG("USB: Returning devName %s, isShared= %d, isEnabled = %d, isOpPending = %d", 
            devName, dev->isShared, dev->isEnabled, dev->isOpPending);
        device = g_slist_next(device);
        return dev->device;
    }
}

int32_t SpiceGlibGlue_isUsbDeviceListChanged() {
    if (!usbWidget) {
        g_error("Requested isUsbDeviceListChanged before initialization");
    }
    return spice_usb_device_widget_is_changed(usbWidget);
}

void SpiceGlibGlue_ShareUsbDevice(SpiceUsbDevice* d) {

    g_debug(" %s:%d:%s()", __FILE__, __LINE__, __func__);

    if (!usbWidget) {
        g_error("Requested UsbWidget before initialization");
    }
    
    spice_usb_device_widget_share(usbWidget, d);
}

void SpiceGlibGlue_UnshareUsbDevice(SpiceUsbDevice* d) {

    g_debug(" %s:%d:%s()", __FILE__, __LINE__, __func__);

    if (!usbWidget) {
        g_error("Requested UsbWidget before initialization");
    }
    
    spice_usb_device_widget_unshare(usbWidget, d);
}

void SpiceGlibGlue_GetUsbErrMsg(char* errMsg) {

    spice_usb_device_widget_get_error_msg(usbWidget, errMsg);
}

int32_t SpiceGlibGlue_isUsbErrMsgChanged() {
    if (!usbWidget) {
        g_error("Requested isUsbErrMsgChanged before initialization");
    }
    return spice_usb_device_widget_is_msg_changed(usbWidget);
}

#endif
