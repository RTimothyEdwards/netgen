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

/* netfile.c  --  support routines for reading/writing netlist files */

#include "config.h"
#define FILE_ACCESS_BITS 0777

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/fcntl.h> /* for SGI */
#ifdef IBMPC
#include <stdlib.h>   /* for calloc */
#endif

#ifdef TCL_NETGEN
#include <tcl.h>
#endif

#include "netgen.h"
#include "hash.h"
#include "objlist.h"
#include "netfile.h"
#include "print.h"

static char buffer[LINELENGTH] = "";  /* buffer for FlushString */
int AutoFillColumn = LINELENGTH; /* enable wraparound at LINELENGTH */

static FILE *outfile;
static int Graph = 0;
int File;

extern char *SetExtension(char *buffer, char *path, char *extension)
/* add 'extension' to 'path' (overwriting previous extension, if any),
   write it into buffer (if buffer is null, malloc a buffer).
   return address of buffer. NOTE: it is SAFE to pass the same
   address as 'path' and as 'buffer', since an internal buffer is used.
*/
{
  char tmpbuf[500];
  char *pt;

  strcpy(tmpbuf, path);

  /* step 1: find the last directory delimiter */
#ifdef IBMPC
  pt = strrchr(tmpbuf, '\\');
#else
  pt = strrchr(tmpbuf, '/');
#endif

  /* if none exists, point to the start of the buffer */
  if (pt == NULL) pt = tmpbuf;

  /* step 2: search to the right, for a '.'   */
  pt = strrchr(pt, '.');
  if (pt != NULL) *pt = '\0';

  /* step 3: add on the desired extension */
  strcat(tmpbuf, extension);

  /* step 4: lower-case the entire name */
  /* (Commented out because this is really stupid.) */
  // for (pt = tmpbuf; *pt != '\0'; pt++)
  //   if (isupper(*pt)) *pt = tolower(*pt);

  if (buffer != NULL) {
    strcpy(buffer, tmpbuf);
    return(buffer);
  }
  return (strsave(tmpbuf));
}


int IsPortInPortlist(struct objlist *ob, struct nlist *tp)
/* returns true if ob points to an object that
     1) is the "best" name for that port
     2) has the same node number as a port
*/
{
  struct objlist *ob2;

#if 1
  int node;

  if (!match(ob->name, NodeAlias(tp, ob))) return (0);
  node = ob->node;
  for (ob2 = tp->cell; ob2 != NULL; ob2 = ob2->next) 
    if ((ob2->node == node) && IsPort(ob2)) return(1);
  return (0);
#else
  int isaport;

  if (!match(ob->name, NodeName(tp, ob->node))) return (0);
  isaport = 0;
  ob2 = tp->cell;
  while (ob2 != NULL) {
    if ((ob2->node == ob->node) && IsPort(ob2)) isaport = 1;
    ob2 = ob2->next;
  }
  return (isaport);
#endif
}


void FlushString (char *format, ...)
{
  va_list argptr;
  char	tmpstr[1000];

  va_start(argptr, format);
  vsprintf(tmpstr, format, argptr);
  va_end(argptr);

  if (AutoFillColumn) {
    if (strlen(buffer) + strlen(tmpstr) + 1 > AutoFillColumn) {
      fprintf(outfile, "%s\n", buffer);
      strcpy(buffer, "     ");
    }
    strcat(buffer, tmpstr);
    if (strchr(buffer, '\n') != NULL) {
      fprintf(outfile, "%s", buffer);
      strcpy(buffer, "");
    }
  }
  else {
    /* check to see if anything is buffered up first */
    if (strlen(buffer)) {
      fprintf(outfile, "%s", buffer);
      strcpy(buffer,"");
    }
    fprintf(outfile, "%s", tmpstr);
  }
}


int OpenFile(char *filename, int linelen)
{
  if (linelen < LINELENGTH) AutoFillColumn = linelen;
  else AutoFillColumn = LINELENGTH;

  if (strlen(filename) > 0) {
    outfile = fopen(filename, "w");
    return (outfile != NULL);
  }
  outfile = stdout;
  return(1);
}


