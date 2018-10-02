NETGENDIR = ..

include ${NETGENDIR}/defs.mak

MANDIRS = $(DESTDIR)${MANDIR} $(DESTDIR)${MANDIR}/man1
DOCDIRS = $(DESTDIR)${DOCDIR}

MANFILES = 
DOCFILES = $(DESTDIR)${DOCDIR}/netgen.doc

install-tcl: ${DOCDIRS} ${DOCFILES}

install: ${DOCDIRS} ${DOCFILES}

${MANDIRS}: make-man-dirs

${DOCDIRS}: make-doc-dirs

make-man-dirs:
	${SCRIPTS}/mkdirs ${MANDIRS}

make-doc-dirs:
	${SCRIPTS}/mkdirs ${DOCDIRS}

# This file does not actually exist. . .
$(DESTDIR)${MANDIR}/man1/netgen.1: netgen.1 ${MANDIRS}
	${CP} netgen.1 $(DESTDIR)${MANDIR}/man1/netgen.1

$(DESTDIR)${DOCDIR}/netgen.doc: netgen.doc ${DOCDIRS}
	${CP} netgen.doc $(DESTDIR)${DOCDIR}/netgen.doc
