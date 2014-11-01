#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([pxz], [4.999.9beta+1], [jnovy@users.sourceforge.net])
AC_CONFIG_SRCDIR([pxz.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([foreign subdir-objects])

# Checks for programs.
AC_PROG_CC_C99
AC_USE_SYSTEM_EXTENSIONS
AC_OPENMP
AC_SYS_LARGEFILE

AC_ARG_WITH([xz],
    AS_HELP_STRING([--with-xz=PATH], [Path to XZ executable]),
    [XZ="$with_gnu_date"],
    [AC_PATH_PROG([XZ], [xz], [xz])]
)

AC_DEFINE_UNQUOTED([XZ_BINARY], ["$XZ"], [Path to XZ executable])

# Checks for libraries.
AC_SEARCH_LIBS([lzma_stream_encoder], [lzma], [],
               [AC_MSG_ERROR([Missing LZMA library])]
)

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h inttypes.h stdio_ext.h stdlib.h string.h sys/time.h unistd.h utime.h])
AC_CHECK_HEADERS([lzma.h], [],
                 [AC_MSG_ERROR([Missing lzma.h header file])]
)

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_FUNC_MALLOC
AC_CHECK_FUNCS([__fpending utime])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
