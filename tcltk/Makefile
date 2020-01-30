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
	$(DESTDIR)${INSTALL_TCLDIR}/tkcon.tcl \
	$(DESTDIR)${INSTALL_TCLDIR}/console.tcl \
	$(DESTDIR)${INSTALL_TCLDIR}/netgen.tcl

tcl-main: netgenexec${EXEEXT} netgen.tcl netgen.sh

install-tcl: netgenexec${EXEEXT} $(DESTDIR)${INSTALL_BINDIR}/netgen.sh ${TCL_FILES}
	${RM} $(DESTDIR)${INSTALL_TCLDIR}/netgenexec${EXEEXT}
	${CP} netgenexec${EXEEXT} $(DESTDIR)${INSTALL_TCLDIR}/netgenexec${EXEEXT}

netgenexec${EXEEXT}: netgenexec.c
	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS_NOSTUB} netgenexec.c \
		-o netgenexec${EXEEXT} \
		${LDFLAGS} ${LIBS} ${LIB_SPECS_NOSTUB}

netgen.tcl: netgen.tcl.in
	sed -e 's%TCL_DIR%${TCLDIR}%g' \
	    -e 's%SHDLIB_EXT%${SHDLIB_EXT}%g' \
	    netgen.tcl.in > netgen.tcl

netgen.sh: netgen.sh.in
	sed -e 's%TCL_DIR%${TCLDIR}%g' \
	    -e 's%PY_DIR%${PYDIR}%g' \
	    -e 's%TCLLIB_DIR%${TCL_LIB_DIR}%g' \
	    -e 's%WISH_EXE%${WISH_EXE}%g' \
	    netgen.sh.in > netgen.sh

$(DESTDIR)${INSTALL_TCLDIR}/%: %
	${RM} $(DESTDIR)${INSTALL_TCLDIR}/$*
	${CP} $* $(DESTDIR)${INSTALL_TCLDIR}/$*

$(DESTDIR)${INSTALL_BINDIR}/netgen.sh: netgen.sh
	${RM} $(DESTDIR)${INSTALL_BINDIR}/netgen.sh $(DESTDIR)${INSTALL_BINDIR}/netgen
	${CP} netgen.sh $(DESTDIR)${INSTALL_BINDIR}/netgen
	(cd $(DESTDIR)${INSTALL_BINDIR}; chmod 0755 netgen)

include ${NETGENDIR}/rules.mak
