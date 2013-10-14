libdleyna_diagnosticsincdir = $(includedir)/dleyna-1.0/libdleyna/diagnostics

DLEYNA_DIAGNOSTICS_VERSION = 1:0:0

AM_CFLAGS =	$(GLIB_CFLAGS)				\
		$(GIO_CFLAGS)				\
		$(DLEYNA_CORE_CFLAGS)			\
		$(GSSDP_CFLAGS)				\
		$(GUPNP_CFLAGS)				\
		$(SOUP_CFLAGS)				\
		$(LIBXML_CFLAGS)			\
		-include config.h

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

pkglib_LTLIBRARIES = libdleyna-diagnostics-1.0.la

libdleyna_diagnosticsinc_HEADERS = 	control-point-diagnostics.h

libdleyna_diagnostics_1_0_la_LDFLAGS =	-version-info $(DLEYNA_DIAGNOSTICS_VERSION) \
					-no-undefined

libdleyna_diagnostics_1_0_la_SOURCES =	$(libdleyna_diagnosticsinc_HEADERS) \
					async.c				\
					device.c			\
					manager.c			\
					server.c			\
					task.c				\
					upnp.c				\
					xml-util.c

libdleyna_diagnostics_1_0_la_LIBADD =	$(GLIB_LIBS)		\
					$(GIO_LIBS)		\
					$(DLEYNA_CORE_LIBS)	\
					$(GSSDP_LIBS)		\
					$(GUPNP_LIBS)		\
					$(SOUP_LIBS)		\
					$(LIBXML_LIBS)

MAINTAINERCLEANFILES =	Makefile.in		\
			aclocal.m4		\
			configure		\
			config.h.in		\
			config.h.in~		\
			build-aux/depcomp	\
			build-aux/compile	\
			build-aux/missing	\
			build-aux/install-sh

sysconf_DATA = dleyna-diagnostics-service.conf

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = dleyna-diagnostics-1.0.pc

EXTRA_DIST = 	$(sysconf_DATA)			\
		async.h				\
		device.h			\
		prop-defs.h			\
		manager.h			\
		server.h			\
		task.h				\
		upnp.h				\
		xml-util.h

CLEANFILES = $(pkgconfig_DATA) dleyna-diagnostics-service.conf
DISTCLEANFILES = $(pkgconfig_DATA) dleyna-diagnostics-service.conf

maintainer-clean-local:
	rm -rf build-aux
