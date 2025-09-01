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

/* spice.c -- Input / output for SPICE and ESACAP formats */

#include "config.h"

#include <stdio.h>
#if 0
#include <stdarg.h>  /* what about varargs, like in pdutils.c ??? */
#endif

#include <stdlib.h>  /* for calloc(), free(), getenv() */
#include <ctype.h>  /* for toupper(), isascii() */
#ifndef IBMPC
#include <sys/types.h>	/* for getpwnam() tilde expansion */
#include <pwd.h>
#endif

#ifdef TCL_NETGEN
#include <tcl.h>
#endif

#include "netgen.h"
#include "hash.h"
#include "objlist.h"
#include "netfile.h"
#include "print.h"
#include "query.h"
#include "objlist.h"
#include "netcmp.h"

// Global storage for parameters from .PARAM
struct hashdict spiceparams;

// Global setting for auto-detect of empty subcircuits as
// black-box subcells.
int auto_blackbox = FALSE;

// Check if a token represents a numerical value (with
// units) or an expression.  This is basically a hack
// to see if it either passes StringIsValue() or is
// enclosed in braces.  Probably should attempt to
// parse the expression, to be pedantic.  Not sure all
// expressions have to be in braces.

int StringIsValueOrExpression(char *token)
{
    if (StringIsValue(token)) return TRUE;
    else if (*token == '{') return TRUE;
    else return FALSE;
}

void SpiceSubCell(struct nlist *tp, int IsSubCell)
{
  struct objlist *ob;
  int node, maxnode;
  char *model;
  struct tokstack *stackptr;

  /* check to see that all children have been dumped */
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    if (ob->type == FIRSTPIN) {
      struct nlist *tp2;

      tp2 = LookupCellFile(ob->model.class, tp->file);
      if ((tp2 != NULL) && !(tp2->dumped) && (tp2->class == CLASS_SUBCKT)) 
	SpiceSubCell(tp2, 1);
    }
  }

  /* print preface, if it is a subcell */
  if (IsSubCell) {
    FlushString(".SUBCKT %s ",tp->name);
    for (ob = tp->cell; ob != NULL; ob = ob->next) 
      if (IsPortInPortlist(ob, tp)) FlushString("%d ", ob->node);
    FlushString("\n");
  }

  /* print names of all nodes, prefixed by comment character */
  maxnode = 0;
  for (ob = tp->cell; ob != NULL; ob = ob->next)
    if (ob->node > maxnode) maxnode = ob->node;

  /* was:  for (node = 0; node <= maxnode; node++)  */
  for (node = 1; node <= maxnode; node++) 
    FlushString("# %3d = %s\n", node, NodeName(tp, node));

  /* traverse list of objects */
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
     if (ob->type == FIRSTPIN) {
        int drain_node, gate_node, source_node;
	char spice_class;
	struct nlist *tp2;

	tp2 = LookupCellFile(ob->model.class, tp->file);
	model = tp2->name;

	/* Convert class numbers (defined in netgen.h) to SPICE classes */
	switch (tp2->class) {
	   case CLASS_NMOS4: case CLASS_PMOS4: case CLASS_FET4:
	   case CLASS_NMOS: case CLASS_PMOS: case CLASS_FET3:
	   case CLASS_FET: case CLASS_ECAP:
	      spice_class = 'M';
	      break;
	   case CLASS_NPN: case CLASS_PNP: case CLASS_BJT:
	      spice_class = 'Q';
	      break;
	   case CLASS_VSOURCE:
	      spice_class = 'V';
	      break;
	   case CLASS_ISOURCE:
	      spice_class = 'I';
	      break;
	   case CLASS_RES: case CLASS_RES3:
	      spice_class = 'R';
	      break;
	   case CLASS_DIODE:
	      spice_class = 'D';
	      break;
	   case CLASS_INDUCTOR:
	      spice_class = 'L';
	      break;
	   case CLASS_CAP: case CLASS_CAP3:
	      spice_class = 'C';
	      break;
	   case CLASS_SUBCKT: case CLASS_MODULE:
	      spice_class = 'X';
	      break;
	   case CLASS_XLINE:
	      spice_class = 'T';
	      break;
	   default:
	      Printf ("Bad device class found.\n");
	      continue;		/* ignore it. . . */
	}
	
        FlushString("%c%s", spice_class, ob->instance.name);

        /* Print out nodes.  FETs switch node order */

	switch (tp2->class) {

	   /* 3-terminal FET devices---handled specially */
	   case CLASS_NMOS: case CLASS_PMOS: case CLASS_FET3:
	      ob = ob->next;
              FlushString(" %s", ob->name);		/* drain */
	      ob = ob->next;
              FlushString(" %s", ob->name);		/* gate */
	      ob = ob->next;
              FlushString(" %s", ob->name);		/* source */
	      ob = ob->next;
	      if (tp2->class == CLASS_NMOS)
                 FlushString(" GND!");		/* default substrate */
	      else if (tp2->class == CLASS_PMOS)
                 FlushString(" VDD!");		/* default well */
	      else 
                 FlushString(" BULK");		/* default bulk---unknown */
	      break;

	   /* All other devices have nodes in order of SPICE syntax */
	   default:
	      while (ob->next != NULL && ob->next->type >= FIRSTPIN) {
                 ob = ob->next;
                 FlushString(" %s", ob->name);
	      }
	      break;
	}

	/* caps, resistors, voltage and current sources, print out device value */

	/* print out device type (model/subcircuit name) */

	switch (tp2->class) {
	   case CLASS_CAP:
	      if (matchnocase(model, "c")) {
		 ob = ob->next;
		 if (ob->type == PROPERTY) {
		    struct valuelist *vl;
		    int i;
		    for (i == 0;; i++) {
		       vl = &(ob->instance.props[i]);
		       if (vl->type == PROP_ENDLIST) break;
		       else if (vl->type == PROP_VALUE) {
			  FlushString(" %g", vl->value.dval);
			  break;
		       }
		    }
		 }
	      }
	      else
		 FlushString(" %s", model); 	/* semiconductor capacitor */
	      break;

	   case CLASS_RES:
	      if (matchnocase(model, "r")) {
		 ob = ob->next;
		 if (ob->type == PROPERTY) {
		    struct valuelist *vl;
		    int i;
		    for (i == 0;; i++) {
		       vl = &(ob->instance.props[i]);
		       if (vl->type == PROP_ENDLIST) break;
		       else if (vl->type == PROP_VALUE) {
			  FlushString(" %g", vl->value.dval);
			  break;
		       }
		    }
		 }
	      }
	      else
		 FlushString(" %s", model); 	/* semiconductor resistor */
	      break;

	   case CLASS_VSOURCE:
	   case CLASS_ISOURCE:
	      ob = ob->next;
	      if (ob->type == PROPERTY) {
		 struct valuelist *vl;
		 int i;
		 for (i == 0;; i++) {
		    vl = &(ob->instance.props[i]);
		    if (vl->type == PROP_ENDLIST) break;
		    else if (vl->type == PROP_VALUE) {
		       FlushString(" %g", vl->value.dval);
		       break;
		    }
		 }
	      }
	      break;

	   default:
	      FlushString(" %s", model);	/* everything else */
	}
	   
	/* write properties (if any) */
	if (ob) ob = ob->next;
	if (ob && ob->type == PROPERTY) {
	   struct valuelist *kv;
	   int i;
	   for (i = 0; ; i++) {
	      kv = &(ob->instance.props[i]);
	      if (kv->type == PROP_ENDLIST) break;
	      switch (kv->type) {
		 case PROP_STRING:
	            FlushString(" %s=%s", kv->key, kv->value.string);
		    break;
		 case PROP_INTEGER:
	            FlushString(" %s=%d", kv->key, kv->value.ival);
		    break;
		 case PROP_DOUBLE:
		 case PROP_VALUE:
	            FlushString(" %s=%g", kv->key, kv->value.dval);
		    break;
		 case PROP_EXPRESSION:
	            FlushString(" %s=", kv->key);
		    stackptr = kv->value.stack;
		    while (stackptr->next != NULL)
		       stackptr = stackptr->next;
		    
		    while (stackptr != NULL) {
		       switch (stackptr->toktype) {
			  case TOK_STRING:
			     FlushString("%s", stackptr->data.string);
			     break;
			  case TOK_DOUBLE:
			     FlushString("%d", stackptr->data.dvalue);
			     break;
			  case TOK_MULTIPLY:
			     FlushString("*");
			     break;
			  case TOK_DIVIDE:
			     FlushString("/");
			     break;
			  case TOK_PLUS:
			     FlushString("+");
			     break;
			  case TOK_MINUS:
			     FlushString("-");
			     break;
			  case TOK_FUNC_OPEN:
			     FlushString("(");
			     break;
			  case TOK_FUNC_CLOSE:
			     FlushString(")");
			     break;
			  case TOK_GT:
			     FlushString(">");
			     break;
			  case TOK_LT:
			     FlushString("<");
			     break;
			  case TOK_GE:
			     FlushString(">=");
			     break;
			  case TOK_LE:
			     FlushString("<=");
			     break;
			  case TOK_EQ:
			     FlushString("==");
			     break;
			  case TOK_NE:
			     FlushString("!=");
			     break;
			  case TOK_GROUP_OPEN:
			     FlushString("{");
			     break;
			  case TOK_GROUP_CLOSE:
			     FlushString("}");
			     break;
			  case TOK_FUNC_IF:
			     FlushString("IF(");
			     break;
			  case TOK_FUNC_THEN:
			  case TOK_FUNC_ELSE:
			     FlushString(",");
			     break;
			  case TOK_SGL_QUOTE:
			     FlushString("'");
			     break;
			  case TOK_DBL_QUOTE:
			     FlushString("\"");
			     break;
		       }
		       stackptr = stackptr->last;
		    }
	            FlushString(" ");
		    break;
	      }
	   }
	}
	FlushString("\n");
    }
  }
	
  if (IsSubCell) FlushString(".ENDS\n");
  tp->dumped = 1;
}


