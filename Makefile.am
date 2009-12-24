SUBDIRS = . doc src programs

ACLOCAL_AMFLAGS = -I m4

##
## Version number management
##

EXTRA_DIST = .version
BUILT_SOURCES = $(top_srcdir)/.version

$(top_srcdir)/.version:
	echo $(VERSION) >$@-t && mv $@-t $@

dist-hook: check-version
	echo $(VERSION) >$(distdir)/build-aux/tarball-version

install-recursive install-exec-recursive install-data-recursive installcheck-recursive: check-version

VERSION_GEN = (cd $(top_srcdir); build-aux/version-gen \
	build-aux/tarball-version build-aux/package-version)

.PHONY: check-version
check-version:
	set -e; \
	if ! test "x$(VERSION)" = "x`$(VERSION_GEN)`"; then \
	   echo "Version string not up to date, reconfing..." >&2; \
	   ( cd "$(top_srcdir)" && rm -rf autom4te.cache && $${AUTORECONF:-autoreconf} ) >&2; \
	   echo "Done, now run make again with your intended rule." >&2; exit 1; \
	fi
