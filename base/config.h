#ifndef _CONFIG_H
#define _CONFIG_H

/* config.h -- Copyright 1990, Massimo A. Sivilotti, Caltech */

/* This file documents all the system-dependent configuration options.
   It is probably better to supply these options on the command-line
   by predefining preprocessor symbols, than by editing this file */

/******************************************************************/


/* define the following if your system does not provide the SYSV string
   routines:  strpbrk(), strcspn(), and strtok() */
/* #define NEED_STRING */

/* define the following if your system does not provide strstr() */
/* #define NEED_STRSTR */

/* define the following if your system does not provide strcasecmp() */
/* #define NEED_STRCASECMP */

/* define the following if your system does not provide strdup */
/* #define NEED_STRDUP */

/* define the following if your system does not provide strtol() */
/* #define NEED_STRTOL */


/* define the following if your system does not do so in values.h */
/* INT_MAX is only used by the random number generator in netcmp.h */
/* #define INT_MAX 31999 */  /* or some other largish number */


/* define the following if your system provides times(2) data */
/* #define HAVE_TIMES */

/* define the following if your system provides getrusage(2) data */
/* #define HAVE_GETRUSAGE */

/* define the following if your system has ANSI C:  clock and CLOCKS_PER_SEC */
/* #define HAVE_CLOCK */

/* define the following if your (nominally BSD) system provides the SYSV
   string functions:  strchr, strrchr, memcpy, and memset */
/* #define HAVE_SYSV_STRING */

/* define the following if your system lacks vsprintf() and vfprintf() */
/* #define NEED_VPRINTF */

/* define the following to disable DBUG code */
/* #define DBUG_OFF */

/* define HAVE_GETOPT if your system has getopt(3) */
/* #define HAVE_GETOPT */


/***********************************************************/
/*           REGULAR EXPRESSION STUFF                      */

/* define the following if your system provides REGCMP(3X) */
/* #define HAVE_REGCMP */

/* define the following if your system provides re_exec and re_comp */
/* #define HAVE_RE_COMP */

/* IF NEITHER HAVE_REGCMP nor HAVE_RE_COMP, use internal routines */



#if defined(BSD) && !defined(HAVE_SYSV_STRING)
#include <strings.h>
#else
#include <string.h>
#ifndef IBMPC
#include <memory.h>
#endif
#endif

#ifdef HAVE_MALLINFO
#include <malloc.h>
#endif

/*  SOME GOOD DEFAULTS   */
/*  eventually, these should be independent of the standards... */

#ifdef VMUNIX
/* #define NEED_STRCASECMP */
#define HAVE_STRCASECMP
#define HAVE_GETOPT

#ifdef BSD
#ifndef ultrix
#define NEED_STRSTR
#endif /* ultrix */
#define NEED_STRDUP
#define HAVE_GETRUSAGE
#define NEED_STRING
#ifndef sun
#define NEED_STRTOL
#endif
#endif /* BSD */

#if !defined(HAVE_GETRUSAGE) && !defined(HAVE_CLOCK)
#define HAVE_TIMES
#endif /* not HAVE_GETRUSAGE or HAVE_CLOCK */
#endif /* VMUNIX */


/* some simple equivalences */
#if defined(BSD) && !defined(HAVE_SYSV_STRING)
#define strchr(s,c) index(s,c)
#define strrchr(s,c) rindex(s,c)
#define memcpy(to,from,len) bcopy((char *)(from), (char *)(to), len) 
#define memzero(ptr, len) bzero((char *)(ptr),len)
#else
#define memzero(ptr, len) memset(ptr, 0, len)
#endif


#ifndef INLINE
#ifdef __GNUC__
#define INLINE inline
#else
#define INLINE
#endif /* __GNUC__ */
#endif

#ifdef HAVE_SYSV_STRING
#undef NEED_STRING
#endif

/* some standards !!!! */

#ifdef ANSI_LIBRARY
#undef NEED_STRSTR
#undef NEED_STRING
#undef NEED_STRTOL
#undef NEED_VPRINTF
#define HAVE_CLOCK
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>


#ifdef HPUX
extern int write(int filedes, void *buf, unsigned nbyte); /* SUN has this */
#endif

#ifndef HPUX
extern int open(char *path, int oflag, ...); /* HPUX has it in <sys/fcntl.h> */
#endif

#endif /* ANSI_LIBRARY */



#ifdef NEED_PROTOTYPES
/* proto.h contains prototypes for all undefined functions */
#include "proto.h"
#endif

/* get random number functions, and any string functions we are missing */
#include "pdutils.h"

/* Casting of allocation functions */
#ifdef TCL_NETGEN
  #define CALLOC(a, s)	tcl_calloc(a, s)
  #define MALLOC(s)	Tcl_Alloc(s)
  #define FREE(a)	Tcl_Free((char *)a)
  extern char *Tcl_Strdup(const char *);
  #define STRDUP(a)	Tcl_Strdup((const char *)a)
#else
  #include <stdlib.h>
  #define CALLOC(a, s)	calloc(a, s)
  #define MALLOC(s)	malloc(s)
  #define FREE(a)	free(a)
  #define STRDUP(a)	strdup(a)
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#endif /* _CONFIG_H */
