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

/* timing.c -- routines to measure execution time of NETGEN commands */

/*************************************************************************/
/*                                                                       */
/*    General timing stuff:  CPUTime returns process CPU time in seconds */
/*                           ElapsedCPUTime(t) returns time since t      */
/*                                                                       */
/*************************************************************************/

#include "config.h"
#include "timing.h"

#ifdef HAVE_CLOCK

#include <time.h>

#ifdef NEED_PROTOTYPES
extern clock_t clock(void); /* prototype */
#endif

float CPUTime(void)
/* return the process CPU time in seconds */
{
  /* ANSI standard says CLK_TCK below, but this is not what HP says!!! */
  return((float)(clock()) / (float) (CLOCKS_PER_SEC));
}

#else /* HAVE_CLOCK */

#ifdef HAVE_TIMES

#include <sys/types.h> /* for times(2) stuff */
#include <sys/param.h> /* for times(2) stuff */
#include <sys/times.h> /* for times(2) stuff */

/* 
#ifndef HZ
#define HZ 60
#endif
*/

#ifdef NEED_PROTOTYPES
extern long times(struct tms *buf); /* prototype */
#endif

float CPUTime(void)
/* return the process CPU time in seconds */
{
  struct tms buf;

  times(&buf);
  return((float)(buf.tms_utime) / (float) HZ);
}

#else /* not HAVE_TIMES */

#ifdef HAVE_GETRUSAGE
#include <sys/time.h>
#include <sys/resource.h>

#ifdef NEED_PROTOTYPES
extern int getrusage(int who, struct rusage *ruse);
#endif

float CPUTime(void)
{
  struct rusage endtime;

  getrusage(RUSAGE_SELF, &endtime);
  /* user time is in a struct timeval endtime.ru_utime */
  return(endtime.ru_utime.tv_sec + (float)(endtime.ru_utime.tv_usec)/1.0e6);
}

#else /* not HAVE_GETRUSAGE */

#include <time.h>      /* for time() */

static long st;

float CPUTime(void)
/* return the elapsed CPU time in seconds (actually, elapsed wall time) */
{
  long now;

  time(&now);
  /* keep start time offset in st, to prevent precision problems
     when coercing to a float */
  if (st == 0) st = now;
  return((float)(now - st));
}

#endif /* HAVE_GETRUSAGE */
#endif /* HAVE_TIMES */
#endif /* HAVE_CLOCK */

float ElapsedCPUTime(float since)
{
  return(CPUTime()  - since);
}


