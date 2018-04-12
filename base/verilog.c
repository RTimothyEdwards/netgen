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

/* verilog.c -- Input for Verilog format (structural verilog only) */

/* The verilog input is limited to "structural verilog", that is,	*/
/* verilog code that only defines inputs, outputs, local nodes (via the	*/
/* "wire" statement), and instanced modules.  All modules are read down	*/
/* to the point where either a module (1) does not conform to the	*/
/* requirements above, or (2) has no module definition, in which case	*/
/* it is treated as a "black box" subcircuit and therefore becomes a	*/
/* low-level device.  Because in verilog syntax all instances of a	*/
/* module repeat both the module pin names and the local connections,	*/
/* placeholders can be built without the need to redefine pins later,	*/
/* as must be done for formats like SPICE that don't declare pin names	*/
/* in an instance call.							*/

/* Note that use of 1'b0 or 1'b1 and similar variants is prohibited;	*/
/* the structural netlist should either declare VSS and/or VDD as	*/
/* inputs, or use tie-high and tie-low standard cells.			*/

/* Most verilog syntax has been captured below.  Still to do:  Handle	*/
/* vectors that are created on the fly using {...} notation, including	*/
/* the {n{...}} concatenation method.					*/

#include "config.h"

#include <stdio.h>
#include <stdlib.h>  /* for calloc(), free(), getenv() */
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

#define VLOG_DELIMITERS " \t\r\n,;"
#define VLOG_DELIMITERS2 " \t\r\n,()"

// Global storage for verilog parameters
struct hashdict verilogparams;

// Global storage for wire buses
struct hashdict buses;

struct bus {
    int start;
    int end;
};

// Free a bus structure in the hash table during cleanup

int freebus (struct hashlist *p)
{
    struct bus *wb;

    wb = (struct bus *)(p->ptr);
    FREE(wb);
    return 1;
}

// Create a new bus structure

struct bus *NewBus()
{
    struct bus *wb;

    wb = (struct bus *)CALLOC(1, sizeof(struct bus));
    if (wb == NULL) Fprintf(stderr, "NewBus: Core allocation error\n");
    return (wb);
}

// Get bus indexes from the notation name[a:b].  If there is only "name"
// then look up the name in the bus hash list and return the index bounds.
// Return 0 on success, 1 on syntax error, and -1 if signal is not a bus.

int GetBus(char *astr, struct bus *wb)
{
    char *colonptr, *brackstart, *brackend;
    int result, start, end;

    if (wb == NULL) return;
    else {
        wb->start = -1;
        wb->end = -1;
    }
    
    brackstart = strchr(astr, '[');
    if (brackstart != NULL) {
	brackend = strchr(astr, ']');
	if (brackend == NULL) {
	    Printf("Badly formed array notation \"%s\"\n", astr);
	    return 1;
	}
	*brackend = '\0';
	colonptr = strchr(astr, ':');
	if (colonptr) *colonptr = '\0';
	result = sscanf(brackstart + 1, "%d", &start);
	if (colonptr) *colonptr = ':';
	if (result != 1) {
	    Printf("Badly formed array notation \"%s\"\n", astr);
	    *brackend = ']';
	    return 1;
	}
	if (colonptr)
	    result = sscanf(colonptr + 1, "%d", &end);
        else {
	    result = 1;
	    end = start;	// Single bit
        }
	*brackend = ']';
	if (result != 1) {
	    Printf("Badly formed array notation \"%s\"\n", astr);
	    return 1;
	}
	wb->start = start;
	wb->end = end;
    }
    else {
	struct bus *hbus;
	hbus = (struct bus *)HashLookup(astr, &buses);
	if (hbus != NULL) {
	    wb->start = hbus->start;
	    wb->end = hbus->end;
	}
	else
	    return -1;
    }
    return 0;
}

// Output a Verilog Module.  Note that since Verilog does not describe
// low-level devices like transistors, capacitors, etc., then this
// format is limited to black-box subcircuits.  Cells containing any
// such low-level devices are ignored.

