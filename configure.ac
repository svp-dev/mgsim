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

AC_PROG_RANLIB
AC_PROG_SED
AM_PATH_PYTHON([2.6], [], [AC_MSG_WARN([The 'readtrace' utility requires Python >= 2.6.])])

m4_define([MY_OPT_PROG],
[AC_ARG_VAR(m4_toupper([$1]), [$2])
AC_CHECK_PROGS(m4_toupper([$1]), [m4_default([$3], [$1])],
               [$am_aux_dir/missing $1])
])

MY_OPT_PROG([help2man], [--help output to manpage converter], [help2man])
MY_OPT_PROG([rst2man], [reStructuredText to manpage converter], [rst2man rst2man.py])
MY_OPT_PROG([tbl], [Troff table converter], [tbl])
MY_OPT_PROG([groff], [GNU roff formatter], [groff])
MY_OPT_PROG([ps2pdf], [Postscript to PDF converter], [ps2pdf])

## language / library tests

AC_LANG_PUSH([C++])
AC_PROG_CXX

WARN_CXXFLAGS=
if test "x$GXX" = xyes; then
  WARN_CXXFLAGS="-W -Wall -Wextra -Weffc++ -Wundef -Wshadow -Wpointer-arith -Wmissing-declarations -Wwrite-strings"
fi
AC_SUBST([WARN_CXXFLAGS])

# enable extensions for the non-standard tests below
AC_USE_SYSTEM_EXTENSIONS

# check support for abi::__cxa_demangle
AX_CXX_GCC_ABI_DEMANGLE

# check support for C++11
CHECK_CXX11

AC_CACHE_CHECK([for __builtin_unreachable],
               [mg_cv_unreachable],
               [
                  mg_cv_unreachable=no
                  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([], 
                                                     [__builtin_unreachable();])], 
                                    [mg_cv_unreachable=yes])
               ])
if test x$mg_cv_unreachable = xyes; then
   AC_DEFINE([HAVE___BUILTIN_UNREACHABLE],[1],[Define to 1 if __builtin_unreachable() is available.])
fi

AC_CACHE_CHECK([which template to use for unique pointers],
               [mg_cv_uniqueptr],
               [
                  mg_cv_uniqueptr=none
                  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([@%:@include <memory>], 
                                                     [std::unique_ptr<int> x; x.reset(0); return 0;])], 
                                    [mg_cv_uniqueptr=unique_ptr])
                  if test x$mg_cv_uniqueptr = xnone; then
                    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([@%:@include <memory>], 
                                                       [std::auto_ptr<int> x; x.reset(0); return 0;])], 
                                      [mg_cv_uniqueptr=auto_ptr])
                  fi
               ])
if test x$mg_cv_uniqueptr = xnone; then
  AC_MSG_ERROR([Support for std::unique_ptr or std::auto_ptr is required.])
fi
AC_DEFINE_UNQUOTED([UNIQUE_PTR], [$mg_cv_uniqueptr], [Define to unique_ptr if available, otherwise auto_ptr])

