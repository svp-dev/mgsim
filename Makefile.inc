include $(srcdir)/sim/Makefile.inc
include $(srcdir)/arch/Makefile.inc
include $(srcdir)/cli/Makefile.inc

bin_PROGRAMS += mgsim
dist_man1_MANS += mgsim.1

mgsim_SOURCES = $(SIM_SOURCES) $(ARCH_SOURCES) $(CLI_SOURCES)
mgsim_CPPFLAGS = $(SIM_EXTRA_CPPFLAGS) $(ARCH_EXTRA_CPPFLAGS) $(CLI_EXTRA_CPPFLAGS) $(AM_CPPFLAGS)
mgsim_CXXFLAGS = $(SIM_EXTRA_CXXFLAGS) $(ARCH_EXTRA_CXXFLAGS) $(CLI_EXTRA_CXXFLAGS) $(AM_CXXFLAGS)

## 
## Manual page rules
##
$(dist_man1_MANS): $(top_srcdir)/.version
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) $(@:.1=)
	$(AM_V_GEN)$(HELP2MAN) -N --output=$@ \
	    `test -r $(srcdir)/$(@:.1=) && echo $(srcdir) || echo .`/$(@:.1=)

MAINTAINERCLEANFILES += $(dist_man1_MANS)
