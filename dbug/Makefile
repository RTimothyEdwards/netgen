#
#  FILE
#
#	Makefile    Makefile for dbug package
#
#  SCCS ID
#
#	@(#)Makefile	1.11 3/12/88
#
#  DESCRIPTION
#
#	Makefile for the dbug package (under UNIX system V or 4.2BSD).
#
#	Interesting things to make are:
#
#	lib	=>	Makes the runtime support library in the
#			current directory.
#
#	lintlib	=>	Makes the lint library in the current directory.
#
#	install	=>	Install pieces from current directory to
#			where they belong.
#
#	doc	=>	Makes the documentation in the current
#			directory.
#
#	clean	=>	Remove objects, temporary files, etc from
#			current directory.
#
#	superclean =>	Remove everything except sccs source files.
#			Uses interactive remove for verification.

# Define NO_VARARGS if you have no <varargs.h> to include.
#VARARGS =	-DNO_VARARGS

SH =		/bin/sh
AR =		ar
RM =		rm
CFLAGS =	-O $(VARARGS)
GFLAGS =	-s
INSTALL =	$(SH) install.sh
CHMOD =		chmod
MAKE =		make
INC =		/usr/include/local
LIB =		/usr/lib
RANLIB =	$(SH) ranlib.sh
MODE =		664

# The following is provided for example only, it is set by "doinstall.sh".
LLIB =		/usr/lib

.SUFFIXES:	.r .r~ .c .c~

.c~.c:
		$(GET) $(GFLAGS) -p $< >$*.c

.r~.r:
		$(GET) $(GFLAGS) -p $< >$*.r

.c~.r:
		$(GET) $(GFLAGS) -p $< >$*.c
		sed "s/\\\/\\\e/" <$*.c >$*.r
		$(RM) -f $*.c

.c.r:
		sed "s/\\/\\e/" <$< >$*.r
#		sed "s/\\\/\\\e/" <$< >$*.r

EXAMPLES =	example1.r example2.r example3.r
OUTPUTS =	output1.r output2.r output3.r output4.r output5.r

NROFF_INC =	main.r factorial.r $(OUTPUTS) $(EXAMPLES)


#
#	The default thing to do is remake the local runtime support
#	library, local lint library, and local documentation as
#	necessary.
#

all :		lib lintlib analyze doc

lib :		libdbug.a

lintlib :	llib-ldbug.ln

doc :		factorial user.t

#
#	Make the local runtime support library "libdbug.a" from
#	sources.
#

libdbug.a :	dbug.o
		rm -f $@
		$(AR) cr $@ $?
		$(RANLIB) $@

#
#	Clean up the local directory.
#

clean :
		$(RM) -f *.o *.ln *.a *.BAK nohup.out factorial $(NROFF_INC)

superclean :
		$(RM) -i ?[!.]*

#
#	Install the new header and library files.  Since things go in
#	different places depending upon the system (lint libraries
#	go in /usr/lib under SV and /usr/lib/lint under BSD for example),
#	the shell script "doinstall.sh" figures out the environment and
#	does a recursive make with the appropriate pathnames set.
#

install :		
			$(SH) doinstall.sh $(MAKE) sysinstall

sysinstall:		$(INC) $(INC)/dbug.h $(LIB)/libdbug.a \
			$(LLIB)/llib-ldbug.ln $(LLIB)/llib-ldbug

$(INC) :
			-if test -d $@ ;then true ;else mkdir $@ ;fi

$(INC)/dbug.h :		dbug.h
			$(INSTALL) $? $@
			$(CHMOD) $(MODE) $@

$(LIB)/libdbug.a :	libdbug.a
			$(INSTALL) $? $@
			$(CHMOD) $(MODE) $@

$(LLIB)/llib-ldbug.ln :	llib-ldbug.ln
			$(INSTALL) $? $@
			$(CHMOD) $(MODE) $@

$(LLIB)/llib-ldbug :	llib-ldbug
			$(INSTALL) $? $@
			$(CHMOD) $(MODE) $@

#
#	Make the local lint library.
#

llib-ldbug.ln : 	llib-ldbug
			$(SH) mklintlib.sh $? $@

#
#	Make the test/example program "factorial".
#
#	Note that the objects depend on the LOCAL dbug.h file and
#	the compilations are set up to find dbug.h in the current
#	directory even though the sources have "#include <dbug.h>".
#	This allows the examples to look like the code a user would
#	write but still be used as test cases for new versions
#	of dbug.

factorial :	main.o factorial.o libdbug.a
		$(CC) -o $@ main.o factorial.o libdbug.a

main.o :	main.c dbug.h
		$(CC) $(CFLAGS) -c -I. main.c

factorial.o :	factorial.c dbug.h
		$(CC) $(CFLAGS) -c -I. factorial.c


#
#	Make the analyze program for runtime profiling support.
#

analyze :	analyze.o libdbug.a
		$(CC) -o $@ analyze.o libdbug.a

analyze.o :	analyze.c useful.h dbug.h
		$(CC) $(CFLAGS) -c -I. analyze.c

#
#	Rebuild the documentation
#

user.t :	user.r $(NROFF_INC)
		nroff -cm user.r >$@

#
#	Run the factorial program to produce the sample outputs.
#

output1.r:	factorial
		./factorial 1 2 3 4 5 >$@

output2.r:	factorial
		./factorial -#t:o 2 3 >$@

output3.r:	factorial
		./factorial -#d:t:o 3 >$@

output4.r:	factorial
		./factorial -#d,result:o 4 >$@

output5.r:	factorial
		./factorial -#d:f,factorial:F:L:o 3 >$@

#
#	All files included by user.r depend on user.r, thus
#	forcing them to be remade if user.r changes.
#

$(NROFF_INC) :	user.r