void CloseFile(char *filename)
{
	if (strlen(filename) > 0) 
		fclose(outfile);
}


/* STUFF TO READ INPUT FILES */

static char *line = NULL;	/* actual line read in */
static char *linetok;   	/* line copied to this, then munged by strdtok */
static int  linesize = 0;	/* amount of memory allocated for line */
static int  linenum;
char	*nexttok;
static FILE *infile = NULL;

/* For purposes of having "include" files, keep a stack of the open	*/
/* files.								*/

struct filestack {
   FILE *file;
   struct filestack *next;
};

static struct filestack *OpenFiles = NULL;

#define WHITESPACE_DELIMITER " \t\n\r"

/*----------------------------------------------------------------------*/
/* TrimQuoted() ---							*/
/* Remove spaces from inside single- or double-quoted strings.		*/
/*----------------------------------------------------------------------*/

void TrimQuoted(char *line)
{
    char *qstart, *qend, *lptr;
    int slen;
    int changed;

    /* Single-quoted entries */
    changed = TRUE;
    lptr = line;
    while (changed)
    {
	changed = FALSE;
	qstart = strchr(lptr, '\'');
	if (qstart)
	{
	    qend = strchr(qstart + 1, '\'');
	    if (qend && (qend > qstart)) {
		slen = strlen(lptr);
		for (lptr = qstart + 1; lptr < qend; lptr++) {
		    if (*lptr == ' ') {
			memmove(lptr, lptr + 1, slen);
			qend--;
			changed = TRUE;
		    }
		}
		lptr++;
	    }
	}
    }

    /* Double-quoted entries */
    changed = TRUE;
    lptr = line;
    while (changed)
    {
	changed = FALSE;
	qstart = strchr(lptr, '\"');
	if (qstart)
	{
	    qend = strchr(qstart + 1, '\"');
	    if (qend && (qend > qstart)) {
		slen = strlen(lptr);
		for (lptr = qstart + 1; lptr < qend; lptr++) {
		    if (*lptr == ' ') {
			memmove(lptr, lptr + 1, slen);
			qend--;
			changed = TRUE;
		    }
		}
		lptr++;
	    }
	}
    }

}

/*----------------------------------------------------------------------*/
/* GetNextLineNoNewline()						*/
/*									*/
/* Fetch the next line, and grab the first token from the next line.	*/
/* If there is no next token (next line is empty, and ends with a 	*/
/* newline), then place NULL in nexttok.				*/
/*									*/
/*----------------------------------------------------------------------*/

int GetNextLineNoNewline(char *delimiter)
{
  char *newbuf;
  int testc;

  if (feof(infile)) return -1;

  // This is more reliable than feof() ...
  testc = getc(infile);
  if (testc == -1) return -1;
  ungetc(testc, infile); 

  if (linesize == 0) {
	/* Allocate memory for line */
	linesize = 500;
	line = (char *)MALLOC(linesize);
	linetok = (char *)MALLOC(linesize);
  }
  fgets(line, linesize, infile);
  while (strlen(line) == linesize - 1) {
       newbuf = (char *)MALLOC(linesize + 500);
       strcpy(newbuf, line);
       FREE(line);
       line = newbuf;
       fgets(line + linesize - 1, 501, infile);
       linesize += 500;
       FREE(linetok);
       linetok = (char *)MALLOC(linesize);
  }
  linenum++;
  strcpy(linetok, line);
  TrimQuoted(linetok);

  nexttok = strdtok(linetok, WHITESPACE_DELIMITER, delimiter);
  return 0;
}

/*----------------------------------------------------------------------*/
/* Get the next line of input from file "infile", and find the first	*/
/* valid token.								*/
/*----------------------------------------------------------------------*/

void GetNextLine(char *delimiter)
{
    do {
	if (GetNextLineNoNewline(delimiter) == -1) return;
    } while (nexttok == NULL);
}

/*----------------------------------------------------------------------*/
/* if nexttok is already NULL, force scanner to read new line		*/
/*----------------------------------------------------------------------*/

