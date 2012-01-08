SVN = svn
bin_PROGRAMS =
dist_man1_MANS = 
MAINTAINERCLEANFILES =
EXTRA_DIST = 

include $(srcdir)/Makefile.inc

SUBDIRS = . tools tests programs doc

DISTCHECK_CONFIGURE_FLAGS = --target=mtalpha
ACLOCAL_AMFLAGS = -I build-aux

##
## Multi-way distcheck
##
check-mtsparc:
	mkdir -p build-mtsparc && \
	  cdir=`cd $(top_srcdir) && pwd` && \
	  cd build-mtsparc && \
	  $$cdir/configure --target=mtsparc && \
	  $(MAKE) $(AM_MAKEFLAGS) check

check-mtalpha:
	mkdir -p build-mtalpha && \
	  cdir=`cd $(top_srcdir) && pwd` && \
	  cd build-mtalpha && \
	  $$cdir/configure --target=mtalpha && \
	  $(MAKE) $(AM_MAKEFLAGS) check

check-all: check-mtsparc check-mtalpha

distcheck-mtsparc:
	$(MAKE) $(AM_MAKEFLAGS) distcheck DISTCHECK_CONFIGURE_FLAGS='--target=mtsparc --disable-sdl'
	$(MAKE) $(AM_MAKEFLAGS) distcheck DISTCHECK_CONFIGURE_FLAGS='--target=mtsparc --enable-sdl'

distcheck-mtalpha:
	$(MAKE) $(AM_MAKEFLAGS) distcheck DISTCHECK_CONFIGURE_FLAGS='--target=mtalpha --disable-sdl'
	$(MAKE) $(AM_MAKEFLAGS) distcheck DISTCHECK_CONFIGURE_FLAGS='--target=mtalpha --enable-sdl'

distcheck-all: distcheck-mtsparc distcheck-mtalpha

.PHONY: check-all distcheck-all

include $(srcdir)/build-aux/version.mk

dist-hook: check-version
	## Ensure that CACTI is not bundled, as its license does not permit redistribution.
	rm -rf $(distdir)/cacti

	## prepare the version file
	echo $(VERSION) >$(distdir)/build-aux/tarball-version
	##
	## we generate sources during configuration, which confuses Automake
	## wrt distribution. So be sure that they are not present during "make
	## dist".
	##
	for f in $(CONFIG_CLEAN_FILES) $(DISTCLEANFILES); do rm -f $(distdir)/$$f; done

ChangeLog:
	cd $(srcdir); \
	if test -e .git; then \
	     git log --stat --graph >$@ || (rm -f $@; false); \
	else \
	     echo "No source for ChangeLog. Cannot generate." >&2; \
	     exit 1; \
	fi
