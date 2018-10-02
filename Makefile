#
# Makefile redesigned for netgen-1.3
#

NETGENDIR = .
PROGRAMS = netgen python
MODULES = base

MAKEFLAGS =
INSTALL_CAD_DIRS = lib doc

include defs.mak

all:	$(ALL_TARGET)

standard:
	@echo --- errors and warnings logged in file make.log
	@${MAKE} mains 2>&1 | tee -a make.log

tcl:
	@echo --- errors and warnings logged in file make.log
	@${MAKE} tcllibrary 2>&1 | tee -a make.log

force:	clean all

defs.mak:
	@echo No \"defs.mak\" file found.  Run "configure" to make one.

config:
	${NETGENDIR}/configure

tcllibrary: modules
	@echo --- making Tcl shared-object libraries
	for dir in ${PROGRAMS}; do \
		(cd $$dir && ${MAKE} tcl-main); done

mains: modules
	@echo --- making main programs
	for dir in ${PROGRAMS}; do \
		(cd $$dir && ${MAKE} main); done

modules:
	@echo --- making modules
	for dir in ${MODULES}; do \
		(cd $$dir && ${MAKE} module); done

depend:
	for dir in ${MODULES} ${PROGRAMS}; do \
		(cd $$dir && ${MAKE} depend); done

install: $(INSTALL_TARGET)

install-netgen:
	@echo --- installing executable to $(DESTDIR)${BINDIR}
	@echo --- installing run-time files to $(DESTDIR)${LIBDIR}
	@${MAKE} install-real >> install.log

install-real: install-dirs
	for dir in ${PROGRAMS} ${INSTALL_CAD_DIRS}; do \
		(cd $$dir && ${MAKE} install); done

install-tcl-dirs:
	${NETGENDIR}/scripts/mkdirs $(DESTDIR)${BINDIR} \
		$(DESTDIR)${TCLDIR} $(DESTDIR)${PYDIR}

install-dirs:
	${NETGENDIR}/scripts/mkdirs $(DESTDIR)${BINDIR}

install-tcl: install-dirs
	@echo --- installing executable to $(DESTDIR)${BINDIR}
	@echo --- installing run-time files to $(DESTDIR)${LIBDIR}
	@${MAKE} install-tcl-real 2>&1 >> install.log

install-tcl-real: install-tcl-dirs
	for dir in ${INSTALL_CAD_DIRS} ${PROGRAMS}; do \
		(cd $$dir && ${MAKE} install-tcl); done

clean:
	for dir in ${MODULES} ${PROGRAMS} ${UNUSED_MODULES}; do \
		(cd $$dir && ${MAKE} clean); done
	${RM} *.tmp */*.tmp *.sav */*.sav *.log TAGS tags

distclean:
	touch defs.mak
	@${MAKE} clean
	${RM} defs.mak old.defs.mak ${NETGENDIR}/scripts/defs.mak
	${RM} ${NETGENDIR}/scripts/default.conf
	${RM} ${NETGENDIR}/scripts/config.log ${NETGENDIR}/scripts/config.status
	${RM} scripts/netgen.spec netgen-`cat VERSION` netgen-`cat VERSION`.tgz
	${RM} *.log

dist:
	${RM} scripts/netgen.spec netgen-`cat VERSION` netgen-`cat VERSION`.tgz
	sed -e /@VERSION@/s%@VERSION@%`cat VERSION`% \
	    scripts/netgen.spec.in > scripts/netgen.spec
	ln -nsf . netgen-`cat VERSION`
	tar zchvf netgen-`cat VERSION`.tgz --exclude CVS \
	    --exclude netgen-`cat VERSION`/netgen-`cat VERSION` \
	    --exclude netgen-`cat VERSION`/netgen-`cat VERSION`.tgz \
	    netgen-`cat VERSION`

clean-mains:
	for dir in ${PROGRAMS}; do \
		(cd $$dir && ${RM} $$dir); done

