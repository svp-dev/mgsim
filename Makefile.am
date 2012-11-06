SUBDIRS = tools programs doc .

DISTCHECK_CONFIGURE_FLAGS = --target=mtalpha
ACLOCAL_AMFLAGS = -I build-aux

# Preset variables so they can be incremented by
# the included Makefiles below.
bin_PROGRAMS =
dist_man1_MANS = 
CLEANFILES = 
MAINTAINERCLEANFILES =
EXTRA_DIST = 

##
## Multi-way distcheck
##
CHECK_TARGETS = mtsparc mtalpha mips mipsel

check-target-%:
	mkdir -p build-$* && \
	  cdir=`cd $(top_srcdir) && pwd` && \
	  cd build-$* && \
	  $$cdir/configure --target=$* && \
	  $(MAKE) $(AM_MAKEFLAGS) check

smoketest-target-%:
	mkdir -p build-$* && \
	  cdir=`cd $(top_srcdir) && pwd` && \
	  cd build-$* && \
	  $$cdir/configure $(SMOKETEST_CONFIGURE_FLAGS) --target=$* && \
	  $(MAKE) $(AM_MAKEFLAGS) smoketest

distcheck-target-%:
	$(MAKE) $(AM_MAKEFLAGS) distcheck DISTCHECK_CONFIGURE_FLAGS='--target=$* --disable-sdl'
	$(MAKE) $(AM_MAKEFLAGS) distcheck DISTCHECK_CONFIGURE_FLAGS='--target=$* --enable-sdl'

smoketest-all: $(foreach T,$(CHECK_TARGETS),smoketest-target-$(T))
check-all: $(foreach T,$(CHECK_TARGETS),check-target-$(T))
discheck-all: $(foreach T,$(CHECK_TARGETS),distcheck-target-$(T))

.PHONY: check-all distcheck-all smoketest-all

include $(srcdir)/build-aux/version.mk

dist-hook: check-version
	echo $(VERSION) >$(distdir)/build-aux/tarball-version

.PHONY: ChangeLog

ChangeLog:
	$(AM_V_GEN)if test -e $(srcdir)/.git; then \
	     (cd $(srcdir) && git log --stat --graph) >$@.tmp || (rm -f $@.tmp; false); \
	     if ! (test -r $(srcdir)/$@ && cmp $@.tmp $(srcdir)/$@) >/dev/null 2>&1; then mv -f $@.tmp $@; else rm -f $@.tmp; fi; \
	fi

## General build rules.
include $(srcdir)/Makefile.inc

## General test rules. This must be at the end because it sets
## SECONDEXPANSION.
include $(srcdir)/tests/Makefile.inc