void SkipTok(char *delimiter)
{
    if (nexttok != NULL && 
		(nexttok = strdtok(NULL, WHITESPACE_DELIMITER, delimiter)))
	return;
    GetNextLine(delimiter);
}

/*----------------------------------------------------------------------*/
/* like SkipTok, but will not fetch a new line when the buffer is empty */
/* must be preceeded by at least one call to SkipTok()			*/
/*----------------------------------------------------------------------*/

void SkipTokNoNewline(char *delimiter)
{
    nexttok = strdtok(NULL, WHITESPACE_DELIMITER, delimiter);
}

/*----------------------------------------------------------------------*/
/* if the next token ends the line, then this routine will check the	*/
/* first character only of the next line.  If "+", then it will pass	*/
/* that token and find the next token;  otherwise, it backs up.		*/
/* Must be preceeded by at least one call to SkipTok()			*/
/*									*/
/* Modified 3/17/2015 to handle the case where a line may be followed	*/
/* by "+" (continuation line) but without anything on the continuation	*/
/* line.  At each end-of-line, the next line must be checked for either	*/
/* a continuation line or a valid token.				*/
/*									*/
/* Modified 3/30/2015 to include the condition where a comment line is	*/
/* in the middle of a series of continuation lines.			*/
/*----------------------------------------------------------------------*/

void SpiceTokNoNewline(void)
{
    int contline;

    if ((nexttok = strdtok(NULL, WHITESPACE_DELIMITER, NULL)) != NULL) return;

    while (nexttok == NULL) {
	contline = getc(infile);
	if (contline == '*') {
	   GetNextLine(WHITESPACE_DELIMITER);
	   SkipNewLine(NULL);
	   continue;
	}
	else if (contline != '+') {
	    ungetc(contline, infile);
	    return;
	}
	if (GetNextLineNoNewline(WHITESPACE_DELIMITER) == -1) break;
    }
}

/*----------------------------------------------------------------------*/
/* Skip to the next token, ignoring any C-style comments.		*/
/*----------------------------------------------------------------------*/

void SkipTokComments(char *delimiter)
{
    SkipTok(delimiter);
    while (nexttok) {
	if (match(nexttok, "//")) {
	    SkipNewLine(delimiter);
	    SkipTok(delimiter);
	}
	else if (match(nexttok, "/*")) {
	    while (nexttok && !match(nexttok, "*/"))
		SkipTok(delimiter);
	    if (nexttok) SkipTok(delimiter);
	}
	else break;
    }
}

/*----------------------------------------------------------------------*/
/* skip to the end of the current line					*/
/*----------------------------------------------------------------------*/

void SkipNewLine(char *delimiter)
{
    while (nexttok != NULL)
	nexttok = strdtok(NULL, WHITESPACE_DELIMITER, delimiter);
}

/*----------------------------------------------------------------------*/
/* skip to the end of the current line, also skipping over any		*/
/* continuation lines beginning with "+" (SPICE syntax)			*/
/*----------------------------------------------------------------------*/

void SpiceSkipNewLine(void)
{
  int contline;

  SkipNewLine(NULL);
  contline = getc(infile);

  while (contline == '+') {
     ungetc(contline, infile);
     GetNextLine(WHITESPACE_DELIMITER);
     SkipNewLine(NULL);
     contline = getc(infile);
  }
  ungetc(contline, infile);
}

/*----------------------------------------------------------------------*/
/* Function similar to strtok() for token parsing.  The difference is	*/
/* that it takes two sets of delimiters.  The first is whitespace	*/
/* delimiters, which separate tokens.  The second is functional 	*/
/* delimiters, which separate tokens and have additional meaning, such	*/
/* as parentheses, commas, semicolons, etc.  The parser needs to know	*/
/* when such tokens occur in the string, so they are returned as	*/
/* individual tokens.							*/
/*									*/
/* Definition of delim2:  String of single delimiter characters.  The	*/
/* special character "X" (which is never a delimiter in practice) is	*/
/* used to separate single-character delimiters from two-character	*/
/* delimiters (this presumably could be further extended as needed).	*/
/* so ",;()" would be a valid delimiter set, but to include C-style	*/
/* comments and verilog-style parameter lists, one would need		*/
/* "X/**///#(X,;()".  Two-character delimiters should go first so that	*/
/* they have precedence over one-character delimiters.  'X' should be	*/
/* the first character of the delimiter string in addition to marking	*/
/* the boundary between two-character and one-character delimiters.	*/
/*----------------------------------------------------------------------*/

