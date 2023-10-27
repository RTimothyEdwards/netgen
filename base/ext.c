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

/* ext.c  --  Input/output routines for Berkeley .ext and .sim formats */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>	/* for strtod() */
#include <stdarg.h>
#include <ctype.h>

#include "netgen.h"
#include "objlist.h"
#include "netfile.h"
#include "print.h"
#include "hash.h"

static int invlambda = 100;	/* Used in sim and prm files */

void extCell(char *name, int filenum)
{
  struct nlist *tp, *tp2;
  struct objlist *ob, *ob2;
  int i;
  char FileName[500];

  tp = LookupCellFile(name, filenum);
  if (tp == NULL) {
    Printf ("No cell '%s' found.\n", name);
    return;
  }

  /* eliminate transistors as cells (EXT KLUGE) */
  ob = tp->cell;
  if (match(name, "n") || match(name, "p") || match(name, "e") ||
	match(name, "b") || match(name, "r") || matchnocase(name, "c")) {
    SetExtension(FileName, name, EXT_EXTENSION);
    if (!OpenFile(FileName, 0)) {
      Printf("ext(): Unable to open output file: %s.",FileName);
      return;
    }
    FlushString("timestamp 500000000\n");
    FlushString("version 4.0\n");
    FlushString("tech scmos\n");
    ob2 = ob;
    for (i = 0; i < 3; i++) {
      FlushString("node \"%s\" 1 1 0 0\n", ob2->name);
      ob2 = ob2->next;
    }
    FlushString("fet %sfet 0 0 0 0 0 0 0 ", name);
    ob2 = ob;
    for (i = 0; i < 3; i++) {
      FlushString("\"%s\" 4 0 ", ob2->name);
      ob2 = ob2->next;
    }
    FlushString("\n");
    CloseFile(FileName);
    tp->dumped = 1;		/* set dumped flag */
    return;
  }

  /* check to see that all children have been dumped */
  ob = tp->cell;
  while (ob != NULL) {
    if (ob->type == FIRSTPIN && ob->model.class) {
       tp2 = LookupCellFile(ob->model.class, filenum);
       if ((tp2 != NULL) && !(tp2->dumped)) 
          extCell(tp2->name, filenum);
    }
    ob = ob->next;
  }

  SetExtension(FileName, name, EXT_EXTENSION);
  if (!OpenFile(FileName, 0)) {
    perror("ext(): Unable to open output file.");
    return;
  }

  /* print out header list */
  FlushString("timestamp 500000000\n");
  FlushString("version 4.0\n");
  FlushString("tech scmos\n");

  /* run through cell's contents, defining all ports and nodes */
  for (ob = tp->cell; ob != NULL; ob = ob->next) 
    if ((ob->type == NODE) || IsPort(ob)) {
      char *nodename;

      FlushString ("node \"%s\" 1 1 0 0\n", ob->name);
      nodename = NodeAlias(tp,ob);
      if (!match(ob->name, nodename))
	FlushString ("merge \"%s\" \"%s\"\n", ob->name, nodename);
    }

  /* now run through cell's contents, print instances */

  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    if (ob->type == FIRSTPIN) {
      /* this is an instance */
      /* print out cell, but special case transistors */
      FlushString ("use %s %s 0 0 0 0 0 0\n",
		   ob->model.class, ob->instance.name);
      /* print out parameter list */
      ob2 = ob;
      do {
	char *nodename;
	nodename = NodeAlias(tp, ob2);
	if (!match(ob2->name, nodename))
	  FlushString ("merge \"%s\" \"%s\"\n", ob2->name, nodename);
	ob2 = ob2->next;
      } while ((ob2 != NULL) && (ob2->type > FIRSTPIN));
    }
  }
  FlushString ("\n");
  CloseFile(FileName);
  Printf("Wrote file: %s\n",FileName);
  tp->dumped = 1;		/* set dumped flag */
}


void Ext(char *name, int filenum)
{
	ClearDumpedList();
	if (LookupCellFile(name, filenum) != NULL) 
		extCell(name, filenum);
}

void GetExtName(char *name, char *nexttok)
{
#ifndef TCL_NETGEN
  char *p;
#endif

  /* strip leading and trailing quotes, if any exist */
  if (*nexttok == '"') {
    strcpy(name, nexttok+1);
    name[strlen(name) - 1] = '\0';
  }
  else strcpy(name, nexttok);

#ifndef TCL_NETGEN
  /* Quick hack to circumvent problems parsing brackets from magic.	*/
  /* Tcl version attempts to deal with arrays properly and has a	*/
  /* command to suppress wildcard behavior.				*/
  while ((p = strpbrk(name, "[]")) != NULL) *p = '_';
#endif
}

