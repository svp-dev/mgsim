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

##
## Version number management
##

EXTRA_DIST += .version build-aux/version-gen build-aux/package-version build-aux/config.sub.patch
BUILT_SOURCES = $(top_srcdir)/.version

$(top_srcdir)/.version:
	echo $(VERSION) >$@-t && mv $@-t $@

dist-hook: check-version
	echo $(VERSION) >$(distdir)/build-aux/tarball-version

install-recursive install-exec-recursive install-data-recursive installcheck-recursive: check-version

VERSION_GEN = (cd $(top_srcdir); build-aux/version-gen \
	build-aux/tarball-version build-aux/package-version)

.PHONY: check-version _version
check-version:
	set -e; \
	if ! test "x$(VERSION)" = "x`$(VERSION_GEN)`"; then \
	   echo "Version string not up to date: run 'make _version' first." >&2; \
	   exit 1; \
	fi

_version:
	cd $(srcdir) && rm -rf autom4te.cache .version && $${AUTORECONF:-autoreconf}
	$(MAKE) $(AM_MAKEFLAGS) .version