AC_MSG_CHECKING([for the number of bits in a char])
AC_COMPUTE_INT([char_size], [CHAR_BIT], [#include <limits.h>], [char_size=unknown])
AC_MSG_RESULT([$char_size])
if test x$char_size != x8; then
  AC_MSG_ERROR([This program requires CHAR_BIT == 8, found $char_size.])
fi

# non-standard POSIX functions
AC_CHECK_FUNCS([getdtablesize fsync fdopendir getrusage])
AC_CHECK_MEMBERS([struct rusage.ru_maxrss],[],[],[@%:@include <sys/resource.h>])

if test x$ac_cv_member_struct_rusage_ru_maxrss = xyes; then
   # Linux/BSD use kilobytes; OS X uses bytes. Need to detect.
   # If using bytes, we assume that a process that does nothing
   # else but request 1MB from malloc() should not weigh
   # more than 1GB of resident size.
   AC_MSG_CHECKING([which unit rusage.ru_maxrss uses])
   AC_RUN_IFELSE(
      [AC_LANG_PROGRAM([@%:@include <sys/resource.h>
                        @%:@include <stdlib.h>
                        @%:@include <string.h>],
                       [struct rusage ru;
                        char *c = (char*)malloc(1024*1024);
                        memset(c, 42, 1024*1024);
                        getrusage(RUSAGE_SELF, &ru);
                        if (ru.ru_maxrss >= 1024*1024ll)
                           return 1;
                        return 0;
                        ])],
      [maxrssunit=kbytes], [maxrssunit=bytes], [maxrssunit=unknown])
    AC_MSG_RESULT([$maxrssunit])
    AC_DEFINE_UNQUOTED([RUSAGE_MAX_RSS_DIVIDER],
                       [`if test $maxrssunit = bytes; then echo 1024; else echo 1; fi`],
                       [Define to the value by which to divide rusage.ru_maxrss to obtain kilobytes])
fi

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

memory_list=""
m4_define([ENABLE_MEMORY], [dnl
AC_ARG_ENABLE([mem_$1], 
              [AC_HELP_STRING([--disable-mem-$1], [disable support for the $2 memory type (default enable)])],
              [], [enable_mem_$1=yes])
if test "x$enable_mem_$1" = "xyes"; then
  memory_list="$memory_list $1"
  AC_DEFINE([ENABLE_MEM_$2], [1],[Define to enable the $1 memory model])
fi
AM_CONDITIONAL([ENABLE_MEM_$2], [test "x$enable_mem_$1" = "xyes"])
])
ENABLE_MEMORY([serial], [SERIAL])
ENABLE_MEMORY([banked], [BANKED])
ENABLE_MEMORY([parallel], [PARALLEL])
ENABLE_MEMORY([ddr], [DDR])
ENABLE_MEMORY([coma], [COMA])
ENABLE_MEMORY([zlcoma], [ZLCOMA])

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
  if ! test -r "$srcdir/cacti/cacti_interface.h"; then
     AC_MSG_WARN([CACTI sources not available, cannot use CACTI.])
     enable_cacti=no
  fi
  if test "x$ax_pthread_ok" != "xyes"; then
     AC_MSG_WARN([POSIX threads not available, cannot use CACTI.])
     enable_cacti=no
  fi
fi
AM_CONDITIONAL([ENABLE_CACTI], [test "x$enable_cacti" = "xyes"])

## Check if 'slc' is available.

AC_PATH_PROG([SLC], [slc], [no], [$prefix/bin$PATH_SEPARATOR$PATH])

## Target selection

m4_define([MGS_TARGET_CHECK],
[dnl
AC_ARG_VAR([AS_$2], [Location of the `as' utility configured for the $3 binary format.])
AC_ARG_VAR([LD_$2], [Location of the `ld' utility configured for the $3 binary format.])
AC_ARG_VAR([OBJDUMP_$2], 
           [Location of the `objdump' utility configured for the $3 binary format.])

enable_[$1]_tests=no
enable_compiled_[$1]_tests=no

have_[$1]_binutils=no
AC_PATH_PROG([AS_$2], [$4-as], [no], [$prefix/bin$PATH_SEPARATOR$PATH])
AC_PATH_PROG([LD_$2], [$4-ld], [no], [$prefix/bin$PATH_SEPARATOR$PATH])
if test "x$AS_$2" != "xno" -a "x$LD_$2" != "xno"; then
   have_[$1]_binutils=yes
fi

if test "x$target_cpu" = "x$1"; then

  if test "x$have_[$1]_binutils" != xyes; then
     AC_MSG_WARN([Some $3 utilities are missing. Cannot run the $3 test suite.])
  else
     enable_[$1]_tests=yes
  fi

  AC_PATH_PROG([OBJDUMP_$2], [$4-objdump], [no], [$prefix/bin$PATH_SEPARATOR$PATH]) 
  if test "x$OBJDUMP_$2" != xno; then
     AC_DEFINE_UNQUOTED([OBJDUMP_$2], ["$OBJDUMP_$2"],
                        [Define to the location of the `objdump' utility for $3.])
  fi

  if test "x$SLC" != "x:" ; then
    AC_MSG_CHECKING([whether $SLC supports -b $5])
    AC_LANG_CONFTEST([AC_LANG_SOURCE([int main(void) { return 0; }])])
    if $SLC -b $5 -c -o conftest.o conftest.c >/dev/null 2>&1; then
      AC_MSG_RESULT([yes])
      enable_compiled_[$1]_tests=yes
    else
      AC_MSG_RESULT([no])
      AC_MSG_WARN([borked $SLC -b $5. Cannot run the compiled $3 test suite.])
    fi
  else
    AC_MSG_WARN([slc not found. Cannot run the compiled $3 test suite.])
  fi

  target_found=yes
  AC_DEFINE([TARGET_$2], [1],
            [Define when building a simulator using the $3 core model.])

fi

AM_CONDITIONAL([HAVE_$2_BINUTILS], [test x$have_$1_binutils = xyes])
AM_CONDITIONAL([ENABLE_$2_TESTS], [test x$enable_$1_tests = xyes])
AM_CONDITIONAL([ENABLE_COMPILED_$2_TESTS], [test x$enable_compiled_$1_tests = xyes])


])

enable_compiled_tests=no
target_found=no

MGS_TARGET_CHECK([mtalpha], [MTALPHA], [MT-Alpha], [mtalpha-linux-gnu], [mta_n])
MGS_TARGET_CHECK([mtsparc], [MTSPARC], [MT-SPARC], [mtsparc-linux-gnu], [mtsn_n])
MGS_TARGET_CHECK([mips], [MIPS32], [MIPS (no MT)], [mips-linux-gnu], [mips_s])
MGS_TARGET_CHECK([mipsel], [MIPS32EL], [MIPSel (no MT)], [mipsel-linux-gnu], [mipsel_s])

if test $target_found = no; then
  AC_MSG_ERROR([Unsupported simulated core model: $target_cpu (did you use --target?)])
fi

AC_SUBST([GET_TEST_LIST],['$(TEST_LIST)'])

AC_CONFIG_FILES([
	Makefile
	tools/Makefile
	programs/Makefile
	doc/Makefile
])
AC_CONFIG_FILES([tools/readtrace], [chmod +x tools/readtrace])
AC_CONFIG_FILES([tools/viewlog], [chmod +x tools/viewlog])

AC_OUTPUT

cat <<EOF
*
* Configuration done.
*
* ISA target:             $target_cpu
* Memory models:          $memory_list
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
* MT-Alpha tests:         (asm) $enable_mtalpha_tests (compiled) $enable_compiled_mtalpha_tests
* MT-SPARC tests:         (asm) $enable_mtsparc_tests (compiled) $enable_compiled_mtsparc_tests
* MIPS tests:             (asm) $enable_mips_tests (compiled) $enable_compiled_mips_tests
* MIPSel tests:           (asm) $enable_mipsel_tests (compiled) $enable_compiled_mipsel_tests
*
* C++ compiler:           $CXX $CPPFLAGS $WARN_CXXFLAGS $AM_CXXFLAGS $CXXFLAGS $LDFLAGS
* Destination dir:        $prefix
*
Now run:

    make all
    make check

    # then optionally:
    make install

    # to generate PDF versions of the manual pages:
    cd doc && make pdf

EOF