char *ReadExt(char *fname, int doflat, int *fnum)
{
  int cdnum = 1, rdnum = 1;
  int CellDefInProgress = 0;
  int filenum;

  if ((filenum = OpenParseFile(fname, *fnum)) < 0) {
    char name[MAX_STR_LEN];

    SetExtension(name, fname, EXT_EXTENSION);
    if ((filenum = OpenParseFile(name, *fnum)) < 0) {
      Printf("Error in ext file read: No file %s\n",name);
      *fnum = filenum;
      return NULL;
    }    
  }
  else {
							
    /* If "fname" had an extension on it, we don't want */
    /* the extension in the cell name.			*/
    char *pptr;
    if ((pptr = strrchr(fname, '.')) != NULL) *pptr = '\0';
  }

  /* Make sure all .ext file reading is case sensitive */
  matchfunc = match;
  matchintfunc = matchfile;
  hashfunc = hashcase;

  if (LookupCellFile(fname, filenum) != NULL) {
    Printf("Error:  Duplicate cell name \"%s\"!\n", fname);
    CloseParseFile();
    *fnum = filenum;
    return NULL;
  }

  while (!EndParseFile()) {
    SkipTok(NULL);

    if (EndParseFile()) break;
    if (nexttok[0] == '#') SkipNewLine(NULL);
    else if (match(nexttok, "timestamp")) SkipNewLine(NULL);
    else if (match(nexttok, "version"))  SkipNewLine(NULL);
    else if (match(nexttok, "tech"))  SkipNewLine(NULL);
    else if (match(nexttok, "scale"))  SkipNewLine(NULL);
    else if (match(nexttok, "style"))  SkipNewLine(NULL);
    else if (match(nexttok, "resistclasses"))  SkipNewLine(NULL);
    else if (match(nexttok, "node")) {
      char name[MAX_STR_LEN];

      /* No cell is generated until at least one valid "node" or "use"	*/
      /* has been read in the file.					*/

      if (!CellDefInProgress) {
	CellDef(fname, filenum);
	CellDefInProgress = 1;
      }
      SkipTok(NULL);
      GetExtName(name, nexttok);
      Node(name);  	/* Ports will be determined by context */
      SkipNewLine(NULL);
    }
    else if (match(nexttok, "equiv")) {
      char name[MAX_STR_LEN];
      char name2[MAX_STR_LEN];
      SkipTok(NULL);
      GetExtName(name, nexttok);
      if (LookupObject(name,CurrentCell) == NULL) Node(name);
      SkipTok(NULL);
      GetExtName(name2, nexttok);
      if (LookupObject(name2,CurrentCell) == NULL) Node(name2);
      join(name, name2);
      SkipNewLine(NULL);
    }
    else if (match(nexttok, "device")) {
      char dev_name[MAX_STR_LEN], dev_class[MAX_STR_LEN];
      char gate[MAX_STR_LEN], drain[MAX_STR_LEN], source[MAX_STR_LEN], subs[MAX_STR_LEN];
      char inststr[64];
      SkipTok(NULL);
      strcpy(dev_class, nexttok);
      SkipTok(NULL);
      strcpy(dev_name, nexttok);
      SkipTok(NULL); /* x coord of gate box */
      strcpy(inststr, dev_class);
      strcat(inststr, "@");
      strcat(inststr, nexttok);
      SkipTok(NULL); /* y coord of gate box */
      strcat(inststr, ",");
      strcat(inststr, nexttok);
      SkipTok(NULL); /* skip coord of gate box */
      SkipTok(NULL); /* skip coord of gate box */

      /* Device-dependent parameters */

      if (match(dev_class, "mosfet")) {
         SkipTok(NULL); /* skip device length */
         SkipTok(NULL); /* skip device width */
         SkipTok(NULL);
         GetExtName(subs, nexttok);
      }
      else if (match(dev_class, "bjt")) {
         SkipTok(NULL); /* skip device length */
         SkipTok(NULL); /* skip device width */
         SkipTok(NULL);
         GetExtName(subs, nexttok);
      }
      else if (match(dev_class, "devcap")) {
         SkipTok(NULL); /* skip device length */
         SkipTok(NULL); /* skip device width */	/* or. . . */
      }
      else if (match(dev_class, "devres")) {
         SkipTok(NULL); /* skip device length */
         SkipTok(NULL); /* skip device width */	/* or. . . */
      }
      else if (match(dev_class, "diode")) {
         SkipTok(NULL);
         GetExtName(gate, nexttok);
         SkipTok(NULL); /* skip terminal length */
         SkipTok(NULL); /* skip terminal attributes */
         SkipTok(NULL);
         GetExtName(drain, nexttok);
         SkipTok(NULL); /* skip terminal length */
         SkipTok(NULL); /* skip terminal attributes */
      }
      else if (match(dev_class, "subckt")) {
         SkipTok(NULL); /* skip device length */
         SkipTok(NULL); /* skip device width */
         SkipTok(NULL);
         GetExtName(subs, nexttok);
	 while (nexttok != NULL) {
            SkipTok(NULL);
            GetExtName(gate, nexttok);
            SkipTok(NULL); /* skip terminal length */
            SkipTok(NULL); /* skip terminal attributes */
	 }
      }
      else if (match(dev_class, "rsubckt")) {
         SkipTok(NULL); /* skip device length */
         SkipTok(NULL); /* skip device width */
         SkipTok(NULL);
         GetExtName(subs, nexttok);
      }
      SkipTokNoNewline(NULL);
      SkipNewLine(NULL);
    }
    else if (match(nexttok, "fet")) {	/* old-style FET record */
      char fet_class[MAX_STR_LEN];
      char gate[MAX_STR_LEN], drain[MAX_STR_LEN], source[MAX_STR_LEN], subs[MAX_STR_LEN];
      char inststr[64];
      SkipTok(NULL);
      strcpy(fet_class, nexttok);
      SkipTok(NULL); /* x coord of gate box */
      strcpy(inststr, fet_class);
      strcat(inststr, "@");
      strcat(inststr, nexttok);
      SkipTok(NULL); /* y coord of gate box */
      strcat(inststr, ",");
      strcat(inststr, nexttok);
      SkipTok(NULL); /* skip coord of gate box */
      SkipTok(NULL); /* skip coord of gate box */
      SkipTok(NULL); /* skip gate area */
      SkipTok(NULL); /* skip gate perimeter */
      SkipTok(NULL);
      GetExtName(subs, nexttok);
      SkipTok(NULL);
      GetExtName(gate, nexttok);
      SkipTok(NULL); /* skip terminal length */
      SkipTok(NULL); /* skip terminal attributes */
      SkipTok(NULL);
      GetExtName(drain, nexttok);
      SkipTok(NULL); /* skip terminal length */
      SkipTok(NULL); /* skip terminal attributes */
      SkipTokNoNewline(NULL);
      if (nexttok == NULL) {
	/* This gets around a problem with the magic extractor in which */
	/* transistors having shorted source-drain are written into the */
	/* .ext file missing one terminal.  This should be corrected in */
	/* magic.							*/
	strcpy(source, drain);
      }
      else
        GetExtName(source, nexttok);
      SkipNewLine(NULL);
      /* remap transistors into things we know about */
      if (match(fet_class, "nfet"))
	  N(fname, inststr, gate, drain, source);
      else if (match(fet_class, "pfet"))
	  P(fname, inststr, gate, drain, source);
      else if (match(fet_class, "ecap"))
	  E(fname, inststr, gate, drain, source);
      else if (match(fet_class, "bnpn"))
	  B(fname, inststr, subs, gate, source);
      else if (match(fet_class, "zpolyResistor"))
	  Res3(fname, inststr, gate, drain, source);
      else {
	  Printf("Unknown fet type in ext: '%s'\n", fet_class);
	  InputParseError(stderr);
      }
    }
    else if (match(nexttok, "cap")) {
      if (IgnoreRC) {
	/* ignore all capacitances */
	SkipNewLine(NULL);
      }
      else {
        char ctop[MAX_STR_LEN], cbot[MAX_STR_LEN], cdummy[MAX_STR_LEN];
	SkipTok(NULL);
        GetExtName(ctop, nexttok);
	SkipTok(NULL);
        GetExtName(cbot, nexttok);
	SkipNewLine(NULL);	/* Skip over capacitance value */
	Cap(fname, NULL, ctop, cbot);
      }
    }
    else if (match(nexttok, "use")) {
      char name[MAX_STR_LEN];
      char instancename[MAX_STR_LEN];
      char *basename;

      /* No cell is generated until at least one valid "node" or "use"	*/
      /* has been read in the file.					*/

      if (!CellDefInProgress) {
	CellDef(fname, filenum);
	CellDefInProgress = 1;
      }

      SkipTok(NULL);
      GetExtName(name, nexttok);
      if ((basename = strrchr(name,'/')) != NULL) {
	char tmp[MAX_STR_LEN];
	strcpy(tmp, basename+1);
	strcpy(name, tmp);
      }
      SkipTok(NULL);
      GetExtName(instancename, nexttok);
      Printf("Instancing %s as %s\n", name, instancename);
      Instance(name, instancename);
      if (doflat) {
        Printf("Flattening %s in %s\n", instancename, fname);
	flattenInstancesOf(NULL, filenum, name);
      }
      SkipNewLine(NULL);
    }
    else if (match(nexttok, "merge")) {
      char name[MAX_STR_LEN];
      char name2[MAX_STR_LEN];
      SkipTok(NULL);
      GetExtName(name, nexttok);
      SkipTok(NULL);
      GetExtName(name2, nexttok);
      if (doflat)
	 join(name, name2);
      else if ((strchr(name, '/') == NULL) && (strchr(name2, '/') == NULL))
	 join(name, name2);
      else {
      }
      SkipNewLine(NULL);
    }
    else {
      Printf("Strange token in ext: '%s'\n", nexttok);
      InputParseError(stderr);
      SkipNewLine(NULL);
    }
  }
  CloseParseFile();
  *fnum = filenum;
  return (CellDefInProgress) ? CurrentCell->name : NULL;
}

