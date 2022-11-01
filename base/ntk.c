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

/* ntk.c -- Input / output routines for .NTK format */

/* define the following to permit the definition of any unrecognized
   instances, as they are read in the .ntk file */
#define DEFINE_UNDEFINED_CELLS

#include "config.h"

#include <stdio.h>
#include <stdarg.h>

#ifdef TCL_NETGEN
#include <tcl.h>
#endif

#include "netgen.h"
#include "hash.h"
#include "objlist.h"
#include "netfile.h"
#include "print.h"

void ntkCell(char *name)
{
  struct nlist *tp, *tp2;
  struct objlist *ob, *ob2;

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
      ntkCell(tp2->name);
  }

  /* print out header list */
  FlushString("c %s ", tp->name);
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    if (IsPortInPortlist(ob, tp))
      FlushString("%s ", ob->name); /* unique ports only */
  }
  FlushString(";\n");

  /* run through cell's contents, defining all unique elements */
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    /* next test used to be reversed */
    if (match(ob->name, NodeAlias(tp, ob)) && 
	!IsPortInPortlist(ob,tp))
      FlushString ("s 1 %s ;\n", ob->name);
  }

  /* now run through cell's contents, print instances */
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    if (ob->type == FIRSTPIN) {
      /* this is an instance */
      /* print out cell, but special case transistors */
      if (match(ob->model.class, "n")) FlushString("N 2 ");
      else if (match(ob->model.class, "p")) FlushString("P 2 ");
      else FlushString ("h %s %s ", ob->model.class, ob->instance.name);

      /* print out parameter list */
      ob2 = ob;
      tp2 = LookupCell(ob->model.class);
      do {
	char	*nm;

	/* assume a form <instancename>/<port name> */
	/* was: strchr 12/12/88, but this was not right for FLATTENED things!*/
#if 1
	struct objlist *newob;

	nm = strrchr(ob2->name,SEPARATOR[0]) + 1;
	newob = LookupObject(nm, tp2);
	if (match(nm, NodeAlias(tp2, newob))) 
	  FlushString ("%s ", NodeAlias(tp, ob2));
#else
	int	nodenum;

	nm = strrchr(ob2->name,SEPARATOR[0]) + 1;
	nodenum = LookupObject(nm, tp2)->node;
	if ((nodenum == -1) || 
	    match(nm, NodeName(tp2, nodenum)))
	  FlushString ("%s ", NodeName(tp, ob2->node));
#endif
	ob2 = ob2->next;
      } while ((ob2 != NULL) && (ob2->type > FIRSTPIN));
      FlushString(";\n");
    }
  }
  FlushString (".\n");
  tp->dumped = 1;		/* set dumped flag */
}


void Ntk(char *name, char *filename)
{
  struct objlist *ob;
  struct nlist *tp;
  int	global_port;
  char FileName[500];

  if (filename == NULL || strlen(filename) == 0) 
    SetExtension(FileName, name, NTK_EXTENSION);
  else 
    SetExtension(FileName, filename, NTK_EXTENSION);

  if (!OpenFile(FileName, 80)) {
    Printf("Unable to open NTK file %s\n", FileName);
    return;
  }
  ClearDumpedList();
#if 1
  /* create top level call */
  if ((tp = LookupCell(name)) != NULL) {
    ntkCell(name);
    ob = tp->cell;
    global_port = 1;
    for (ob = tp->cell; ob != NULL; ob = ob->next) 
      if (IsPortInPortlist(ob, tp))
	FlushString ("s 1 %s #%d ;\n", 
		     NodeAlias(tp,ob), global_port++);
    FlushString("h %s %s ", name, name);
    for (ob = tp->cell; ob != NULL; ob = ob->next)
      if (IsPortInPortlist(ob,tp))
	FlushString ("%s ", NodeAlias(tp,ob));
    FlushString(";\n.\n.\n");
  }
#else
  /* same as above, but uses while loops instead of for loops */
  if (LookupCell(name) != NULL) {
    ntkCell(name);
    tp = LookupCell(name);
    ob = tp->cell;
    global_port = 1;
    do {
      if (IsPortInPortlist(ob, tp))
	FlushString ("s 1 %s #%d ;\n", 
		     NodeName(tp,ob->node), global_port++);
      ob = ob->next;
    } while (ob != NULL);
    FlushString("h %s %s ", name, name);
    ob = LookupCell(name)->cell;
    do {
      if (IsPortInPortlist(ob,tp))
	FlushString ("%s ", NodeName(tp,ob->node));
      ob = ob->next;
    } while (ob != NULL);
    FlushString(";\n.\n.\n");
  }
#endif
  CloseFile(FileName);
}



