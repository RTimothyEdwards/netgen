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

// See netfile.c for explanation of delimiters.  'X'
// separates single-character delimiters from two-character delimiters.
#define VLOG_DELIMITERS "X///**/#((**)X,;:(){}[]="
#define VLOG_PIN_NAME_DELIMITERS "X///**/(**)X()"
#define VLOG_PIN_CHECK_DELIMITERS "X///**/(**)X,;(){}"

// Global storage for verilog parameters
struct hashdict verilogparams;
// Global storage for verilog definitions
struct hashdict verilogdefs;

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

//-------------------------------------------------------------------------
// Get bus indexes from the notation name[a:b].  If there is only "name"
// then look up the name in the bus hash list and return the index bounds.
// Return 0 on success, 1 on syntax error, and -1 if signal is not a bus.
//
// Note that this routine relies on the delimiter characters including
// "[", ":", and "]" when calling NextTok.
//-------------------------------------------------------------------------

int GetBusTok(struct bus *wb)
{
    int result, start, end;
    struct property *kl = NULL;

    if (wb == NULL) return 0;
    else {
        wb->start = -1;
        wb->end = -1;
    }

    if (match(nexttok, "[")) {
	SkipTokComments(VLOG_DELIMITERS);

	result = sscanf(nexttok, "%d", &start);
	if (result != 1) {
	    // Is name in the parameter list?
	    kl = (struct property *)HashLookup(nexttok, &verilogparams);
	    if (kl == NULL) {
		Printf("Array value %s is not a number or a parameter.\n",
				nexttok);
		return 1;
	    }
	    else {
		if (kl->type == PROP_STRING) {
		    result = sscanf(kl->pdefault.string, "%d", &start);
		    if (result != 1) {
		        Printf("Parameter %s has value %s that cannot be parsed"
				" as an integer.\n", nexttok, kl->pdefault.string);
			return 1;
		    }
		}
		else if (kl->type == PROP_INTEGER) {
		    start = kl->pdefault.ival;
		}
		else if (kl->type == PROP_DOUBLE) {
		    start = (int)kl->pdefault.dval;
		    if ((double)start != kl->pdefault.dval) {
		        Printf("Parameter %s has value %g that cannot be parsed"
				" as an integer.\n", nexttok, kl->pdefault.dval);
			return 1;
		    }
		}
		else {
		    Printf("Parameter %s has unknown type; don't know how"
				" to parse.\n", nexttok);
		    return 1;
		}
	    }
	}
	SkipTokComments(VLOG_DELIMITERS);
	if (match(nexttok, "]")) {
	    result = 1;
	    end = start;	// Single bit
	}
	else if (!match(nexttok, ":")) {
	    Printf("Badly formed array notation:  Expected colon, found %s\n", nexttok);
	    return 1;
	}
	else {
	    SkipTokComments(VLOG_DELIMITERS);

	    result = sscanf(nexttok, "%d", &end);
	    if (result != 1) {
		// Is name in the parameter list?
	        kl = (struct property *)HashLookup(nexttok, &verilogparams);
		if (kl == NULL) {
		    Printf("Array value %s is not a number or a parameter.\n",
					nexttok);
		    return 1;
		}
		else {
		    if (kl->type == PROP_STRING) {
			result = sscanf(kl->pdefault.string, "%d", &end);
			if (result != 1) {
		            Printf("Parameter %s has value %s that cannot be parsed"
					" as an integer.\n", nexttok,
					kl->pdefault.string);
			    return 1;
			}
		    }
		    else if (kl->type == PROP_INTEGER) {
			end = kl->pdefault.ival;
		    }
		    else if (kl->type == PROP_DOUBLE) {
		        end = (int)kl->pdefault.dval;
		        if ((double)end != kl->pdefault.dval) {
			    Printf("Cannot parse second digit from parameter "
					"%s value %g\n", nexttok, kl->pdefault.dval);
			    return 1;
			}
		    }
		    else {
		        Printf("Parameter %s has unknown type; don't know how"
					" to parse.\n", nexttok);
		        return 1;
		    }
		}
	    }
	}
	wb->start = start;
	wb->end = end;

	while (!match(nexttok, "]")) {
	    SkipTokComments(VLOG_DELIMITERS);
	    if (nexttok == NULL) {
		Printf("End of file reached while reading array bounds.\n");
		return 1;
	    }
	    else if (match(nexttok, ";")) {
		// Better than reading to end-of-file, give up on end-of-statement
		Printf("End of statement reached while reading array bounds.\n");
		return 1;
	    }
	}
    }
    else {
	struct bus *hbus;
	hbus = (struct bus *)HashLookup(nexttok, &buses);
	if (hbus != NULL) {
	    wb->start = hbus->start;
	    wb->end = hbus->end;
	}
	else
	    return -1;
    }
    return 0;
}