char *strdtok(char *pstring, char *delim1, char *delim2)
{
    static char *stoken = NULL;
    static char *sstring = NULL;
    char *s, *s2;
    char first = FALSE;
    int twofer;

    if (pstring != NULL) {
	/* Allocate enough memory to hold the string;  tokens will be put here */
	if (sstring != NULL) FREE(sstring);
	sstring = (char *)MALLOC(strlen(pstring) + 1);
	stoken = pstring;
	first = TRUE;
    }

    /* Skip over "delim1" delimiters at the string beginning */
    for (; *stoken; stoken++) {
	for (s2 = delim1; *s2; s2++)
	    if (*stoken == *s2)
		break;
	if (*s2 == '\0') break;
    }

    if (*stoken == '\0') return NULL;	/* Finished parsing */

    /* "stoken" is now set.  Now find the end of the current token */

    s = stoken;

    /* Special verilog rule:  If a name begins with '\', then all characters    */
    /* are a valid part of the name until a space character is reached.   The   */
    /* space character becomes part of the verilog name.  The remainder of the  */
    /* name is parsed according to the rules of "delim2".                       */

    if (*s == '\\') {
        while (*s != '\0') {
	    if (*s == ' ') {
		s++;
		break;
	    }
	    s++;
	}
    }

    /* Check string from position stoken.  If a character in "delim2" is found,	*/
    /* save the character in "lastdelim", null the byte at that position, and	*/
    /* return the token.  If a character in "delim1" is found, do the same but	*/
    /* continue checking characters as long as there are contiguous "delim1"	*/
    /* characters.  If the series ends in a character from "delim2", then treat	*/
    /* as for "delim2" above.  If not, then set "lastdelim" to a null byte and	*/
    /* return the token.							*/	

    for (; *s; s++) {
	twofer = (delim2 && (*delim2 == 'X')) ? TRUE : FALSE;
	for (s2 = ((twofer == TRUE) ? delim2 + 1 : delim2); s2 && *s2; s2++) {
	    if (*s2 == 'X') {
		twofer = FALSE;
		continue;
	    }
	    if (twofer) {
		if ((*s == *s2) && (*(s + 1) == *(s2 + 1))) {
		    if (s == stoken) {
			strncpy(sstring, stoken, 2);
			*(sstring + 2) = '\0';
			stoken = s + 2;
		    }
		    else {
			strncpy(sstring, stoken, (int)(s - stoken));
			*(sstring + (s - stoken)) = '\0';
			stoken = s;
		    }
		    return sstring;
		}
		s2++;
		if (*s2 == '\0') break;
	    }
	    else if (*s == *s2) {
		if (s == stoken) {
		    strncpy(sstring, stoken, 1);
		    *(sstring + 1) = '\0';
		    stoken = s + 1;
		}
		else {
		    strncpy(sstring, stoken, (int)(s - stoken));
		    *(sstring + (s - stoken)) = '\0';
		    stoken = s;
		}
		return sstring;
	    }
	}
	for (s2 = delim1; *s2; s2++) {
	    if (*s == *s2) {
		strncpy(sstring, stoken, (int)(s - stoken));
		*(sstring + (s - stoken)) = '\0';
		stoken = s;
		return sstring;
	    }
	}
    }
    strcpy(sstring, stoken);	/* Just copy to the end */
    stoken = s;
    return sstring;
}

/*----------------------------------------------------------------------*/

void InputParseError(FILE *f)
{
  char *ch;

  Fprintf(f,"line number %d = '", linenum);
  for (ch = line; *ch != '\0'; ch++) {
    if (isprint(*ch)) Fprintf(f, "%c", *ch);
    else if (*ch != '\n') Fprintf(f,"<<%d>>", (int)(*ch));
  }
  Fprintf(f,"'\n");
}

