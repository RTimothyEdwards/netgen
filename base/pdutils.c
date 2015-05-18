/* "NETGEN", a netlist-specification tool for VLSI
   Copyright (C) 1989, 1990   Massimo A. Sivilotti
   Author's address: mass@csvax.cs.caltech.edu;
                     Caltech 256-80, Pasadena CA 91125.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation (any version).

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file copying.  If not, write to
the Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

/* pdutils.c -- various public-domain versions of useful functions */

#include "config.h"

#include <stdio.h>
#include <ctype.h>
#ifdef IBMPC
#include <alloc.h>
#endif

#if defined(__STDC__) || defined(IBMPC)
#include <stdarg.h>
#else
#include <varargs.h>
#endif

#include "netgen.h"

/*************************************************************************/
/*                                                                       */
/*    Random number generators                                           */
/*                                                                       */
/*************************************************************************/

/* See NUMERICAL RECIPES IN C */

#include <math.h>

#define M 714025L
#define IA 1366
#define IC 150889L

static long idum = -1;  /* needs to be initialized to avoid seg fault if 0 */

float ran2(void)
{
	static long iy,ir[98];
	static int iff=0;
	int j;
	void nrerror();

	if (idum < 0 || iff == 0) {
		iff=1;
		if ((idum=(IC-(idum)) % M) < 0) idum = -idum;
		for (j=1;j<=97;j++) {
			idum=(IA*idum+IC) % M;
			ir[j]=idum;
		}
		idum=(IA*idum+IC) % M;
		iy=idum;
	}
	j=(int)(1 + 97.0*iy/M); /* the cast was added by Glenn for C++ */
	if (j > 97 || j < 1) perror("RAN2: This cannot happen.");
	iy=ir[j];
	idum=(IA*idum+IC) % M;
	ir[j]=idum;
	return (float) iy/M;
}

#undef M
#undef IA
#undef IC

#if 0
int Random(int max)
{
  int i;
  float f;
  f = ran2();
  i = f * max;
  printf("Random(%d) computes %f, returns: %d\n", max, f, i);
  return(i);
}
#else
int Random(int max)
{
  return(ran2() * max);
}
#endif

long RandomSeed(long seed)
/* initialize idum to some negative integer */
{
	long oldidum;

	oldidum = idum;
	if (seed == 0) seed = -1;
	if (seed > 0) seed = -seed;
	idum = seed;
	return(oldidum);
}

float RandomUniform(void)
{
	return(ran2());
}

/*************************************************************************/
/*                                                                       */
/*    String manipulation routines for deficient libraries               */
/*                                                                       */
/*************************************************************************/


#ifdef NEED_STRSTR
/* strstr - find first occurrence of wanted in s */
/* return pointer to found location, or NULL if none */
char *strstr(char *s, char *wanted)
{
	register char *scan;
	register int len;
	register char firstc;
/* deleted by Glenn -- Not necessary (and conflict with declarations in .h)
	extern int strcmp();
	extern int strlen();
*/

	/*
	 * The odd placement of the two tests is so "" is findable.
	 * Also, we inline the first char for speed.
	 * The ++ on scan has been moved down for optimization.
	 */
	firstc = *wanted;
	len = strlen(wanted);
	for (scan = s; *scan != firstc || strncmp(scan, wanted, len) != 0; )
		if (*scan++ == '\0')
			return(NULL);
	return(scan);
}
#endif

#ifdef NEED_STRCASECMP
/* strcasecmp - compare string s1 to s2, case insensitive */
/* returns <0 for <, 0 for ==, >0 for > */
int strcasecmp(char *s1, char *s2)
{
	register char *scan1;
	register char *scan2;

	scan1 = s1;
	scan2 = s2;
	while (*scan1 != '\0' && toupper(*scan1) == toupper(*scan2)) {
		scan1++;
		scan2++;
	}

	/*
	 * The following case analysis is necessary so that characters
	 * which look negative collate low against normal characters but
	 * high against the end-of-string NUL.
	 */
	if (*scan1 == '\0' && *scan2 == '\0')
		return(0);
	else if (*scan1 == '\0')
		return(-1);
	else if (*scan2 == '\0')
		return(1);
	else
		return(*scan1 - *scan2);
}
#endif

#ifdef NEED_STRING
/*
 * char *strtok(s,delim);
 *
 * Get next token from string s (NULL on 2nd, 3rd, etc. calls),
 * where tokens are nonempty strings separated by runs of
 * chars from delim.  Writes NULs into s to end tokens.  delim need not
 * remain constant from call to call.
 */

static char *scanpoint = NULL;

