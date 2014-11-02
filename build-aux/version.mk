##
## Version number management
##
# (inspired from the same code in sl-core)

EXTRA_DIST += .version build-aux/git-version-gen
BUILT_SOURCES += $(top_srcdir)/.version

$(top_srcdir)/.version:
	echo $(VERSION) >$@-t && mv $@-t $@

## dist-hook overridden in Makefile.am
#dist-hook: check-version
#	echo $(VERSION) >$(distdir)/build-aux/tarball-version

install-recursive install-exec-recursive install-data-recursive installcheck-recursive: check-version

VERSION_GEN = (cd $(top_srcdir) \
	       && build-aux/git-version-gen build-aux/tarball-version s/$(PACKAGE_NAME)-/v/ $(PACKAGE_NAME))

.PHONY: check-version _version
check-version:
	set -e; \
	if ! test "x$(VERSION)" = "x`$(VERSION_GEN)`"; then \
	   echo "Version string not up to date: run 'make _version' first." >&2; \
	   exit 1; \
	fi

_version:
	cd $(srcdir) && rm -rf autom4te.cache .version && $${AUTORECONF:-autoreconf}
	$(MAKE) .version
