AM_CPPFLAGS = -I$(top_srcdir)/include -DG_LOG_DOMAIN=\"SpiceGlue\" $(GLIB_CFLAGS) $(SPICEGLIB_CFLAGS) $(FLEXVDI_SPICE_CLIENT_CFLAGS)
AM_LDFLAGS = -no-undefined

lib_LTLIBRARIES=libspiceglue.la
libspiceglue_la_LIBADD=$(GLIB_LIBS) $(SPICEGLIB_LIBS)
libspiceglue_la_SOURCES=glue-spice-widget.c glue-service.c glue-spicy.c

if WITH_CLIPBOARD_WIN32
libspiceglue_la_SOURCES+=glue-clipboard-win32.c
endif

if WITH_CLIPBOARD_MACOS
libspiceglue_la_SOURCES+=glue-clipboard-macos.c
endif

if WITH_USBREDIR
libspiceglue_la_SOURCES+= usb-device-widget.c usb-device-widget.h usb-glue.c usb-glue.h
endif

if WITH_PRINTING
libspiceglue_la_LIBADD +=	$(FLEXVDI_SPICE_CLIENT_LIBS)
libspiceglue_la_SOURCES += glue-printing.c 
endif
