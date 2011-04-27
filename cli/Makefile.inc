CLI_SOURCES = \
	cli/commands.h \
	cli/commands.cpp \
	cli/simreadline.h \
	cli/simreadline.cpp \
	cli/main.cpp

CLI_EXTRA_CPPFLAGS = -I$(srcdir)/cli -DMGSIM_CONFIG_PATH=\"$(pkgdatadir)/config.ini\"

