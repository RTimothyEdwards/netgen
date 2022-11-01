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

/* print.c -- routines to format and buffer output */

#include "config.h" 

#include <stdio.h>
#include <stdarg.h>  /* what about varargs support, as in pdutils.h ??? */
#include <ctype.h>

#ifdef TCL_NETGEN
#include <tcl.h>

void tcl_stdflush(FILE *);
void tcl_vprintf(FILE *, char *, va_list);

extern int ColumnBase;
#endif

#define MAXFILES 4

struct filestr {
  FILE *f;
  char buffer[MAX_STR_LEN];
  int wrap;  /* column to wrap around in, or 0 if no wrap */
} file_buffers[MAXFILES];

static int findfile(FILE *f)
/* return the index into file_buffers of the file, or -1 if not found */
{
  int i;
  for (i = 0; i < MAXFILES; i++)
    if (file_buffers[i].f == f) return(i);
  return(-1);
}

static int freefile(void)
/* return the index of next free slot in file_buffers, or -1 */
{
  int i;
  for (i = 0; i < MAXFILES; i++)
    if (file_buffers[i].f == NULL) return(i);
  return(-1);
}


FILE *LoggingFile = NULL; /* if LoggingFile is non-null, write to it as well */
int NoOutput = 0;         /* by default, we allow stdout to be printed */

#ifdef HAVE_X11
#include "xnetgen.h"

void Output(FILE *f, char *s)
{
  if (f == stderr) X_display_line(s);
  else {
    if (f == stdout) {
      if (!NoOutput) X_display_line(s);
    }
    else fprintf(f,"%s",s);
  }
  if (LoggingFile != NULL) fprintf(LoggingFile, "%s", s);
}
#else
#ifndef TCL_NETGEN

void Output(FILE *f, char *s)
{
  if (!NoOutput || f != stdout) fprintf(f,"%s",s);
  if (LoggingFile != NULL) fprintf(LoggingFile, "%s", s);
}

#endif /* TCL_NETGEN */
#endif /* HAVE_X11 */

#ifdef TCL_NETGEN

void Fprintf(FILE *f, char *format, ...)
{
  va_list ap;

  va_start(ap, format);
  if (!NoOutput) tcl_vprintf(f, format, ap);
  if (LoggingFile != NULL) vfprintf(LoggingFile, format, ap);
  va_end(ap);
}

#else

void Fprintf(FILE *f, char *format, ...)
{
  va_list ap;
  int FileIndex;
  char tmpstr[MAX_STR_LEN];
  int bufferlongenough;
  int linewrapexceeded;

  va_start(ap, format);
  vsprintf(tmpstr, format, ap);
  va_end(ap);

  FileIndex = findfile(f);
  if (FileIndex == -1) {
    fprintf(f, "%s", tmpstr);
    return;
  }

  /* file is being buffered, so see if there is space in the buffer */
  bufferlongenough = (strlen(file_buffers[FileIndex].buffer) + 
		      strlen(tmpstr) < sizeof(file_buffers[0].buffer) - 3);

  linewrapexceeded = (file_buffers[FileIndex].wrap &&
		      strlen(file_buffers[FileIndex].buffer) + 
		      strlen(tmpstr) > file_buffers[FileIndex].wrap);

  if (bufferlongenough && !linewrapexceeded) {
    strcat(file_buffers[FileIndex].buffer, tmpstr);
    if (strchr(tmpstr,'\n') != NULL) {
#if 0
      int i;
      i = strlen(file_buffers[FileIndex].buffer);
      /* trim any trailing spaces */
      for (i = i-1; i >= 0 && 
	   isspace((file_buffers[FileIndex].buffer)[i]); i--)
	(file_buffers[FileIndex].buffer)[i] = '\0';
#endif
      Output(f, file_buffers[FileIndex].buffer);
      strcpy(file_buffers[FileIndex].buffer,"");
    }
  }
  else {
    /* string too long, or line wrap exceeded, so flush it all */
    if (linewrapexceeded) {
#if 1
      strcat(file_buffers[FileIndex].buffer, "\n");
#endif
      Output(f, file_buffers[FileIndex].buffer);
      strcpy(file_buffers[FileIndex].buffer,"     ");
      strcat(file_buffers[FileIndex].buffer, tmpstr);
    }
    else {
      /* buffer is too small, so flush buffer and new string */
#if 1
      Output(f, file_buffers[FileIndex].buffer);
      Output(f, tmpstr);
      strcpy(file_buffers[FileIndex].buffer,"");
#else
      strcat(file_buffers[FileIndex].buffer, tmpstr);
      Output(f, file_buffers[FileIndex].buffer);
      strcpy(file_buffers[FileIndex].buffer,"");
#endif
    }
  }
}

