AM_CFLAGS =	$(GLIB_CFLAGS)				\
		$(GIO_CFLAGS)				\
		$(DLEYNA_CORE_CFLAGS)			\
		-I$(top_builddir)/libdleyna/diagnostics	\
		-include config.h

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}
		
libexec_PROGRAMS = dleyna-diagnostics-service

dleyna_diagnostics_service_SOURCES =	daemon.c

dleyna_diagnostics_service_LDADD =	$(GLIB_LIBS)							\
					$(GIO_LIBS)							\
					$(DLEYNA_CORE_LIBS)						\
					$(top_builddir)/libdleyna/diagnostics/libdleyna-diagnostics-1.0.la

dbusservicedir = $(DBUS_SERVICE_DIR)
dbusservice_in_files = com.intel.dleyna-diagnostics.service.in
dbusservice_DATA = com.intel.dleyna-diagnostics.service

# Replace the 'libexecdir' marker with its fully expanded value
%.service: %.service.in Makefile
	$(AM_V_GEN) $(SED) -e "s|\@libexecdir\@|$(libexecdir)|" $< > $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = dleyna-diagnostics-service-1.0.pc

EXTRA_DIST = $(dbusservice_in_files)

CLEANFILES = $(dbusservice_DATA)
DISTCLEANFILES = $(dbusservice_DATA)