void VerilogModule(struct nlist *tp)
{
  struct objlist *ob, *mob;
  int node, maxnode;
  char *model;
  struct tokstack *stackptr;

  /* 1st pass:  traverse list of objects for low-level device checks */

  for (ob = tp->cell; ob != NULL; ob = ob->next) {
     if (ob->type == FIRSTPIN) {
	struct nlist *tp2;

	tp2 = LookupCellFile(ob->model.class, tp->file);

	/* Check the class.  Low-level devices cause the	*/
	/* routine to return without generating output.		*/

	switch (tp2->class) {
	   case CLASS_NMOS4: case CLASS_PMOS4: case CLASS_FET4:
	   case CLASS_NMOS: case CLASS_PMOS: case CLASS_FET3:
	   case CLASS_FET: case CLASS_ECAP:
	   case CLASS_NPN: case CLASS_PNP: case CLASS_BJT:
	   case CLASS_RES: case CLASS_RES3:
	   case CLASS_DIODE: case CLASS_INDUCTOR:
	   case CLASS_CAP: case CLASS_CAP3:
	   case CLASS_XLINE:
	      return;
	   case CLASS_SUBCKT: case CLASS_MODULE:
	      break;
	   default:
	      Printf ("Bad device class \"%s\" found.\n", tp2->class);
	      break;		/* ignore it. . . */
	}
     }
  }

  /* Check to see that all children have been dumped first */

  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    if (ob->type == FIRSTPIN) {
      struct nlist *tp2;

      tp2 = LookupCellFile(ob->model.class, tp->file);
      if ((tp2 != NULL) && !(tp2->dumped) && (tp2->class == CLASS_SUBCKT)) 
	VerilogModule(tp2);
    }
  }

  /* Print module pin list */

  FlushString("module %s (\n",tp->name);
  for (ob = tp->cell; ob != NULL; ob = ob->next) 
      if (IsPortInPortlist(ob, tp)) FlushString("input %s,\n", ob->name);
  FlushString(");\n");

  /* Print names of all nodes as 'wire' statements */

  maxnode = 0;
  for (ob = tp->cell; ob != NULL; ob = ob->next)
    if (ob->node > maxnode) maxnode = ob->node;

  /* was:  for (node = 0; node <= maxnode; node++)  */
  for (node = 1; node <= maxnode; node++) 
    FlushString("   wire %s;\n", NodeName(tp, node));

  /* 2nd pass:  traverse list of objects for output */

  for (ob = tp->cell; ob != NULL; ob = ob->next) {
     if (ob->type == FIRSTPIN) {
        int drain_node, gate_node, source_node;
	struct nlist *tp2;

	tp2 = LookupCellFile(ob->model.class, tp->file);
	model = tp2->name;

	/* Check the class.  Low-level devices cause the routine to	*/
	/* return value 1 (don't output).				*/

	switch (tp2->class) {
	   case CLASS_SUBCKT: case CLASS_MODULE:
	      break;
	   default:
	      Printf ("Bad device class found.\n");
	      continue;		/* ignore it. . . */
	}
	
        FlushString("%s %s (\n", model, ob->instance.name);

        /* Print out nodes.  */

	mob = tp2->cell;
	while (ob) {
	   if (ob->type >= FIRSTPIN)
              FlushString(".%s(%s),\n", mob->name, ob->name);
           ob = ob->next;
           mob = mob->next;
	   if (ob->next && ob->next->type <= FIRSTPIN) break;
	}
        FlushString(");\n", model, ob->instance.name);
     }
  }
	
  FlushString("endmodule\n");
  tp->dumped = 1;
}

/* Write a Verilog module (top-level routine) */

void VerilogTop(char *name, int fnum, char *filename)
{
  struct nlist *tp;
  char FileName[500];

  tp = LookupCellFile(name, fnum);

  if (tp == NULL) {
    Printf ("No cell '%s' found.\n", name);
    return;
  }

  if (filename == NULL || strlen(filename) == 0) 
    SetExtension(FileName, name, VERILOG_EXTENSION);
  else 
    SetExtension(FileName, filename, VERILOG_EXTENSION);

  if (!OpenFile(FileName, 80)) {
    perror("write verilog: Unable to open output file.");
    return;
  }
  ClearDumpedList();
  /* Start with general information in comment lines at the top */
  FlushString("/*\n");
  FlushString(" * Verilog structural netlist for cell %s\n", name);
  FlushString(" * Written by Netgen %s.%s\n\n", NETGEN_VERSION, NETGEN_REVISION);
  FlushString(" */\n");
  VerilogModule(tp);
  CloseFile(FileName);
}

/* If any pins are marked unconnected, see if there are	*/
/* other pins of the same name that have connections.	*/