void SpiceCell(char *name, int fnum, char *filename)
{
  struct nlist *tp;
  char FileName[500];

  tp = LookupCellFile(name, fnum);

  if (tp == NULL) {
    Printf ("No cell '%s' found.\n", name);
    return;
  }

  if (filename == NULL || strlen(filename) == 0) 
    SetExtension(FileName, name, SPICE_EXTENSION);
  else 
    SetExtension(FileName, filename, SPICE_EXTENSION);

  if (!OpenFile(FileName, 80)) {
    perror("ext(): Unable to open output file.");
    return;
  }
  ClearDumpedList();
  /* all spice decks begin with comment line */
  FlushString("SPICE deck for cell %s written by Netgen %s.%s\n\n", 
	      name, NETGEN_VERSION, NETGEN_REVISION);
  SpiceSubCell(tp, 0);
  CloseFile(FileName);
}

/*------------------------------------------------------*/
/* Routine to update instances with proper pin names,	*/
/* if the instances were called before the cell		*/
/* definition.						*/
/*------------------------------------------------------*/

int renamepins(struct hashlist *p, int file)
{
   struct nlist *ptr, *tc;
   struct objlist *ob, *ob2, *obp;

   ptr = (struct nlist *)(p->ptr);

   if (ptr->file != file)
      return 1;

   for (ob = ptr->cell; ob != NULL; ob = ob->next) {
      if (ob->type == FIRSTPIN) {
	 tc = LookupCellFile(ob->model.class, file);
	 obp = ob;
	 for (ob2 = tc->cell; ob2 != NULL; ob2 = ob2->next) {
	    if (ob2->type != PORT) break;
	    else if ((obp->type < FIRSTPIN) || (obp->type == FIRSTPIN && obp != ob)) {
	       Fprintf(stderr, "Pin count mismatch between cell and instance of %s\n",
			tc->name);
	       InputParseError(stderr);
	       break;
	    }
	    if (!matchnocase(ob2->name, obp->name + strlen(obp->instance.name) + 1)) {
	       // Printf("Cell %s pin correspondence: %s vs. %s\n",
	       // 	tc->name, obp->name, ob2->name);
	       FREE(obp->name);
	       obp->name = (char *)MALLOC(strlen(obp->instance.name)
				+ strlen(ob2->name) + 2);
	       sprintf(obp->name, "%s/%s", obp->instance.name, ob2->name);
	    }
	    obp = obp->next;
	    if (obp == NULL) break;
	 }
      }
   }
}

/* If any pins are marked unconnected, see if there are	*/
/* other pins of the same name that have connections.	*/
/* Also remove any unconnected globals (just for cleanup) */

void CleanupSubcell() {
   int maxnode = 0;
   int has_devices = FALSE;
   struct objlist *sobj, *nobj, *lobj, *pobj;

   if (CurrentCell == NULL) return;

   for (sobj = CurrentCell->cell; sobj; sobj = sobj->next)
      if (sobj->node > maxnode)
	 maxnode = sobj->node + 1;

   lobj = NULL;
   for (sobj = CurrentCell->cell; sobj != NULL;) {
      nobj = sobj->next;
      if (sobj->type == FIRSTPIN)
	 has_devices = TRUE;
      if (sobj->node < 0) {
         if (IsGlobal(sobj)) {
 	    if (lobj != NULL)
	       lobj->next = sobj->next;
	    else
	       CurrentCell->cell = sobj->next;
	    FreeObjectAndHash(sobj, CurrentCell);
	 }
	 else if (IsPort(sobj) && sobj->model.port == PROXY)
	    sobj->node = maxnode++;
	 else if (IsPort(sobj)) {
	    for (pobj = CurrentCell->cell; pobj && (pobj->type == PORT);
			pobj = pobj->next) {
	       if (pobj == sobj) continue;
	       if (matchnocase(pobj->name, sobj->name) && pobj->node >= 0) {
		  sobj->node = pobj->node;
		  break;
	       }
	    }
	    lobj = sobj;
	 }
	 else
	    lobj = sobj;
      }
      else
         lobj = sobj;
      sobj = nobj;
   }
   if ((has_devices == FALSE) && (auto_blackbox == TRUE))
      SetClass(CLASS_MODULE);
}

/*------------------------------------------------------*/
/* Push a subcircuit name onto the stack		*/
/*------------------------------------------------------*/

void PushStack(char *cellname, struct cellstack **top)
{
   struct cellstack *newstack;

   newstack = (struct cellstack *)CALLOC(1, sizeof(struct cellstack));
   newstack->cellname = cellname; 
   newstack->next = *top;
   *top = newstack; 
}

/*------------------------------------------------------*/
/* Pop a subcircuit name off of the stack		*/
/*------------------------------------------------------*/

void PopStack(struct cellstack **top)
{
   struct cellstack *stackptr;

   stackptr = *top;
   if (!stackptr) return;
   *top = stackptr->next;
   FREE(stackptr);
}

/* Forward declaration */
extern void IncludeSpice(char *, int, struct cellstack **, int);

/*------------------------------------------------------*/
/* Read a SPICE deck					*/
/*------------------------------------------------------*/