char *strtok(char *s, char *delim)
{
	register char *scan;
	char *tok;
	register char *dscan;

	if (s == NULL && scanpoint == NULL)
		return(NULL);
	if (s != NULL)
		scan = s;
	else
		scan = scanpoint;

	/*
	 * Scan leading delimiters.
	 */
	for (; *scan != '\0'; scan++) {
		for (dscan = delim; *dscan != '\0'; dscan++)
			if (*scan == *dscan)
				break;
		if (*dscan == '\0')
			break;
	}
	if (*scan == '\0') {
		scanpoint = NULL;
		return(NULL);
	}

	tok = scan;

	/*
	 * Scan token.
	 */
	for (; *scan != '\0'; scan++) {
		for (dscan = delim; *dscan != '\0';)	/* ++ moved down. */
			if (*scan == *dscan++) {
				scanpoint = scan+1;
				*scan = '\0';
				return(tok);
			}
	}

	/*
	 * Reached end of string.
	 */
	scanpoint = NULL;
	return(tok);
}



/*
 * strcspn - find length of initial segment of s consisting entirely
 * of characters not from reject
 */

int strcspn(char *s, char *reject)
{
	register char *scan;
	register char *rscan;
	register int count;

	count = 0;
	for (scan = s; *scan != '\0'; scan++) {
		for (rscan = reject; *rscan != '\0';)	/* ++ moved down. */
			if (*scan == *rscan++)
				return(count);
		count++;
	}
	return(count);
}


/*
 * strpbrk - find first occurrence of any char from breakat in s
 */


char *strpbrk(char *s, char *breakat)
/* found char, or NULL if none */
{
	register char *sscan;
	register char *bscan;

	for (sscan = s; *sscan != '\0'; sscan++) {
		for (bscan = breakat; *bscan != '\0';)	/* ++ moved down. */
			if (*sscan == *bscan++)
				return(sscan);
	}
	return(NULL);
}

#endif 

#ifdef NEED_STRTOL
long strtol(char *str, char **ptr, int base)
/* extremely poor emulator */
{
  long result;
  int fields;

  if (ptr != NULL) *ptr = str;
  fields = sscanf(str,"%ld",&result);
  if (fields == 1) {
    if (ptr != NULL) *ptr = str+1;  /* bogus, but why not */
    return(result);
  }
  return(0);
}
#endif

#ifdef NEED_VPRINTF

#if 1

/* Portable vsprintf  by Robert A. Larson <blarson@skat.usc.edu> */

/* Copyright 1989 Robert A. Larson.
 * Distribution in any form is allowed as long as the author
 * retains credit, changes are noted by their author and the
 * copyright message remains intact.  This program comes as-is
 * with no warentee of fitness for any purpouse.
 *
 * Thanks to Doug Gwen, Chris Torek, and others who helped clarify
 * the ansi printf specs.
 *
 * Please send any bug fixes and improvments to blarson@skat.usc.edu .
 * The use of goto is NOT a bug.
 */

/* Feb	7, 1989		blarson		First usenet release */

/* This code implements the vsprintf function, without relying on
 * the existance of _doprint or other system specific code.
 *
 * Define NOVOID if void * is not a supported type.
 *
 * Two compile options are available for efficency:
 *	INTSPRINTF	should be defined if sprintf is int and returns
 *			the number of chacters formated.
 *	LONGINT		should be defined if sizeof(long) == sizeof(int)
 *
 *	They only make the code smaller and faster, they need not be
 *	defined.
 *
 * UNSIGNEDSPECIAL should be defined if unsigned is treated differently
 * than int in argument passing.  If this is definded, and LONGINT is not,
 * the compiler must support the type unsingned long.
 *
 * Most quirks and bugs of the available sprintf fuction are duplicated,
 * however * in the width and precision fields will work correctly
 * even if sprintf does not support this, as will the n format.
 *
 * Bad format strings, or those with very long width and precision
 * fields (including expanded * fields) will cause undesired results.
 */

#ifdef OSK		/* os9/68k can take advantage of both */
#define LONGINT
#define INTSPRINTF
#endif

/* This must be a typedef not a #define! */
#ifdef NOVOID
typedef char *pointer;
#else
typedef void *pointer;
#endif

#ifdef	INTSPRINTF
#define Sprintf(string,format,arg)	(sprintf((string),(format),(arg)))
#else
#define Sprintf(string,format,arg)	(\
	sprintf((string),(format),(arg)),\
	strlen(string)\
)
#endif

typedef int *intp;