/* Hierarchical and quasi-Flattened versions of the above */

char *ReadExtHier(char *fname, int *fnum)
{
   return ReadExt(fname, 0, fnum);
}

char *ReadExtFlat(char *fname, int *fnum)
{
   return ReadExt(fname, 1, fnum);
}


/***********************  .SIM FORMAT SUPPORT **************************/

void simCell(char *name, int filenum)
{
  struct nlist *tp, *tp2;
  struct objlist *ob, *ob2;
  char FileName[500], simclass;
  char writeLine[1024], paramString[128];
  short i, p, mult;
  double l, w, v;

  tp = LookupCellFile(name, filenum);
  if (tp == NULL) {
    Printf ("No cell '%s' found.\n", name);
    return;
  }

  /* Check procedure disabled because new extensions to .sim format
   * allow 'x' records to represent low-level components that are
   * modeled as SPICE subcircuits.
   */
#if 0
  /* check to see that all children have been dumped */
  ob = tp->cell;
  while (ob != NULL) {
    if (ob->type == FIRSTPIN && ob->model.class) {
       tp2 = LookupCellFile(ob->model.class, filenum);
       if ((tp2 != NULL) && !(tp2->dumped) && (tp2->class == CLASS_SUBCKT)) 
          Printf("Cell must be flat before .SIM written.  Found instance: %s\n",
		tp2->name);
    }
    ob = ob->next;
  }
#endif

  SetExtension(FileName, name, SIM_EXTENSION);
  if (!OpenFile(FileName, 0)) {
    perror("sim(): Unable to open output file.");
    return;
  }

  /* print out header list */
  /* distance units are multiplied by invlambda */

  FlushString("| units: %d    tech: scmos\n", 100 * invlambda);

  /* now run through cell's contents, print instances */
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    if (ob->type == FIRSTPIN) {

      /* this is an instance */

      tp2 = LookupCellFile(ob->model.class, filenum);
      switch (tp2->class) {
	 case CLASS_NMOS: case CLASS_NMOS4:
	    simclass = 'n';
	    break;	
	 case CLASS_PMOS: case CLASS_PMOS4:
	    simclass = 'p';
	    break;	
	 case CLASS_FET4: case CLASS_FET3: case CLASS_FET:
	    /* take an educated guess. . . */
	    if (tolower(ob->model.class[0]) == 'p')
	       simclass = 'p';
	    else if (tolower(ob->model.class[0]) == 'n')
	       simclass = 'n';
	    else if (tolower(ob->model.class[strlen(ob->model.class) - 1]) == 'p')
	       simclass = 'p';
	    else
	       simclass = 'n';
	    break;	
	 case CLASS_CAP:
	    simclass = 'c';
	    break;
	 case CLASS_RES:
	    simclass = 'r';
	    break;
	 case CLASS_NPN:
	    simclass = 'b';
	    break;	
	 case CLASS_SUBCKT:
	 case CLASS_MODULE:
	    simclass = 'x';
	    break;	
	 default:
	    simclass = '|';
	    break;	
      }

      if (simclass != 'x')
	 FlushString("%c", simclass);

      switch (tp2->class) {
	 case CLASS_NMOS: case CLASS_NMOS4:
	 case CLASS_PMOS: case CLASS_PMOS4:
	 case CLASS_FET4: case CLASS_FET3: case CLASS_FET:

	    ob2 = ob->next;
	    /* write gate and drain */
	    FlushString(" %s", NodeAlias(tp, ob2));
	    FlushString(" %s", NodeAlias(tp, ob));
	    ob2 = ob2->next;
	    FlushString(" %s", NodeAlias(tp, ob2));	/* write source */

	    /* Skip any bulk node on 4-terminal devices */
	    while ((ob2 != NULL) && (ob2->type > FIRSTPIN)) ob2 = ob2->next;
	    
	    /* default minimum L/W transistors (scale?) */
	    l = 2;
	    w = 4;
	    if (ob2 && ob2->type == PROPERTY) {
	       struct property *kl;
	       struct valuelist *vl;
	       kl = (struct property *)HashLookup("length", &(tp2->propdict));
	       vl = (struct valuelist *)ob2->instance.name;
	       l = 1.0e6 * vl[kl->idx].value.dval;	/* m -> um */
	       kl = (struct property *)HashLookup("width", &(tp2->propdict));
	       w = 1.0e6 * vl[kl->idx].value.dval;	/* m -> um */
	    }
	    FlushString(" %g %g\n", l, w);   
	    break;

	 case CLASS_NPN: case CLASS_PNP: case CLASS_BJT:
	    ob2 = ob->next;
	    FlushString(" %s", NodeAlias(tp, ob2));	/* base */
	    ob2 = ob2->next;
	    /* emitter and collector */
	    FlushString(" %s\n", NodeAlias(tp, ob2));
	    FlushString(" %s\n", NodeAlias(tp, ob));
	    /* skip any other pins (there shouldn't be any. . .) */
	    while ((ob2 != NULL) && (ob2->type > FIRSTPIN)) ob2 = ob2->next;
	    break;

	 case CLASS_CAP: case CLASS_RES:
	 case CLASS_CAP3: case CLASS_RES3: case CLASS_ECAP:
	    v = 1;
	    ob2 = ob;
	    for (i = 0; i < 2; i++) {
	      FlushString(" %s", NodeAlias(tp, ob2));
	      ob2 = ob2->next;
	      if ((ob2 == NULL) || (ob2->type <= FIRSTPIN)) break;
	    }
	    while ((ob2 != NULL) && (ob2->type > FIRSTPIN))
	      ob2 = ob2->next;  /* Skip dummy node on 3-terminal devices */

	    if (ob2 && ob2->type == PROPERTY) {
	       struct property *kl;
	       struct valuelist *vl;
	       kl = (struct property *)HashLookup("value", &(tp2->propdict));
	       vl = (struct valuelist *)ob2->instance.name;
	       if (tp2->class == CLASS_CAP)
	          v = 1.0e15 * vl[kl->idx].value.dval;	/* F -> fF */
	       else if (tp2->class == CLASS_RES)
		  v = vl->value.dval;		/* Ohms (no conversion) */
	    }
	    FlushString(" %g\n", v);   
	    break;

	 case CLASS_SUBCKT: case CLASS_MODULE:
	    *writeLine = 'x';
	    *(writeLine + 1) = '\0';
	    mult = 1;

	    /* Important---Need to look up the cell definition;  if
	     * the first pin is "drain" then it is a FET, and pins
	     * get swapped from D-G-S-B to G-S-D-B.  Source and drain
	     * are treated here as equivalent because the .sim format
	     * has no concept of an asymmetric source and drain.
	     */
	    ob2 = tp2->cell;
	    if ((*matchfunc)(ob2->next->name, "gate"))
	    {
	      strcat(writeLine, " ");
	      strcat(writeLine, NodeAlias(tp, ob->next));
	      strcat(writeLine, " ");
	      strcat(writeLine, NodeAlias(tp, ob));
	      ob2 = ob->next->next;
	    }
	    else
	      ob2 = ob;

	    while (ob2 != NULL) {
	      strcat(writeLine, " ");
	      strcat(writeLine, NodeAlias(tp, ob2));
	      ob2 = ob2->next;
	      if ((ob2 == NULL) || (ob2->type <= FIRSTPIN)) break;
	    }

	    if (ob2 && ob2->type == PROPERTY) {
	       struct valuelist *vl;
	       /* Only known parameters are L, W, X, and Y */
	       for (p = 0;; p++) {
	          vl = (struct valuelist *)(&(ob2->instance.props[p]));
		  if (vl->type == PROP_ENDLIST) {
	             strcat(writeLine, " l=1");   
		     break;
		  }
		  else if ((*matchfunc)(vl->key, "L")) {
	             v = vl->value.dval;
	             sprintf(paramString, " l=%d", (int)(0.5 + (v * invlambda)));   
	             strcat(writeLine, paramString);
		     break;
		  }
	       }
	       for (p = 0;; p++) {
	          vl = (struct valuelist *)(&(ob2->instance.props[p]));
		  if (vl->type == PROP_ENDLIST) {
	             strcat(writeLine, " w=1");   
		     break;
		  }
		  else if ((*matchfunc)(vl->key, "W")) {
	             v = vl->value.dval;
	             sprintf(paramString, " w=%d", (int)(0.5 + (v * invlambda)));   
	             strcat(writeLine, paramString);
		     break;
		  }
	       }
	       for (p = 0;; p++) {
	          vl = (struct valuelist *)(&(ob2->instance.props[p]));
		  if (vl->type == PROP_ENDLIST) {
	             strcat(writeLine, " x=0");   
		     break;
		  }
		  else if ((*matchfunc)(vl->key, "X")) {
	             i = vl->value.ival;
	             sprintf(paramString, " x=%d", i);
	             strcat(writeLine, paramString);
		     break;
		  }
	       }
	       for (p = 0;; p++) {
	          vl = (struct valuelist *)(&(ob2->instance.props[p]));
		  if (vl->type == PROP_ENDLIST) {
	             strcat(writeLine, " y=0");   
		     break;
		  }
		  else if ((*matchfunc)(vl->key, "Y")) {
	             i = vl->value.ival;
	             sprintf(paramString, " y=%d", i);
	             strcat(writeLine, paramString);
		  }
	       }
	       for (p = 0;; p++) {
	          vl = (struct valuelist *)(&(ob2->instance.props[p]));
		  if (vl->type == PROP_ENDLIST) {
		     break;
		  }
		  else if ((*matchfunc)(vl->key, "M")) {
		     if (vl->type == PROP_INTEGER)
	                mult = vl->value.ival;
		     else
	                mult = vl->value.dval;
		  }
	       }
	    }
	    strcat(writeLine, " ");
	    strcat(writeLine, tp2->name);   
	    strcat(writeLine, "\n");   

	    /* Multiple instances (M != 1) are written multiple times.
	     * NF is ignored in favor of having a single device with
	     * the total width.
	     */
	    for (i = 0; i < mult; i++)
		FlushString(writeLine);
	    break;

	 default:
	    FlushString("| unhandled component %s\n", tp2->name);   
	    break;
      }
    }
  }

  FlushString ("\n");
  CloseFile(FileName);
  Printf("Wrote file: %s\n",FileName);

  tp->dumped = 1;		/* set dumped flag */
}


