# 
# MGSim, the Microgrids simulator.
# 
# Copyright (C) 2008,2009,2010,2011 Universiteit van Amsterdam..
# All rights reserved.
# 

AC_PREREQ([2.61])
AC_INIT([mgsim], 
        m4_esyscmd([build-aux/git-version-gen build-aux/tarball-version s/mgsim-/v/ mgsim]), 
        [microgrids@svp-home.org])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([sys_config.h])

AC_CANONICAL_TARGET

AM_INIT_AUTOMAKE([1.11 tar-ustar no-define foreign dist-bzip2 subdir-objects
                  parallel-tests color-tests silent-rules])
AM_SILENT_RULES([yes])

## extra programs

AC_CHECK_PROGS([HELP2MAN], [help2man], [$am_aux_dir/missing help2man])
AC_PROG_SED
AM_PATH_PYTHON([2.6], [], [AC_MSG_WARN([The 'readtrace' utility requires Python >= 2.6.])])

## language / library tests

AC_LANG_PUSH([C++])
AC_PROG_CXX

WARN_CXXFLAGS=
if test "x$GXX" = xyes; then
  WARN_CXXFLAGS="-W -Wall -Wwrite-strings"
fi
AC_SUBST([WARN_CXXFLAGS])

# enable extensions for the non-standard tests below
AC_USE_SYSTEM_EXTENSIONS

