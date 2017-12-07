MODULE = netgen
NETGENDIR = ..
SRCS = netgen_main.c

include ${NETGENDIR}/defs.mak

EXTRA_LIBS = ${NETGENDIR}/base/libbase.o \
	     ${MAIN_EXTRA_LIBS}

DFLAGS += ${GR_DFLAGS}
DFLAGS += -DNETGEN_DATE="\"`date`\""

LIBS += ${GR_LIBS} -lm
CFLAGS += ${GR_CFLAGS} -I${NETGENDIR}/base
CLEANS += netgen netcomp ntk2adl inetcomp ntk2xnf

main: netgen netcomp inetcomp ntk2adl ntk2xnf

tcl-main: tclnetgen${SHDLIB_EXT}

tclnetgen${SHDLIB_EXT}: ${EXTRA_LIBS}
	@echo --- making netgen Tcl library \(tclnetgen${SHDLIB_EXT}\)
	${RM} tclnetgen${SHDLIB_EXT}
	${CC} ${CFLAGS} ${CPPFLAGS} -o $@ ${LDDL_FLAGS} \
		${LD_RUN_PATH} ${EXTRA_LIBS} ${LD_EXTRA_LIBS} \
		-lc ${LIBS} ${LIB_SPECS} ${LDFLAGS}

$(DESTDIR)${BINDIR}/netgen:
	${RM} $@
	${CP} netgen $@

$(DESTDIR)${BINDIR}/ntk2adl:
	${RM} $@
	${CP} ntk2adl $@

$(DESTDIR)${BINDIR}/ntk2xnf:
	${RM} $@
	${CP} ntk2xnf $@

$(DESTDIR)${BINDIR}/netcomp:
	${RM} $@
	${CP} netcomp $@

$(DESTDIR)${BINDIR}/inetcomp:
	${RM} $@
	${CP} inetcomp $@

netcomp: netcomp.c
	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS} netcomp.c -o $@ ${EXTRA_LIBS} \
		${LIBS} ${LDFLAGS}

inetcomp: inetcomp.c
	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS} inetcomp.c -o $@ ${EXTRA_LIBS} \
		${LIBS} ${LDFLAGS}

ntk2adl: ntk2adl.c
	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS} ntk2adl.c -o $@ ${EXTRA_LIBS} \
		${LIBS} ${LDFLAGS}

ntk2xnf: ntk2xnf.c
	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS} ntk2xnf.c -o $@ ${EXTRA_LIBS} \
		${LIBS} ${LDFLAGS}

install: $(DESTDIR)${BINDIR}/netgen $(DESTDIR)${BINDIR}/netcomp \
		$(DESTDIR)${BINDIR}/inetcomp $(DESTDIR)${BINDIR}/ntk2adl \
		$(DESTDIR)${BINDIR}/ntk2xnf

install-tcl: $(DESTDIR)${TCLDIR} $(DESTDIR)${TCLDIR}/tclnetgen${SHDLIB_EXT}

$(DESTDIR)${TCLDIR}/tclnetgen${SHDLIB_EXT}: tclnetgen${SHDLIB_EXT}
	${RM} $(DESTDIR)${TCLDIR}/tclnetgen${SHDLIB_EXT}
	${CP} tclnetgen${SHDLIB_EXT} $(DESTDIR)${TCLDIR}/tclnetgen${SHDLIB_EXT}

include ${NETGENDIR}/rules.mak