void Sim(char *name, int filenum)
{
	ClearDumpedList();
	if (LookupCellFile(name, filenum) != NULL) 
		simCell(name, filenum);
}

/*-------------------------------------------------*/
/* Check whether a string token is a valid integer */
/*-------------------------------------------------*/

int StrIsInt(char *s)
{
    if (*s == '-' || *s == '+') s++;
    while (*s)
        if (!isdigit(*s++))
            return (0);

    return (1);
}

/*------------------------------------------------------*/
/* Read a .prm format file.  This is specifically to	*/
/* get the "device" lines that indicate how a SPICE	*/
/* subcircuit model or a .sim "x" record needs to be	*/
/* translated into a specific component type like a	*/
/* FET, diode, resistor, etc.				*/
/*------------------------------------------------------*/

char *ReadPrm(char *fname, int *fnum)
{
  int filenum;
  struct keyvalue *kvlist = NULL;
  struct nlist *tp;

  if ((filenum = OpenParseFile(fname, *fnum)) < 0) {
    char name[MAX_STR_LEN];

    SetExtension(name, fname, PRM_EXTENSION);
    if (OpenParseFile(name, *fnum) < 0) {
      Printf("Error in prm file read: No file %s\n",name);
      *fnum = filenum;
      return NULL;
    }    
  }

  /* Make sure all .prm file reading is case INsensitive */
  /* This is because the only reason to read a PRM file	 */
  /* is to find all the subcircuit device types	so that	 */
  /* a SPICE file can be read and a SIM file written.	 */

  matchfunc = matchnocase;
  matchintfunc = matchfile;
  hashfunc = hashnocase;

  CellDef(fname, filenum);

  while (!EndParseFile()) {
    char devicename[MAX_STR_LEN];
    SkipTok(NULL);

    if (EndParseFile()) break;
    if (nexttok[0] == ';') continue;		/* Comment line */
    else if (nexttok[0] == '\0') continue;	/* Blank line */
    else if (match(nexttok, "lambda")) {
      SkipTok(NULL);
      invlambda = (int)(0.5 + (1.0 / atof(nexttok)));
      SkipNewLine(NULL); /* skip any attributes */
    }
    else if (match(nexttok, "device")) {
      SkipTok(NULL);
      if (match(nexttok, "nfet")) {
	 SkipTok(NULL);
	 strcpy(devicename, nexttok);
	 /* Create 4-terminal nfet subcircuit device record */
	 if (LookupCellFile(devicename, filenum) == NULL) {
	     CellDef(devicename, filenum);
	     Port("drain");
	     Port("gate");
	     Port("source");
	     Port("bulk");
	     PropertyDouble(devicename, filenum, "l", 0.01, 0.0);
	     PropertyDouble(devicename, filenum, "w", 0.01, 0.0);
	     PropertyInteger(devicename, filenum, "nf", 0, 1);
	     PropertyInteger(devicename, filenum, "m", 0, 1);
	     SetClass(CLASS_SUBCKT);
	     EndCell();
	     ReopenCellDef(fname, filenum);
	 }
         LinkProperties(devicename, kvlist);
         SkipNewLine(NULL); /* skip any attributes */
      }
      else if (match(nexttok, "pfet")) {
	 SkipTok(NULL);
	 strcpy(devicename, nexttok);
	 /* Create 4-terminal pfet subcircuit device record */
	 if (LookupCellFile(devicename, filenum) == NULL) {
	     CellDef(devicename, filenum);
	     Port("drain");
	     Port("gate");
	     Port("source");
	     Port("well");
	     PropertyDouble(devicename, filenum, "l", 0.01, 0.0);
	     PropertyDouble(devicename, filenum, "w", 0.01, 0.0);
	     PropertyInteger(devicename, filenum, "nf", 0, 1);
	     PropertyInteger(devicename, filenum, "m", 0, 1);
	     SetClass(CLASS_SUBCKT);
	     EndCell();
	     ReopenCellDef(fname, filenum);
	 }
         LinkProperties(devicename, kvlist);
         SkipNewLine(NULL); /* skip various attributes */
      }
      else if (match(nexttok, "resistor")) {
	 SkipTok(NULL);
	 strcpy(devicename, nexttok);
	 /* Resistor device has additional record for the value */
	 /* (Need to do something with this. . . ?) */
	 SkipTok(NULL);
	 /* Create resistor subcircuit device record */
         if (LookupCellFile(devicename, filenum) == NULL) {
            CellDef(devicename, filenum);
            Port("end_a");
            Port("end_b");
            PropertyDouble(devicename, filenum, "value", 0.01, 0.0);
	    PropertyDouble(devicename, filenum, "l", 0.01, 0.0);
	    PropertyDouble(devicename, filenum, "w", 0.01, 0.0);
	    PropertyInteger(devicename, filenum, "m", 0, 1);
            SetClass(CLASS_SUBCKT);
            EndCell();
            ReopenCellDef(fname, filenum);
         }
	 LinkProperties(devicename, kvlist);
         SkipNewLine(NULL); /* skip various attributes */
      }
      else if (match(nexttok, "capacitor")) {
	 SkipTok(NULL);
	 strcpy(devicename, nexttok);
	 /* Capacitor device has additional record for the value */
	 /* (Need to do something with this. . . ?) */
	 SkipTok(NULL);
	 /* Create capacitor subcircuit device record */
         if (LookupCellFile(devicename, filenum) == NULL) {
            CellDef(devicename, filenum);
            Port("top");
            Port("bottom");
            PropertyDouble(devicename, filenum, "value", 0.01, 0.0);
	    PropertyDouble(devicename, filenum, "l", 0.01, 0.0);
	    PropertyDouble(devicename, filenum, "w", 0.01, 0.0);
	    PropertyInteger(devicename, filenum, "m", 0, 1);
            SetClass(CLASS_SUBCKT);
            EndCell();
            ReopenCellDef(fname, filenum);  /* Reopen */
         }
	 LinkProperties(devicename, kvlist);
         SkipNewLine(NULL);
      }
      else if (match(nexttok, "diode")) {
	 SkipTok(NULL);
	 strcpy(devicename, nexttok);
	 /* Create diode subcircuit device record */
         if (LookupCellFile(devicename, filenum) == NULL) {
            CellDef(devicename, filenum);
            Port("anode");
            Port("cathode");
	    PropertyInteger(devicename, filenum, "m", 0, 1);
            SetClass(CLASS_SUBCKT);
            EndCell();
            ReopenCellDef(fname, filenum);  /* Reopen */
         }
	 LinkProperties(devicename, kvlist);
         SkipNewLine(NULL);
      }
      else {
	Printf("Unknown device type in .prm: '%s'\n", nexttok);
        InputParseError(stderr);
        SkipNewLine(NULL);
      }
    }
    else {
      /* Could spell out all the keywords used in .prm files    */
      /* but probably not worth the effort.                     */
      SkipNewLine(NULL);
    }
    DeleteProperties(&kvlist);
  }
  EndCell();
  CloseParseFile();

  tp = LookupCellFile(fname, filenum);
  if (tp) tp->flags |= CELL_TOP;

  *fnum = filenum;
  return fname;
}