/*----------------------------------------------------------------------*/

int OpenParseFile(char *name, int fnum)
{
  /* Push filestack */
  
  FILE *locfile;
  struct filestack *newfile;

  locfile = fopen(name,"r");
  linenum = 0;
  /* reset the token scanner */
  nexttok = NULL;  

  if (locfile != NULL) {
     if (infile != NULL) {
        newfile = (struct filestack *)MALLOC(sizeof(struct filestack));
        newfile->file = infile;
        newfile->next = OpenFiles;
        OpenFiles = newfile;
     }
     infile = locfile;

     if (fnum != -1)
	return fnum;
     else if (OpenFiles == NULL)
        return Graph++;
     else
        return Graph;
  }
  return -1;
}

int EndParseFile(void)
{
  return (feof(infile));
}

int CloseParseFile(void)
{
  struct filestack *lastfile;
  int rval;
  rval = fclose(infile);
  infile = (FILE *)NULL;

  /* Pop filestack if not empty */
  lastfile = OpenFiles;
  if (lastfile != NULL) {
     OpenFiles = lastfile->next;
     infile = lastfile->file;
     FREE(lastfile);
  }
  
  return rval;
}

/*************************** general file reader *******************/

char *ReadNetlist(char *fname, int *fnum)
{
  int index, filenum;
  struct filetype {
    char *extension;
    char *(*proc)(char *, int *);
  };
  
#ifdef mips
  struct filetype formats[7];

  formats[0].extension = NTK_EXTENSION;
  formats[0].proc = ReadNtk;
  formats[1].extension = EXT_EXTENSION;
  formats[1].proc = ReadExtHier;
  formats[2].extension = SIM_EXTENSION;
  formats[2].proc = ReadSim;
  formats[3].extension = SPICE_EXTENSION;
  formats[3].proc = ReadSpice;
  formats[4].extension = NETGEN_EXTENSION;
  formats[4].proc = ReadNetgenFile;
  formats[5].extension = VERILOG_EXTENSION;
  formats[5].proc = ReadVerilogFile;
  formats[6].extension = NULL;
  formats[6].proc = NULL;

#else  /* not mips (i.e. compiler with reasonable initializers) */
  
  struct filetype formats[] =
    {
      {NTK_EXTENSION, ReadNtk},
      {EXT_EXTENSION, ReadExtHier},
      {SIM_EXTENSION, ReadSim},
      {SPICE_EXTENSION, ReadSpice},
      {SPICE_EXT2, ReadSpice},
      {SPICE_EXT3, ReadSpice},
      {VERILOG_EXTENSION, ReadVerilog},
      {NETGEN_EXTENSION, ReadNetgenFile},
      {NULL, NULL}
    };
#endif /* not mips */

  /* make first pass looking for extension */
  for (index = 0; formats[index].extension != NULL; index++) {
    if (strstr(fname, formats[index].extension) != NULL) {
      return (*(formats[index].proc))(fname, fnum);
    }
  }
  /* try appending extensions in sequence, and testing for file existance */
  for (index = 0; formats[index].extension != NULL; index++) {
    char testname[200];
    strcpy(testname, fname);
    strcat(testname, formats[index].extension);
    if (OpenParseFile(testname, *fnum) >= 0) {
      CloseParseFile();
      return (*(formats[index].proc))(testname, fnum);
    }
  }

  /* Check to see if file exists */
  if (OpenParseFile(fname, *fnum) >= 0) {
    char test[3];

    /* SPICE files have many extensions.  Look for first character "*" */

    if (fgets(test, 2, infile) == NULL) test[0] = '\0';
    CloseParseFile();
    if (test[0] == '*') {		/* Probably a SPICE deck */
      return ReadSpice(fname, fnum);
    }
    else if (test[0] == '|') {		/* Probably a sim netlist */
      return ReadSim(fname, fnum);
    }
    else {
      Printf("ReadNetlist: don't know type of file '%s'\n",fname);
      *fnum = -1;
      return NULL;
    }
  }
  Printf("ReadNetlist: unable to find file '%s'\n",fname);
  *fnum = -1;
  return NULL;
}


