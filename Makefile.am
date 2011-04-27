bin_PROGRAMS =
dist_man1_MANS = 
MAINTAINERCLEANFILES =

include $(srcdir)/Makefile.inc

SUBDIRS = . tools programs doc

ACLOCAL_AMFLAGS = -I build-aux

##
## Version number management
##

EXTRA_DIST = .version build-aux/version-gen build-aux/package-version
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
