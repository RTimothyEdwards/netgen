NETGENDIR = ..

include ${NETGENDIR}/defs.mak

MANDIRS = $(DESTDIR)${INSTALL_MANDIR} $(DESTDIR)${INSTALL_MANDIR}/man1
DOCDIRS = $(DESTDIR)${INSTALL_DOCDIR}

MANFILES = 
DOCFILES = $(DESTDIR)${INSTALL_DOCDIR}/netgen.doc

install-tcl: ${DOCDIRS} ${DOCFILES}

install: ${DOCDIRS} ${DOCFILES}

${MANDIRS}: make-man-dirs

${DOCDIRS}: make-doc-dirs

make-man-dirs:
	${SCRIPTS}/mkdirs ${MANDIRS}

make-doc-dirs:
	${SCRIPTS}/mkdirs ${DOCDIRS}

# This file does not actually exist. . .
$(DESTDIR)${INSTALL_MANDIR}/man1/netgen.1: netgen.1 ${MANDIRS}
	${CP} netgen.1 $(DESTDIR)${INSTALL_MANDIR}/man1/netgen.1

$(DESTDIR)${INSTALL_DOCDIR}/netgen.doc: netgen.doc ${DOCDIRS}
	${CP} netgen.doc $(DESTDIR)${INSTALL_DOCDIR}/netgen.doc
