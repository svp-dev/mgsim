dist_pkgdata_DATA = config.ini nobounds.ini mgsim.h mtconf.h mtconf.c gfxlib.h gfxlib.c

CLEANFILES = nobounds.ini

nobounds.ini: $(srcdir)/config.ini
	$(SED) -e 's/QueueSize[ =].*/QueueSize = -1/g;s/BufferSize[ =].*/BufferSize = -1/g' >$@ <$<
