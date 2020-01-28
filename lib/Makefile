NETGENDIR = ..

include ${NETGENDIR}/defs.mak

LIB_FILES = ntk2adl.sh spice spice.bot spice.top

LIB_INST_FILES= $(LIB_FILES:%=$(DESTDIR)${INSTALL_LIBDIR}/netgen/%)
BIN_INST_FILES= $(BIN_FILES:%=$(DESTDIR)${INSTALL_BINDIR}/%)

install-tcl: ${LIB_INST_FILES}

install: ${LIB_INST_FILES}

$(DESTDIR)${INSTALL_LIBDIR}/netgen/%: %
	../scripts/mkdirs $(DESTDIR)${INSTALL_LIBDIR}/netgen
	${RM} $(DESTDIR)${INSTALL_LIBDIR}/netgen/$*
	${CP} $* $(DESTDIR)${INSTALL_LIBDIR}/netgen/$*
