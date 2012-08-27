CLI_SOURCES = \
	cli/commands.h \
        cli/tables.cpp \
        cli/aliases.cpp \
        cli/commandline.cpp \
        cli/stepsystem.cpp \
        cli/cmd_breakpoint.cpp \
        cli/cmd_trace.cpp \
        cli/cmd_show.cpp \
        cli/cmd_misc.cpp \
        cli/cmd_sim.cpp \
        cli/cmd_component.cpp \
	cli/lookup.cpp \
	cli/simreadline.h \
	cli/simreadline.cpp \
	cli/main.cpp

CLI_EXTRA_CPPFLAGS = -I$(srcdir)/cli