//--------------------------------------------------------------------
// GetBus() is similar to GetBusTok() (see above), but it parses from
// a string instead of the input tokenizer.
//--------------------------------------------------------------------

int GetBus(char *astr, struct bus *wb)
{
    char *colonptr, *brackstart, *brackend, *sigend, sdelim;
    int result, start, end;

    if (wb == NULL) return 0;
    else {
        wb->start = -1;
        wb->end = -1;
    }

    /* Check for wire bundles.  If there are bundles, process each  */
    /* section separately and concatenate the sizes.		    */
    /* To be done:  Handle nested bundles, including N-times concatenation */

    if (*astr == '{') {
	struct bus wbb;

	astr++;
	wb->end = 0;
	while((*astr != '\0') && (*astr != '}')) {
	    sigend = strchr(astr, ',');
	    if (sigend == NULL) sigend = strchr(astr, '}');
	    if (sigend == NULL) {
		Printf("Badly formed wire bundle \"%s\"\n", astr - 1);
		return 1;
	    }
	    sdelim = *sigend;
	    *sigend = '\0';
	    if (GetBus(astr, &wbb) == 0) {
		if (wbb.start > wbb.end)
		    wb->start += (wbb.start - wbb.end + 1);
		else
		    wb->start += (wbb.end - wbb.start + 1);
	    }
	    else {
		wb->start++;
	    }
	    *sigend = sdelim;
	    astr = sigend + 1;
	}
	return 0;
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
	    end = start;        // Single bit
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

   if (buses.hashtab != NULL) {
      RecurseHashTable(&buses, freebus);
      HashKill(&buses);
   }
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
  int cdnum = 1, rdnum = 1, i, ival;
  int warnings = 0, hasports, inlined_decls = 0, localcount = 1;
  double dval;
  char devtype, in_module, in_param;
  char *eqptr, *matchptr;
  struct keyvalue *kvlist = NULL;
  char inst[256], model[256], instname[256], portname[256], pkey[256];
  struct nlist *tp;
  struct objlist *parent, *sobj, *nobj, *lobj, *pobj;

  inst[255] = '\0';
  model[255] = '\0';
  instname[255] = '\0';
  in_module = (char)0;
  in_param = (char)0;

  while (!EndParseFile()) {

    SkipTokComments(VLOG_DELIMITERS); /* get the next token */
    if ((EndParseFile()) && (nexttok == NULL)) break;
    else if (nexttok == NULL)
      break;

    /* Ignore end-of-statement markers */
    else if (match(nexttok, ";"))
      continue;

    /* Ignore primitive definitions */
    else if (match(nexttok, "primitive")) {
	while (1) {
	    SkipNewLine(VLOG_DELIMITERS);
	    SkipTokComments(VLOG_DELIMITERS);
	    if (EndParseFile()) break;
	    if (match(nexttok, "endprimitive")) {
	       in_module = 0;
	       break;
	    }
	}
    }

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
	 struct bus wb, *nb;

	 PushStack(tp->name, CellStackPtr);

	 /* Need to support both types of I/O lists:  Those	*/
	 /* that declare names only in the module list and	*/
	 /* follow with input/output and vector size		*/
	 /* declarations as individual statements in the module	*/
	 /* definition, and those which declare everything	*/
	 /* inside the pin list.				*/

         SkipTokComments(VLOG_DELIMITERS);

	 // Check for parameters within #( ... ) 

	 if (match(nexttok, "#(")) {
	    SkipTokComments(VLOG_DELIMITERS);
	    in_param = (char)1;
	 }
	 else if (match(nexttok, "(")) {
	    SkipTokComments(VLOG_DELIMITERS);
	 }

	 wb.start = wb.end = -1;
         while ((nexttok != NULL) && !match(nexttok, ";")) {
	    if (in_param) {
		if (match(nexttok, ")")) {
		    in_param = (char)0;
		    SkipTokComments(VLOG_DELIMITERS);
		    if (!match(nexttok, "(")) {
		        Fprintf(stderr, "Badly formed module block parameter list.\n");
		        goto skip_endmodule;
		    }
		}
		else if (match(nexttok, "=")) {

		    // The parameter value is the next token.
		    SkipTokComments(VLOG_DELIMITERS); /* get the next token */
		    eqptr = nexttok;

		    // Try first as a double, otherwise it's a string
		    // Double value's slop defaults to 1%.
		    if (ConvertStringToFloat(eqptr, &dval) == 1)
		        PropertyDouble(tp->name, filenum, pkey, 0.01, dval);
		    else
			PropertyString(tp->name, filenum, pkey, 0, eqptr);
		}
		else {
		    /* Assume this is a keyword and save it */
		    strcpy(pkey, nexttok);
		}
	    }
	    else if (!match(nexttok, ",")) {
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
				!match(nexttok, "wire") && !match(nexttok, "logic") &&
				!match(nexttok, "integer")) {
			if (match(nexttok, "[")) {
			   if (GetBusTok(&wb) != 0) {
			      // Didn't parse as a bus, so wing it
			      wb.start = wb.end = -1;
			      Port(nexttok);
			   }
			}
			else {
			   if (wb.start != -1) {
			      if (wb.start > wb.end) {
				 for (i = wb.start; i >= wb.end; i--) {
				    sprintf(portname, "%s[%d]", nexttok, i);
				    Port(portname);
				 }
			      }
			      else {
				 for (i = wb.start; i <= wb.end; i++) {
				    sprintf(portname, "%s[%d]", nexttok, i);
				    Port(portname);
				 }
			      }
			      /* Also register this port as a bus */
			      nb = NewBus();
			      nb->start = wb.start;
			      nb->end = wb.end;
			      HashPtrInstall(nexttok, nb, &buses);

			      wb.start = wb.end = -1;
			   }
			   else {
			      Port(nexttok);
			   }
			}
		        hasports = 1;
		    }
		}
	    }
	    SkipTokComments(VLOG_DELIMITERS);
	    if (nexttok == NULL) break;
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
	    SkipTokComments(VLOG_DELIMITERS);
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
	struct bus wb;
 
	// Parsing of ports as statements not in the module pin list.
	wb.start = wb.end = -1;
	while (1) {
	    SkipTokComments(VLOG_DELIMITERS);
	    if (EndParseFile()) break;

	    if (match(nexttok, ";")) {
		// End of statement
		break;
	    }
	    else if (match(nexttok, "[")) {
		if (GetBusTok(&wb) != 0) {
		    // Didn't parse as a bus, so wing it
		    wb.start = wb.end = -1;
		    Port(nexttok);
		}
	    }
	    else if (!match(nexttok, ",")) {
		if (wb.start != -1) {
		    if (wb.start > wb.end) {
			for (i = wb.start; i >= wb.end; i--) {
			    sprintf(portname, "%s[%d]", nexttok, i);
			    Port(portname);
			}
		    }
		    else {
			for (i = wb.start; i <= wb.end; i++) {
			    sprintf(portname, "%s[%d]", nexttok, i);
			    Port(portname);
			}
		    }
		    wb.start = wb.end = -1;
		}
		else {
		    Port(nexttok);
		}
	    }
	    hasports = 1;
	}
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
      while (*quotptr != '\'' && *quotptr != '\"' && 
		*quotptr != '\0' && *quotptr != '\n') quotptr++;
      if (*quotptr == '\'' || *quotptr == '\"') *quotptr = '\0';
	
      IncludeVerilog(iptr, filenum, CellStackPtr, blackbox);
      FREE(iname);
      SkipNewLine(VLOG_DELIMITERS);
    }
    else if (match(nexttok, "`define")) {
      struct property *kl = NULL;

      // Pick up key-value pair and store in current cell

      /* Parse for definitions used in expressions.  Save */
      /* definitions in the "verilogdefs" hash table.	 */

      SkipTokNoNewline(VLOG_DELIMITERS);
      if ((nexttok == NULL) || (nexttok[0] == '\0')) break;

      kl = NewProperty();
      kl->key = strsave(nexttok);
      kl->idx = 0;
      kl->merge = MERGE_NONE;

      SkipTokNoNewline(VLOG_DELIMITERS);
      if ((nexttok == NULL) || (nexttok[0] == '\0')) {
	 // Let "`define X" be equivalent to "`define X 1".  Use integer value.
	 kl->type = PROP_INTEGER;
	 kl->pdefault.ival = 1;
         kl->slop.ival = 0;
      }
      else if (ConvertStringToInteger(nexttok, &ival) == 1) {
	 /* Parameter parses as an integer */
      	 kl->type = PROP_INTEGER;
	 kl->pdefault.ival = ival;
         kl->slop.ival = 0;		// Exact match default
      }
      else if (ConvertStringToFloat(nexttok, &dval) == 1) {
	 /* Parameter parses as a floating-point number */
      	 kl->type = PROP_DOUBLE;
	 kl->pdefault.dval = dval;
         kl->slop.dval = 0.01;		// One percent default
      }
      else {
	 /* Treat the parameter as a string */
      	 kl->type = PROP_STRING;
	 kl->pdefault.string = strsave(nexttok);
         kl->slop.dval = 0.0;
      }
      HashPtrInstall(kl->key, kl, &verilogdefs);
    }
    else if (match(nexttok, "`undef")) {
      struct property *kl = NULL;

      SkipTokNoNewline(VLOG_DELIMITERS);
      if ((nexttok == NULL) || (nexttok[0] == '\0')) break;

      kl = HashLookup(nexttok, &verilogdefs);
      if (kl != NULL) {
	  HashDelete(nexttok, &verilogdefs);
	  if (kl->type == PROP_STRING)
             if (kl->pdefault.string != NULL)
	         FREE(kl->pdefault.string);
	  FREE(kl->key);
      }
      /* Presumably it is not an error to undefine an undefined keyword */
    }
    else if (match(nexttok, "localparam")) {
      // Pick up key = value pairs and store in current cell
      while (nexttok != NULL)
      {
	 struct property *kl = NULL;

	 /* Parse for parameters used in expressions.  Save	*/
	 /* parameters in the "verilogparams" hash table.	*/

	 SkipTokNoNewline(VLOG_DELIMITERS);
	 if ((nexttok == NULL) || (nexttok[0] == '\0')) break;
	 if ((eqptr = strchr(nexttok, '=')) != NULL) {
	    *eqptr = '\0';
	    kl = NewProperty();
	    kl->key = strsave(nexttok);
	    kl->idx = 0;
	    kl->merge = MERGE_NONE;

	    if (ConvertStringToInteger(eqptr + 1, &ival) == 1) {
	       kl->type = PROP_INTEGER;
	       kl->slop.ival = 0;
	       kl->pdefault.ival = ival;
	    }
	    else if (ConvertStringToFloat(eqptr + 1, &dval) == 1) {
	       kl->type = PROP_DOUBLE;
	       kl->slop.dval = 0.01;
	       kl->pdefault.dval = dval;
	    }
	    else {
	       kl->type = PROP_STRING;
	       kl->slop.dval = 0.0;
	       kl->pdefault.string = strsave(eqptr + 1);
	    }
	    HashPtrInstall(nexttok, kl, &verilogparams);
	 }
      }
    }

    else if (match(nexttok, "wire") || match(nexttok, "assign")) {	/* wire = node */
	struct bus wb, wb2, *nb;
	char nodename[128], noderoot[100];
	int is_wire = match(nexttok, "wire");
	int j;
	struct objlist *lhs, *rhs;

	/* Get left-hand side expression.  If this is a wire statement,	*/
	/* then define the wire.  If is_wire is false, then the wire	*/
	/* should already be defined.					*/

	if (is_wire) {
	    SkipTokNoNewline(VLOG_DELIMITERS);
	    if (match(nexttok, "real"))
		SkipTokNoNewline(VLOG_DELIMITERS);
	    else if (match(nexttok, "logic"))
		SkipTokNoNewline(VLOG_DELIMITERS);

	    if (GetBusTok(&wb) == 0) {
		/* Handle bus notation */
		SkipTokNoNewline(VLOG_DELIMITERS);
		strcpy(noderoot, nexttok);
		if (wb.start > wb.end) {
		    for (i = wb.end; i <= wb.start; i++) {
			sprintf(nodename, "%s[%d]", nexttok, i);
			if (LookupObject(nodename, CurrentCell) == NULL)
			    Node(nodename);
			if (i == wb.start) lhs = LookupObject(nodename, CurrentCell);
		    }
		}
		else {
		    for (i = wb.start; i <= wb.end; i++) {
			sprintf(nodename, "%s[%d]", nexttok, i);
			if (LookupObject(nodename, CurrentCell) == NULL)
			    Node(nodename);
			if (i == wb.start) lhs = LookupObject(nodename, CurrentCell);
		    }
		}
		nb = NewBus();
		nb->start = wb.start;
		nb->end = wb.end;
		HashPtrInstall(nexttok, nb, &buses);
	    }
	    else {
		if (LookupObject(nexttok, CurrentCell) == NULL) {
		    Node(nexttok);
		    lhs = LookupObject(nexttok, CurrentCell);
		}
	    }
	    while (1) {
		SkipTokNoNewline(VLOG_DELIMITERS);
		if (match(nexttok, ",")) {
		    SkipTokComments(VLOG_DELIMITERS);
		    if (LookupObject(nexttok, CurrentCell) == NULL) {
			Node(nexttok);
			lhs = LookupObject(nexttok, CurrentCell);
		    }
		}
		else break;
	    }
	}
	else {	    /* "assign" */
	    SkipTokComments(VLOG_PIN_CHECK_DELIMITERS);
	    if (GetBus(nexttok, &wb) == 0) {
		char *aptr = strchr(nexttok, '[');
		if (aptr != NULL) {
		    *aptr = '\0';
		    /* Find object of first net in bus */
		    strcpy(noderoot, nexttok);
		    sprintf(nodename, "%s[%d]", nexttok, wb.start);
		    lhs = LookupObject(nodename, CurrentCell);
		    *aptr = '[';
		}
	    }
	    else {
		lhs = LookupObject(nexttok, CurrentCell);
	    }
	    SkipTokComments(VLOG_DELIMITERS);
	    if (lhs && ((!nexttok) || (!match(nexttok, "=")))) {
		fprintf(stderr, "Empty assignment for net %s\n", lhs->name);
	    }
	}

	/* Check for assignment statement, and handle any allowed uses.	    */
	/* Any uses other than those mentioned below will cause the entire  */
	/* module to be treated as a black box.				    */

	// Allowed uses of "assign" for netlists:
	//    "assign a = b" joins two nets.
	//    "assign a = {b, c, ...}" creates a bus from components.
	//    "assign" using any boolean arithmetic is not structural verilog.

	if (nexttok && match(nexttok, "=")) {
	    char assignname[128], assignroot[100];

	    i = wb.start;
	    while (1) {
		SkipTokComments(VLOG_PIN_CHECK_DELIMITERS);
		if (!nexttok) break;

		if (match(nexttok, "{")) {
		    /* RHS is a bundle */
		    continue;
		}
		else if (match(nexttok, "}")) {
		    /* End of bundle */
		    continue;
		}
		else if (match(nexttok, ",")) {
		    /* Additional signals in bundle */
		    continue;
		}
		else if (match(nexttok, ";")) {
		    /* End of assignment */
		    break;
		}
		else {
		    if (GetBus(nexttok, &wb2) == 0) {
			char *aptr = strchr(nexttok, '[');
			j = wb2.start;
			if (aptr != NULL) {
			    *aptr = '\0';
			    strcpy(assignroot, nexttok);
			    sprintf(assignname, "%s[%d]", nexttok, j);
			    rhs = LookupObject(assignname, CurrentCell);
			    *aptr = '[';
			}
		    }
		    else {
			j = -1;
			rhs = LookupObject(nexttok, CurrentCell);
		    }
		    if ((lhs == NULL) || (rhs == NULL)) {
			if (rhs != NULL) {
			    Printf("Improper assignment;  left-hand side cannot "
					"be parsed.\n");
			    Printf("Right-hand side is \"%s\".\n", rhs->name);
			    break;
			}
			if (lhs != NULL) {
			    Printf("Improper assignment;  right-hand side cannot "
					"be parsed.\n");
			    Printf("Left-hand side is \"%s\".\n", lhs->name);
			    /* Not parsable, probably behavioral verilog? */
			    Printf("Module '%s' is not structural verilog, "
				    "making black-box.\n", model);
			    SetClass(CLASS_MODULE);
			    goto skip_endmodule;
			}
		    }
		    while (1) {
			/* Assign bits in turn from bundle in RHS to bits of LHS    */
			/* until bits in signal are exhausted or LHS is full.	    */

			if (i != -1)
			    sprintf(nodename, "%s[%d]", noderoot, i);
			else
			    sprintf(nodename, lhs->name);
			if (j != -1)
			    sprintf(assignname, "%s[%d]", assignroot, j);
			else
			    sprintf(assignname, rhs->name);

			join(nodename, assignname);

			if (i == wb.end) break;
			i += (wb.end > wb.start) ? 1 : -1;

			if (j == wb2.end) break;
			j += (wb2.end > wb2.start) ? 1 : -1;
		    }
		}
	    }
	}
	while (nexttok && !match(nexttok, ";"))
	    SkipTokComments(VLOG_DELIMITERS);
    }
    else if (match(nexttok, "endmodule")) {
      // No action---new module is started with next 'module' statement,
      // if any.
      SkipNewLine(VLOG_DELIMITERS);
      in_module = (char)0;	    /* Should have been done already */
    }
    else if (nexttok[0] == '`') {
      // Ignore any other directive starting with a backtick (e.g., `timescale)
      SkipNewLine(VLOG_DELIMITERS);
    }
    else if (match(nexttok, "reg") || match(nexttok, "always")) {
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

      struct portelement *head, *tail, *scan, *last, *scannext;
      struct objlist *obptr;

      strncpy(modulename, nexttok, 99);
      if (!(*CellStackPtr)) {
	CellDef(fname, filenum);
	PushStack(fname, CellStackPtr);
      }
      
      head = NULL;
      tail = NULL;
      SkipTokComments(VLOG_DELIMITERS);

      // Next token must be '#(' (parameters) or an instance name

      if (match(nexttok, "#(")) {

	 // Read the parameter list
	 SkipTokComments(VLOG_DELIMITERS);

         while (nexttok != NULL) {
	    char *paramname;

	    if (match(nexttok, ")")) {
		SkipTokComments(VLOG_DELIMITERS);
		break;
	    }
	    else if (match(nexttok, ",")) {
		SkipTokComments(VLOG_DELIMITERS);
		continue;
	    }

	    // We need to look for parameters of the type ".name(value)"

	    else if (nexttok[0] == '.') {
		paramname = strsave(nexttok + 1);
	        SkipTokComments(VLOG_DELIMITERS);
		if (!match(nexttok, "(")) {
		    Printf("Error: Expecting parameter value, got %s.\n", nexttok);
		}
	        SkipTokComments(VLOG_DELIMITERS);
		if (match(nexttok, ")")) {
		    Printf("Error: Parameter with no value found.\n");
		}
		else {
	            AddProperty(&kvlist, paramname, nexttok);
	            SkipTokComments(VLOG_DELIMITERS);
		    if (!match(nexttok, ")")) {
		       Printf("Error: Expecting end of parameter value, "
				"got %s.\n", nexttok);
		    }
		}
		FREE(paramname);
	    }
	    SkipTokComments(VLOG_DELIMITERS);
	 }
	 if (!nexttok) {
	    Printf("Error: Still reading module, but got end-of-file.\n");
	    goto skip_endmodule;
	 }
      }

      strncpy(instancename, nexttok, 99);
      /* Printf("Diagnostic:  new instance is %s\n", instancename); */
      SkipTokComments(VLOG_DELIMITERS);

      arraystart = arrayend = -1;
      if (match(nexttok, "[")) {
	 // Handle instance array notation.
	 struct bus wb;
	 if (GetBusTok(&wb) == 0) {
	     arraystart = wb.start;
	     arrayend = wb.end;
	 }
	 SkipTokComments(VLOG_DELIMITERS);
      }

      if (match(nexttok, "(")) {
	 char savetok = (char)0;
	 struct portelement *new_port;

	 // Read the pin list
         while (nexttok != NULL) {
	    SkipTokComments(VLOG_DELIMITERS);
	    // NOTE: Deal with `ifdef et al. properly.  Ignoring for now.
	    while (nexttok[0] == '`') {
		SkipNewLine(VLOG_DELIMITERS);
		SkipTokComments(VLOG_DELIMITERS);
	    }
	    if (match(nexttok, ")")) break;
	    else if (match(nexttok, ",")) continue;

	    // We need to look for pins of the type ".name(value)"

	    if (nexttok[0] != '.') {
	        Printf("Badly formed subcircuit pin line at \"%s\"\n", nexttok);
	        SkipNewLine(VLOG_DELIMITERS);
	    }
	    else {
	       new_port = (struct portelement *)CALLOC(1, sizeof(struct portelement));
	       new_port->name = strsave(nexttok + 1);
	       SkipTokComments(VLOG_DELIMITERS);
	       if (!match(nexttok, "(")) {
	           Printf("Badly formed subcircuit pin line at \"%s\"\n", nexttok);
	           SkipNewLine(VLOG_DELIMITERS);
	       }
	       SkipTokComments(VLOG_PIN_CHECK_DELIMITERS);
	       if (match(nexttok, ")")) {
		  char localnet[100];
		  // Empty parens, so create a new local node
		  savetok = (char)1;
		  if (arraystart != -1) {
		     /* No-connect on an instance array must also be an array */
		     sprintf(localnet, "_noconnect_%d_[%d:%d]", localcount++,
				arraystart, arrayend);
		  }
		  else
		     sprintf(localnet, "_noconnect_%d_", localcount++);
		  new_port->net = strsave(localnet);
	       }
	       else {
		  if (!strcmp(nexttok, "{")) {
		     char *in_line_net = (char *)MALLOC(1);
		     char *new_in_line_net = NULL;
		     *in_line_net = '\0';
		     /* In-line array---read to "}" */
		     while (nexttok) {
			 new_in_line_net = (char *)MALLOC(strlen(in_line_net) +
				    strlen(nexttok) + 1);
			 /* Roundabout way to do realloc() becase there is no REALLOC() */
			 strcpy(new_in_line_net, in_line_net);
			 strcat(new_in_line_net, nexttok);
			 FREE(in_line_net);
			 in_line_net = new_in_line_net;
			 if (!strcmp(nexttok, "}")) break;
			 SkipTokComments(VLOG_PIN_CHECK_DELIMITERS);
		     }
		     if (!nexttok) {
			 Printf("Unterminated net in pin %s\n", in_line_net);
		     }
		     new_port->net = in_line_net;
		  }
		  else
		     new_port->net = strsave(nexttok);

		  /* Read array information along with name;  will be parsed later */
		  SkipTokComments(VLOG_PIN_CHECK_DELIMITERS);
		  if (match(nexttok, "[")) {
		      /* Check for space between name and array identifier */
		      SkipTokComments(VLOG_PIN_NAME_DELIMITERS);
		      if (!match(nexttok, ")")) {
			 char *expnet;
			 expnet = (char *)MALLOC(strlen(new_port->net)
				    + strlen(nexttok) + 2);
			 sprintf(expnet, "%s[%s", new_port->net, nexttok);
			 FREE(new_port->net);
			 new_port->net = expnet;
		      }
		      SkipTokComments(VLOG_DELIMITERS);
		  }

	          if (!match(nexttok, ")")) {
	              Printf("Badly formed subcircuit pin line at \"%s\"\n", nexttok);
	              SkipNewLine(VLOG_DELIMITERS);
		  }
	       }

	       if (head == NULL) head = new_port;
	       else tail->next = new_port;
	       new_port->next = NULL;
	       tail = new_port;
	    }
	 }
      }
      else {
         Printf("Expected to find instance pin block but got \"%s\"\n", nexttok);
      }
      /* Instance should end with a semicolon */
      SkipTokComments(VLOG_DELIMITERS);
      if (!match(nexttok, ";")) {
	 Printf("Expected to find end of instance but got \"%s\"\n", nexttok);
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
	 Fprintf(stderr, "Warning:  Cell %s has no pins\n", modulename);
      }

      /* Check that the module exists.  If not, generate an empty	*/
      /* module entry matching the call.				*/

      tp = LookupCellFile(modulename, filenum);
      if (tp == NULL) {
         struct bus wb, pb;
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
	 tp = CurrentCell;
         EndCell();
	 ReopenCellDef((*CellStackPtr)->cellname, filenum);		/* Reopen */
      }

      /* Work through scan list and expand ports/nets that are arrays */

      last = (struct portelement *)NULL;
      scan = head;
      while (scan != NULL) {
	 int portstart, portend, portnum;

	 scannext = scan->next;
	 portstart = -1;

	 for (obptr = tp->cell; obptr && obptr->type == PORT; obptr = obptr->next) {
	    char *delimiter;
	    if ((delimiter = strrchr(obptr->name, '[')) != NULL) {
	       *delimiter = '\0';
	       if ((*matchfunc)(obptr->name, scan->name)) {
		  if (sscanf(delimiter + 1, "%d", &portnum) == 1) {
		     if (portstart == -1)
			portstart = portnum;
		     else
		        portend = portnum;
		  }
	       }
	       *delimiter = '[';
	    }
	 }
	 if (portstart != -1) {
	    struct bus wb;
	    struct portelement *new_port;
	    char vname[256];
	    int j, result;
	    struct objlist *bobj;
	    char *bptr;
	    int minnet, maxnet, testidx;

	    result = GetBus(scan->net, &wb);
	    if (result == -1) {
		/* CHECK:  THIS CODE SHOULD BE DELETED, IT IS NOT THE ISSUE */
		/* Not bus notation, but check if signal was defined as a bus */
		wb.start = wb.end = -1;
		minnet = maxnet = -1;

		/* Pins should be in index order start->end.  Other nodes */
		/* should be in order start->end by node number.	  */

		for (bobj = CurrentCell->cell; bobj; bobj = bobj->next) {
		    if (bobj->type == PORT) {
			if ((bptr = strchr(bobj->name, '[')) != NULL) {
			    *bptr = '\0';
			    if (!strcmp(bobj->name, scan->net)) {
				*bptr = '[';
				if (wb.start == -1)
				    sscanf(bptr + 1, "%d", &wb.start);
				else
				    sscanf(bptr + 1, "%d", &wb.end);
			    }
			}
		    }
		    else if (bobj->type == NODE) {
			if ((bptr = strchr(bobj->name, '[')) != NULL) {
			    *bptr = '\0';
			    if (!strcmp(bobj->name, scan->net)) {
				if (sscanf(bptr + 1, "%d", &testidx) == 1) {
				    if (minnet == -1) {
					minnet = maxnet = bobj->node;
					wb.start = wb.end = testidx;
				    }
				    else if (bobj->node < minnet) {
					minnet = bobj->node;
					wb.start = testidx;
				    }
				    else if (bobj->node > maxnet) {
					maxnet = bobj->node;
					wb.end = testidx;
				    }
				}
			    }
			    *bptr = '[';
			}
		    }
		}
		if (wb.start != -1) result = 0;
	    }

	    if (result == 0) {
	       if (((wb.start - wb.end) != (portstart - portend)) &&
		   	((wb.start - wb.end) != (portend - portstart))) {
		  if (((wb.start - wb.end) != (arraystart - arrayend)) &&
			((wb.start - wb.end) != (arrayend - arraystart))) {
		     Fprintf(stderr, "Error:  Net %s bus width does not match "
				"port %s bus width.\n", scan->net, scan->name);
		  }
		  // Otherwise, net is bit-sliced across array of instances.
	       }
	       else if (wb.start > wb.end) {
		  char *bptr, *cptr, cchar, *netname;
		  unsigned char is_bundle = 0;
		  struct bus wbb;

		  i = wb.start;
		  j = portstart;

		  netname = scan->net;
		  if (*netname == '{') {
		     is_bundle = 1;
		     netname++;
		     cptr = strchr(netname, ',');
		     if (cptr == NULL) cptr = strchr(netname, '}');
		     if (cptr == NULL) cptr = netname + strlen(netname) - 1;
		     cchar = *cptr;
		     *cptr = '\0';
		  }

		  // Remove indexed part of scan->net
		  if (GetBus(netname, &wbb) == 0) {
		     i = wbb.start;
		     if ((bptr = strchr(netname, '[')) != NULL)
			 *bptr = '\0';
		  }
		  else
		     i = -1;

		  if (is_bundle) *cptr = cchar;	 /* Restore bundle delimiter */
		  
		  while (1) {
	             new_port = (struct portelement *)CALLOC(1,
				sizeof(struct portelement));
	             sprintf(vname, "%s[%d]", scan->name, j);
	             new_port->name = strsave(vname);
		     if (i == -1)
			 sprintf(vname, "%s", netname); 
		     else
			 sprintf(vname, "%s[%d]", netname, i); 
	             new_port->net = strsave(vname);

		     if (last == NULL)
			head = new_port;
		     else
			last->next = new_port;

		     new_port->next = scannext;
		     last = new_port;

		     if (j == portend) break;

		     if (portstart > portend) j--;
		     else j++;
		     if (wbb.start > wbb.end) i--;
		     else i++;

		     if (is_bundle &&
			    ((i == -1) ||
			    ((wbb.start > wbb.end) && (i < wbb.end)) ||
			    ((wbb.start < wbb.end) && (i > wbb.end)))) {
		         if (bptr) *bptr = '[';

			 netname = cptr + 1;
		         cptr = strchr(netname, ',');
			 if (cptr == NULL) cptr = strchr(netname, '}');
			 if (cptr == NULL) cptr = netname + strlen(netname) - 1;
			 cchar = *cptr;
			 *cptr = '\0';

			 if (GetBus(netname, &wbb) == 0) {
			    i = wbb.start;
			    if ((bptr = strchr(netname, '[')) != NULL)
				*bptr = '\0';
			 }
			 else i = -1;

			 *cptr = cchar;	    /* Restore delimiter */
		     }
		  }
		  FREE(scan);
		  scan = last;
	       }
	    }
	    else if (portstart != portend) {
	       Fprintf(stderr, "Error:  Single net %s is connected to bus port %s\n",
			scan->net, scan->name);
	    }
	 }
         last = scan;
	 scan = scannext;
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
            do {
	       struct bus wb;
	       char *obpinname;
	       int obpinidx;

	       // NOTE:  Verilog allows any order of pins, since both the
	       // instance and cell pin names are given.  So for each pin
	       // in obptr (which defines the pin order) , we have to find
	       // the corresponding pin in the scan list.
	
	       obpinname = strrchr(obptr->name, '/');
	       if (!obpinname) break;
	       obpinname++;

	       scan = head;
	       obpinidx = -1;
	       while (scan != NULL) {
		  if (match(obpinname, scan->name)) {
		     break;
		  }
		  scan = scan->next;
	       }
	       if (scan == NULL) {
		  Fprintf(stderr, "Error:  No match in call for pin %s\n", obpinname);
		  break;
	       }

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
		       if (obpinidx == -1) {
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
		       else {
			  // NOTE:  Making unsupportable assumption that
			  // pin and port indexes match---need to fix this!
			  sprintf(pinname, "%s[%d]", scanroot, obpinidx);
	                  if (LookupObject(pinname, CurrentCell) == NULL)
			     Node(pinname);
	                  join(pinname, obptr->name);
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
            } while (obptr != NULL && obptr->type > FIRSTPIN);
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
  struct property *kl = NULL;
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
  InitializeHashTable(&verilogdefs, OBJHASHSIZE);
  definitions = &verilogdefs;

  /* Add the pre-defined key "LVS" to verilogdefs */

  kl = NewProperty();
  kl->merge = MERGE_NONE;
  kl->key = strsave("LVS");
  kl->idx = 0;
  kl->type = PROP_INTEGER;
  kl->slop.ival = 0;
  kl->pdefault.ival = 1;
  HashPtrInstall(kl->key, kl, &verilogdefs);

  ReadVerilogFile(fname, filenum, &CellStack, blackbox);
  CloseParseFile();

  // Cleanup
  while (CellStack != NULL) PopStack(&CellStack);

  RecurseHashTable(&verilogparams, freeprop);
  HashKill(&verilogparams);
  RecurseHashTable(&verilogdefs, freeprop);
  HashKill(&verilogdefs);
  definitions = (struct hashdict *)NULL;

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
	   fprintf(stderr,"Error in Verilog file include: No file %s\n", fname);
	   return;
        }    
     }
  }
  ReadVerilogFile(fname, parent, CellStackPtr, blackbox);
  CloseParseFile();
}

