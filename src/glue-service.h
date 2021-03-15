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

#include "glue-spice-widget.h"

SpiceDisplay* global_display(void);
int16_t SpiceGlibGlue_Connect(char* host,
			      char* port, char* tls_port, char* ws_port,
			      char* password,
			      char* ca_file, char* cert_subj,
			      int32_t enable_sound);
void SpiceGlibGlue_Disconnect(void);
void SpiceGlibGlue_InitializeLogging(int32_t verbosityLevel);
void SpiceGlibGlue_MainLoop(void);
void SpiceGlibGlueSetDisplayBuffer(uint32_t *display_buffer,
				   int32_t width, int32_t height);
int16_t SpiceGlibGlueIsDisplayBufferUpdated(int32_t width, int32_t height);
int16_t SpiceGlibGlueLockDisplayBuffer(int32_t *width, int32_t *height);
void SpiceGlibGlueUnlockDisplayBuffer(void);
int16_t SpiceGlibGlueGetCursorPosition(int32_t* x, int32_t* y);
int32_t SpiceGlibGlue_SpiceKeyEvent(int16_t isDown, int32_t hardware_keycode);
void SpiceGlibGlue_SetLogCallback(void (*log_callback)(int8_t *));
void SpiceGlibGlue_SetBufferResizeCallback(void (*buffer_resize_callback)(int, int, int));
void SpiceGlibGlue_SetBufferUpdateCallback(void (*buffer_update_callback)(int, int, int, int));
void SpiceGlibGlue_SetBufferDisconnectCallback(void (*disconnect_callback)(void));