char *ReadNtk (char *fname, int *fnum)
{
  char	model[MAX_STR_LEN], instancename[MAX_STR_LEN], name[MAX_STR_LEN];
  struct objlist *ob;
  int CellDefInProgress = 0;
  int filenum;
  char *LastCellRead = NULL;

  if ((filenum = OpenParseFile(fname, *fnum)) < 0) {
    SetExtension(name, fname, NTK_EXTENSION);
    if ((filenum = OpenParseFile(name, *fnum)) < 0) {
      Printf("Error in ntk file read: No file %s\n",name);
      *fnum = filenum;
      return NULL;
    }    
  }

  while (!EndParseFile()) {
    SkipTok(NULL);
    if (EndParseFile()) break;
    if (nexttok[0] == '|') SKIPTO(";");
    else if (match(nexttok, "c")) {
      if (CellDefInProgress) {
	Printf("Recursive cell definition: cell %s open.\n",
		CurrentCell->name);
	EndCell();
	CellDefInProgress = 0;
      }
      SkipTok(NULL);
      CellDef(nexttok, CurrentCell->file);
      LastCellRead = CurrentCell->name;
      CellDefInProgress = 1;
      SkipTok(NULL);
      while (!match(nexttok, ";")) {
	Port(nexttok);
	SkipTok(NULL);
      }
    } 
    else if (match(nexttok, "s")) {
      char last[MAX_STR_LEN];
      *last = '\0';
      if (!CellDefInProgress) {
	/* fake cell declaration for top-level call */
	if (LookupCell(fname) == NULL) CellDef(fname, CurrentCell->file);
	else CellDef(NTK_EXTENSION, CurrentCell->file);
	CellDefInProgress = 1;
	if (LastCellRead == NULL)
	  LastCellRead = CurrentCell->name;
      }
      SkipTok(NULL);
      SkipTok(NULL);		/* eat the 'size' of the node */
      /* after the 'size', all names are synonyms */
      while (!match(nexttok, ";")) {
#if 1	
	if (strrchr(nexttok, PHYSICALPIN[0]) == NULL) Node(nexttok);
	else {
	  Printf("WARNING: internal node %-10s promoted to global port!\n",
		 nexttok);
	  Global(nexttok); /* make Actel pins in subcells visible */
	}
#else
	Node(nexttok);
#endif
	if (strlen(last)) join(last, nexttok);
	strcpy(last, nexttok);
	SkipTok(NULL);
      }
    } 
    else if (match(nexttok, "h")) {
      if (!CellDefInProgress) {
	CellDef("_MAIN", CurrentCell->file);
	CellDefInProgress = 1;
	if (LastCellRead == NULL)
	  LastCellRead = CurrentCell->name;
      }
      SkipTok(NULL);
      strcpy(model, nexttok);
      strcpy(instancename, nexttok);
      strcat(instancename, INSTANCE_DELIMITER);
      SkipTok(NULL);
      strcat(instancename, nexttok);
      if (LookupCell(model) == NULL) {
#ifdef DEFINE_UNDEFINED_CELLS
	char *previous_cell;
	int args, i;
	char *ports[100];

	previous_cell = CurrentCell->name;
	CellDef(model, CurrentCell->file);
	SkipTok(NULL); 
	args = 0;
	while (!match(nexttok, ";")) {
	  sprintf(name, "pin%d", args+1);
	  Port(name);
	  ports[args] = strsave(nexttok);
	  args++;
	  /* check for overflow */
	  if (args == (sizeof(ports) / sizeof(ports[0]))) {
	    while (!match(nexttok, ";")) SkipTok(NULL);
	    break; /* out of while loop */
	  }
	  /* if no overflow, get the next token */
	  SkipTok(NULL);
	}
	EndCell();
	/* now, reopen previous cell, instance the new cell,
           and wire it up */
	ReopenCellDef(previous_cell, CurrentCell->file);
	Instance(model, instancename);
	for (i = 0; i < args; i++) {
	  sprintf(name, "%s%spin%d", instancename, SEPARATOR, i+1);
	  join(ports[i], name);
	  FREE(ports[i]);
	}

#else
	Printf("Unknown cell class: %s instanced in cell %s\n",
		model, CurrentCell->name);
	SKIPTO(";");
#endif
      }
      else {
	Instance(model, instancename);
	ob = LookupCell(model)->cell;
	while ((ob != NULL) && !IsPort(ob))
	  ob = ob->next;
	SkipTok(NULL);
	while (!match(nexttok, ";")) {
	  strcpy(name, instancename);
	  strcat(name, SEPARATOR);
	  strcat(name, ob->name);
	  /* Connect(nexttok,name); */
	  join(nexttok, name);
	  do {
	    ob = ob->next;
	  } while ((ob != NULL) && !IsPort(ob)) ;
	  SkipTok(NULL);
	}
      }
    } 
    else if (match(nexttok, "n") || match(nexttok,"N")) {
      if (!CellDefInProgress) {
	CellDef("_MAIN", CurrentCell->file);
	CellDefInProgress = 1;
	if (LastCellRead == NULL)
	  LastCellRead = CurrentCell->name;
      }
      SkipTok(NULL);
      SkipTok(NULL);		/* skip the transistor size */
      strcpy(name, nexttok);
      SkipTok(NULL);
      strcpy(model, nexttok);
      SkipTok(NULL);
      strcpy(instancename, nexttok);
      N(fname, NULL, name, model, instancename);
      SKIPTO(";");
    } 
    else if (match(nexttok, "p") || match(nexttok,"P")) {
      if (!CellDefInProgress) {
	CellDef("_MAIN", CurrentCell->file);
	CellDefInProgress = 1;
	if (LastCellRead == NULL)
	  LastCellRead = CurrentCell->name;
      }
      SkipTok(NULL);
      SkipTok(NULL);		/* skip the transistor size */
      strcpy(name, nexttok);
      SkipTok(NULL);
      strcpy(model, nexttok);
      SkipTok(NULL);
      strcpy(instancename, nexttok);
      P(fname, NULL, name, model, instancename);
      SKIPTO(";");
    } 
    else if (match(nexttok, ".")) {
      EndCell();
      CellDefInProgress = 0;
    }
    else {
      Printf("Strange token in ntk: '%s'\n", nexttok);
      InputParseError(stderr);
    }
  }
  CloseParseFile();

  *fnum = filenum;
  return LastCellRead;
}