void CleanupModule() {
   int maxnode = 0;
   int has_submodules = FALSE;
   struct objlist *sobj, *nobj, *lobj, *pobj;

   if (CurrentCell == NULL) return;

   for (sobj = CurrentCell->cell; sobj; sobj = sobj->next)
      if (sobj->node > maxnode)
	 maxnode = sobj->node + 1;

   lobj = NULL;
   for (sobj = CurrentCell->cell; sobj != NULL;) {
      nobj = sobj->next;
      if (sobj->type == FIRSTPIN)
	 has_submodules = TRUE;
      if (sobj->node < 0) {
	 if (IsPort(sobj) && sobj->model.port == PROXY)
	    sobj->node = maxnode++;
	 else if (IsPort(sobj)) {
	    for (pobj = CurrentCell->cell; pobj && (pobj->type == PORT);
			pobj = pobj->next) {
	       if (pobj == sobj) continue;
	       if (match(pobj->name, sobj->name) && pobj->node >= 0) {
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
   if (has_submodules == FALSE) SetClass(CLASS_MODULE);

   RecurseHashTable(&verilogparams, freebus);
   HashKill(&buses);
}

/*------------------------------------------------------*/
/* Structure for stacking nested module definitions	*/
/*------------------------------------------------------*/

struct cellstack {
   char *cellname;
   struct cellstack *next;
};

/* Forward declarations */
extern void IncludeVerilog(char *, int, struct cellstack **, int);

/* External declarations (from spice.c) */
extern void PushStack(char *cellname, struct cellstack **top);
extern void PopStack(struct cellstack **top);

/*------------------------------------------------------*/
/* Read a verilog structural netlist			*/
/*------------------------------------------------------*/

void ReadVerilogFile(char *fname, int filenum, struct cellstack **CellStackPtr,
		int blackbox)
{
  int cdnum = 1, rdnum = 1, i;
  int warnings = 0, hasports, inlined_decls = 0, localcount = 1;
  char devtype, in_module, in_comment, in_param;
  char *eqptr, *parptr, *matchptr;
  struct keyvalue *kvlist = NULL;
  char inst[256], model[256], instname[256];
  struct nlist *tp;
  struct objlist *parent, *sobj, *nobj, *lobj, *pobj;

  inst[255] = '\0';
  model[255] = '\0';
  instname[255] = '\0';
  in_module = (char)0;
  in_comment = (char)0;
  in_param = (char)0;
  
  while (!EndParseFile()) {

    SkipTok(VLOG_DELIMITERS); /* get the next token */
    if ((EndParseFile()) && (nexttok == NULL)) break;

    /* Handle comment blocks */
    if (nexttok[0] == '/' && nexttok[1] == '*') {
        in_comment = (char)1;
    }
    else if (nexttok[0] == '*' && nexttok[1] == '/') {
        in_comment = (char)0;
	continue;
    }
    if (in_comment == (char)1) continue;

    /* Handle comment lines */
    if (match(nexttok, "//"))
	SkipNewLine(VLOG_DELIMITERS);

    else if (match(nexttok, "module")) {
      InitializeHashTable(&buses, OBJHASHSIZE);
      SkipTokNoNewline(VLOG_DELIMITERS);
      if (nexttok == NULL) {
	 Fprintf(stderr, "Badly formed \"module\" line\n");
	 goto skip_endmodule;
      }

      if (in_module == (char)1) {
	  Fprintf(stderr, "Missing \"endmodule\" statement on subcircuit.\n");
          InputParseError(stderr);
      }
      in_module = (char)1;

      /* Save pointer to current cell */
      if (CurrentCell != NULL)
         parent = CurrentCell->cell;
      else
	 parent = NULL;

      /* Check for existence of the cell.  We may need to rename it. */

      snprintf(model, 99, "%s", nexttok);
      tp = LookupCellFile(nexttok, filenum);

      /* Check for name conflict with duplicate cell names	*/
      /* This may mean that the cell was used before it was	*/
      /* defined, but CDL files sometimes just redefine the	*/
      /* same cell over and over.  So check if it's empty.	*/

      if ((tp != NULL) && (tp->class != CLASS_MODULE)) {
	 int n;
	 char *ds;

	 // NOTE:  Use this to ignore the new definition---should be
	 // an option to netgen.
	 /* goto skip_endmodule; */

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
	 FreePorts(nexttok);
	 CellDelete(nexttok, filenum);	/* This removes any PLACEHOLDER flag */
	 CellDef(model, filenum);
	 tp = LookupCellFile(model, filenum);
      }
      else if (tp == NULL) {	/* Completely new cell, no name conflict */
         CellDef(model, filenum);
         tp = LookupCellFile(model, filenum);
      }

      hasports = (char)0;
      inlined_decls = (char)0;

      if (tp != NULL) {

	 PushStack(tp->name, CellStackPtr);

	 /* Need to support both types of I/O lists:  Those	*/
	 /* that declare names only in the module list and	*/
	 /* follow with input/output and vector size		*/
	 /* declarations as individual statements in the module	*/
	 /* definition, and those which declare everything	*/
	 /* inside the pin list.				*/

         SkipTok(VLOG_DELIMITERS);

	 // Check for parameters within #( ... ) 

	 if (match(nexttok, "#(")) {
	    SkipTok(VLOG_DELIMITERS);
	    while (match(nexttok, "//")) {
		SkipNewLine(VLOG_DELIMITERS);
		SkipTok(VLOG_DELIMITERS);
	    }
	    in_param = (char)1;
	 }
	 else if (match(nexttok, "(")) {
	    SkipTok(VLOG_DELIMITERS);
	    while (match(nexttok, "//")) {
		SkipNewLine(VLOG_DELIMITERS);
		SkipTok(VLOG_DELIMITERS);
	    }
	 }

         while ((nexttok != NULL) && (nexttok[0] != ';')) {
	    if (in_param) {
	        if (!strcmp(nexttok, ")")) {
		    in_param = (char)0;
		    SkipTok(VLOG_DELIMITERS);
		    if (strcmp(nexttok, "(")) {
		        Fprintf(stderr, "Badly formed module block parameter list.\n");
		        goto skip_endmodule;
		    }
		}
		else if ((eqptr = strchr(nexttok, '=')) != NULL) {
		    *eqptr = '\0';
		    // Only String properties allowed
		    PropertyString(tp->name, filenum, nexttok, 0, eqptr + 1);
		}
	    }
	    else {
	        if (match(nexttok, ")")) break;
		// Ignore input, output, and inout keywords, and handle buses.

		if (inlined_decls == (char)0) {
		    if (match(nexttok, "input") || match(nexttok, "output") ||
				match(nexttok, "inout"))
			inlined_decls = (char)1;
		}
		else {
		    if (!match(nexttok, "input") && !match(nexttok, "output") &&
				!match(nexttok, "inout") && !match(nexttok, "real") &&
				!match(nexttok, "logic") && !match(nexttok, "integer")) {
		        Port(nexttok);
		        hasports = 1;
		    }
		}
	    }
	    SkipTok(VLOG_DELIMITERS);
	    while (match(nexttok, "//")) { SkipNewLine(VLOG_DELIMITERS); SkipTok(VLOG_DELIMITERS); }
         }
	 SetClass((blackbox) ? CLASS_MODULE : CLASS_SUBCKT);

	 if (inlined_decls == 1) {
	    if (hasports == 0)
		// If the cell defines no ports, then create a proxy
		Port((char *)NULL);

	    /* In the blackbox case, don't read the cell contents	*/
	    if (blackbox) goto skip_endmodule;
	 }
      }
      else {

skip_endmodule:
	 /* There was an error, so skip to the end of the	*/
	 /* subcircuit definition				*/

	 while (1) {
	    SkipNewLine(VLOG_DELIMITERS);
	    SkipTok(VLOG_DELIMITERS);
	    if (EndParseFile()) break;
	    if (match(nexttok, "endmodule")) {
	       in_module = 0;
	       break;
	    }
	 }
      }
    }
    else if (match(nexttok, "input") || match(nexttok, "output")
		|| match(nexttok, "inout")) {
 
	// To be completed:  Duplicate parsing of ports, except as statements
	// and not in the module pin list.
	SkipNewLine(VLOG_DELIMITERS);
    }
    else if (match(nexttok, "endmodule")) {

      CleanupModule();
      EndCell();

      if (in_module == (char)0) {
	  Fprintf(stderr, "\"endmodule\" occurred outside of a module!\n");
          InputParseError(stderr);
      }
      in_module = (char)0;

      if (*CellStackPtr) PopStack(CellStackPtr);
      if (*CellStackPtr) ReopenCellDef((*CellStackPtr)->cellname, filenum);
      SkipNewLine(VLOG_DELIMITERS);
    }

    else if (match(nexttok, "`include")) {
      char *iname, *iptr, *quotptr, *pathend, *userpath = NULL;

      SkipTokNoNewline(VLOG_DELIMITERS);
      if (nexttok == NULL) continue;	/* Ignore if no filename */

      // Any file included in another Verilog file needs to be
      // interpreted relative to the path of the parent Verilog file,
      // unless it's an absolute pathname.

      pathend = strrchr(fname, '/');
      iptr = nexttok;
      while (*iptr == '\'' || *iptr == '\"') iptr++;
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
	
      IncludeVerilog(iptr, filenum, CellStackPtr, blackbox);
      FREE(iname);
      SkipNewLine(VLOG_DELIMITERS);
    }
    else if (match(nexttok, "`define") || match(nexttok, "localparam")) {

      // Pick up key = value pairs and store in current cell
      while (nexttok != NULL)
      {
	 /* Parse for parameters used in expressions.  Save	*/
	 /* parameters in the "verilogparams" hash table.	*/

	 SkipTokNoNewline(VLOG_DELIMITERS);
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
	    HashPtrInstall(nexttok, kl, &verilogparams);
	 }
      }
    }

    else if (match(nexttok, "wire")) {	/* wire = node */
      struct bus wb, *nb;
      char nodename[128];
      SkipTokNoNewline(VLOG_DELIMITERS);
      if (match(nexttok, "real")) SkipTokNoNewline(VLOG_DELIMITERS);
      while (nexttok != NULL) {
	 /* Handle bus notation */
	 if (GetBus(nexttok, &wb) == 0) {
	     SkipTokNoNewline(VLOG_DELIMITERS);
	     if (wb.start > wb.end) {
		for (i = wb.end; i <= wb.start; i++) {
		    sprintf(nodename, "%s[%d]", nexttok, i);
		    Node(nodename);
		}
	     }
	     else {
		for (i = wb.start; i <= wb.end; i++) {
		    sprintf(nodename, "%s[%d]", nexttok, i);
		    Node(nodename);
		}
	     }
	     nb = NewBus();
	     nb->start = wb.start;
	     nb->end = wb.end;
	     HashPtrInstall(nexttok, nb, &buses);
	 }
	 else {
	     Node(nexttok);
	 }
         SkipTokNoNewline(VLOG_DELIMITERS);
      }
    }
    else if (match(nexttok, "endmodule")) {
      // No action---new module is started with next 'module' statement,
      // if any.
      SkipNewLine(VLOG_DELIMITERS);
    }
    else if (nexttok[0] == '`') {
      // Ignore any other directive starting with a backtick
      SkipNewLine(VLOG_DELIMITERS);
    }
    else if (match(nexttok, "reg") || match(nexttok, "assign")
		|| match(nexttok, "always")) {
      Printf("Module '%s' is not structural verilog, making black-box.\n", model);
      // To be done:  Remove any contents (but may not be necessary)
      // Recast as module
      SetClass(CLASS_MODULE);
      goto skip_endmodule;
    }
    else {	/* module instances */
      char instancename[100], modulename[100];
      int itype, arraystart, arrayend, arraymax, arraymin;

      instancename[99] = '\0';
      modulename[99] = '\0';

      struct portelement {
	char *name;	// Name of port in subcell
	char *net;	// Name of net connecting to port in the parent
	struct portelement *next;
      };

      struct portelement *head, *tail, *scan, *scannext;
      struct objlist *obptr;

      strncpy(modulename, nexttok, 99);
      if (!(*CellStackPtr)) {
	CellDef(fname, filenum);
	PushStack(fname, CellStackPtr);
      }
      
      head = NULL;
      tail = NULL;
      SkipTok(VLOG_DELIMITERS);

      // Next token must be '#(' (parameters) or '(' (pin list)

      if (!strcmp(nexttok, "#(")) {

	 // Read the parameter list

         while (nexttok != NULL) {
	    char *paramname;
	    SkipTok(VLOG_DELIMITERS2);
	    while (match(nexttok, "//")) {
		SkipNewLine(VLOG_DELIMITERS);
		SkipTok(VLOG_DELIMITERS2);
	    }
	    if (!strcasecmp(nexttok, ";")) {
		SkipTok(VLOG_DELIMITERS);
		break;
	    }

	    // We need to look for parameters of the type ".name(value)"

	    SkipTok(VLOG_DELIMITERS2);
	    while (match(nexttok, "//")) {
		SkipNewLine(VLOG_DELIMITERS);
		SkipTok(VLOG_DELIMITERS2);
	    }
	    if (nexttok[0] != '.') {
	        Printf("Badly formed subcircuit parameter line at \"%s\"\n", nexttok);
	    }
	    else {
		paramname = strsave(nexttok + 1);
	        SkipTok(VLOG_DELIMITERS2);
		while (match(nexttok, "//")) {
		    SkipNewLine(VLOG_DELIMITERS);
		    SkipTok(VLOG_DELIMITERS2);
		}
	        AddProperty(&kvlist, paramname, nexttok);
		FREE(paramname);
	    }
	 }
      }

      // Catch instance name followed by open parenthesis with no space
      if ((parptr = strchr(nexttok, '(')) != NULL) *parptr = '\0';

      // Then comes the instance name
      strncpy(instancename, nexttok, 99);
      if (!parptr)
	 SkipTok(VLOG_DELIMITERS);
      else {
	 *parptr = '(';
	 nexttok = parptr;
      }
      /* Printf("Diagnostic:  new instance is %s\n", instancename); */
      while (match(nexttok, "//")) {
	  SkipNewLine(VLOG_DELIMITERS);
	  SkipTok(VLOG_DELIMITERS);
      }

      arraystart = arrayend = -1;
      if (nexttok[0] == '[') {
	 // Handle instance array notation.
	 struct bus wb;
	 if (GetBus(nexttok, &wb) == 0) {
	     arraystart = wb.start;
	     arrayend = wb.end;
	 }
	 SkipTok(VLOG_DELIMITERS);
      }

      if (nexttok[0] == '(') {
	 char savetok = (char)0;
	 struct portelement *new_port;

	 // Note that the open parens does not necessarily have to be
	 // followed by space.
	 if (nexttok[1] != '\0') {
	    nexttok++;
	    savetok = (char)1;
	 }

	 // Read the pin list
         while (nexttok != NULL) {
	    if (savetok == (char)0) SkipTok(VLOG_DELIMITERS2);
	    savetok = (char)0;
	    while (match(nexttok, "//")) {
		SkipNewLine(VLOG_DELIMITERS);
		SkipTok(VLOG_DELIMITERS2);
	    }
	    if (!strcasecmp(nexttok, ";")) break;

	    // We need to look for pins of the type ".name(value)"

	    if (nexttok[0] != '.') {
	        Printf("Badly formed subcircuit pin line at \"%s\"\n", nexttok);
	    }
	    else {
	       new_port = (struct portelement *)CALLOC(1, sizeof(struct portelement));
	       new_port->name = strsave(nexttok + 1);
	       SkipTok(VLOG_DELIMITERS2);
	       while (match(nexttok, "//")) {
		    SkipNewLine(VLOG_DELIMITERS);
		    SkipTok(VLOG_DELIMITERS2);
	       }
	       if (match(nexttok, ";") || (nexttok[0] == '.')) {
		  char localnet[100];
		  // Empty parens, so create a new local node
		  savetok = (char)1;
		  sprintf(localnet, "_noconnect_%d_", localcount++);
		  new_port->net = strsave(localnet);
	       }
	       else
	          new_port->net = strsave(nexttok);

	       if (head == NULL) head = new_port;
	       else tail->next = new_port;
	       new_port->next = NULL;
	       tail = new_port;
	    }
	 }
      }
      else {
	 // There are too many statements in too many variants of verilog
	 // to track them all, let alone dealing with macro substitutions
	 // and such.  If it doesn't look like a circuit instance and isn't
	 // otherwise handled above, treat as a non-structural statement
	 // and recast the device class as a black-box module.
         SetClass(CLASS_MODULE);
	 goto skip_endmodule;
      }

      /* Check for ignored class */

      if ((itype = IsIgnored(modulename, filenum)) == IGNORE_CLASS) {
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
            Printf("Instance of '%s' is shorted, ignoring.\n", modulename);
	    while (head) {
	       p = head->next;
	       FREE(head);
	       head = p;
            }
            return;
         }
      }

      if (head == NULL) {
	 Fprintf(stderr, "Warning:  Cell %s has no pins\n", scan->name);
      }

      /* Check that the module exists.  If not, generate an empty	*/
      /* module entry matching the call.				*/

      tp = LookupCellFile(modulename, filenum);
      if (tp == NULL) {
         struct bus wb;
	 char defport[128];

	 Fprintf(stdout, "Creating placeholder cell definition for "
			"module %s.\n", modulename);
	 CellDef(modulename, filenum);
	 CurrentCell->flags |= CELL_PLACEHOLDER;
         for (scan = head; scan != NULL; scan = scan->next) {
	    // Check if net name is a wire bus or portion of a bus
	    if (GetBus(scan->net, &wb) == 0) {
		int range;
		if ((arrayend - arraystart) == (wb.end - wb.start)) {
		    // Net is a bus, but net is split over arrayed instances
		    Port(scan->name);
		}
		else if (wb.start > wb.end) {
		    range = wb.start - wb.end;
		    for (i = range; i >= 0; i--) {
			sprintf(defport, "%s[%d]", scan->name, i);
			Port(defport);
		    }
		}
		else {
		    range = wb.end - wb.start;
		    for (i = 0; i <= range; i++) {
			sprintf(defport, "%s[%d]", scan->name, i);
			Port(defport);
		    }
		}
	    }
	    else {
		Port(scan->name);
	    }
	 }
	 if (head == NULL) {
	    Port((char *)NULL);	// Must have something for pin 1
	 }
	 SetClass(CLASS_MODULE);
         EndCell();
	 ReopenCellDef((*CellStackPtr)->cellname, filenum);		/* Reopen */
      }

      arraymax = (arraystart > arrayend) ? arraystart : arrayend;
      arraymin = (arraystart > arrayend) ? arrayend : arraystart;

      for (i = arraymin; i <= arraymax; i++) {
	 char *brackptr;
	 int j;
	 char locinst[128];

         if (i != -1)
	    sprintf(locinst, "%s[%d]", instancename, i);
	 else
	    strcpy(locinst, instancename);
	 Instance(modulename, locinst);
	 LinkProperties(modulename, kvlist);

         obptr = LookupInstance(locinst, CurrentCell);
         if (obptr != NULL) {
            scan = head;
	    if (scan != NULL)
            do {
	       struct bus wb;
	       if (GetBus(scan->net, &wb) == 0) {
		   char *scanroot;
		   scanroot = strsave(scan->net);
		   brackptr = strchr(scanroot, '[');
		   if (brackptr) *brackptr = '\0';

		   if (arraystart == -1) {
		       // Port may be an array
		       int range;
		       char pinname[128];

		       // Check if port is an array
		       if (strchr(obptr->name, '[') == NULL) {
			   if (wb.start != wb.end) {
			       Printf("Error: Bus connected to single port\n");
			   }
			   // Use only the first bit of the bus
			   sprintf(pinname, "%s[%d]", scanroot, wb.start);
	                   if (LookupObject(pinname, CurrentCell) == NULL)
				Node(pinname);
	                   join(pinname, obptr->name);
			   if (brackptr) *brackptr = '[';
		       }
		       else if (wb.start > wb.end) {
			   range = wb.start - wb.end;
		           for (j = range; j >= 0; j--) {
			       sprintf(pinname, "%s[%d]", scanroot, j);
	                       if (LookupObject(pinname, CurrentCell) == NULL)
				   Node(pinname);
	                       join(pinname, obptr->name);
			       if (j == 0) break;
			       if (obptr->next && (brackptr =
					strchr(obptr->next->name, '[')) != NULL) {
				   if (strncmp(obptr->next->name, obptr->name,
						(int)(brackptr - obptr->next->name))) {
				       Printf("Error: More bits in net than in port!\n");
				       break;
				   }
			           obptr = obptr->next;
			       }
			       else {
				   Printf("Error: More bits in net than in port!\n");
				   break;
			       }
			   }
			   // Are there port bits left over?
			   while (obptr->next && ((brackptr =
				strchr(obptr->next->name, '[')) != NULL) &&
				(!strncmp(obptr->next->name, obptr->name,
					(int)(brackptr - obptr->next->name)))) {
			       Printf("Error: More bits in port than in net!\n");
			       obptr = obptr->next;
			   }
		       }
		       else {
			   range = wb.end - wb.start;
		           for (j = 0; j <= range; j++) {
			       sprintf(pinname, "%s[%d]", scanroot, j);
	                       if (LookupObject(pinname, CurrentCell) == NULL)
				   Node(pinname);
	                       join(pinname, obptr->name);
			       if (j == range) break;
			       if (obptr->next && (brackptr =
					strchr(obptr->next->name, '[')) != NULL) {
				   if (strncmp(obptr->next->name, obptr->name,
						(int)(brackptr - obptr->next->name))) {
				       Printf("Error: More bits in net than in port!\n");
				       break;
				   }
			           obptr = obptr->next;
			       }
			       else {
				   Printf("Error: More bits in net than in port!\n");
				   break;
			       }
			   }
			   // Are there port bits left over?
			   while (obptr->next && ((brackptr =
				strchr(obptr->next->name, '[')) != NULL) &&
				(!strncmp(obptr->next->name, obptr->name,
					(int)(brackptr - obptr->next->name)))) {
			       Printf("Error: More bits in port than in net!\n");
			       obptr = obptr->next;
			   }
		       }
		   }
		   else {
		       // Instance must be an array
		       char netname[128];
		       int slice;
		       if (wb.start > wb.end && arraystart > arrayend)
			   slice = wb.start - (arraystart - i);
		       else if (wb.start < wb.end && arraystart > arrayend)
			   slice = wb.start + (arraystart - i);
		       else if (wb.start > wb.end && arraystart < arrayend)
			   slice = wb.start - (arraystart + i);
		       else // (wb.start < wb.end && arraystart < arrayend)
			   slice = wb.start + (arraystart + i);
		       sprintf(netname, "%s[%d]", scanroot, slice);
	               if (LookupObject(netname, CurrentCell) == NULL) Node(netname);
	               join(netname, obptr->name);
		   }
		   FREE(scanroot);
	       }
	       else {
	           if (LookupObject(scan->net, CurrentCell) == NULL) Node(scan->net);
	           join(scan->net, obptr->name);
	       }
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
	              Fprintf(stderr, "Too many warnings. . . will not "
				"report any more.\n");
                }
	        warnings++;
	     }
	  }
	  if (i == -1) break;	/* No array */
      }
      DeleteProperties(&kvlist);

      /* free up the allocated list */
      scan = head;
      while (scan != NULL) {
	scannext = scan->next;
	FREE(scan->name);
	FREE(scan->net);
	FREE(scan);
	scan = scannext;
      }
    }
    continue;

baddevice:
    Fprintf(stderr, "Badly formed line in input.\n");
  }

  /* Watch for bad ending syntax */

  if (in_module == (char)1) {
     Fprintf(stderr, "Missing \"endmodule\" statement in module.\n");
     InputParseError(stderr);
  }

  if (*(CellStackPtr)) {
     CleanupModule();
     EndCell();
     if (*CellStackPtr) PopStack(CellStackPtr);
     if (*CellStackPtr) ReopenCellDef((*CellStackPtr)->cellname, filenum);
  }

  if (warnings)
     Fprintf(stderr, "File %s read with %d warning%s.\n", fname,
		warnings, (warnings == 1) ? "" : "s");
}

/*----------------------------------------------*/
/* Top-level verilog module file read routine	*/
/*----------------------------------------------*/

char *ReadVerilogTop(char *fname, int *fnum, int blackbox)
{
  struct cellstack *CellStack = NULL;
  struct nlist *tp;
  int filenum;

  // Make sure CurrentCell is clear
  CurrentCell = NULL;

  if ((filenum = OpenParseFile(fname, *fnum)) < 0) {
    char name[100];

    SetExtension(name, fname, VERILOG_EXTENSION);
    if ((filenum = OpenParseFile(name, *fnum)) < 0) {
      Fprintf(stderr,"Error in Verilog file read: No file %s\n",name);
      *fnum = filenum;
      return NULL;
    }    
  }

  /* All Verilog file reading is case sensitive.  However:  if	*/
  /* a SPICE file was read before it, then it will be forced to	*/
  /* be case insensitive, with a stern warning.			*/

  if (matchfunc == matchnocase) {
     Printf("Warning:  A case-insensitive file has been read and so the	"
		"verilog file must be treated case-insensitive to match.\n");
  }

  InitializeHashTable(&verilogparams, OBJHASHSIZE);

  /* All verilog files should start with a comment line,  */
  /* but we won't depend upon it.  Any comment line	  */
  /* will be handled by the main Verilog file processing. */

  ReadVerilogFile(fname, filenum, &CellStack, blackbox);
  CloseParseFile();

  // Cleanup
  while (CellStack != NULL) PopStack(&CellStack);

  RecurseHashTable(&verilogparams, freeprop);
  HashKill(&verilogparams);

  // Record the top level file.
  if (LookupCellFile(fname, filenum) == NULL) CellDef(fname, filenum);

  tp = LookupCellFile(fname, filenum);
  if (tp) tp->flags |= CELL_TOP;

  *fnum = filenum;
  return fname;
}

/*--------------------------------------*/
/* Wrappers for ReadVerilogTop()	*/
/*--------------------------------------*/

char *ReadVerilog(char *fname, int *fnum)
{
   return ReadVerilogTop(fname, fnum, 0);
}

/*--------------------------------------*/
/* Verilog file include routine		*/
/*--------------------------------------*/

void IncludeVerilog(char *fname, int parent, struct cellstack **CellStackPtr,
		int blackbox)
{
  int filenum = -1;
  char name[256];

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

	/* If that fails, see if a standard Verilog extension	*/
	/* helps, if the file didn't have an extension.  But	*/
	/* really, we're getting desperate at this point.	*/

	if (strchr(fname, '.') == NULL) {
           SetExtension(name, fname, VERILOG_EXTENSION);
           filenum = OpenParseFile(name, parent);
	}
        if (filenum < 0) {
           Fprintf(stderr,"Error in Verilog file include: No file %s\n",name);
           return;
        }    
     }
  }
  ReadVerilogFile(fname, parent, CellStackPtr, blackbox);
  CloseParseFile();
}