/*************************** simple NETGEN format ******************/

/* define the following for SLOW, but portable format */
/* #define USE_PORTABLE_FILE_FORMAT */

/* define the following if we are to buffer reads */
#define BUFFER_READS

#ifdef USE_PORTABLE_FILE_FORMAT
/* the following routines use streams, and are portable but slow */

void NetgenFileCell(char *name)
{
  struct nlist *tp, *tp2;
  struct objlist *ob;

  tp = LookupCell(name);
  if (tp == NULL) {
    Printf("No cell '%s' found.\n", name);
    return;
  }

  /* do NOT dump primitive cells */
  if (tp->class != CLASS_SUBCKT) 
    return;

  /* check to see that all children have been dumped */
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    tp2 = LookupCell(ob->model.class);
    if ((tp2 != NULL) && !(tp2->dumped)) 
      NetgenFileCell(tp2->name);
  }

  FlushString("Cell: %s\n", name);
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    FlushString("  %s %d %d ", ob->name, ob->node, ob->type);
    if (ob->type >= FIRSTPIN) FlushString("%s %s",ob->model.class, ob->instance.name);
    FlushString("\n");
  }
  FlushString("EndCell: %s\n\n", name);

  tp->dumped = 1;		/* set dumped flag */
}



void WriteNetgenFile(char *name, char *filename)
{
  char FileName[500];

  if (filename == NULL || strlen(filename) == 0) 
    SetExtension(FileName, name, NETGEN_EXTENSION);
  else 
    SetExtension(FileName, filename, NETGEN_EXTENSION);

  if (!OpenFile(FileName, 80)) {
    Printf("Unable to open NETGEN file %s\n", FileName);
    return;
  }
  ClearDumpedList();

  /* create top level call */
  if (LookupCell(name) != NULL) NetgenFileCell(name);

  CloseFile(FileName);
}


char *ReadNetgenFile (char *fname, int *fnum)
{
  char name[100];
  char *LastCellRead = NULL;
  int filenum;

  if ((filenum = OpenParseFile(fname, *fnum)) < 0) {
    SetExtension(name, fname, NETGEN_EXTENSION);
    if ((filenum = OpenParseFile(name, *fnum)) < 0) {
      Printf("Error in netgen read: No file %s\n",name);
      *fnum = -1;
      return NULL;
    }    
  }
  
  while (!feof(infile)) {
    char string[400];
    fscanf(infile, "%400s", string);
    if (feof(infile)) break; /* out of while loop */
    if (match(string,"Cell:")) {
      fscanf(infile, "%400s", string);
      CellDef(string, -1);
      LastCellRead = CurrentCell->name;
      while (1) {
	struct objlist *ob;
	fscanf(infile,"%400s",string);
	if (match(string,"EndCell:")) {
	  fscanf(infile,"%400s", string); /* get extra cell name */
	  break; /* get out of inner while loop */
	}
	if (feof(infile)) break; /* something awful happened */
	/* it must be an object */
	ob = (struct objlist *)CALLOC(1,sizeof(struct objlist));
	ob->name = strsave(string);
	fscanf(infile,"%d",&(ob->node));
	fscanf(infile,"%d",&(ob->type));
	if (ob->type >= FIRSTPIN) {
	  fscanf(infile,"%400s",string);
	  ob->model.class = strsave(string);
	  fscanf(infile,"%400s",string);
	  ob->instance.name = strsave(string);
	}
	else {
	  ob->model.class = strsave(" ");
	  ob->instance.name = strsave(" ");
	}
	if (ob->type == FIRSTPIN) {
	  if (NULL == LookupCell(ob->model.class))
	    Printf("WARING: instance of non-existant cell: %s\n", ob->model.class);
	  AddInstanceToCurrentCell(ob);
	  CurrentCell->class = CLASS_SUBCKT;  /* there is at least one instance */
	}
	AddToCurrentCell(ob);
      }
      EndCell();
    }
  }

  CloseParseFile();
  *fnum = filenum;
  return LastCellRead;
}

