# 
# MGSim, the Microgrids simulator.
# 
# Copyright (C) 2008,2009 Universiteit van Amsterdam..
# All rights reserved.
# 

AC_PREREQ([2.61])
AC_INIT([mgsim], 
        m4_esyscmd([build-aux/version-gen build-aux/tarball-version build-aux/package-version]), 
        [m.lankamp@uva.nl])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([sys_config.h])

AC_CANONICAL_TARGET

AM_INIT_AUTOMAKE([1.11 tar-ustar no-define foreign dist-bzip2 subdir-objects
                  parallel-tests color-tests silent-rules])
AM_SILENT_RULES([yes])

AC_PROG_CXX

WARN_CXXFLAGS=
if test "x$GCC" = xyes; then
  WARN_CXXFLAGS="-W -Wall -Wwrite-strings -Wno-unused"
fi
AC_SUBST([WARN_CXXFLAGS])

AC_CHECK_PROGS([HELP2MAN], [help2man], [$am_aux_dir/missing help2man])


# Feature checks

AC_HEADER_ASSERT

AC_LIB_READLINE
if test "x$ac_cv_lib_readline" = xno; then
     AC_MSG_WARN([The readline library or development header could not be found.])
     AC_MSG_WARN([The simulator prompt will not support advanced command editing.])
fi

AC_ARG_ENABLE([softfpu],
              [AC_HELP_STRING([--enable-softfpu], 
                              [enable software emulation of IEEE 754 (default is disabled)])],
              [], [enable_softfpu=no])
if test "x$enable_softfpu" = "xyes"; then
  AC_DEFINE([EMULATE_IEEE754], [1],
            [Define to 1 if IEEE 754 software emulation should be used])
fi

AC_ARG_ENABLE([profile],
              [AC_HELP_STRING([--enable-profile], 
                              [enable profiling during execution (default is disabled)])],
              [], [enable_profile=no])
if test "x$enable_profile" = "xyes"; then
  AM_CXXFLAGS="$AM_CXXFLAGS -pg"
fi

AC_ARG_ENABLE([sdl],
              [AC_HELP_STRING([--enable-sdl],  
                              [enable support for graphics output using SDL])],
              [], [enable_sdl=yes])
if test "x$enable_sdl" = "xyes"; then
  AM_PATH_SDL
  if test "x$no_sdl" = xyes; then
    AC_MSG_ERROR([SDL not found])
    enable_sdl=no
  fi
fi
AM_CONDITIONAL([ENABLE_SDL], [test "x$enable_sdl" = "xyes"])

AC_ARG_ENABLE([monitor], 
              [AC_HELP_STRING([--disable-monitor], [disable support for simulation monitoring])],
              [], [enable_monitor=yes])
if test "x$enable_monitor" = "xyes"; then
  AX_PTHREAD
  if test "x$ax_pthread_ok" != "xyes"; then
     AC_MSG_WARN([POSIX threads not available, cannot use monitor.])
     enable_monitor=no
  fi
fi
AM_CONDITIONAL([ENABLE_MONITOR], [test "x$enable_monitor" = "xyes"])


AM_PATH_PYTHON([2.6], [], [AC_MSG_WARN([The 'readtrace' utility requires Python >= 2.6.])])

# Target selection
enable_mtalpha_tests=no
enable_mtsparc_tests=no

AC_ARG_VAR([AS_MTALPHA], [Location of the `as' utility configured for the MT-Alpha binary format.])
AC_ARG_VAR([LD_MTALPHA], [Location of the `ld' utility configured for the MT-Alpha binary format.])
AC_ARG_VAR([AS_MTSPARC], [Location of the `as' utility configured for the MT-Sparc binary format.])
AC_ARG_VAR([LD_MTSPARC], [Location of the `ld' utility configured for the MT-Sparc binary format.])

case "$target_cpu" in
mtalpha)

  AC_PATH_PROG([AS_MTALPHA], [mtalpha-linux-gnu-as], [no], [$prefix/bin$PATH_SEPARATOR$PATH])
  AC_PATH_PROG([LD_MTALPHA], [mtalpha-linux-gnu-ld], [no], [$prefix/bin$PATH_SEPARATOR$PATH])

  enable_mtalpha_tests=yes
  if test "x$AS_MTALPHA" = "xno" \
       -o "x$LD_MTALPHA" = "xno"; then
     AC_MSG_WARN([Some Alpha/MT-Alpha utilities are missing. Cannot run Alpha test suite.])
     enable_mtalpha_tests=no
  fi

  AC_DEFINE([TARGET_MTALPHA], [1],
            [Define when building a simulator for MT-Sparc.])

  ;;

mtsparc)

  AC_PATH_PROG([AS_MTSPARC], [mtsparc-linux-gnu-as], [no], [$prefix/bin$PATH_SEPARATOR$PATH])
  AC_PATH_PROG([LD_MTSPARC], [mtsparc-linux-gnu-ld], [no], [$prefix/bin$PATH_SEPARATOR$PATH])

  enable_mtsparc_tests=yes
  if test "x$AS_MTSPARC" = "xno" \
       -o "x$LD_MTSPARC" = "xno"; then
     AC_MSG_WARN([Some Sparc/MT-Sparc utilities are missing. Cannot run Sparc test suite.])
     enable_mtsparc_tests=no
  fi

  AC_DEFINE([TARGET_MTSPARC], [1],
            [Define when building a simulator for MT-Alpha.])
  ;;

*)
  AC_MSG_ERROR([Unknown simulation target: $target_cpu (did you use --target?)])

esac

AM_CONDITIONAL([ENABLE_MTSPARC_TESTS], [test x$enable_mtsparc_tests = xyes])
AM_CONDITIONAL([ENABLE_MTALPHA_TESTS], [test x$enable_mtalpha_tests = xyes])

AC_CONFIG_LINKS([arch/proc/ISA.h:arch/proc/ISA.$target_cpu.h
                 arch/proc/ISA.cpp:arch/proc/ISA.$target_cpu.cpp])

AC_CONFIG_FILES([
	Makefile
	tools/Makefile
        tests/Makefile
	programs/Makefile
	doc/Makefile
])
AC_CONFIG_FILES([tools/readtrace], [chmod +x tools/readtrace])
AC_CONFIG_FILES([tools/aslink], [chmod +x tools/aslink])

AC_OUTPUT
