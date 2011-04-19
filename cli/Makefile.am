bin_PROGRAMS = 
noinst_PROGRAMS = 
dist_man1_MANS =

FLAGS_OPT = -O3 -DNDEBUG
FLAGS_DEBUG = -g -DDEBUG 

FLAGS_COMMON = -W -Wall -Wwrite-strings -Wno-unused \
       -DMGSIM_CONFIG_PATH=\"$(pkgdatadir)/config.ini\"

SOURCES = \
	commands.h \
	commands.cpp \
	simreadline.h \
	simreadline.cpp \
	main.cpp

if ENABLE_MTALPHA
bin_PROGRAMS += mgsim-alpha
noinst_PROGRAMS += mgsim-alpha.dbg 
dist_man1_MANS += mgsim-alpha.1

mgsim_alpha_SOURCES = $(SPECIAL_SRC)
mgsim_alpha_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_OPT) $(FLAGS_COMMON)

mgsim_alpha_dbg_SOURCES = $(SPECIAL_SRC)
mgsim_alpha_dbg_CXXFLAGS = $(FLAGS_ALPHA) $(FLAGS_DEBUG) $(FLAGS_COMMON)
endif

if ENABLE_MTSPARC
bin_PROGRAMS += mgsim-sparc
noinst_PROGRAMS += mgsim-sparc.dbg
dist_man1_MANS += mgsim-sparc.1

mgsim_sparc_SOURCES = $(SOURCES)
mgsim_sparc_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_OPT) $(FLAGS_COMMON)

mgsim_sparc_dbg_SOURCES = $(SOURCES)
mgsim_sparc_dbg_CXXFLAGS = $(FLAGS_SPARC) $(FLAGS_DEBUG) $(FLAGS_COMMON)
endif

## 
## Manual page rules
##
$(dist_man1_MANS): $(top_srcdir)/.version
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) $(@:.1=)
	$(AM_V_GEN)$(HELP2MAN) -N --output=$@ \
	    `test -r $(srcdir)/$(@:.1=) && echo $(srcdir) || echo .`/$(@:.1=)

MAINTAINERCLEANFILES = $(dist_man1_MANS)