#else  /* don't use PORTABLE_FILE_FORMAT */

/* the following versions use binary files and are non-portable, but fast */

#ifdef IBMPC
#include <io.h>       /* read, write */
#include <fcntl.h>
#else  /* not IBMPC */
#ifdef VMUNIX
#ifdef BSD
#include <sys/file.h>
#include <sys/types.h>
#include <sys/uio.h>
#else  /* not BSD */
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <fcntl.h>
#endif /* not BSD */
#endif /* VMUNIX */
#endif /* IBMPC  */

#define END_OF_CELL 0x0fff
#define N_BYTE_ORDER  0x0102

void NetgenFileCell(char *name)
{
  struct nlist *tp, *tp2;
  struct objlist *ob;
  int len;

  tp = LookupCell(name);
  if (tp == NULL) {
    Printf("No cell '%s' found.\n", name);
    return;
  }

  /* do NOT dump primitive cells */
  if (tp->class != CLASS_SUBCKT) 
    return;

  /* check to see that all children have been dumped */
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    tp2 = LookupCell(ob->model.class);
    if ((tp2 != NULL) && !(tp2->dumped)) 
      NetgenFileCell(tp2->name);
  }


  len = strlen(name) + 1;
  write(File, &len, sizeof(len));
  write(File, name, len);

  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    len = strlen(ob->name) + 1;
    write(File, &len, sizeof(len));
    write(File, ob->name, len);
    write(File, &(ob->node), sizeof(ob->node));
    write(File, &(ob->type), sizeof(ob->type));
    if (ob->type >= FIRSTPIN) {
      len = strlen(ob->model.class) + 1;
      write(File, &len, sizeof(len));
      write(File, ob->model.class, len);    
      len = strlen(ob->instance.name) + 1;
      write(File, &len, sizeof(len));
      write(File, ob->instance.name, len);
    }
  }
  len = END_OF_CELL;
  write(File,&len, sizeof(len));
  tp->dumped = 1;		/* set dumped flag */
}



void WriteNetgenFile(char *name, char *filename)
{
  char FileName[500];
  int i, filenum;

  if (filename == NULL || strlen(filename) == 0) 
    SetExtension(FileName, name, NETGEN_EXTENSION);
  else 
    SetExtension(FileName, filename, NETGEN_EXTENSION);

  if ((filenum = open(FileName, O_WRONLY | O_CREAT | O_TRUNC, 
		   FILE_ACCESS_BITS)) == -1) {
    Printf("Unable to open NETGEN file %s\n", FileName);
    return;
  }
  ClearDumpedList();

  /* write out a sanity check */
  i = N_BYTE_ORDER;
  write(filenum, &i, sizeof(i));
  write(filenum, &i, sizeof(i));
  /* create top level call */
  if (LookupCell(name) != NULL) NetgenFileCell(name);

  close(File);
}


#ifdef BUFFER_READS
#define READ_BUFSIZ 5000
char *readbuf;
int bytes_in_buffer;
char *bufptr;

int READ(void *buf, int bytes)
{
  if (bytes_in_buffer >= bytes) {
    memcpy(buf, bufptr, bytes);
    bufptr += bytes;
    bytes_in_buffer -= bytes;
    return(bytes);
  }
  else {
#if 1
    int chars;

    /* need to refill buffer */
    if (bufptr > readbuf + bytes_in_buffer) {
      /* shift to front of buffer only if no overlap */
      memcpy(readbuf, bufptr, bytes_in_buffer); 
      bufptr = readbuf + bytes_in_buffer;
    }
    chars = read(File, bufptr, READ_BUFSIZ - bytes_in_buffer);
    bytes_in_buffer += chars;
    if (bytes_in_buffer >= bytes) {
      memcpy(buf, readbuf, bytes);
      bufptr = readbuf + bytes;
      bytes_in_buffer -= bytes;
      return(bytes);
    }
    else {
      memcpy(buf, readbuf, bytes_in_buffer);
      bufptr = readbuf;
      bytes = bytes_in_buffer;
      bytes_in_buffer = 0;
      return(bytes);
    }
#else
    int chars;
    char tmpbuf[READ_BUFSIZ];

    /* need to refill buffer */
    memcpy(tmpbuf, bufptr, bytes_in_buffer); /* shift to front of buffer */
    memcpy(readbuf, tmpbuf, bytes_in_buffer); /* use tmpbuf for safety */
    bufptr = readbuf + bytes_in_buffer;
    chars = read(File, bufptr, READ_BUFSIZ - bytes_in_buffer);
    bytes_in_buffer += chars;
    if (bytes_in_buffer >= bytes) {
      memcpy(buf, readbuf, bytes);
      bufptr = readbuf + bytes;
      bytes_in_buffer -= bytes;
      return(bytes);
    }
    else {
      memcpy(buf, readbuf, bytes_in_buffer);
      bufptr = readbuf;
      bytes = bytes_in_buffer;
      bytes_in_buffer = 0;
      return(bytes);
    }
#endif
  }
}