int vsprintf(dest, format, args)
char *dest;
register char *format;
va_list args;
{
  register char *dp = dest;
  register char c;
  register char *tp;
  char tempfmt[64];
#ifndef LONGINT
  int longflag;
#endif

  tempfmt[0] = '%';
  while(c = *format++) {
    if(c=='%') {
      tp = &tempfmt[1];
#ifndef LONGINT
      longflag = 0;
#endif
    continue_format:
      switch(c = *format++) {
      case 's':
	*tp++ = c;
	*tp = '\0';
	dp += Sprintf(dp, tempfmt, va_arg(args, char *));
	break;
      case 'u':
      case 'x':
      case 'o':
      case 'X':
#ifdef UNSIGNEDSPECIAL
	*tp++ = c;
	*tp = '\0';
#ifndef LONGINT
	if(longflag)
	  dp += Sprintf(dp, tempfmt, va_arg(args, unsigned long));
	else
#endif
	  dp += Sprintf(dp, tempfmt, va_arg(args, unsigned));
	break;
#endif
      case 'd':
      case 'c':
      case 'i':
	*tp++ = c;
	*tp = '\0';
#ifndef LONGINT
	if(longflag)
	  dp += Sprintf(dp, tempfmt, va_arg(args, long));
	else
#endif
	  dp += Sprintf(dp, tempfmt, va_arg(args, int));
	break;
      case 'f':
      case 'e':
      case 'E':
      case 'g':
      case 'G':
	*tp++ = c;
	*tp = '\0';
	dp += Sprintf(dp, tempfmt, va_arg(args, double));
	break;
      case 'p':
	*tp++ = c;
	*tp = '\0';
	dp += Sprintf(dp, tempfmt, va_arg(args, pointer));
	break;
      case '-':
      case '+':
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
      case '.':
      case ' ':
      case '#':
      case 'h':
	*tp++ = c;
	goto continue_format;
      case 'l':
#ifndef LONGINT
	longflag = 1;
	*tp++ = c;
#endif
	goto continue_format;
      case '*':
	tp += Sprintf(tp, "%d", va_arg(args, int));
	goto continue_format;
      case 'n':
	*va_arg(args, intp) = dp - dest;
	break;
      case '%':
      default:
	*dp++ = c;
	break;
      }
    } else *dp++ = c;
  }
  *dp = '\0';
  return dp - dest;
}

/* Portable vfprintf and vprintf by Robert A. Larson <blarson@skat.usc.edu> */

int vfprintf(dest, format, args)
FILE *dest;
register char *format;
va_list args;
{
  register char c;
  register char *tp;
  register int count = 0;
  char tempfmt[64];
#ifndef LONGINT
  int longflag;
#endif

  tempfmt[0] = '%';
  while(c = *format++) {
    if(c=='%') {
      tp = &tempfmt[1];
#ifndef LONGINT
      longflag = 0;
#endif
    continue_format:
      switch(c = *format++) {
      case 's':
	*tp++ = c;
	*tp = '\0';
	count += fprintf(dest, tempfmt, va_arg(args, char *));
	break;
      case 'u':
      case 'x':
      case 'o':
      case 'X':
#ifdef UNSIGNEDSPECIAL
	*tp++ = c;
	*tp = '\0';
#ifndef LONGINT
	if(longflag)
	  count += fprintf(dest, tempfmt, va_arg(args, unsigned long));
	else
#endif
	  count += fprintf(dest, tempfmt, va_arg(args, unsigned));
	break;
#endif
      case 'd':
      case 'c':
      case 'i':
	*tp++ = c;
	*tp = '\0';
#ifndef LONGINT
	if(longflag)
	  count += fprintf(dest, tempfmt, va_arg(args, long));
	else
#endif
	  count += fprintf(dest, tempfmt, va_arg(args, int));
	break;
      case 'f':
      case 'e':
      case 'E':
      case 'g':
      case 'G':
	*tp++ = c;
	*tp = '\0';
	count += fprintf(dest, tempfmt, va_arg(args, double));
	break;
      case 'p':
	*tp++ = c;
	*tp = '\0';
	count += fprintf(dest, tempfmt, va_arg(args, pointer));
	break;
      case '-':
      case '+':
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
      case '.':
      case ' ':
      case '#':
      case 'h':
	*tp++ = c;
	goto continue_format;
      case 'l':
#ifndef LONGINT
	longflag = 1;
	*tp++ = c;
#endif
	goto continue_format;
      case '*':
	tp += Sprintf(tp, "%d", va_arg(args, int));
	goto continue_format;
      case 'n':
	*va_arg(args, intp) = count;
	break;
      case '%':
      default:
	putc(c, dest);
	count++;
	break;
      }
    } else {
      putc(c, dest);
      count++;
    }
  }
  return count;
}

int vprintf(format, args)
char *format;
va_list args;
{
    return vfprintf(stdout, format, args);
}


#else
/* this code works on machines that have _doprnt (Vaxes, etc) */

int vfprintf(FILE *fp, char *fmt, va_list args)
{
	_doprnt(fmt, args, fp);
	return(ferror(fp)? EOF: 0);
}

int vsprintf(char *str, char *fmt, va_list args)
{
	struct _iobuf _strbuf;

	_strbuf._flag = _IOWRT+_IOSTRG;
	_strbuf._ptr = str;
	_strbuf._cnt = 32767;
	_doprnt(fmt, args, &_strbuf);
	putc('\0', &_strbuf);
	return(strlen(str));
}
#endif  
#endif  /* NEED_VPRINTF */