void ReadSpiceFile(char *fname, int filenum, struct cellstack **CellStackPtr,
		int blackbox)
{
  int cdnum = 1, rdnum = 1;
  int warnings = 0, update = 0, hasports = 0;
  char *eqptr, devtype, in_subckt;
  struct keyvalue *kvlist = NULL;
  char inst[MAX_STR_LEN], model[MAX_STR_LEN], instname[MAX_STR_LEN];
  struct nlist *tp, *tpsave;
  struct objlist *parent, *sobj, *nobj, *lobj, *pobj;

  inst[MAX_STR_LEN-1] = '\0';
  model[MAX_STR_LEN-1] = '\0';
  instname[MAX_STR_LEN-1] = '\0';
  in_subckt = (char)0;
  
  while (!EndParseFile()) {

    SkipTok(NULL); /* get the next token */
    if ((EndParseFile()) && (nexttok == NULL)) break;
    if (nexttok == NULL) break;

    if (nexttok[0] == '*') SkipNewLine(NULL);

    else if (matchnocase(nexttok, ".SUBCKT")) {
      SpiceTokNoNewline();
      if (nexttok == NULL) {
	 Fprintf(stderr, "Badly formed .subkt line\n");
	 goto skip_ends;
      }

      if (in_subckt == (char)1) {
	  Fprintf(stderr, "Missing .ENDS statement on subcircuit.\n");
          InputParseError(stderr);
      }
      in_subckt = (char)1;

      /* Save pointer to current cell */
      if (CurrentCell != NULL)
         parent = CurrentCell->cell;
      else
	 parent = NULL;

      /* Check for existence of the cell.  We may need to rename it. */

      snprintf(model, MAX_STR_LEN-1, "%s", nexttok);
      tp = LookupCellFile(nexttok, filenum);
      tpsave = NULL;

      /* Check for name conflict with duplicate cell names	*/
      /* This may mean that the cell was used before it was	*/
      /* defined, but CDL files sometimes just redefine the	*/
      /* same cell over and over.  So check if it's empty.	*/

      if ((tp != NULL) && (tp->class != CLASS_MODULE)) {
	 int n;
	 char *ds;

	 // NOTE:  Use this to ignore the new definition---should be
	 // an option to netgen.
	 /* goto skip_ends; */

	 ds = strrchr(model, '[');
	 if ((ds != NULL) && (*(ds + 1) == '['))
	    sscanf(ds + 2, "%d", &n);
	 else {
	    ds = model + strlen(model);
	    sprintf(ds, "[[0]]");
	    n = -1;
	 }

	 Printf("Duplicate cell %s in file\n", nexttok);
	 tp->flags |= CELL_DUPLICATE;
         while (tp != NULL) {
	    n++;
	    /* Append "[[n]]" to the preexisting model name to force uniqueness */
	    sprintf(ds, "[[%d]]", n);
            tp = LookupCellFile(model, filenum);
	 }
	 Printf("Renaming original cell to %s\n", model);
	 InstanceRename(nexttok, model, filenum);
	 CellRehash(nexttok, model, filenum);
         CellDef(nexttok, filenum);
         tp = LookupCellFile(nexttok, filenum);
      }
      else if (tp != NULL) {	/* Make a new definition for an empty cell */
	 /* Handle issue with SPICE read after verilog, where a placeholder
	  * was created from the verilog.  (1) If the pin names are "1", "2",
	  * "3", then this is a SPICE placeholder, and just remove the CellDef
	  * and re-create it.  Otherwise, create new cell "_PLACEHOLDER_".
	  * (2) After encountering .ends, run MatchPins between the two cells.
	  * (3) delete the original cell and rename the new cell.
	  */
	 int i = 1;
	 char pname[10];
	 for (pobj = tp->cell; pobj && pobj->type == PORT; pobj = pobj->next) {
	     sprintf(pname, "%d", i);
	     if (!matchnocase(pobj->name, pname)) break;
	     i++;
	 }
	 if ((pobj == NULL) || (pobj->type != PORT)) {
	    /* This is a SPICE placeholder created because the cell was instanced
	     * before it was defined.  However, the pins can be assumed to be in
	     * the correct order, and pin reordering does not need to be done.
	     */
	    FreePorts(nexttok);
	    CellDelete(nexttok, filenum);	/* This removes any PLACEHOLDER flag */
	    CellDef(model, filenum);
	    tp = LookupCellFile(model, filenum);
	    update = 1;	/* Will need to update existing instances */
	 }
	 else {
	    /* This is (probably) a verilog placeholder created because the
	     * verilog was read before the (SPICE) definitions.  The verilog
	     * netlist should have named the pins of the parent cell.  However,
	     * there is no guarantee the order of pins is correct.  The MatchPins()
	     * routine from netcmp.c can be used here to match the cell against
	     * the placeholder, and reorder the pins in all instances to match.
	     * Note that we cannot just reorder the SPICE pins to match the
	     * verilog order, because there may be other SPICE netlists which
	     * instance the cell with the correct SPICE port order.
	     */
	     tpsave = tp;
             CellDef("_PLACEHOLDER_", filenum);
             tp = LookupCellFile("_PLACEHOLDER_", filenum);
	 }
      }
      else if (tp == NULL) {	/* Completely new cell, no name conflict */
         CellDef(model, filenum);
         tp = LookupCellFile(model, filenum);
      }

      hasports = 0;
      if (tp != NULL) {

	 PushStack(tp->name, CellStackPtr);

         /* Tokens on the rest of the line are ports or		*/
	 /* properties.  Treat everything with an "=" as a	*/
	 /* property, all others as ports. "M=" is *not* a	*/
	 /* valid property meaning "number of" in a SUBCKT	*/
	 /* line, and if it exists, it should be recorded as	*/
	 /* a property, and (to be done) NOT treated as		*/
	 /* referring to number of devices in a subcircuit	*/
	 /* call.						*/

         SpiceTokNoNewline();
         while (nexttok != NULL) {

	    // Because of somebody's stupid meddling with
	    // SPICE syntax, we have to check for and ignore
	    // any use of the keyword "PARAMS:"

	    if (!strcasecmp(nexttok, "PARAMS:")) {
		SpiceTokNoNewline();
		continue;
	    }

	    if ((eqptr = strchr(nexttok, '=')) != NULL) {
		*eqptr = '\0';
		// Only String properties allowed
		PropertyString(tp->name, filenum, nexttok, 0, eqptr + 1);
	    }
	    else {
		Port(nexttok);
		hasports = 1;
	    }
	    SpiceTokNoNewline();
         }
	 SetClass((blackbox) ? CLASS_MODULE : CLASS_SUBCKT);

	 if (hasports == 0) {
	    // If the cell defines no ports, then create a proxy
	    Port((char *)NULL);
	 }

	 /* Copy all global nodes from parent into child cell */
	 for (sobj = parent; sobj != NULL; sobj = sobj->next) {
	    if (IsGlobal(sobj)) {
	       Global(sobj->name);
	    }
	 }

	 /* In the blackbox case, don't read the cell contents	*/
	 if (blackbox) goto skip_ends;
      }
      else {

skip_ends:
	 /* There was an error, so skip to the end of the	*/
	 /* subcircuit definition				*/

	 while (1) {
	    SpiceSkipNewLine();
	    SkipTok(NULL);
	    if (EndParseFile()) break;
	    if (matchnocase(nexttok, ".ENDS")) {
	       in_subckt = 0;
	       break;
	    }
	 }
      }
    }
    else if (matchnocase(nexttok, ".ENDS")) {

      CleanupSubcell();
      EndCell();

      if (in_subckt == (char)0) {
	  Fprintf(stderr, ".ENDS occurred outside of a subcircuit!\n");
          InputParseError(stderr);
      }
      in_subckt = (char)0;

      if (*CellStackPtr) PopStack(CellStackPtr);
      if (*CellStackPtr) ReopenCellDef((*CellStackPtr)->cellname, filenum);
      SkipNewLine(NULL);

      if (tpsave != NULL) {
	 struct nlist *tpplace;
	 char *savename;

	 /* Handle a placeholder from a verilog file that has been replaced
	  * by a netlist with pins in a different order.  The pins need to
	  * be matched, corrected in the original cell and all instances,
	  * and the new cell deleted.
	  */

	 Printf("Verilog placeholder %s replaced by SPICE definition\n",
		tpsave->name);
         tpplace = LookupCellFile("_PLACEHOLDER_", filenum);
	 /* MatchPins is part of netcmp and normally Circuit2 is the
	  * circuit being matched, so set Circuit2 to the original
	  * verilog black-box cell, and MatchPins() will force its
	  * pins to be rearranged to match the SPICE definition just
	  * read.
	  */
	 Circuit2 = tpsave;
	 MatchPins(tpplace, tpsave, 0);
	 savename = strsave(tpsave->name);
	 /* Now the original verilog black-box cell can be removed */
	 FreePorts(savename);
	 CellDelete(savename, filenum);
	 /* And _PLACEHOLDER_ is renamed to the original name of the cell. */
	 CellRehash("_PLACEHOLDER_", savename, filenum);
	 tpsave = NULL;
	 Circuit2 = NULL;
	 FREE(savename);
      }
    }
    else if (matchnocase(nexttok, ".MODEL")) {
      unsigned char class = CLASS_SUBCKT;
      struct nlist *ncell;

      /* A .MODEL statement can refine our knowledge of whether a Q or	*/
      /* M device is type "n" or "p", allowing us to properly translate	*/
      /* to other formats (e.g., .sim).	  If there are no .MODEL	*/
      /* statements, the "equate classes" command must be used.		*/

      SpiceTokNoNewline();
      if (nexttok == NULL) continue;	/* Ignore if no model name */
      snprintf(model, MAX_STR_LEN-1, "%s", nexttok);
      SpiceTokNoNewline();
      if (nexttok == NULL) goto baddevice;

      if (!strcasecmp(nexttok, "NMOS")) {
	 class = CLASS_NMOS;
      }
      else if (!strcasecmp(nexttok, "PMOS")) {
	 class = CLASS_PMOS;
      }
      else if (!strcasecmp(nexttok, "PNP")) {
	 class = CLASS_PNP;
      }
      else if (!strcasecmp(nexttok, "NPN")) {
	 class = CLASS_NPN;
      }
      else if (!strcasecmp(nexttok, "NPN")) {
	 class = CLASS_NPN;
      }
      else if (!strcasecmp(nexttok, "D")) {
	 class = CLASS_DIODE;
      }
      else if (!strcasecmp(nexttok, "R")) {
	 class = CLASS_RES;
      }
      else if (!strcasecmp(nexttok, "V")) {
	 class = CLASS_VSOURCE;
      }
      else if (!strcasecmp(nexttok, "I")) {
	 class = CLASS_ISOURCE;
      }
      else if (!strcasecmp(nexttok, "C")) {
	 class = CLASS_CAP;
      }
      else if (!strcasecmp(nexttok, "L")) {
	 class = CLASS_INDUCTOR;
      }

      /* Convert class of "model" to "class" */
      if (class != CLASS_SUBCKT) {
         ncell = LookupCellFile(model, filenum);
         if (ncell) ncell->class = class;
      }

      SpiceSkipNewLine();
    }

    // Handle some commonly-used cards

    else if (matchnocase(nexttok, ".GLOBAL")) {
      while (nexttok != NULL) {
	 int numnodes = 0;
         SpiceTokNoNewline();
	 if ((nexttok == NULL) || (nexttok[0] == '\0')) break;

	 // First handle backward references
	 if (CurrentCell != NULL)
 	    numnodes = ChangeScopeCurrent(nexttok, NODE, GLOBAL);	 

	 /* If there are no backward references, then treat it	*/
	 /* as a forward reference				*/

         if (numnodes == 0) {
	    // If there is no current cell, make one
	    if (!(*CellStackPtr)) {
	       CellDef(fname, filenum);
	       PushStack(fname, CellStackPtr);
	    }
	    Global(nexttok);
	 }
      }
      SpiceSkipNewLine();
    }
    else if (matchnocase(nexttok, ".INCLUDE")) {
      char *iname, *iptr, *quotptr, *pathend, *userpath = NULL;

      SpiceTokNoNewline();
      if (nexttok == NULL) continue;	/* Ignore if no filename */

      // Any file included in another SPICE file needs to be
      // interpreted relative to the path of the parent SPICE file,
      // unless it's an absolute pathname.

      pathend = strrchr(fname, '/');
      iptr = nexttok;
      while (*iptr == '\'' || *iptr == '\"' || *iptr == '`') iptr++;
      if ((pathend != NULL) && (*iptr != '/') && (*iptr != '~')) {
	 *pathend = '\0';
	 iname = (char *)MALLOC(strlen(fname) + strlen(iptr) + 2);
	 sprintf(iname, "%s/%s", fname, iptr);
	 *pathend = '/';
      }
#ifndef IBMPC
      else if ((*iptr == '~') && (*(iptr + 1) == '/')) {
	 /* For ~/<path>, substitute tilde from $HOME */
	 userpath = getenv("HOME");
	 iname = (char *)MALLOC(strlen(userpath) + strlen(iptr));
	 sprintf(iname, "%s%s", userpath, iptr + 1);
      }
      else if (*iptr == '~') {
	 /* For ~<user>/<path>, substitute tilde from getpwnam() */
	 struct passwd *passwd;
	 char *pathstart;
         pathstart = strchr(iptr, '/');
	 if (pathstart) *pathstart = '\0';
	 passwd = getpwnam(iptr + 1);
	 if (passwd != NULL) {
	    userpath = passwd->pw_dir;
	    if (pathstart) {
	       *pathstart = '/';
	       iname = (char *)MALLOC(strlen(userpath) + strlen(pathstart) + 1);
	       sprintf(iname, "%s%s", userpath, pathstart);
	    }
	    else {
	       /* Almost certainly an error, but make the substitution anyway */
	       iname = STRDUP(userpath);
	    }
	 }
	 else {
	    /* Probably an error, but copy the filename verbatim */
	    iname = STRDUP(iptr);
	 }
      }
#endif
      else
	 iname = STRDUP(iptr);

      // Eliminate any single or double quotes around the filename
      iptr = iname;
      quotptr = iptr;
      while (*quotptr != '\'' && *quotptr != '\"' && *quotptr != '`' &&
		*quotptr != '\0' && *quotptr != '\n') quotptr++;
      if (*quotptr == '\'' || *quotptr == '\"' || *quotptr == '`') *quotptr = '\0';
	
      IncludeSpice(iptr, filenum, CellStackPtr, blackbox);
      FREE(iname);
      SpiceSkipNewLine();
    }

    else if (matchnocase(nexttok, ".PARAM")) {

      // Pick up key:value pairs and store in current cell
      while (nexttok != NULL)
      {
	 /* Parse for parameters used in expressions.  Save	*/
	 /* parameters in the "spiceparams" hash table.		*/

	 SpiceTokNoNewline();
	 if ((nexttok == NULL) || (nexttok[0] == '\0')) break;
	 if ((eqptr = strchr(nexttok, '=')) != NULL)
	 {
	    struct property *kl = NULL;

	    *eqptr = '\0';
	    kl = NewProperty();
	    kl->key = strsave(nexttok);
	    kl->idx = 0;
	    kl->type = PROP_STRING;
	    kl->slop.dval = 0.0;
	    kl->pdefault.string = strsave(eqptr + 1);
	    HashPtrInstall(nexttok, kl, &spiceparams);
	 }
      }
    }

    /* Ignore anything in a .CONTROL ... .ENDC block */
    else if (matchnocase(nexttok, ".CONTROL")) {
	while (1) {
	    SpiceSkipNewLine();
	    SkipTok(NULL);
	    if (EndParseFile()) break;
	    if (matchnocase(nexttok, ".ENDC"))
	       break;
	}
    }

    // Blackbox (library) mode---parse only subcircuits and models;
    // ignore all components.

    else if (blackbox) {
      SpiceSkipNewLine();
    }

    else if (toupper(nexttok[0]) == 'Q') {
      char emitter[MAX_STR_LEN], base[MAX_STR_LEN], collector[MAX_STR_LEN];
      emitter[MAX_STR_LEN-1] = '\0';
      base[MAX_STR_LEN-1] = '\0';
      collector[MAX_STR_LEN-1] = '\0';

      if (!(*CellStackPtr)) {
	CellDef(fname, filenum);
	PushStack(fname, CellStackPtr);
      }
      strncpy(inst, nexttok + 1, MAX_STR_LEN-1); SpiceTokNoNewline(); 
      if (nexttok == NULL) goto baddevice;
      strncpy(collector, nexttok, MAX_STR_LEN-1); SpiceTokNoNewline();
      if (nexttok == NULL) goto baddevice;
      strncpy(base, nexttok, MAX_STR_LEN-1);   SpiceTokNoNewline();
      if (nexttok == NULL) goto baddevice;
      strncpy(emitter, nexttok, MAX_STR_LEN-1);  SpiceTokNoNewline();
      /* make sure all the nodes exist */
      if (LookupObject(collector, CurrentCell) == NULL) Node(collector);
      if (LookupObject(base, CurrentCell) == NULL) Node(base);
      if (LookupObject(emitter, CurrentCell) == NULL) Node(emitter);

      /* Read the device model */
      snprintf(model, MAX_STR_LEN-1, "%s", nexttok);

      while (nexttok != NULL)
      {
	 /* Parse for M and other parameters */

	 SpiceTokNoNewline();
	 if ((nexttok == NULL) || (nexttok[0] == '\0')) break;
	 if ((eqptr = strchr(nexttok, '=')) != NULL)
	 {
	    *eqptr = '\0';
	    AddProperty(&kvlist, nexttok, eqptr + 1);
	 }
      }

      if (LookupCellFile(model, filenum) == NULL) {
	 CellDef(model, filenum);
	 Port("collector");
	 Port("base");
	 Port("emitter");
	 PropertyInteger(model, filenum, "M", 0, 1);
	 SetClass(CLASS_BJT);
         EndCell();
	 ReopenCellDef((*CellStackPtr)->cellname, filenum);	/* Reopen */
      }
      else if (CountPorts(model, filenum) != 3) {
	 /* Modeled device:  Make sure it has the right number of ports */
	 Fprintf(stderr, "Device \"%s\" has wrong number of ports for a BJT.\n");
	 goto baddevice;
      }

      snprintf(instname, MAX_STR_LEN-1, "%s:%s", model, inst);
      Cell(instname, model, collector, base, emitter);
      pobj = LinkProperties(model, kvlist);
      ReduceExpressions(pobj, NULL, CurrentCell, TRUE);
      DeleteProperties(&kvlist);
    }
    else if (toupper(nexttok[0]) == 'M') {
      char drain[MAX_STR_LEN], gate[MAX_STR_LEN], source[MAX_STR_LEN], bulk[MAX_STR_LEN];
      drain[MAX_STR_LEN-1] = '\0';
      gate[MAX_STR_LEN-1] = '\0';
      source[MAX_STR_LEN-1] = '\0';
      bulk[MAX_STR_LEN-1] = '\0';

      if (!(*CellStackPtr)) {
	CellDef(fname, filenum);
	PushStack(fname, CellStackPtr);
      }
      strncpy(inst, nexttok + 1, MAX_STR_LEN-1); SpiceTokNoNewline(); 
      if (nexttok == NULL) goto baddevice;
      strncpy(drain, nexttok, MAX_STR_LEN-1);  SpiceTokNoNewline();
      if (nexttok == NULL) goto baddevice;
      strncpy(gate, nexttok, MAX_STR_LEN-1);   SpiceTokNoNewline();
      if (nexttok == NULL) goto baddevice;
      strncpy(source, nexttok, MAX_STR_LEN-1); SpiceTokNoNewline();
      if (nexttok == NULL) goto baddevice;
      /* make sure all the nodes exist */
      if (LookupObject(drain, CurrentCell) == NULL) Node(drain);
      if (LookupObject(gate, CurrentCell) == NULL) Node(gate);
      if (LookupObject(source, CurrentCell) == NULL) Node(source);

      /* handle the substrate node */
      strncpy(bulk, nexttok, MAX_STR_LEN-1); SpiceTokNoNewline();
      if (LookupObject(bulk, CurrentCell) == NULL) Node(bulk);

      /* Read the device model */
      snprintf(model, MAX_STR_LEN-1, "%s", nexttok);

      while (nexttok != NULL)
      {
	 /* Parse for parameters; treat "M" separately */

	 SpiceTokNoNewline();
	 if ((nexttok == NULL) || (nexttok[0] == '\0')) break;
	 if ((eqptr = strchr(nexttok, '=')) != NULL)
	 {
	    *eqptr = '\0';
	    AddProperty(&kvlist, nexttok, eqptr + 1);
	 }
      }

      /* Treat each different model name as a separate device class	*/
      /* The model name is prefixed with "M/" so that we know this is a	*/
      /* SPICE transistor.						*/

      if (LookupCellFile(model, filenum) == NULL) {
	 CellDef(model, filenum);
	 Port("drain");
	 Port("gate");
	 Port("source");
	 Port("bulk");
	 PropertyDouble(model, filenum, "L", 0.01, 0.0);
	 PropertyDouble(model, filenum, "W", 0.01, 0.0);
	 PropertyInteger(model, filenum, "M", 0, 1);
	 SetClass(CLASS_FET);
         EndCell();
	 ReopenCellDef((*CellStackPtr)->cellname, filenum);	/* Reopen */
      }
      else if (CountPorts(model, filenum) != 4) {
	 /* Modeled device:  Make sure it has the right number of ports */
	 Fprintf(stderr, "Device \"%s\" has wrong number of ports for a MOSFET.\n");
	 goto baddevice;
      }

      snprintf(instname, MAX_STR_LEN-1, "%s:%s", model, inst);
      Cell(instname, model, drain, gate, source, bulk);
      pobj = LinkProperties(model, kvlist);
      ReduceExpressions(pobj, NULL, CurrentCell, TRUE);
      DeleteProperties(&kvlist);
      SpiceSkipNewLine();
    }
    else if (toupper(nexttok[0]) == 'C') {	/* 2-port capacitors */
      int usemodel = 0;

      if (IgnoreRC) {
	 SpiceSkipNewLine();
      }
      else {
        char ctop[MAX_STR_LEN], cbot[MAX_STR_LEN];
        ctop[MAX_STR_LEN-1] = '\0';
        cbot[MAX_STR_LEN-1] = '\0';

        if (!(*CellStackPtr)) {
	  CellDef(fname, filenum);
	  PushStack(fname, CellStackPtr);
        }
        strncpy(inst, nexttok + 1, MAX_STR_LEN-1); SpiceTokNoNewline(); 
        if (nexttok == NULL) goto baddevice;
        strncpy(ctop, nexttok, MAX_STR_LEN-1); SpiceTokNoNewline();
        if (nexttok == NULL) goto baddevice;
        strncpy(cbot, nexttok, MAX_STR_LEN-1); SpiceTokNoNewline();

        /* make sure all the nodes exist */
        if (LookupObject(ctop, CurrentCell) == NULL) Node(ctop);
        if (LookupObject(cbot, CurrentCell) == NULL) Node(cbot);

	/* Get capacitor value (if present), save as property "value" */
	if (nexttok != NULL) {
	   if (StringIsValueOrExpression(nexttok)) {
	      AddProperty(&kvlist, "value", nexttok);
	      SpiceTokNoNewline();
	   }
	}

	/* Semiconductor (modeled) capacitor.  But first need to make	*/
	/* sure that this does not start the list of parameters.	*/

	model[0] = '\0';
	if ((nexttok != NULL) && ((eqptr = strchr(nexttok, '=')) == NULL))
	   snprintf(model, MAX_STR_LEN-1, "%s", nexttok);

	/* Any other device properties? */
        while (nexttok != NULL)
        {
	   SpiceTokNoNewline();
	   if ((nexttok == NULL) || (nexttok[0] == '\0')) break;
	   if ((eqptr = strchr(nexttok, '=')) != NULL) {
	      *eqptr = '\0';
	      AddProperty(&kvlist, nexttok, eqptr + 1);
	   }
	   else if (!strncmp(nexttok, "$[", 2)) {
	      // Support for CDL modeled capacitor format
	      snprintf(model, MAX_STR_LEN-1, "%s", nexttok + 2);
	      if ((eqptr = strchr(model, ']')) != NULL)
		 *eqptr = '\0';
	   }
	   else if (StringIsValueOrExpression(nexttok)) {
		// Suport for value passed to modeled capacitor
	        AddProperty(&kvlist, "value", nexttok);
	   }
	}

	if (model[0] == '\0')
	   strcpy(model, "c");		/* Use default capacitor model */
	else
	{
	   if (LookupCellFile(model, filenum) == NULL) {
	      CellDef(model, filenum);
	      Port("top");
	      Port("bottom");
	      PropertyValue(model, filenum, "value", 0.01, 0.0);
	      PropertyInteger(model, filenum, "M", 0, 1);
	      SetClass(CLASS_CAP);
              EndCell();
	      ReopenCellDef((*CellStackPtr)->cellname, filenum);	/* Reopen */
	   }
           else if (CountPorts(model, filenum) != 2) {
	      /* Modeled device:  Make sure it has the right number of ports */
	      Fprintf(stderr, "Device \"%s\" has wrong number of ports for a "
			"capacitor.\n");
	      goto baddevice;
           }
	   usemodel = 1;
	}

	snprintf(instname, MAX_STR_LEN-1, "%s:%s", model, inst);
	if (usemodel)
           Cell(instname, model, ctop, cbot);
	else
           Cap((*CellStackPtr)->cellname, instname, ctop, cbot);
	pobj = LinkProperties(model, kvlist);
	ReduceExpressions(pobj, NULL, CurrentCell, TRUE);
	DeleteProperties(&kvlist);
      }
    }
    else if (toupper(nexttok[0]) == 'R') {	/* 2-port resistors */
      int usemodel = 0;

      if (IgnoreRC) {
	 SpiceSkipNewLine();
      }
      else {
        char rtop[MAX_STR_LEN], rbot[MAX_STR_LEN];
	rtop[MAX_STR_LEN-1] = '\0';
	rbot[MAX_STR_LEN-1] = '\0';

        if (!(*CellStackPtr)) {
	  CellDef(fname, filenum);
	  PushStack(fname, CellStackPtr);
        }
        strncpy(inst, nexttok + 1, MAX_STR_LEN-1); SpiceTokNoNewline(); 
        if (nexttok == NULL) goto baddevice;
        strncpy(rtop, nexttok, MAX_STR_LEN-1);  SpiceTokNoNewline();
        if (nexttok == NULL) goto baddevice;
        strncpy(rbot, nexttok, MAX_STR_LEN-1);   SpiceTokNoNewline();
        /* make sure all the nodes exist */
        if (LookupObject(rtop, CurrentCell) == NULL) Node(rtop);
        if (LookupObject(rbot, CurrentCell) == NULL) Node(rbot);

	/* Get resistor value (if present); save as property "value" */

	if (nexttok != NULL) {
	   if (StringIsValueOrExpression(nexttok)) {
	      AddProperty(&kvlist, "value", nexttok);
	      SpiceTokNoNewline();
	   }
        }

	/* Semiconductor (modeled) resistor.  But first need to make	*/
	/* sure that this does not start the list of parameters.	*/

	model[0] = '\0';
	if ((nexttok != NULL) && ((eqptr = strchr(nexttok, '=')) == NULL))
	   snprintf(model, MAX_STR_LEN-1, "%s", nexttok);

	/* Any other device properties? */
        while (nexttok != NULL) {
	   SpiceTokNoNewline();
	   if ((nexttok == NULL) || (nexttok[0] == '\0')) break;
	   if ((eqptr = strchr(nexttok, '=')) != NULL) {
	      *eqptr = '\0';
	       AddProperty(&kvlist, nexttok, eqptr + 1);
	   }
	   else if (!strncmp(nexttok, "$[", 2)) {
	      // Support for CDL modeled resistor format
	      snprintf(model, MAX_STR_LEN-1, "%s", nexttok + 2);
	      if ((eqptr = strchr(model, ']')) != NULL)
		 *eqptr = '\0';
	   }
	   else if (StringIsValueOrExpression(nexttok)) {
		// Suport for value passed to modeled resistor
	        AddProperty(&kvlist, "value", nexttok);
	   }
	}

	if (model[0] != '\0')
	{
	   if (LookupCellFile(model, filenum) == NULL) {
	      CellDef(model, filenum);
	      Port("end_a");
	      Port("end_b");
	      PropertyValue(model, filenum, "value", 0.01, 0.0);
	      PropertyInteger(model, filenum, "M", 0, 1);
	      SetClass(CLASS_RES);
              EndCell();
	      ReopenCellDef((*CellStackPtr)->cellname, filenum);	/* Reopen */
	   }
           else if (CountPorts(model, filenum) != 2) {
	      /* Modeled device:  Make sure it has the right number of ports */
	      Fprintf(stderr, "Device \"%s\" has wrong number of ports for a "
			"resistor.\n", model);
	      goto baddevice;
           }
	   usemodel = 1;
	}
	else
	   strcpy(model, "r");		/* Use default resistor model */

	snprintf(instname, MAX_STR_LEN-1, "%s:%s", model, inst);
	if (usemodel)
	   Cell(instname, model, rtop, rbot);
	else
           Res((*CellStackPtr)->cellname, instname, rtop, rbot);
	pobj = LinkProperties(model, kvlist);
	ReduceExpressions(pobj, NULL, CurrentCell, TRUE);
	DeleteProperties(&kvlist);
      }
    }
    else if (toupper(nexttok[0]) == 'D') {	/* diode */
      char cathode[MAX_STR_LEN], anode[MAX_STR_LEN];
      cathode[MAX_STR_LEN-1] = '\0';
      anode[MAX_STR_LEN-1] = '\0';

      if (!(*CellStackPtr)) {
	CellDef(fname, filenum);
	PushStack(fname, CellStackPtr);
      }
      strncpy(inst, nexttok + 1, MAX_STR_LEN-1); SpiceTokNoNewline(); 
      if (nexttok == NULL) goto baddevice;
      strncpy(anode, nexttok, MAX_STR_LEN-1);   SpiceTokNoNewline();
      if (nexttok == NULL) goto baddevice;
      strncpy(cathode, nexttok, MAX_STR_LEN-1); SpiceTokNoNewline();
      /* make sure all the nodes exist */
      if (LookupObject(anode, CurrentCell) == NULL) Node(anode);
      if (LookupObject(cathode, CurrentCell) == NULL) Node(cathode);

      /* Read the device model */
      snprintf(model, MAX_STR_LEN-1, "%s", nexttok);

      while (nexttok != NULL)
      {
	 /* Parse for M and other parameters */

	 SpiceTokNoNewline();
	 if ((nexttok == NULL) || (nexttok[0] == '\0')) break;
	 if ((eqptr = strchr(nexttok, '=')) != NULL)
	 {
	    *eqptr = '\0';
	    AddProperty(&kvlist, nexttok, eqptr + 1);
	 }
      }

      if (LookupCellFile(model, filenum) == NULL) {
	 CellDef(model, filenum);
	 Port("anode");
	 Port("cathode");
         PropertyInteger(model, filenum, "M", 0, 1);
	 SetClass(CLASS_DIODE);
         EndCell();
	 ReopenCellDef((*CellStackPtr)->cellname, filenum);	/* Reopen */
      }
      else if (CountPorts(model, filenum) != 2) {
	 /* Modeled device:  Make sure it has the right number of ports */
	 Fprintf(stderr, "Device \"%s\" has wrong number of ports for a diode.\n");
	 goto baddevice;
      }
      snprintf(instname, MAX_STR_LEN-1, "%s:%s", model, inst);
      Cell(instname, model, anode, cathode);
      pobj = LinkProperties(model, kvlist);
      ReduceExpressions(pobj, NULL, CurrentCell, TRUE);
      DeleteProperties(&kvlist);
    }
    else if (toupper(nexttok[0]) == 'T') {	/* transmission line */
      int usemodel = 0;

      if (IgnoreRC) {
	 SpiceSkipNewLine();
      }
      else {
        char node1[MAX_STR_LEN], node2[MAX_STR_LEN], node3[MAX_STR_LEN], node4[MAX_STR_LEN];
	node1[MAX_STR_LEN-1] = '\0';
	node2[MAX_STR_LEN-1] = '\0';
	node3[MAX_STR_LEN-1] = '\0';
	node4[MAX_STR_LEN-1] = '\0';

        if (!(*CellStackPtr)) {
	  CellDef(fname, filenum);
	  PushStack(fname, CellStackPtr);
        }
        strncpy(inst, nexttok + 1, MAX_STR_LEN-1); SpiceTokNoNewline(); 
        if (nexttok == NULL) goto baddevice;
        strncpy(node1, nexttok, MAX_STR_LEN-1);  SpiceTokNoNewline();
        if (nexttok == NULL) goto baddevice;
        strncpy(node2, nexttok, MAX_STR_LEN-1);   SpiceTokNoNewline();
        if (nexttok == NULL) goto baddevice;
        strncpy(node3, nexttok, MAX_STR_LEN-1);   SpiceTokNoNewline();
        if (nexttok == NULL) goto baddevice;
        strncpy(node4, nexttok, MAX_STR_LEN-1);   SpiceTokNoNewline();
        /* make sure all the nodes exist */
        if (LookupObject(node1, CurrentCell) == NULL) Node(node1);
        if (LookupObject(node2, CurrentCell) == NULL) Node(node2);
        if (LookupObject(node3, CurrentCell) == NULL) Node(node3);
        if (LookupObject(node4, CurrentCell) == NULL) Node(node4);

	/* Lossy (modeled) transmission line.  But first need to make	*/
	/* sure that this does not start the list of parameters.	*/

	model[0] = '\0';
	if ((nexttok != NULL) && ((eqptr = strchr(nexttok, '=')) == NULL))
	   snprintf(model, MAX_STR_LEN-1, "%s", nexttok);

	/* Any other device properties? */
        while (nexttok != NULL) {
	   SpiceTokNoNewline();
	   if ((nexttok == NULL) || (nexttok[0] == '\0')) break;
	   if ((eqptr = strchr(nexttok, '=')) != NULL) {
	      *eqptr = '\0';
	      AddProperty(&kvlist, nexttok, eqptr + 1);
	   }
	}

	if (model[0] != '\0')
	{
	   if (LookupCellFile(model, filenum) == NULL) {
	      CellDef(model, filenum);
	      Port("node1");
	      Port("node2");
	      Port("node3");
	      Port("node4");
	      PropertyInteger(model, filenum, "M", 0, 1);
	      SetClass(CLASS_XLINE);
              EndCell();
	      ReopenCellDef((*CellStackPtr)->cellname, filenum);	/* Reopen */
	   }
           else if (CountPorts(model, filenum) != 4) {
	      /* Modeled device:  Make sure it has the right number of ports */
	      Fprintf(stderr, "Device \"%s\" has wrong number of ports for a "
			"transmission line.\n");
	      goto baddevice;
           }
	   usemodel = 1;
	}
	else
	   strcpy(model, "t");		/* Use default xline model */

	snprintf(instname, MAX_STR_LEN-1, "%s:%s", model, inst);

	if (usemodel)
	   Cell(instname, model, node1, node2, node3, node4);
	else
           XLine((*CellStackPtr)->cellname, instname, node1, node2,
			node3, node4);
	pobj = LinkProperties(model, kvlist);
	ReduceExpressions(pobj, NULL, CurrentCell, TRUE);
	DeleteProperties(&kvlist);
      }
    }
    else if (toupper(nexttok[0]) == 'L') {	/* inductor */
      char end_a[MAX_STR_LEN], end_b[MAX_STR_LEN];
      int usemodel = 0;
      end_a[MAX_STR_LEN-1] = '\0';
      end_b[MAX_STR_LEN-1] = '\0';

      if (!(*CellStackPtr)) {
	CellDef(fname, filenum);
	PushStack(fname, CellStackPtr);
      }
      strncpy(inst, nexttok + 1, MAX_STR_LEN-1); SpiceTokNoNewline(); 
      if (nexttok == NULL) goto baddevice;
      strncpy(end_a, nexttok, MAX_STR_LEN-1);   SpiceTokNoNewline();
      if (nexttok == NULL) goto baddevice;
      strncpy(end_b, nexttok, MAX_STR_LEN-1); SpiceTokNoNewline();
      /* make sure all the nodes exist */
      if (LookupObject(end_a, CurrentCell) == NULL) Node(end_a);
      if (LookupObject(end_b, CurrentCell) == NULL) Node(end_b);

      /* Get inductance value (if present); save as property "value" */

      if (nexttok != NULL) {
	  if (StringIsValueOrExpression(nexttok)) {
	      AddProperty(&kvlist, "value", nexttok);
	      SpiceTokNoNewline();
	  }
      }
	
      /* Semiconductor (modeled) inductor.  But first need to make	*/
      /* sure that this does not start the list of parameters.	*/

      model[0] = '\0';
      if ((nexttok != NULL) && ((eqptr = strchr(nexttok, '=')) == NULL))
	  snprintf(model, MAX_STR_LEN-1, "%s", nexttok);

      /* Any other device properties? */
      while (nexttok != NULL)
      {
	 /* Parse for M and other parameters */

	 SpiceTokNoNewline();
	 if ((nexttok == NULL) || (nexttok[0] == '\0')) break;
	 if ((eqptr = strchr(nexttok, '=')) != NULL)
	 {
	    *eqptr = '\0';
	    AddProperty(&kvlist, nexttok, eqptr + 1);
	 }
      }

      if (model[0] != '\0')
      {
	 if (LookupCellFile(model, filenum) == NULL) {
	    CellDef(model, filenum);
	    Port("end_a");
	    Port("end_b");
	    PropertyInteger(model, filenum, "M", 0, 1);
	    SetClass(CLASS_INDUCTOR);
            EndCell();
	    ReopenCellDef((*CellStackPtr)->cellname, filenum);	/* Reopen */
         }
         else if (CountPorts(model, filenum) != 2) {
	      /* Modeled device:  Make sure it has the right number of ports */
	      Fprintf(stderr, "Device \"%s\" has wrong number of ports for an "
			"inductor.\n");
	      goto baddevice;
         }
	 usemodel = 1;
      }
      else
	 strcpy(model, "l");		/* Use default inductor model */

      snprintf(instname, MAX_STR_LEN-1, "%s:%s", model, inst);
      if (usemodel)
	 Cell(instname, model, end_a, end_b);
      else
	 Inductor((*CellStackPtr)->cellname, instname, end_a, end_b);
      pobj = LinkProperties(model, kvlist);
      ReduceExpressions(pobj, NULL, CurrentCell, TRUE);
      DeleteProperties(&kvlist);
    }

    /* The following SPICE components are treated as	*/
    /* black-box subcircuits (class MODULE):  V, I, E	*/

    else if (toupper(nexttok[0]) == 'V') {	/* voltage source */
      char pos[MAX_STR_LEN], neg[MAX_STR_LEN];
      pos[MAX_STR_LEN-1] = '\0';
      neg[MAX_STR_LEN-1] = '\0';

      if (!(*CellStackPtr)) {
	CellDef(fname, filenum);
	PushStack(fname, CellStackPtr);
      }
      strncpy(inst, nexttok + 1, MAX_STR_LEN-1); SpiceTokNoNewline(); 
      if (nexttok == NULL) goto baddevice;
      strncpy(pos, nexttok, MAX_STR_LEN-1);   SpiceTokNoNewline();
      if (nexttok == NULL) goto baddevice;
      strncpy(neg, nexttok, MAX_STR_LEN-1); SpiceTokNoNewline();
      if (nexttok == NULL) goto baddevice;
      /* make sure all the nodes exist */
      if (LookupObject(pos, CurrentCell) == NULL) Node(pos);
      if (LookupObject(neg, CurrentCell) == NULL) Node(neg);

      /* Get voltage value (if present); save as property "value" */

      if (nexttok != NULL) {
	 if (matchnocase(nexttok, "DC")) {
	     SpiceTokNoNewline();
	 }
      }
      if (nexttok != NULL) {
	 if (StringIsValueOrExpression(nexttok)) {
	     AddProperty(&kvlist, "value", nexttok);
	 }
      }

      /* Any other device properties? */
      while (nexttok != NULL)
      {
	 SpiceTokNoNewline();
	 if ((nexttok == NULL) || (nexttok[0] == '\0')) break;
	 if ((eqptr = strchr(nexttok, '=')) != NULL)
	 {
	    *eqptr = '\0';
	    AddProperty(&kvlist, nexttok, eqptr + 1);
	 }
	 else if (StringIsValueOrExpression(nexttok)) {
	    AddProperty(&kvlist, "value", nexttok);
	    SpiceTokNoNewline();
	 }
      }
      strcpy(model, "vsrc");		/* Default voltage source */
      if (LookupCellFile(model, filenum) == NULL) {
	 CellDef(model, filenum);
	 Port("pos");
	 Port("neg");
	 PropertyInteger(model, filenum, "M", 0, 1);
	 SetClass(CLASS_VSOURCE);
         EndCell();
	 ReopenCellDef((*CellStackPtr)->cellname, filenum);	/* Reopen */
      }
      else if (CountPorts(model, filenum) != 2) {
	 /* Modeled device:  Make sure it has the right number of ports */
	 Fprintf(stderr, "Device \"%s\" has wrong number of ports for a "
			"voltage source.\n", inst);
	 goto baddevice;
      }
      Cell(instname, model, pos, neg);
      pobj = LinkProperties(model, kvlist);
      ReduceExpressions(pobj, NULL, CurrentCell, TRUE);
      DeleteProperties(&kvlist);
    }
    else if (toupper(nexttok[0]) == 'I') {	/* current source */
      char pos[MAX_STR_LEN], neg[MAX_STR_LEN];
      pos[MAX_STR_LEN-1] = '\0';
      neg[MAX_STR_LEN-1] = '\0';

      if (!(*CellStackPtr)) {
	CellDef(fname, filenum);
	PushStack(fname, CellStackPtr);
      }
      strncpy(inst, nexttok + 1, MAX_STR_LEN-1); SpiceTokNoNewline(); 
      if (nexttok == NULL) goto baddevice;
      strncpy(pos, nexttok, MAX_STR_LEN-1);   SpiceTokNoNewline();
      if (nexttok == NULL) goto baddevice;
      strncpy(neg, nexttok, MAX_STR_LEN-1); SpiceTokNoNewline();
      /* make sure all the nodes exist */
      if (LookupObject(pos, CurrentCell) == NULL) Node(pos);
      if (LookupObject(neg, CurrentCell) == NULL) Node(neg);

      /* Any device properties? */
      while (nexttok != NULL)
      {
	 SpiceTokNoNewline();
	 if ((nexttok == NULL) || (nexttok[0] == '\0')) break;
	 if ((eqptr = strchr(nexttok, '=')) != NULL)
	 {
	    *eqptr = '\0';
	    AddProperty(&kvlist, nexttok, eqptr + 1);
	 }
	 else if (StringIsValueOrExpression(nexttok)) {
	    AddProperty(&kvlist, "value", nexttok);
	    SpiceTokNoNewline();
	 }
      }
      strcpy(model, "isrc");		/* Default current source */
      if (LookupCellFile(model, filenum) == NULL) {
	 CellDef(model, filenum);
	 Port("pos");
	 Port("neg");
	 PropertyInteger(model, filenum, "M", 0, 1);
	 SetClass(CLASS_ISOURCE);
         EndCell();
	 ReopenCellDef((*CellStackPtr)->cellname, filenum);	/* Reopen */
      }
      else if (CountPorts(model, filenum) != 2) {
	 /* Modeled device:  Make sure it has the right number of ports */
	 Fprintf(stderr, "Device \"%s\" has wrong number of ports for a "
			"current source.\n");
	 goto baddevice;
      }
      Cell(instname, model, pos, neg);
      pobj = LinkProperties(model, kvlist);
      ReduceExpressions(pobj, NULL, CurrentCell, TRUE);
      DeleteProperties(&kvlist);
    }
    else if (toupper(nexttok[0]) == 'E') {	/* controlled voltage source */
      char pos[MAX_STR_LEN], neg[MAX_STR_LEN], ctrlp[MAX_STR_LEN], ctrln[MAX_STR_LEN];
      pos[MAX_STR_LEN-1] = '\0';
      neg[MAX_STR_LEN-1] = '\0';
      ctrlp[MAX_STR_LEN-1] = '\0';
      ctrln[MAX_STR_LEN-1] = '\0';

      if (!(*CellStackPtr)) {
	CellDef(fname, filenum);
	PushStack(fname, CellStackPtr);
      }
      strncpy(inst, nexttok + 1, MAX_STR_LEN-1); SpiceTokNoNewline(); 
      if (nexttok == NULL) goto baddevice;
      strncpy(pos, nexttok, MAX_STR_LEN-1);   SpiceTokNoNewline();
      if (nexttok == NULL) goto baddevice;
      strncpy(neg, nexttok, MAX_STR_LEN-1); SpiceTokNoNewline();
      if (nexttok == NULL) goto baddevice;
      strncpy(ctrlp, nexttok, MAX_STR_LEN-1); SpiceTokNoNewline();
      if (nexttok == NULL) goto baddevice;
      strncpy(ctrln, nexttok, MAX_STR_LEN-1); SpiceTokNoNewline();

      /* make sure all the nodes exist */
      if (LookupObject(pos, CurrentCell) == NULL) Node(pos);
      if (LookupObject(neg, CurrentCell) == NULL) Node(neg);
      if (LookupObject(ctrlp, CurrentCell) == NULL) Node(neg);
      if (LookupObject(ctrln, CurrentCell) == NULL) Node(neg);

      /* Any device properties? */
      while (nexttok != NULL)
      {
	 SpiceTokNoNewline();
	 if ((nexttok == NULL) || (nexttok[0] == '\0')) break;
	 if ((eqptr = strchr(nexttok, '=')) != NULL)
	 {
	    *eqptr = '\0';
	    AddProperty(&kvlist, nexttok, eqptr + 1);
	 }
	 else if (StringIsValueOrExpression(nexttok)) {
	    AddProperty(&kvlist, "value", nexttok);
	    SpiceTokNoNewline();
	 }
      }
      strcpy(model, "vcvs");		/* Default controlled voltage source */
      if (LookupCellFile(model, filenum) == NULL) {
	 CellDef(model, filenum);
	 Port("pos");
	 Port("neg");
	 Port("ctrlp");
	 Port("ctrln");
	 PropertyInteger(model, filenum, "M", 0, 1);
	 SetClass(CLASS_MODULE);
         EndCell();
	 ReopenCellDef((*CellStackPtr)->cellname, filenum);	/* Reopen */
      }
      else if (CountPorts(model, filenum) != 4) {
	 /* Modeled device:  Make sure it has the right number of ports */
	 Fprintf(stderr, "Device \"%s\" has wrong number of ports for a "
			"controlled voltage source.\n");
	 goto baddevice;
      }
      Cell(instname, model, pos, neg, ctrlp, ctrln);
      pobj = LinkProperties(model, kvlist);
      ReduceExpressions(pobj, NULL, CurrentCell, TRUE);
      DeleteProperties(&kvlist);
    }

    else if (toupper(nexttok[0]) == 'X') {	/* subcircuit instances */
      char instancename[MAX_STR_LEN], subcktname[MAX_STR_LEN];
      int itype, in_props;

      instancename[MAX_STR_LEN-1] = '\0';
      subcktname[MAX_STR_LEN-1] = '\0';

      struct portelement {
	char *name;
	struct portelement *next;
      };

      struct portelement *head, *tail, *scan, *scannext;
      struct objlist *obptr;

      snprintf(instancename, MAX_STR_LEN-1, "%s", nexttok + 1);
      strncpy(instancename, nexttok + 1, MAX_STR_LEN-1);
      if (!(*CellStackPtr)) {
	CellDef(fname, filenum);
	PushStack(fname, CellStackPtr);
      }
      
      head = NULL;
      tail = NULL;
      SpiceTokNoNewline();
      in_props = FALSE;
      while (nexttok != NULL) {
	/* must still be a node or a parameter */
	struct portelement *new_port;

	// CDL format compatibility:  Ignore "/" before the subcircuit name
	if (matchnocase(nexttok, "/")) {
           SpiceTokNoNewline();
	   continue;
	}
	// And (why do they have to keep messing with a perfectly good syntax?!)
	// prepended to the name without a space:
	else if (*nexttok == '/') nexttok++;

	// Ignore token called "PARAMS:"
	if (!strcasecmp(nexttok, "PARAMS:")) {
           SpiceTokNoNewline();
	   continue;
	}

	// We need to look for parameters of the type "name=value" BUT
	// we also need to make sure that what we think is a parameter
	// is actually a circuit name with an equals sign character in it.

	if (((eqptr = strchr(nexttok, '=')) != NULL) &&
	    	((tp = LookupCellFile(nexttok, filenum)) == NULL))
	{
	    in_props = TRUE;
	    *eqptr = '\0';
	    AddProperty(&kvlist, nexttok, eqptr + 1);
	}
	else if (in_props == FALSE)
	{
	    new_port = (struct portelement *)CALLOC(1, sizeof(struct portelement));
	    new_port->name = strsave(nexttok);
	    if (head == NULL) head = new_port;
	    else tail->next = new_port;
	    new_port->next = NULL;
	    tail = new_port;
	}
	else
	{
	    Fprintf(stderr, "Token \"%s\" is not a parameter!\n", nexttok);
            InputParseError(stderr);
	}
	SpiceTokNoNewline();
      }

      /* find the last element of the list, which is not a port,
         but the class type */
      scan = head;
      while (scan != NULL && scan->next != tail && scan->next != NULL)
	 scan = scan->next;
      tail = scan;
      if (scan == NULL) goto baddevice;
      if (scan->next != NULL) scan = scan->next;
      tail->next = NULL;

      /* Check for ignored class */

      if ((itype = IsIgnored(subcktname, filenum)) == IGNORE_CLASS) {
          Printf("Class '%s' instanced in input but is being ignored.\n", model);
          return;
      }

      /* Check for shorted pins */

      if ((itype == IGNORE_SHORTED) && (head != NULL)) {
         unsigned char shorted = (unsigned char)1;
         struct portelement *p;
         for (p = head->next; p; p = p->next) {
            if (strcasecmp(head->name, p->name))
               shorted = (unsigned char)0;
               break;
         }
         if (shorted == (unsigned char)1) {
            Printf("Instance of '%s' is shorted, ignoring.\n", subcktname);
	    while (head) {
	       p = head->next;
	       FREE(head);
	       head = p;
            }
            return;
         }
      }

      /* Create cell name and revise instance name based on the cell name */
      /* For clarity, if "instancename" does not contain the cellname,	  */
      /* then prepend the cellname to the instance name.  HOWEVER, if any */
      /* netlist is using instancename/portname to name nets, then we	  */
      /* will have duplicate node names with conflicting records.  So at  */
      /* very least prepend an "/" to it. . .				  */

      /* NOTE:  Previously an 'X' was prepended to the name, but this	  */
      /* caused serious and common errors where, for example, the circuit */
      /* defined cells NOR and XNOR, causing confusion between node	  */
      /* names.								  */

      if (strncmp(instancename, scan->name, strlen(scan->name))) {
         snprintf(subcktname, MAX_STR_LEN-1, "%s:%s", scan->name, instancename);
         strcpy(instancename, subcktname);
      }
      else {
         snprintf(subcktname, MAX_STR_LEN-1, "/%s", instancename);
         strcpy(instancename, subcktname);
      }
      snprintf(subcktname, MAX_STR_LEN-1, "%s", scan->name);

      if (scan == head) {
	 head = NULL;
	 Fprintf(stderr, "Warning:  Cell %s has no pins\n", scan->name);
      }
      FREE (scan->name);
      FREE (scan);

      /* Check that the subcell exists.  If not, print a warning and	*/
      /* generate an empty subcircuit entry matching the call.		*/

      tp = LookupCellFile(subcktname, filenum);
      if (tp == NULL) {
	 char defport[8];
	 int i;

	 Fprintf(stdout, "Call to undefined subcircuit %s\n"
		"Creating placeholder cell definition.\n", subcktname);
	 CellDef(subcktname, filenum);
	 CurrentCell->flags |= CELL_PLACEHOLDER;
         for (scan = head, i = 1; scan != NULL; scan = scan->next, i++) {
	    sprintf(defport, "%d", i);	
	    Port(defport);
	 }
	 if (head == NULL) {
	    Port((char *)NULL);	// Must have something for pin 1
	 }
	 PropertyInteger(subcktname, filenum, "M", 0, 1);
	 SetClass(CLASS_MODULE);
         EndCell();
	 ReopenCellDef((*CellStackPtr)->cellname, filenum);		/* Reopen */
	 update = 1;
      }
      else if (tp->flags & CELL_VERILOG) {
	 if (tp->flags & CELL_PLACEHOLDER) {
	    /* Flag this as an error.  To do:  Rearrange the verilog instance pins to	*/
	    /* match the SPICE subcircuit pin order.					*/
	    Fprintf(stderr, "Error:  SPICE subcircuit %s should be read before verilog "
			"module using it, or pins may not match!\n", subcktname);
	 }
	 else {
	    Fprintf(stderr, "Error:  SPICE subcircuit %s redefines a verilog module!\n",
			subcktname);
	 }
      }

      /* nexttok is now NULL, scan->name points to class */

      Instance(subcktname, instancename);
      pobj = LinkProperties(subcktname, kvlist);
      ReduceExpressions(pobj, NULL, CurrentCell, TRUE);

      /* (Diagnostic) */
      /* Fprintf(stderr, "instancing subcell: %s (%s):", subcktname, instancename); */
      /*
         for (scan = head; scan != NULL; scan = scan->next)
	    Fprintf(stderr," %s", scan->name);
         Fprintf(stderr,"\n");
      */
      
      obptr = LookupInstance(instancename, CurrentCell);
      if (obptr != NULL) {
         scan = head;
	 if (scan != NULL)
         do {
	    if (LookupObject(scan->name, CurrentCell) == NULL) Node(scan->name);
	    join(scan->name, obptr->name);
	    obptr = obptr->next;
	    scan = scan->next;
         } while (obptr != NULL && obptr->type > FIRSTPIN && scan != NULL);

         if ((obptr == NULL && scan != NULL) ||
		(obptr != NULL && scan == NULL && obptr->type > FIRSTPIN)) {
	     if (warnings <= 100) {
	        Fprintf(stderr,"Parameter list mismatch in %s: ", instancename);

	        if (obptr == NULL)
		   Fprintf(stderr, "Too many parameters in call!\n");
	        else if (scan == NULL)
		   Fprintf(stderr, "Not enough parameters in call!\n");
	        InputParseError(stderr);
	        if (warnings == 100)
	           Fprintf(stderr, "Too many warnings. . . will not report any more.\n");
             }
	     warnings++;
	  }
      }
      DeleteProperties(&kvlist);

      /* free up the allocated list */
      scan = head;
      while (scan != NULL) {
	scannext = scan->next;
	FREE(scan->name);
	FREE(scan);
	scan = scannext;
      }
    }
    else if (matchnocase(nexttok, ".END")) {
      /* Well, don't take *my* word for it.  But we won't flag a warning. */
    }
    else {
       int ntotal;
       char *sstr;
       ntotal = 0;
       for (sstr = nexttok; *sstr != '\0'; sstr++) if (!isascii(*sstr)) ntotal++;
       if ((int)(sstr - nexttok) < (ntotal << 2)) {
           Fprintf(stderr, "Input file \"%s\" appears to be binary"
      			". . . bailing out\n", fname);
           while (*CellStackPtr) PopStack(CellStackPtr);
           return;
       }

      if (warnings <= 100) {
	 Fprintf(stderr, "Ignoring line starting with token: %s\n", nexttok);
	 InputParseError(stderr);
	 if (warnings == 100)
	    Fprintf(stderr, "Too many warnings. . . will not report any more.\n");
      }
      warnings++;
      SpiceSkipNewLine();
    }
    continue;

baddevice:
    Fprintf(stderr, "Badly formed line in input.\n");
  }

  /* Watch for bad ending syntax */

  if (in_subckt == (char)1) {
     Fprintf(stderr, "Missing .ENDS statement on subcircuit.\n");
     InputParseError(stderr);
  }

  if (*(CellStackPtr)) {
     CleanupSubcell();
     EndCell();
     if (*CellStackPtr) PopStack(CellStackPtr);
     if (*CellStackPtr) ReopenCellDef((*CellStackPtr)->cellname, filenum);
  }

  if (update != 0) RecurseCellFileHashTable(renamepins, filenum);

  if (warnings)
     Fprintf(stderr, "File %s read with %d warning%s.\n", fname,
		warnings, (warnings == 1) ? "" : "s");
}

