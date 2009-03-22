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

AC_CONFIG_FILES([
	Makefile
	doc/Makefile
	src/Makefile
	programs/Makefile
])
AC_OUTPUT
