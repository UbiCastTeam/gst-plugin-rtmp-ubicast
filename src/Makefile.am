# Note: plugindir is set in configure

plugin_LTLIBRARIES = libgstrtmp.la

# sources used to compile this plug-in
libgstrtmp_la_SOURCES = gstrtmpsink.c gstrtmpsink.h gstrtmpsrc.c gstrtmpsrc.h gstrtmp.c

# compiler and linker flags used to compile this plugin, set in configure.ac
libgstrtmp_la_CFLAGS = $(GST_CFLAGS) $(SOUP_CFLAGS) $(RTMP_CFLAGS)
libgstrtmp_la_LIBADD = $(GST_LIBS) $(SOUP_LIBS) $(RTMP_LIBS)
libgstrtmp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrtmp_la_LIBTOOLFLAGS = --tag=disable-static

# headers we need but don't want installed
noinst_HEADERS = gstrtmpsink.h gstrtmpsrc.h