/*----------------------------------------------*/
/* Top-level SPICE file read routine		*/
/*----------------------------------------------*/

char *ReadSpiceTop(char *fname, int *fnum, int blackbox)
{
  struct cellstack *CellStack = NULL;
  struct nlist *tp;
  int filenum;

  // Make sure CurrentCell is clear
  CurrentCell = NULL;

  if ((filenum = OpenParseFile(fname, *fnum)) < 0) {

    if (strrchr(fname, '.') == NULL) {
      char name[1024];
      SetExtension(name, fname, SPICE_EXTENSION);
      if ((filenum = OpenParseFile(name, *fnum)) < 0) {
        Fprintf(stderr, "Error in SPICE file read: No file %s\n", name);
        *fnum = filenum;
        return NULL;
      }
    }    
    else {
      Fprintf(stderr, "Error in SPICE file read: No file %s\n", fname);
      *fnum = filenum;
      return NULL;
    }
  }

  /* Make sure all SPICE file reading is case insensitive   */
  /* BUT if a verilog file was read before it, then it will */
  /* be forced to be case sensitive, caveat end-user.	    */

  if (matchfunc == match) {
      Printf("Warning:  A case-sensitive file has been read and so the "
                "SPICE netlist must be treated case-sensitive to match.\n");
  }
  else {
      matchfunc = matchnocase;
      matchintfunc = matchfilenocase;
      hashfunc = hashnocase;
  }

  InitializeHashTable(&spiceparams, OBJHASHSIZE);

  /* All spice files should start with a comment line,	*/
  /* but we won't depend upon it.  Any comment line	*/
  /* will be handled by the main SPICE file processing.	*/

  ReadSpiceFile(fname, filenum, &CellStack, blackbox);
  CloseParseFile();

  // Cleanup
  while (CellStack != NULL) PopStack(&CellStack);

  RecurseHashTable(&spiceparams, freeprop);
  HashKill(&spiceparams);

  // Important:  If the file is a library, containing subcircuit
  // definitions but no components, then it needs to be registered
  // as an empty cell.  Otherwise, the filename is lost and cells
  // cannot be matched to the file!

  if (LookupCellFile(fname, filenum) == NULL) CellDef(fname, filenum);

  tp = LookupCellFile(fname, filenum);
  if (tp) tp->flags |= CELL_TOP;

  *fnum = filenum;
  return fname;
}