/*-------------------------*/
/* Read a .sim format file */
/*-------------------------*/

char *ReadSim(char *fname, int *fnum)
{
  int cdnum = 1, rdnum = 1, filenum;
  int has_lumped = 0;
  char *vstr;
  struct keyvalue *kvlist = NULL;
  struct nlist *tp;
  double simscale = 1.0;

  if ((filenum = OpenParseFile(fname, *fnum)) < 0) {
    char name[MAX_STR_LEN];

    SetExtension(name, fname, SIM_EXTENSION);
    if (OpenParseFile(name, *fnum) < 0) {
      Printf("Error in sim file read: No file %s\n",name);
      *fnum = filenum;
      return NULL;
    }    
  }

  /* Make sure all .sim file reading is case sensitive */
  matchfunc = match;
  matchintfunc = matchfile;
  hashfunc = hashcase;

  CellDef(fname, filenum);

  while (!EndParseFile()) {
    SkipTok(NULL);

    if (EndParseFile()) break;
    if (nexttok[0] == '|') {
      SkipTok(NULL);		/* "units" */
      if (!strcmp(nexttok, "units:")) {
        SkipTok(NULL);
	simscale = strtod(nexttok, NULL);
      }
      SkipNewLine(NULL);
    }
    else if (match(nexttok, "n")) {
      char gate[MAX_STR_LEN], drain[MAX_STR_LEN], source[MAX_STR_LEN];
      char inststr[25], *instptr = NULL;

      SkipTok(NULL);
      GetExtName(gate, nexttok);
      if (LookupObject(gate, CurrentCell) == NULL)
	Node(gate);   /* define the node if it does not already exist */

      SkipTok(NULL);
      GetExtName(drain, nexttok);
      if (LookupObject(drain, CurrentCell) == NULL)
	Node(drain);   /* define the node if it does not already exist */

      SkipTok(NULL);
      GetExtName(source, nexttok);
      if (LookupObject(source, CurrentCell) == NULL)
	Node(source);   /* define the node if it does not already exist */

      SkipTokNoNewline(NULL);	/* length */
      if ((nexttok != NULL) && (nexttok[0] != '\0')) {
	vstr = ScaleStringFloatValue(&nexttok[0], simscale * 1e-8);
	AddProperty(&kvlist, "length", vstr);
        SkipTok(NULL);		/* width */
	if ((nexttok != NULL) && (nexttok[0] != '\0')) {
	    vstr = ScaleStringFloatValue(&nexttok[0], simscale * 1e-8);
	    AddProperty(&kvlist, "width", vstr);
	}
        SkipTokNoNewline(NULL);
        if (nexttok != NULL) {
	  if (StrIsInt(nexttok)) {
	    strcpy(inststr, "n@");
	    strcat(inststr, nexttok);
	    SkipTok(NULL);
	    strcat(inststr, ",");
	    strcat(inststr, nexttok);
	    instptr = inststr;
	  }
	}
      }
      SkipNewLine(NULL); /* skip any attributes */
      N(fname, instptr, gate, drain, source);
      LinkProperties("n", kvlist);
    }
    else if (match(nexttok, "p")) {
      char gate[MAX_STR_LEN], drain[MAX_STR_LEN], source[MAX_STR_LEN];
      char inststr[25], *instptr = NULL;
      SkipTok(NULL);
      GetExtName(gate, nexttok);
      if (LookupObject(gate, CurrentCell) == NULL)
	Node(gate);   /* define the node if it does not already exist */
      SkipTok(NULL);
      GetExtName(drain, nexttok);
      if (LookupObject(drain, CurrentCell) == NULL)
	Node(drain);   /* define the node if it does not already exist */
      SkipTok(NULL);
      GetExtName(source, nexttok);
      if (LookupObject(source, CurrentCell) == NULL)
	Node(source);   /* define the node if it does not already exist */
      SkipTokNoNewline(NULL);	/* length */
      if ((nexttok != NULL) && (nexttok[0] != '\0')) {
	vstr = ScaleStringFloatValue(&nexttok[0], simscale * 1e-8);
	AddProperty(&kvlist, "length", vstr);
        SkipTok(NULL);	/* width */
	if ((nexttok != NULL) && (nexttok[0] != '\0')) {
	    vstr = ScaleStringFloatValue(&nexttok[0], simscale * 1e-8);
	    AddProperty(&kvlist, "width", vstr);
	}
        SkipTokNoNewline(NULL);
        if (nexttok != NULL) {
	  if (StrIsInt(nexttok)) {
	    strcpy(inststr, "p@");
	    strcat(inststr, nexttok);
	    SkipTok(NULL);
	    strcat(inststr, ",");
	    strcat(inststr, nexttok);
	    instptr = inststr;
	  }
	}
      }
      SkipNewLine(NULL); /* skip various attributes */
      P(fname, instptr, gate, drain, source);
      LinkProperties("p", kvlist);
    }
    else if (match(nexttok, "e")) {	/* 3-port capacitors (poly/poly2) */
      char gate[MAX_STR_LEN], drain[MAX_STR_LEN], source[MAX_STR_LEN];
      char inststr[25], *instptr = NULL;
      SkipTok(NULL);
      GetExtName(gate, nexttok);
      if (LookupObject(gate, CurrentCell) == NULL)
	Node(gate);   /* define the node if it does not already exist */
      SkipTok(NULL);
      GetExtName(drain, nexttok);
      if (LookupObject(drain, CurrentCell) == NULL)
	Node(drain);   /* define the node if it does not already exist */
      SkipTok(NULL);
      GetExtName(source, nexttok);
      if (LookupObject(source, CurrentCell) == NULL)
	Node(source);   /* define the node if it does not already exist */
      SkipTokNoNewline(NULL);	/* skip length */
      if (nexttok != NULL) {
        SkipTok(NULL);	/* skip width */
        SkipTokNoNewline(NULL);
        inststr[0] = '\0';
        if (nexttok != NULL) {
	  if (StrIsInt(nexttok)) {
	    strcpy(inststr, "e@");
	    strcat(inststr, nexttok);
	    SkipTok(NULL);
	    strcat(inststr, ",");
	    strcat(inststr, nexttok);
	    instptr = inststr;
	  }
	}
      }
      SkipNewLine(NULL); /* skip various attributes */
      E(fname, instptr, gate, drain, source);
    }
    else if (match(nexttok, "b")) {		/* bipolars added by Tim 7/16/96 */
      char base[MAX_STR_LEN], emitter[MAX_STR_LEN], collector[MAX_STR_LEN];
      char inststr[25], *instptr = NULL;
      SkipTok(NULL);
      GetExtName(base, nexttok);
      if (LookupObject(base, CurrentCell) == NULL)
	Node(base);   /* define the node if it does not already exist */
      SkipTok(NULL);
      GetExtName(emitter, nexttok);
      if (LookupObject(emitter, CurrentCell) == NULL)
	Node(emitter);   /* define the node if it does not already exist */
      SkipTok(NULL);
      GetExtName(collector, nexttok);
      if (LookupObject(collector, CurrentCell) == NULL)
	Node(collector);   /* define the node if it does not already exist */
      SkipTokNoNewline(NULL);	/* skip length */
      if (nexttok != NULL) {
        SkipTok(NULL);	/* skip width */
        SkipTokNoNewline(NULL);
        if (nexttok != NULL) {
	  if (StrIsInt(nexttok)) {
	    strcpy(inststr, "b@");
	    strcat(inststr, nexttok);
	    SkipTok(NULL);
	    strcat(inststr, ",");
	    strcat(inststr, nexttok);
	    instptr = inststr;
	  }
	}
      }
      SkipNewLine(NULL); /* skip various attributes */
      B(fname, instptr, collector, base, emitter);
    }
    else if (matchnocase(nexttok, "c")) { /* 2-port capacitors */
      if (IgnoreRC) {
	/* ignore all capacitances */
	SkipNewLine(NULL);
      }
      else {
        char ctop[MAX_STR_LEN], cbot[MAX_STR_LEN], cdummy[MAX_STR_LEN];
        SkipTok(NULL);
        GetExtName(ctop, nexttok);
        if (LookupObject(ctop, CurrentCell) == NULL)
	  Node(ctop);   /* define the node if it does not already exist */
        SkipTok(NULL);
        GetExtName(cbot, nexttok);
        if (LookupObject(cbot, CurrentCell) == NULL)
	  Node(cbot);   /* define the node if it does not already exist */
	SkipTokNoNewline(NULL);
	if (nexttok != NULL) {
	   vstr = ScaleStringFloatValue(&nexttok[0], 1e-15);
	   AddProperty(&kvlist, "value", vstr);
	}
        SkipNewLine(NULL);
        Cap(fname, NULL, ctop, cbot);
	LinkProperties("c", kvlist);
      }
    }
    else if (match(nexttok, "r")) {	/* 2-port resistors */
      if (IgnoreRC) {
	/* ignore all resistances */
	SkipNewLine(NULL);
      }
      else {
        char rtop[MAX_STR_LEN], rbot[MAX_STR_LEN];
        SkipTok(NULL);
        GetExtName(rtop, nexttok);
        if (LookupObject(rtop, CurrentCell) == NULL)
	  Node(rtop);   /* define the node if it does not already exist */
        SkipTok(NULL);
        GetExtName(rbot, nexttok);
        if (LookupObject(rbot, CurrentCell) == NULL)
	  Node(rbot);   /* define the node if it does not already exist */
	SkipTokNoNewline(NULL);
	if (nexttok != NULL) {
	   AddProperty(&kvlist, "value", &nexttok[0]);
	}
        SkipNewLine(NULL); /* skip various attributes */
        Res(fname, NULL, rtop, rbot);
	LinkProperties("r", kvlist);
      }
    }
    else if (match(nexttok, "z")) {	/* 3-port resistors from magic */
      if (IgnoreRC) {
	/* ignore all capacitances */
	SkipNewLine(NULL);
      }
      else {
        char rtop[MAX_STR_LEN], rbot[MAX_STR_LEN], rdummy[MAX_STR_LEN];
	char inststr[25], *instptr = NULL;
        SkipTok(NULL);
        GetExtName(rdummy, nexttok);
        if (LookupObject(rdummy, CurrentCell) == NULL)
	  Node(rdummy);   /* define the node if it does not already exist */
        SkipTok(NULL);
        GetExtName(rtop, nexttok);
        if (LookupObject(rtop, CurrentCell) == NULL)
	  Node(rtop);   /* define the node if it does not already exist */
        SkipTok(NULL);
        GetExtName(rbot, nexttok);
        if (LookupObject(rbot, CurrentCell) == NULL)
	  Node(rbot);   /* define the node if it does not already exist */
        SkipTokNoNewline(NULL);	/* skip length */
        if (nexttok != NULL) {
          SkipTok(NULL);	/* skip width */
          SkipTokNoNewline(NULL);
          if (nexttok != NULL) {
	    if (StrIsInt(nexttok)) {
	      strcpy(inststr, "z@");
	      strcat(inststr, nexttok);
	      SkipTok(NULL);
	      strcat(inststr, ",");
	      strcat(inststr, nexttok);
	      instptr = inststr;
	    }
	  }
	}
        SkipNewLine(NULL); /* skip various attributes */
        Res3(fname, instptr, rdummy, rtop, rbot);
      }
    }
    else if (match(nexttok, "N")) {
      /* ignore this keyword */
      SkipNewLine(NULL);
    }
    else if (match(nexttok, "A")) {
      /* ignore this keyword */
      SkipNewLine(NULL);
    }
    else if (match(nexttok, "=")) {
      char node1[MAX_STR_LEN], node2[MAX_STR_LEN];
      SkipTok(NULL);
      GetExtName(node1, nexttok);
      SkipTok(NULL);
      GetExtName(node2, nexttok);
      join(node1, node2);
    }
    else if (match(nexttok, "R")) {
      if (has_lumped == 0) {
        Printf("Ignoring lumped resistances (\"R\" records) in .sim.\n");
	has_lumped = 1;	  /* Don't print this message more than once */
      }
      SkipNewLine(NULL);
    }
    else {
      Printf("Strange token in .sim: '%s'\n", nexttok);
      InputParseError(stderr);
      SkipNewLine(NULL);
    }
    DeleteProperties(&kvlist);
  }
  EndCell();
  CloseParseFile();

  tp = LookupCellFile(fname, filenum);
  if (tp) tp->flags |= CELL_TOP;

  *fnum = filenum;
  return fname;
}