#else
#define READ(buf, bytes) read(File, (buf), (bytes))
#endif

char *ReadNetgenFile (char *fname, int *fnum)
{
  char name[100];
  int len, chars;
  char *LastCellRead = NULL;

  if ((File = open(fname, O_RDONLY, FILE_ACCESS_BITS)) == -1) {
    SetExtension(name, fname, NETGEN_EXTENSION);
    if ((File = open(name, O_RDONLY, FILE_ACCESS_BITS)) == -1) {
      Printf("Error in netgen read: No file %s\n",name);
      return NULL;
    }    
  }
  
#ifdef BUFFER_READS
  readbuf = (char *)MALLOC(READ_BUFSIZ);
  bytes_in_buffer = 0;
  bufptr = readbuf;
#endif

  READ(&len, sizeof(len));
  if (len != N_BYTE_ORDER) {
    Printf("Cannot read .ntg files created on different machines!\n");
    Printf("   File has byte order %X, CPU has %X\n",len, N_BYTE_ORDER);
    goto end;
  }
  READ(&len, sizeof(len));
  if (len != N_BYTE_ORDER) {
    Printf("Cannot read .ntg files created on different machines!\n");
    Printf("   Machines have different word sized (CPU int = %d)\n",
	    sizeof(len));
    goto end;
  }

  while (1) {
    char string[400];

    chars = READ(&len, sizeof(len));
    if (chars != sizeof(len)) break; /* we must be done */
    /* otherwise, read the cell name and continue */
    chars = READ(string, len);
    CellDef(string, -1);
    LastCellRead = CurrentCell->name;
    while (1) {
      struct objlist *ob;
      chars = READ(&len, sizeof(len));
      if (chars != sizeof(len) || len == END_OF_CELL) break;
      chars = READ(string, len);

      ob = (struct objlist *)CALLOC(1,sizeof(struct objlist));
      ob->name = (char *)MALLOC(len);
      strcpy(ob->name, string);
      READ(&(ob->node), sizeof(ob->node));
      READ(&(ob->type), sizeof(ob->type));
      if (ob->type >= FIRSTPIN) {
	READ(&len, sizeof(len));
	READ(string, len);
	ob->model.class = (char *)MALLOC(len);
	strcpy(ob->model.class,string);
	READ(&len, sizeof(len));
	READ(string, len);
	ob->instance.name = (char *)MALLOC(len);
	strcpy(ob->instance.name,string);
      }
      else {
	ob->model.class = (char *)CALLOC(1,1);
	ob->instance.name = (char *)CALLOC(1,1);
      }
      if (ob->type == FIRSTPIN) {
	if (NULL == LookupCell(ob->model.class))
	  Printf("WARING: instance of non-existance cell: %s\n",
		 ob->model.class);
	AddInstanceToCurrentCell(ob);
	CurrentCell->class = CLASS_SUBCKT; /* there is at least one instance */
      }
      AddToCurrentCell(ob);
    }
    EndCell();
  }
 end:
#ifdef BUFFER_READS
  FREE(readbuf);
#endif
  close(File);
  *fnum = Graph++;
  return LastCellRead;
}

#endif 	/* !USE_PORTABLE_FILE_FORMAT */
