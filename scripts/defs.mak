# defs.mak.in --
# source file for autoconf-generated "defs.mak" for netgen

# defs.mak.  Generated from defs.mak.in by configure.
# Feel free to change the values in here to suit your needs.
# Be aware that running scripts/configure again will overwrite
# any changes!

SHELL                  = /bin/sh

prefix = ${BUILDROOT}/usr/local
exec_prefix = ${prefix}
bindir = ${exec_prefix}/bin
libdir = ${exec_prefix}/lib
mandir = ${prefix}/share/man

VERSION                = 

SCRIPTS                = ${NETGENDIR}/scripts

INSTALL = /bin/install -c
INSTALL_PROGRAM = ${INSTALL}
INSTALL_DATA = ${INSTALL} -m 644
INSTALL_SCRIPT = ${INSTALL}

# Override standard "make" target when compiling under TCL
ALL_TARGET = tcl
INSTALL_TARGET = install-tcl

# Change CADDIR to install in a different place
BINDIR                 = ${bindir}
MANDIR                 = ${mandir}
LIBDIR                 = ${libdir}
DOCDIR                 = ${libdir}/netgen/doc
TCLDIR                 = ${libdir}/netgen/tcl

MAIN_EXTRA_LIBS        =  ${NETGENDIR}/tcltk/libtcltk.o
LD_EXTRA_LIBS          = 
LD_SHARED              = 
TOP_EXTRA_LIBS         = 
SUB_EXTRA_LIBS         = 

MODULES               +=  tcltk
UNUSED_MODULES        += 
PROGRAMS              +=  tcltk
INSTALL_CAD_DIRS      +=  tcltk

RM                     = rm -f
CP                     = cp
AR                     = @AR@
ARFLAGS                = crv
LINK		       = /bin/ld -r
LD                     = /bin/ld
M4		       = /bin/m4
RANLIB                 = ranlib
SHDLIB_EXT	       = .so
LDDL_FLAGS             =  -shared -Wl,-soname,$@ -Wl,--version-script=${NETGENDIR}/netgen/symbol.map
LD_RUN_PATH            = 
LIB_SPECS              =  -L/usr/lib64 -ltk8.6 -L/usr/lib64 -ltcl8.6
WISH_EXE	       = /usr/bin/wish
TCL_LIB_DIR	       = /usr/lib

CC                     = gcc
CPP                    = gcc -E -x c
CXX                    = @CXX@

CPPFLAGS               = -I. -I${NETGENDIR} 
DFLAGS                 =  -DCAD_DIR=\"${LIBDIR}\" -DTCL_DIR=\"${TCLDIR}\" -DPACKAGE_NAME=\"netgen\" -DPACKAGE_TARNAME=\"netgen\" -DPACKAGE_VERSION=\"1.3\" -DPACKAGE_STRING=\"netgen\ 1.3\" -DPACKAGE_BUGREPORT=\"eda-dev@opencircuitdesign.com\" -DPACKAGE_URL=\"\" -DNETGEN_VERSION=\"1.5\" -DNETGEN_REVISION=\"57\" -DSTDC_HEADERS=1 -DHAVE_SYS_TYPES_H=1 -DHAVE_SYS_STAT_H=1 -DHAVE_STDLIB_H=1 -DHAVE_STRING_H=1 -DHAVE_MEMORY_H=1 -DHAVE_STRINGS_H=1 -DHAVE_INTTYPES_H=1 -DHAVE_STDINT_H=1 -DHAVE_UNISTD_H=1 -DSIZEOF_VOID_P=8 -DSIZEOF_UNSIGNED_INT=4 -DSIZEOF_UNSIGNED_LONG=8 -DSIZEOF_UNSIGNED_LONG_LONG=8 -DSTDC_HEADERS=1 -DHAVE_SETENV=1 -DHAVE_PUTENV=1 -DHAVE_DIRENT_H=1 -DHAVE_LIMITS_H=1 -DHAVE_VA_COPY=1 -DHAVE___VA_COPY=1 -DDBUG_OFF=1 -DTCL_NETGEN=1 -Dlinux=1 -DSYSV=1 -DISC=1 -DSHDLIB_EXT=\".so\" -DNDEBUG
CFLAGS                 = -g -m64 -fPIC -fPIC 

DEPEND_FILE            = Depend
DEPEND_FLAG            = -MM
EXEEXT		       = 

GR_CFLAGS              =  
GR_DFLAGS              =  -DX11 -DXLIB -DNDEBUG
GR_LIBS                =  -lX11 
GR_SRCS                =  ${TK_SRCS}

OBJS      = ${SRCS:.c=.o}
LIB_OBJS  = ${LIB_SRCS:.c=.o}
CLEANS    = ${OBJS} ${LIB_OBJS} lib${MODULE}.a lib${MODULE}.o ${MODULE}
