NETGENDIR = ..

include ${NETGENDIR}/defs.mak

LIB_FILES = ntk2adl.sh spice spice.bot spice.top

LIB_INST_FILES= $(LIB_FILES:%=$(DESTDIR)${LIBDIR}/netgen/%)
BIN_INST_FILES= $(BIN_FILES:%=$(DESTDIR)${BINDIR}/%)

install-tcl: ${LIB_INST_FILES}

install: ${LIB_INST_FILES}

$(DESTDIR)${LIBDIR}/netgen/%: %
	../scripts/mkdirs $(DESTDIR)${LIBDIR}/netgen
	${RM} $(DESTDIR)${LIBDIR}/netgen/$*
	${CP} $* $(DESTDIR)${LIBDIR}/netgen/$*
