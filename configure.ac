AC_INIT([spiceglue], [2.2], [devel@flexvdi.com], [spiceglue], [http://flexvdi.com])
AM_INIT_AUTOMAKE([foreign])
AC_CONFIG_FILES([Makefile src/Makefile])
AC_CONFIG_MACRO_DIRS([m4])

LT_INIT([win32-dll])

AC_PROG_CC

PKG_CHECK_MODULES([GLIB], [glib-2.0])
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

PKG_CHECK_MODULES([SPICEGLIB], [spice-client-glib-2.0])
AC_SUBST(SPICEGLIB_CFLAGS)
AC_SUBST(SPICEGLIB_LIBS)

case "$host_os" in
     *mingw*|*cygwin*)
        os_win32=yes
        os_macos=no
        ;;
     *darwin*)
        os_win32=no
        os_macos=yes
        ;;
     *)
        os_win32=no
        os_macos=no
        ;;
esac

AC_ARG_ENABLE([printing],
    AS_HELP_STRING([--disable-printing], [Disable flexVDI follow-me printing support]))
 
AS_IF([test "x$enable_printing" != "xno"], [
    AC_DEFINE(PRINTING)
	enable_printing="yes"
	PKG_CHECK_MODULES([FLEXVDI_SPICE_CLIENT], [flexvdi-spice-client])
	AC_SUBST(FLEXVDI_SPICE_CLIENT_CFLAGS)
	AC_SUBST(FLEXVDI_SPICE_CLIENT_LIBS)
	], )
AM_CONDITIONAL([WITH_PRINTING], [test "x$enable_printing" != "xno"])


AC_ARG_ENABLE([usbredir],
    AS_HELP_STRING([--disable-usbredir], [Disable USB redirection support]))

AS_IF([test "x$enable_usbredir" != "xno"], [
    AC_DEFINE(USBREDIR)
	enable_usbredir="yes"
	PKG_CHECK_MODULES([USB], [libusb-1.0])
	], )
AM_CONDITIONAL([WITH_USBREDIR], [test "x$enable_usbredir" != "xno"])


AC_ARG_ENABLE([clipboard],
    AS_HELP_STRING([--disable-clipboard], [Disable Clipboard sharing support]))

AS_IF([test "x$enable_clipboard" != "xno"], [
    AC_DEFINE(USE_CLIPBOARD)
	enable_clipboard="yes"
	], )

WITH_CLIPBOARD_WIN32=0
WITH_CLIPBOARD_MACOS=0
if test "x$enable_clipboard" != "xno"; then
    if test "$os_win32" = "yes"; then
        WITH_CLIPBOARD_WIN32=1
    elif test "$os_macos" = "yes"; then
        WITH_CLIPBOARD_MACOS=1
    fi
fi

AM_CONDITIONAL(WITH_CLIPBOARD_WIN32, [test "x$WITH_CLIPBOARD_WIN32" = "x1"])
AM_CONDITIONAL(WITH_CLIPBOARD_MACOS, [test "x$WITH_CLIPBOARD_MACOS" = "x1"])

AC_OUTPUT

AC_MSG_NOTICE([

        Spiceglue $VERSION
        ===================

        prefix:                   ${prefix}
        c compiler:               ${CC}
        Target:                   ${target}

        Follow-me printing:       ${enable_printing}
        USB redirection:          ${enable_usbredir}
        Clipboard sharing:        ${enable_clipboard}

        Now type 'make' to build $PACKAGE

])
