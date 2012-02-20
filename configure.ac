# Process this file with autoconf to produce a configure script.

AC_INIT(jfman, 0.1)
AC_CONFIG_AUX_DIR(build-aux)
AC_CANONICAL_HOST
AM_INIT_AUTOMAKE
AM_CONFIG_HEADER(config.h)
AM_MAINTAINER_MODE

AC_ISC_POSIX
AC_PROG_CC
AM_PROG_CC_STDC
AC_HEADER_STDC

pkg_modules="gtk+-2.0 >= 2.0.0"
PKG_CHECK_MODULES(GTK, [$pkg_modules])
PACKAGE_CFLAGS="$PACKAGE_CFLAGS $GTK_CFLAGS"
PACKAGE_LIBS="$PACKAGE_LIBS $GTK_LIBS"

win32=no
case "${host_os}" in
  *mingw* | pw32* | cygwin*)
	win32="yes"
    AC_CHECK_TOOL(RC, windres, :)
    if test "${RC}" = ":"; then
      AC_MSG_FAILURE([Error: you need windres to compile this program.])
    fi
    PACKAGE_CFLAGS="$PACKAGE_CFLAGS -mms-bitfields"
esac
AM_CONDITIONAL(WITH_WIN32, test x"$win32" = "xyes")
AC_SUBST(PACKAGE_CFLAGS)
AC_SUBST(PACKAGE_LIBS)

GETTEXT_PACKAGE=jfman
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package.])

# Add the languages which your application supports here.
ALL_LINGUAS="es"

# Add maintainer information for the POT file here.
POT_COPYRIGHT_HOLDER="'Andrew Makousky'"
AC_SUBST(POT_COPYRIGHT_HOLDER)
POT_BUGS_ADDRESS="'andrew@home-dell-2300'"
AC_SUBST(POT_BUGS_ADDRESS)

AM_GLIB_GNU_GETTEXT

AC_CONFIG_FILES([
Makefile
po/Makefile.in
src/Makefile
])
AC_OUTPUT