/*--------------------------------------*/
/* Wrappers for ReadSpiceTop() 		*/
/*--------------------------------------*/

char *ReadSpice(char *fname, int *fnum)
{
   return ReadSpiceTop(fname, fnum, 0);
}

/*--------------------------------------*/

char *ReadSpiceLib(char *fname, int *fnum)
{
   return ReadSpiceTop(fname, fnum, 1);
}

/*--------------------------------------*/
/* SPICE file include routine		*/
/*--------------------------------------*/

void IncludeSpice(char *fname, int parent, struct cellstack **CellStackPtr,
		int blackbox)
{
  int filenum = -1;
  char name[MAX_STR_LEN];

  /* If fname does not begin with "/", then assume that it is	*/
  /* in the same relative path as its parent.			*/
  
  if (fname[0] != '/') {
     char *ppath;
     if (*CellStackPtr && ((*CellStackPtr)->cellname != NULL)) {
	strcpy(name, (*CellStackPtr)->cellname);
	ppath = strrchr(name, '/');
	if (ppath != NULL)
           strcpy(ppath + 1, fname);
	else
           strcpy(name, fname);
        filenum = OpenParseFile(name, parent);
     }
  }

  /* If we failed the path relative to the parent, then try the	*/
  /* filename alone (relative to the path where netgen was	*/
  /* executed).							*/

  if (filenum < 0) {
     if ((filenum = OpenParseFile(fname, parent)) < 0) {

	/* If that fails, see if a standard SPICE extension	*/
	/* helps, if the file didn't have an extension.  But	*/
	/* really, we're getting desperate at this point.	*/

	if (strrchr(fname, '.') == NULL) {
           SetExtension(name, fname, SPICE_EXTENSION);
           filenum = OpenParseFile(name, parent);
	   if (filenum < 0) {
             Fprintf(stderr, "Error in SPICE file include: No file %s\n", name);
             return;
	   }
        }    
        else {
          Fprintf(stderr, "Error in SPICE file include: No file %s\n", fname);
          return;
	}
     }
  }
  ReadSpiceFile(fname, parent, CellStackPtr, blackbox);
  CloseParseFile();
}

