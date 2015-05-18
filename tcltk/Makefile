MODULE = tcltk
NETGENDIR = ..
SRCS = tclnetgen.c

include ${NETGENDIR}/defs.mak

EXTRA_LIBS = ${MAIN_EXTRA_LIBS}

DFLAGS += -DNETGEN_DATE="\"`date`\""
LIBS += -lm
CLEANS += netgen.sh netgen.tcl netgenexec${EXEEXT}
CFLAGS += -I${NETGENDIR}/base

TCL_FILES = \
	$(DESTDIR)${TCLDIR}/tkcon.tcl \
	$(DESTDIR)${TCLDIR}/console.tcl \
	$(DESTDIR)${TCLDIR}/netgen.tcl

tcl-main: netgenexec${EXEEXT} netgen.tcl netgen.sh

install-tcl: netgenexec${EXEEXT} $(DESTDIR)${BINDIR}/netgen.sh ${TCL_FILES}
	${RM} $(DESTDIR)${TCLDIR}/netgenexec${EXEEXT}
	${CP} netgenexec${EXEEXT} $(DESTDIR)${TCLDIR}/netgenexec${EXEEXT}

netgenexec${EXEEXT}: netgenexec.c
	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS} netgenexec.c -o netgenexec${EXEEXT} \
		${LDFLAGS} ${LIBS} ${LIB_SPECS}

netgen.tcl: netgen.tcl.in
	sed -e /TCL_DIR/s%TCL_DIR%${TCLDIR}%g \
	    -e /SHDLIB_EXT/s%SHDLIB_EXT%${SHDLIB_EXT}%g \
	    netgen.tcl.in > netgen.tcl

netgen.sh: netgen.sh.in
	sed -e /TCL_DIR/s%TCL_DIR%${TCLDIR}%g \
	    -e /TCLLIB_DIR/s%TCLLIB_DIR%${TCL_LIB_DIR}%g \
	    -e /WISH_EXE/s%WISH_EXE%${WISH_EXE}%g \
	    netgen.sh.in > netgen.sh

$(DESTDIR)${TCLDIR}/%: %
	${RM} $(DESTDIR)${TCLDIR}/$*
	${CP} $* $(DESTDIR)${TCLDIR}/$*

$(DESTDIR)${BINDIR}/netgen.sh: netgen.sh
	${RM} $(DESTDIR)${BINDIR}/netgen.sh $(DESTDIR)${BINDIR}/netgen
	${CP} netgen.sh $(DESTDIR)${BINDIR}/netgen
	(cd $(DESTDIR)${BINDIR}; chmod 0755 netgen)

include ${NETGENDIR}/rules.mak
