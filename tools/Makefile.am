bin_SCRIPTS = readtrace viewlog
dist_man1_MANS = readtrace.1 viewlog.1

nodist_noinst_SCRIPTS = aslink compile
dist_noinst_SCRIPTS = timeout runtest.sh

readtrace.1: readtrace.in
	$(AM_V_GEN)$(HELP2MAN) -N --output=$@ ./readtrace

viewlog.1: viewlog.in
	$(AM_V_GEN)$(HELP2MAN) -N --output=$@ ./viewlog

MAINTAINERCLEANFILES = $(dist_man1_MANS)