#endif

void Finsert(FILE *f)
{
  /* try to insert f, if not alread present */
  if (findfile(f) == -1) {
    /* not found, so try to insert it */
    int freeslot;

    freeslot = freefile();
    if (freeslot != -1) {
	file_buffers[freeslot].f = f;
	strcpy(file_buffers[freeslot].buffer,"");
      }
    fflush(f);
  }
}

#ifdef TCL_NETGEN

void Printf(char *format, ...)
{
  va_list ap;

  va_start(ap, format);
  tcl_vprintf(stdout, format, ap);
  va_end(ap);
}

#else

void Printf(char *format, ...)
{
  va_list ap;
  char tmpstr[MAX_STR_LEN];

  va_start(ap, format);
  vsprintf(tmpstr, format, ap);
  va_end(ap);

  /* insert stdout, if not already done */
  Finsert(stdout);
  Fprintf(stdout, "%s",tmpstr);
}

#endif

int Fcursor(FILE *f)
/* return the current column number of the cursor , or 0 if not known */
{
  int i;

  i = findfile(f);
  if (i == -1) return(0);
  return(strlen(file_buffers[i].buffer));
}

int Fwrap(FILE *f, int col)
/* set the wraparound column for this file */
/* returns the previous wrap around column */
{
  int i;
  int oldcol;

  i = findfile(f);
  if (i == -1) return(0);
  oldcol = file_buffers[i].wrap;
  file_buffers[i].wrap = col;
  return(oldcol);
}

/* If "f" is NULL, then print to stdout and NOT */
/* to the log file.				*/

void Ftab(FILE *f, int col)
{
  int i;
  int spaces;
  FILE *locf = (f == NULL) ? stdout : f;

  i = findfile(locf);
  if (i == -1) {
#ifdef TCL_NETGEN
    char *padding;
    if ((col - ColumnBase) <= 0) return;
    padding = (char *)MALLOC(col - ColumnBase + 1);
    for (i = 0; i < col - ColumnBase; i++)
      padding[i] = ' ';
    padding[i] = '\0';
    if (f)
       Fprintf(f, "%s", padding);
    else
       Printf("%s", padding);
#else
    for (i = 0; i < col; i++)
      if (f)
         Fprintf(f, " ");
      else
         Printf(" ");
#endif
    return;
  }

  /* try to pad the string with spaces */
  spaces = col - strlen(file_buffers[i].buffer) - 1;
  for (; spaces > 0; spaces--) strcat(file_buffers[i].buffer, " ");
  return;
}

FILE *Fopen(char *name, char *mode)
{
  FILE *ret;
  int i;

  ret = fopen(name,mode);
  i = freefile();
  if (i != -1) {
    /* empty slot */
    file_buffers[i].f = ret;
    strcpy(file_buffers[i].buffer, "");
  }
  return(ret);
}

void Fflush(FILE *f)
{
  int i;

#ifdef HAVE_X11
  if (f == stdout || f == stderr) {
    i = findfile(f);
    if (i != -1) {
      if (strlen(file_buffers[i].buffer)) 
	Output(f, file_buffers[i].buffer);
      strcpy(file_buffers[i].buffer,"");
    }
    X_display_refresh();
    return;
  }
#endif    

#ifdef TCL_NETGEN
  if (f == stdout || f == stderr) {
    i = findfile(f);
    if (i != -1) {
      if (strlen(file_buffers[i].buffer)) 
	Fprintf(f, file_buffers[i].buffer);
      strcpy(file_buffers[i].buffer, "");
    }
    tcl_stdflush(f);
    return;
  }
#endif
    
  i = findfile(f);
  if (i != -1) {
    if (strlen(file_buffers[i].buffer)) 
      fprintf(f,"%s",file_buffers[i].buffer);
    strcpy(file_buffers[i].buffer,"");
  }
  fflush(f);
}

int Fclose(FILE *f)
{
  int i;

  Fflush(f);
  i = findfile(f);
  if (i != -1) file_buffers[i].f = NULL;
  return (fclose(f));
}

      
  
  