AC_MSG_CHECKING([for the number of bits in a char])
AC_COMPUTE_INT([char_size], [CHAR_BIT], [#include <limits.h>], [char_size=unknown])
AC_MSG_RESULT([$char_size])
if test x$char_size != x8; then
  AC_MSG_ERROR([This program requires CHAR_BIT == 8, found $char_size.])
fi

# non-standard POSIX functions
AC_CHECK_FUNCS([getdtablesize fsync fdopendir])

AC_MSG_CHECKING([for dirfd])
AC_TRY_LINK([#include <dirent.h>],
            [DIR *p; int d = dirfd(p);],
            [AC_MSG_RESULT([yes])
             AC_DEFINE([HAVE_DIRFD],[1],[Define to 1 if dirfd(DIR*) is available.])],
            [AC_MSG_RESULT([no])])
AC_CHECK_MEMBERS([DIR.d_fd],[],[],[#include <dirent.h>])

AC_CHECK_MEMBERS([struct dirent.d_ino],[],[],[#include <dirent.h>])
AC_CHECK_MEMBERS([struct dirent.d_type],[],[],[#include <dirent.h>])
AC_CHECK_MEMBERS([struct stat.st_blocks],[],[],[#include <sys/stat.h>])
AC_CHECK_MEMBERS([struct stat.st_blksize],[],[],[#include <sys/stat.h>])
AC_CHECK_MEMBERS([struct stat.st_atimespec],[],[],[#include <sys/stat.h>])
AC_CHECK_MEMBERS([struct stat.st_mtimespec],[],[],[#include <sys/stat.h>])
AC_CHECK_MEMBERS([struct stat.st_ctimespec],[],[],[#include <sys/stat.h>])
AC_CHECK_MEMBERS([struct tm.tm_gmtoff],[],[],[#include <sys/time.h>])

AC_SEARCH_LIBS([nanosleep], [rt])
AC_CHECK_FUNCS([nanosleep usleep])

# support for PTYs
AC_SYS_POSIX_TERMIOS
AC_CHECK_FUNCS([tcsetattr tcgetattr])
if test $ac_cv_sys_posix_termios = yes; then
  AC_DEFINE([HAVE_POSIX_TERMIOS], [1], [Define to 1 if termios.h is available])
fi

AC_CHECK_FUNCS([posix_openpt unlockpt ptsname grantpt getpt])
AC_MSG_CHECKING([for /dev/ptmx])
if test -c /dev/ptmx -a -r /dev/ptmx -a -w /dev/ptmx; then
   AC_DEFINE([HAVE_DEV_PTMX], [1], [Define to 1 if /dev/ptmx is supported])
   have_dev_ptmx=yes
else
   have_dev_ptmx=no
fi
AC_MSG_RESULT([$have_dev_ptmx])

# --enable-assert
AC_HEADER_ASSERT

# argp 
AC_SEARCH_LIBS([argp_parse], [c argp])
if test "x$ac_cv_search_argp_parse" = xno; then
   AC_MSG_ERROR([Unable to find argp. Is it installed? If necessary use CPPFLAGS=... LDFLAGS=... to specify its location.])
fi

# libev
AC_SEARCH_LIBS([ev_version_minor], [ev])
if test "x$ac_cv_search_ev_version_minor" = xno; then
   AC_MSG_ERROR([Unable to find libev. Is it it installed? If necessary use CPPFLAGS=... LDFLAGS=... to specify its location.])
fi

# libreadline / libhistory
AC_LIB_READLINE
if test "x$ac_cv_lib_readline" = xno; then
     AC_MSG_WARN([The readline library or development header could not be found.])
     AC_MSG_WARN([The simulator prompt will not support advanced command editing.])
fi

# libsdl
AC_ARG_ENABLE([sdl],
              [AC_HELP_STRING([--disable-sdl],  
                              [disable support for graphics output using SDL (default is try to enable)])],
              [], [enable_sdl=yes])
if test "x$enable_sdl" = "xyes"; then
  AM_PATH_SDL
  if test "x$no_sdl" = xyes; then
    AC_MSG_ERROR([SDL not found])
    enable_sdl=no
  fi
fi
AM_CONDITIONAL([ENABLE_SDL], [test "x$enable_sdl" = "xyes"])

AC_LANG_POP([C++])

## Feature checks

AC_ARG_ENABLE([trace-checks],
              [AC_HELP_STRING([--disable-trace-checks],
                              [disable checking of the allowed storage traces upon failed trace checks])],
              [], [enable_trace_checks=yes])
if test "x$enable_trace_checks" = xno; then
  AC_DEFINE([DISABLE_TRACE_CHECKS], [1],
            [Define if storage trace checks should be disabled])  
fi

AC_ARG_ENABLE([verbose-trace-checks],
              [AC_HELP_STRING([--disable-verbose-trace-checks],
                              [disable printing of the allowed storage traces upon failed trace checks])],
              [], [enable_verbose_trace_checks=yes])
if test "x$enable_verbose_trace_checks" = xyes; then
  AC_DEFINE([VERBOSE_TRACE_CHECKS], [1],
            [Define if storage trace checks should fail verbosely])
fi

AC_ARG_ENABLE([abort-on-trace-failure],
              [AC_HELP_STRING([--disable-abort-on-trace-failure],
                              [do not abort the simulation upon failed trace checks])],
              [], [enable_abort_on_trace_failure=yes])
if test "x$enable_abort_on_trace_failure" = xyes; then
  AC_DEFINE([ABORT_ON_TRACE_FAILURE], [1],
            [Define if failed storage trace checks should abort the simulation])
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

AX_PTHREAD

AC_ARG_ENABLE([monitor], 
              [AC_HELP_STRING([--disable-monitor], [disable support for simulation monitoring (default is try to enable)])],
              [], [enable_monitor=yes])
if test "x$enable_monitor" = "xyes"; then
  if test "x$ax_pthread_ok" != "xyes"; then
     AC_MSG_WARN([POSIX threads not available, cannot use monitor.])
     enable_monitor=no
  fi
fi
AM_CONDITIONAL([ENABLE_MONITOR], [test "x$enable_monitor" = "xyes"])

AC_ARG_ENABLE([cacti], 
              [AC_HELP_STRING([--disable-cacti], [disable support for area calculations with CACTI (default is try to enable)])],
              [], [enable_cacti=yes])
if test "x$enable_cacti" = "xyes"; then
  if ! test -d "$srcdir/cacti"; then
     AC_MSG_WARN([CACTI sources not available, cannot use CACTI.])
     enable_cacti=no
  fi
  if test "x$ax_pthread_ok" != "xyes"; then
     AC_MSG_WARN([POSIX threads not available, cannot use CACTI.])
     enable_cacti=no
  fi
fi
AM_CONDITIONAL([ENABLE_CACTI], [test "x$enable_cacti" = "xyes"])


## Target selection
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

enable_compiled_tests=no

AC_PATH_PROG([SLC], [slc], [no], [$prefix/bin$PATH_SEPARATOR$PATH])
if test "x$SLC" != "x:" ; then
   case "$target_cpu" in
        mtalpha)
           AC_MSG_CHECKING([whether $SLC supports -b mta_n])
           AC_LANG_CONFTEST([AC_LANG_SOURCE([int main(void) { return 0; }])])
           if $SLC -b mta_n -c -o conftest.o conftest.c; then
             AC_MSG_RESULT([yes])
             enable_compiled_tests=yes
           else
             AC_MSG_RESULT([no])
             AC_MSG_WARN([borked $SLC -b mta_n. Cannot run the compiled MT-Alpha test suite.])
           fi
           ;;
        mtsparc)
           AC_MSG_CHECKING([whether $SLC supports -b mtsn_n])
           AC_LANG_CONFTEST([AC_LANG_SOURCE([int main(void) { return 0; }])])
           if $SLC -b mtsn_n -c -o conftest.o conftest.c; then
              AC_MSG_RESULT([yes])
              enable_compiled_tests=yes
           else
              AC_MSG_RESULT([no])
              AC_MSG_WARN([borked $SLC -b mtsn_n. Cannot run the compiled MT-Sparc test suite.])
           fi
           ;;
  esac
else
   AC_MSG_WARN([slc not found. Cannot run the compiled test suite.])
fi

AM_CONDITIONAL([ENABLE_C_TESTS], [test x$enable_compiled_tests = xyes])


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
AC_CONFIG_FILES([tools/viewlog], [chmod +x tools/viewlog])
AC_CONFIG_FILES([tools/aslink], [chmod +x tools/aslink])
AC_CONFIG_FILES([tools/compile], [chmod +x tools/compile])

AC_OUTPUT

cat <<EOF
*
* Configuration done.
*
* ISA target:             $target_cpu
*
* CACTI area computation: $enable_cacti 
* SDL output:             $enable_sdl
* Trace checks:           $enable_trace_checks
* Verbose trace checks:   $enable_verbose_trace_checks
* Abort on trace failure: $enable_abort_on_trace_failure
* Software IEEE754:       $enable_softfpu
* Asynchronous monitor:   $enable_monitor
* Area calculation:       $enable_cacti
*
* MT-Alpha tests:         $enable_mtalpha_tests
* MT-SPARC tests:         $enable_mtsparc_tests
* Compiled tests:         $enable_compiled_tests
*
* C++ compiler:           $CXX $CPPFLAGS $AM_CXXFLAGS $CXXFLAGS $LDFLAGS
* Destination dir:        $prefix
*
Now run:

    make all
    make check

EOF