/*--------------------------------------*/
/*--------------------------------------*/

void EsacapSubCell(struct nlist *tp, int IsSubCell)
{
  struct objlist *ob;
  int node, maxnode;

  /* check to see that all children have been dumped */
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    if (ob->type == FIRSTPIN) {
      struct nlist *tp2;

      tp2 = LookupCellFile(ob->model.class, tp->file);
      if ((tp2 != NULL) && !(tp2->dumped) && (tp2->class == CLASS_SUBCKT)) 
	EsacapSubCell(tp2, 1);
    }
  }

  /* print preface, if it is a subcell */
  if (IsSubCell) {
    FlushString("# %s doesn't know how to generate ESACAP subcells\n");
    FlushString("# Look in spice.c \n\n");
    FlushString(".SUBCKT %s ",tp->name);
    for (ob = tp->cell; ob != NULL; ob = ob->next) 
      if (IsPortInPortlist(ob, tp)) FlushString("%d ", ob->node);
    FlushString("# End of bogus ESACAP subcell\n");
    FlushString("\n");
  }

  /* print names of all nodes, prefixed by comment character */
  maxnode = 0;
  for (ob = tp->cell; ob != NULL; ob = ob->next)
    if (ob->node > maxnode) maxnode = ob->node;

/* was: for (node = 0; node <= maxnode; node++) */
  for (node = 1; node <= maxnode; node++) 
    FlushString("# %3d = %s\n", node, NodeName(tp, node));


  /* traverse list of objects */
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    if (ob->type == FIRSTPIN) {
      int drain_node, gate_node, source_node;
      /* print out element, but special-case transistors */
      if (match (ob->model.class, "n") || matchnocase(ob->model.class, "p")) {
	FlushString("X%s ",ob->instance.name);
	/* note: this code is dependent on the order defined in Initialize()*/
	gate_node = ob->node;
	ob = ob->next;
	drain_node = ob->node;
	ob = ob->next;
	source_node = ob->node;
	FlushString("(%d %d %d ",drain_node, gate_node, source_node);
	/* write fake substrate connections: NSUB and PSUB */
	/* write fake transistor sizes: NL, NW, PL and PW */
	/* write fake transistor classes: NCHANNEL and PCHANNEL  */
	if (matchnocase(ob->model.class, "n")) 
	  FlushString("NSUB)=SMOS(TYPE=NCHANNEL,W=NW,L=NL);\n");
	else FlushString("PSUB)=SMOS(TYPE=PCHANNEL,W=PW,L=PL);\n");
      }
      else {
	/* it must be a subckt */
	FlushString("### BOGUS SUBCKT: X%s %d ", ob->instance.name, ob->node);
	while (ob->next != NULL && ob->next->type > FIRSTPIN) {
	  ob = ob->next;
	  FlushString("%d ",ob->node);
	}
	FlushString("X%s\n", ob->model.class);
      }
    }
  }
	
  if (IsSubCell) FlushString(".ENDS\n");
  tp->dumped = 1;
}

/*--------------------------------------*/
/*--------------------------------------*/

void EsacapCell(char *name, char *filename)
{
  struct nlist *tp;
  char FileName[500];

  tp = LookupCellFile(name, -1);
  if (tp == NULL) {
    Printf ("No cell '%s' found.\n", name);
    return;
  }

  if (filename == NULL || strlen(filename) == 0) 
    SetExtension(FileName, name, ESACAP_EXTENSION);
  else 
    SetExtension(FileName, filename, ESACAP_EXTENSION);

  if (!OpenFile(FileName, 80)) {
    perror("ext(): Unable to open output file.");
    return;
  }
  ClearDumpedList();
  /* all Esacap decks begin with the following comment line */
  FlushString("# ESACAP deck for cell %s written by Netgen %s.%s\n\n", 
	      name, NETGEN_VERSION, NETGEN_REVISION);
  EsacapSubCell(tp, 0);
  FlushString("# end of ESACAP deck written by Netgen %s.%s\n\n",
		NETGEN_VERSION, NETGEN_REVISION);
  CloseFile(FileName);
}
