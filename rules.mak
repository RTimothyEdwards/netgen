# You shouldn't need to edit this file, see the defs.mak file

module: lib${MODULE}.o

depend: ${DEPEND_FILE}

${DEPEND_FILE}:
	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS} ${DEPEND_FLAG} ${SRCS} > ${DEPEND_FILE}

.c.o:
	@echo --- compiling ${MODULE}/$*.o
	${RM} $*.o
	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS}  -c $*.c

lib${MODULE}.o: ${OBJS}
	@echo --- linking lib${MODULE}.o
	${RM} lib${MODULE}.o
	${LINK} ${OBJS} -o lib${MODULE}.o ${EXTERN_LIBS}

lib: lib${MODULE}.a

lib${MODULE}.a: ${OBJS} ${LIB_OBJS}
	@echo --- archiving lib${MODULE}.a
	${RM} lib${MODULE}.a
	${AR} ${ARFLAGS} lib${MODULE}.a ${OBJS} ${LIB_OBJS}
	${RANLIB} lib${MODULE}.a

${MODULE}: lib${MODULE}.o ${EXTRA_LIBS}
	@echo --- building main ${MODULE}
	${RM} ${MODULE}
	${CC} ${CFLAGS} ${CPPFLAGS} ${DFLAGS} lib${MODULE}.o ${EXTRA_LIBS} \
		-o ${MODULE} ${LIBS} ${LDFLAGS}

clean:
	${RM} ${CLEANS}

tags: ${SRCS} ${LIB_SRCS}
	ctags ${SRCS} ${LIB_SRCS}

include ${DEPEND_FILE}
