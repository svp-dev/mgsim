SOURCES = \
	commands.h \
	commands.cpp \
	simreadline.h \
	simreadline.cpp \
	main.cpp

AM_CPPFLAGS = @AM_CPPFLAGS@ -DMGSIM_CONFIG_PATH=\"$(pkgdatadir)/config.ini\"

bin_PROGRAMS = mgsim
dist_man1_MANS = mgsim.1

mgsim_SOURCES = $(SOURCES)
mgsim_LDADD = \
    $(top_builddir)/arch/libarch.a \
    $(top_builddir)/arch/libarch_zlcoma.a \
    $(top_builddir)/arch/libarch_mlcoma.a \
    $(top_builddir)/sim/libsim.a

## 
## Manual page rules
##
$(dist_man1_MANS): $(top_srcdir)/.version
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) $(@:.1=)
	$(AM_V_GEN)$(HELP2MAN) -N --output=$@ \
	    `test -r $(srcdir)/$(@:.1=) && echo $(srcdir) || echo .`/$(@:.1=)

MAINTAINERCLEANFILES = $(dist_man1_MANS)
