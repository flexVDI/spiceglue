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

#include "glue-spice-widget.h"

SpiceDisplay* global_display();
int16_t SpiceGlibGlue_Connect(char* host,
			      char* port, char* tls_port, char* ws_port,
			      char* password,
			      char* ca_file, char* cert_subj,
			      int32_t enable_sound);
void SpiceGlibGlue_Disconnect(void);
void SpiceGlibGlue_InitializeLogging(int32_t verbosityLevel);
void SpiceGlibGlue_MainLoop(void);
