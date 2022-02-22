AC_PREREQ([2.69])
AC_INIT([captdriver], [0.1.4])

AC_CONFIG_SRCDIR([src/std.h])

AM_INIT_AUTOMAKE([1.9 no-dist-gzip dist-xz tar-ustar])
AC_LANG([C])
AC_PROG_CC

AC_PATH_PROG([CUPS_CONFIG], [cups-config])
if test -z "${CUPS_CONFIG}"; then
    AC_MSG_FAILURE([CUPS library not found.])
fi
AC_PATH_PROG([PKG_CONFIG], [pkg-config])
if test -z "${PKG_CONFIG}"; then
	AC_MSG_FAILURE([Please install pkg-config.])
fi
CUPS_CFLAGS="`${CUPS_CONFIG} --cflags`"
CUPS_LIBS="`${CUPS_CONFIG} --image --libs`"
LIBUSB_1_0_LIBS="`${PKG_CONFIG} --libs libusb-1.0`"
LIBUSB_1_0_CFLAGS="`${PKG_CONFIG} --cflags libusb-1.0`"
AC_SUBST(CUPS_CFLAGS)
AC_SUBST(CUPS_LIBS)
AC_SUBST(LIBUSB_1_0_CFLAGS)
AC_SUBST(LIBUSB_1_0_LIBS)

AC_CONFIG_FILES([Makefile
                 src/Makefile
                ])
AC_OUTPUT
