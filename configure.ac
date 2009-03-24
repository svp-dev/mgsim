# 
# MGSim, the Microgrids simulator.
# 
# Copyright (C) 2008,2009 Universiteit van Amsterdam..
# All rights reserved.
# 

AC_PREREQ([2.63])
AC_INIT([mgsim], [1.x], [m.lankamp@uva.nl])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([src/sys_config.h])

AM_INIT_AUTOMAKE([1.10 tar-ustar no-define foreign dist-bzip2])

# Override the default CXXFLAGS since
# Makefile.am applies new values.
CXXFLAGS=''

AC_PROG_CXX
AC_LIB_READLINE

AC_ARG_ENABLE([mtalpha], 
[AC_HELP_STRING([--disable-mtalpha], [disable support for MT-Alpha compilation (default is try to enable)])],
[], [enable_mtalpha=yes])

AC_ARG_ENABLE([mtalpha-tests], 
[AC_HELP_STRING([--disable-mtalpha-tests], [disable support for MT-Alpha test suite])],
[], [enable_mtalpha_tests=yes])

AC_ARG_VAR([AS_MTALPHA], [Location of the `as' utility configured for the MT-Alpha binary format.])
AC_PATH_PROG([AS_MTALPHA], [mtalpha-linux-gnu-as], [no], [$prefix/bin$PATH_SEPARATOR$PATH])

AC_ARG_VAR([LD_MTALPHA], [Location of the `ld' utility configured for the MT-Alpha binary format.])
AC_PATH_PROG([LD_MTALPHA], [mtalpha-linux-gnu-ld], [no], [$prefix/bin$PATH_SEPARATOR$PATH])

if test "x$enable_mtalpha" = "xyes"; then
  if test "x$AS_MTALPHA" = "xno" \
       -o "x$LD_MTALPHA" = "xno"; then
     AC_MSG_WARN([Some Alpha/MT-Alpha utilities are missing. Cannot run Alpha test suite.])
     enable_mtalpha_tests=no
  fi
else
  enable_mtalpha_tests=no
fi
AM_CONDITIONAL([ENABLE_MTALPHA], [test x$enable_mtalpha = xyes])
AM_CONDITIONAL([ENABLE_MTALPHA_TESTS], [test x$enable_mtalpha_tests = xyes])

AC_ARG_ENABLE([mtsparc], 
[AC_HELP_STRING([--disable-mtsparc], [disable support for MT-Sparc compilation (default is try to enable)])],
[], [enable_mtsparc=yes])

AC_ARG_ENABLE([mtsparc-tests], 
[AC_HELP_STRING([--disable-mtsparc-tests], [disable support for MT-Sparc test suite])],
[], [enable_mtsparc_tests=yes])

AC_ARG_VAR([AS_MTSPARC], [Location of the `as' utility configured for the MT-Sparc binary format.])
AC_PATH_PROG([AS_MTSPARC], [mtsparc-leon-linux-as], [no], [$prefix/bin$PATH_SEPARATOR$PATH])

AC_ARG_VAR([LD_MTSPARC], [Location of the `ld' utility configured for the MT-Sparc binary format.])
AC_PATH_PROG([LD_MTSPARC], [mtsparc-leon-linux-ld], [no], [$prefix/bin$PATH_SEPARATOR$PATH])

if test "x$enable_mtsparc" = "xyes"; then
  if test "x$AS_MTSPARC" = "xno" \
       -o "x$LD_MTSPARC" = "xno"; then
     AC_MSG_WARN([Some Sparc/MT-Sparc utilities are missing. Cannot run Sparc test suite.])
     enable_mtsparc_tests=no
  fi
else
  enable_mtsparc_tests=no
fi
AM_CONDITIONAL([ENABLE_MTSPARC], [test x$enable_mtsparc = xyes])
AM_CONDITIONAL([ENABLE_MTSPARC_TESTS], [test x$enable_mtsparc_tests = xyes])


AC_CONFIG_FILES([
	Makefile
	doc/Makefile
	src/Makefile
        programs/aslink.sh
	programs/Makefile
	programs/mtalpha/Makefile
	programs/mtsparc/Makefile
])
AC_OUTPUT
