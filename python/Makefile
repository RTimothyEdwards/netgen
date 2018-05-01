MODULE = python
NETGENDIR = ..
SRCS = 
SCRIPTS = consoletext.py helpwindow.py lvs_manager.py treeviewsplit.py
SCRIPTS += tksimpledialog.py tooltip.py lvs_help.txt

SCRIPTINSTALL = $(DESTDIR)${PYDIR}

include ${NETGENDIR}/defs.mak

main: lvs_manager.py

tcl-main: lvs_manager.py

$(DESTDIR)${PYDIR}/consoletext.py:
	${RM} $@
	${CP} consoletext.py $@

$(DESTDIR)${PYDIR}/helpwindow.py:
	${RM} $@
	${CP} helpwindow.py $@

lvs_manager.py: lvs_manager.py.in
	sed -e '/SUBST_SCRIPT_DIR/s#SUBST_SCRIPT_DIR#$(SCRIPTINSTALL)#' \
		lvs_manager.py.in > lvs_manager.py

$(DESTDIR)${PYDIR}/lvs_manager.py: lvs_manager.py
	${RM} $@
	${CP} lvs_manager.py $@

$(DESTDIR)${PYDIR}/treeviewsplit.py: treeviewsplit.py
	${RM} $@
	${CP} treeviewsplit.py $@

$(DESTDIR)${PYDIR}/tksimpledialog.py: tksimpledialog.py
	${RM} $@
	${CP} tksimpledialog.py $@

$(DESTDIR)${PYDIR}/tooltip.py: tooltip.py
	${RM} $@
	${CP} tooltip.py $@

$(DESTDIR)${PYDIR}/lvs_help.txt: lvs_help.txt
	${RM} $@
	${CP} lvs_help.txt $@

install: install-tcl

install-tcl: $(DESTDIR)${PYDIR} $(DESTDIR)${PYDIR}/consoletext.py \
	$(DESTDIR)${PYDIR}/helpwindow.py $(DESTDIR)${PYDIR}/lvs_manager.py \
	$(DESTDIR)${PYDIR}/treeviewsplit.py $(DESTDIR)${PYDIR}/tksimpledialog.py \
	$(DESTDIR)${PYDIR}/tooltip.py $(DESTDIR)${PYDIR}/lvs_help.txt

include ${NETGENDIR}/rules.mak
