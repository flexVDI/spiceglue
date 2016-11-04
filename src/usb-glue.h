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
 * Simple functions to use usb redirection in spice client programs.
 */

#ifdef USBREDIR
#ifndef _GLUE_USB_H
#define _GLUE_USB_H

#include "glue-spice-widget.h"

/* Called internally by spiceglue during initialization.
 */
void usb_glue_register_session(SpiceSession* session);

/* Create an internal list of  connected usbDevices to be retrieved by one by 
 * one by SpiceGlibGlueGetNextUsbDevice()
 */
void SpiceGlibGlue_GetUsbDeviceList();

/* 
 * Copies to devName the name of the next device in the list.
 * The char* passed in "devName" must provide capacity of at least 
 * MAX_USB_DEVICE_NAME_SIZE bytes, which is the limit that we artificially impose.
 * In the next invocation after the last printer is retrieved,
 * the memory of the local list of printers is freed, and an empty ("") string is passed back.
 * - isShared: true if the device is currently shared with the guest.
 */
SpiceUsbDevice* SpiceGlibGlue_GetNextUsbDevice(char* devName, char* devId, 
        int32_t* isShared, int32_t* isEnabled, int32_t* opPending);

/* 
 * Returns true if the usbDevice List has changed since the last time SpiceGlibGlueGetUsbDeviceList
 * was called
 */ 
int32_t SpiceGlibGlue_isUsbDeviceListChanged();


void SpiceGlibGlue_ShareUsbDevice(SpiceUsbDevice* d);
void SpiceGlibGlue_UnshareUsbDevice(SpiceUsbDevice* d);


/* 
 * Returns true if the usb message has changed since the last time 
 * SpiceGlibGlue_isUsbMsgChanged was called.
 */ 
int32_t SpiceGlibGlue_isUsbErrMsgChanged();

/* Copies the current error message in errMsg (if any) */
void SpiceGlibGlue_GetUsbErrMsg(char* errMsg);

void SpiceGlibGlue_InitWindowsEvents();
void SpiceGlibGlue_FinalizeWindowsEvents();

#endif /* _GLUE_USB_H */
#endif
