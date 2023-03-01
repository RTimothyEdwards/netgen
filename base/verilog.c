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
#include "objlist.h"
#include "netfile.h"
#include "print.h"
#include "hash.h"

// See netfile.c for explanation of delimiters.  'X'
// separates single-character delimiters from two-character delimiters.
#define VLOG_DELIMITERS "X///**/#((**)X,;:(){}[]="
#define VLOG_EQUATION_DELIMITERS "X///**/#((**)X,;:(){}[]=+-*/"
#define VLOG_PIN_NAME_DELIMITERS "X///**/(**)X()"
#define VLOG_PIN_CHECK_DELIMITERS "X///**/(**)X,;(){}"

// Used by portelement structure "flags" record.
#define PORT_NOT_FOUND	0
#define PORT_FOUND	1

// Global storage for verilog parameters
struct hashdict verilogparams;
// Global storage for verilog definitions
struct hashdict verilogdefs;
// Record file pointer that is associated with the hash tables
int hashfile = -1;

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
// Find a character c in a string, assuming that string may contain
// verilog names, where anything, including char c, may appear in the
// string if it is a backslash-escaped name.  Only the position of
// character c outside of a verilog name is reported.
//-------------------------------------------------------------------------

char *strvchr(char *string, char c)
{
    char *s;

    for (s = string; *s != '\0'; s++) {
	if (*s == '\\') {
	    while (*s != '\0' && *s != ' ') s++;
	    if (*s == '\0') {
		Fprintf(stderr, "Error:  Verilog backslash-escaped name"
			" does not end with a space.\n");
		break;
	    }
	}
	if (*s == c) return s;
    }
    return NULL;
}

/* Linked list, much like tokstack in netgen.c, but much simpler. */
/* contents are either a value, if oper = 0, or an operator.	  */

struct expr_stack {
    int value;
    char oper;
    struct expr_stack *next;
    struct expr_stack *last;
};

//-------------------------------------------------------------------------
// Evaluate an expression for an array bound.  This is much like
// ReduceOneExpression() in netgen.c, but only handles basic integer
// arithmetic (+,-,*,/) and grouping by parentheses.
//
// Returns 1 if successful, 0 on error.
// Evaluated result is placed in the integer pointed to by "valptr".
//-------------------------------------------------------------------------

int EvalExpr(struct expr_stack **stackptr, int *valptr)
{
    struct expr_stack *texp, *start, *tmp, *stack;
    int modified, value;

    stack = *stackptr;

    /* Most expressions are going to be just one number, so treat
     * that as a special case.
     */
    if ((stack->oper == '\0') && (stack->last == NULL)) {
	*valptr = stack->value;
	FREE(stack);
	*stackptr = NULL;
	return 1;
    }

    /* Move to the end of the stack, which is the beginning of the
     * expression.
     */
    for (start = stack; start->last; start = start->last);

    /* Run passes until no more modifications can be made */
    modified = TRUE;

    while (modified == TRUE) {
	modified = FALSE;

	/* Find any + or - that is used as a sign of a value */

	for (texp = start; texp; texp = texp->next) {
	    if ((texp->oper == '+') || (texp->oper == '-') || (texp->oper == '*') ||
			(texp->oper == '/') || (texp->oper == '(')) {
			
		if ((texp->next != NULL) && (texp->next->next != NULL) &&
			(texp->next->next->oper == '\0')) {
		
		    if (texp->next->oper == '+') {
			/* Remove the unnecessary sign */
			tmp = texp->next;
			texp->next = tmp->next;
			tmp->next->last = texp;
			FREE(tmp);
			modified = TRUE;
		    }
		    else if (texp->next->oper == '-') {
			/* Remove the sign and negate the value */
			tmp = texp->next;
			texp->next->next->value = -texp->next->next->value;
			texp->next = tmp->next;
			tmp->next->last = texp;
			FREE(tmp);
			modified = TRUE;
		    }
		}
	    }
	}

	/* Reduce (a * b) and (a / b) */

	for (texp = start; texp; texp = texp->next) {
	    if ((texp->last != NULL) && (texp->next != NULL) && (texp->oper != '\0')) {
		if ((texp->last->oper == '\0') && (texp->next->oper == '\0')) {
		    if (texp->oper == '*') {
			/* Multiply */
			texp->last->value *= texp->next->value;
			/* Remove two items from the stack */
			tmp = texp;
			texp = texp->last;
			texp->next = tmp->next->next;
			if (tmp->next->next) tmp->next->next->last = texp;
			FREE(tmp->next);
			FREE(tmp);
			modified = TRUE;
		    }
		    if (texp->oper == '/') {
			/* Divide */
			texp->last->value /= texp->next->value;
			/* Remove two items from the stack */
			tmp = texp;
			texp = texp->last;
			texp->next = tmp->next->next;
			if (tmp->next->next) tmp->next->next->last = texp;
			FREE(tmp->next);
			FREE(tmp);
			modified = TRUE;
		    }
		} 
	    }
	}

	/* Reduce (a + b) and (a - b) */

	for (texp = start; texp; texp = texp->next) {
	    if ((texp->last != NULL) && (texp->next != NULL) && (texp->oper != '\0')) {
		if ((texp->last->oper == '\0') && (texp->next->oper == '\0')) {
		    if (texp->oper == '-') {
			/* Subtract */
			texp->last->value -= texp->next->value;
			/* Remove two items from the stack */
			tmp = texp;
			texp = texp->last;
			texp->next = tmp->next->next;
			if (tmp->next->next) tmp->next->next->last = texp;
			FREE(tmp->next);
			FREE(tmp);
			modified = TRUE;
		    }
		    if (texp->oper == '+') {
			/* Add */
			texp->last->value += texp->next->value;
			/* Remove two items from the stack */
			tmp = texp;
			texp = texp->last;
			texp->next = tmp->next->next;
			if (tmp->next->next) tmp->next->next->last = texp;
			FREE(tmp->next);
			FREE(tmp);
			modified = TRUE;
		    }
		} 
	    }
	}

	/* Reduce (a) */

	for (texp = start; texp; texp = texp->next) {
	    if ((texp->last != NULL) && (texp->next != NULL) && (texp->oper == '\0')) {
		if ((texp->last->oper == '(') && (texp->next->oper == ')')) {
		    tmp = texp->last;
		    texp->last->oper = '\0';
		    texp->last->value = texp->value;
		    texp->last->next = texp->next->next;
		    if (texp->next->next) texp->next->next->last = texp->last;
		    FREE(texp->next);
		    FREE(texp);
		    texp = tmp;
		    modified = TRUE;
		}
	    }
	}
    }

    /* If only one numerical item remains, then place it in valptr and return 1 */
    texp = start;
    if ((texp->oper == '\0') && (texp->next == NULL)) {
	*valptr = texp->value;
	FREE(texp);
	*stackptr = NULL;
	return 1;
    }

    /* Clean up the stack before returning error status */
    while (texp) {
	tmp = texp;
	texp = texp->next;
	FREE(tmp);
    }
    *stackptr = NULL;
    return 0;
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
    int result, start, end, value;
    char oper;
    struct property *kl = NULL;
    struct expr_stack *stack, *newexp;

    if (wb == NULL) return 0;

    wb->start = -1;
    wb->end = -1;
    stack = NULL;

    /* Parse a value for array bounds [a : b], including possible use	*/
    /* of parameters, definitions, and basic arithmetic.		*/

    if (match(nexttok, "[")) {
	start = end = -1;
	oper = '\0';
	while (nexttok) {
	    SkipTokComments(VLOG_EQUATION_DELIMITERS);
    	    if (match(nexttok, "]")) {
	    	result = 1;
		if (stack == NULL) {
		    Printf("Empty array found.\n");
		    return 1;
		}
		if (EvalExpr(&stack, &end) != 1) {
		    Printf("Bad expression found in array.\n");
		    return 1;
		}
		if (start == -1) start = end;	// Single bit
		break;
	    }
	    if (match(nexttok, ":")) {
		if (stack == NULL) {
		    Printf("Empty array start found.\n");
		    return 1;
		}
		if (EvalExpr(&stack, &start) != 1) {
		    Printf("Bad expression found in array.\n");
		    return 1;
		}
		continue;
	    }
	    else if (match(nexttok, "+") || match(nexttok, "-")
	    		|| match(nexttok, "*") || match(nexttok, "/")
	    		|| match(nexttok, "(") || match(nexttok, ")")) {
		newexp = (struct expr_stack *)MALLOC(sizeof(struct expr_stack));
		newexp->oper = *nexttok;
		newexp->value = 0;
		newexp->next = NULL;
		newexp->last = stack;
		if (stack) stack->next = newexp;
		stack = newexp;
		continue;
	    }
	    if ((result = sscanf(nexttok, "%d", &value)) != 1) {

		// Is name in the parameter list?
		kl = (struct property *)HashLookup(nexttok, &verilogparams);
		if (kl == NULL) {
		    Printf("Array value %s is not a number or a parameter.\n",
				nexttok);
		    value = 0;
		    break;
		}
		else {
		    if (kl->type == PROP_STRING) {
			result = sscanf(kl->pdefault.string, "%d", &value);
			if (result != 1) {
		            Printf("Parameter %s has value %s that cannot be parsed"
					" as an integer.\n",
					nexttok, kl->pdefault.string);
		    	    value = 0;
			    break;
			}
		    }
		    else if (kl->type == PROP_INTEGER) {
			value = kl->pdefault.ival;
		    }
		    else if (kl->type == PROP_DOUBLE) {
			value = (int)kl->pdefault.dval;
			if ((double)value != kl->pdefault.dval) {
		            Printf("Parameter %s has value %g that cannot be parsed"
					" as an integer.\n",
					nexttok, kl->pdefault.dval);
		    	    value = 0;
			    break;
			}
		    }
		    else {
			Printf("Parameter %s has unknown type; don't know how"
				" to parse.\n", nexttok);
		    	value = 0;
			break;
		    }
		}
	    }

	    newexp = (struct expr_stack *)MALLOC(sizeof(struct expr_stack));
	    newexp->oper = '\0';
	    newexp->value = value;
	    newexp->next = NULL;
	    newexp->last = stack;
	    if (stack) stack->next = newexp;
	    stack = newexp;
	}

	wb->start = start;
	wb->end = end;

	/* In case of error, stack may need cleaning up */
	while (stack != NULL) {
	    newexp = stack;
	    stack = stack->last;
	    FREE(newexp);
	}

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
    char *colonptr, *brackstart, *brackend, *sigend, sdelim, *aastr;
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
	    sigend = strvchr(astr, ',');
	    if (sigend == NULL) sigend = strvchr(astr, '}');
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

    // Delimiters may appear in backslash-escaped names. . . ignore these.
    aastr = astr;
    if (*aastr == '\\') {
	aastr++;
	while (*aastr != ' ' && *aastr != '\\' && *aastr != '\0') aastr++;
    }

    brackstart = strvchr(aastr, '[');
    if (brackstart != NULL) {
	brackend = strvchr(aastr, ']');
	if (brackend == NULL) {
	    Printf("Badly formed array notation \"%s\"\n", astr);
	    return 1;
	}
	*brackend = '\0';
	colonptr = strvchr(aastr, ':');
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
   struct objlist *myLastPort, *object_it, *myNextObject;

   if (CurrentCell == NULL) return;

   myLastPort = NULL;

   /* Reorder objects so that all ports come first, before nodes, because
    * parts of the code depend on it.
    */

   for (object_it = CurrentCell->cell; object_it && object_it->type <= 0;
		object_it = myNextObject ) {

       myNextObject = object_it->next;
       if (!myNextObject)	// end of list
           continue;

       if (myLastPort == NULL) {
           if (object_it->type == PORT) {
               myLastPort = object_it;	    // port at begining of list
               myNextObject = object_it;    // otherwise skips one
           }
           else if (myNextObject->type == PORT) {
               object_it->next = myNextObject->next;
               myNextObject->next = CurrentCell->cell;
               CurrentCell->cell = myNextObject;
               myLastPort = myNextObject;
           }
       }
       else if (myNextObject->type == PORT) {
	   object_it->next = myNextObject->next;
	   myNextObject->next = myLastPort->next;
	   myLastPort->next = myNextObject;
	   myLastPort = myNextObject;
       }
   }

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

/* Forward declarations */
extern void IncludeVerilog(char *, int, struct cellstack **, int);

/* External declarations (from spice.c) */
extern void PushStack(char *cellname, struct cellstack **top);
extern void PopStack(struct cellstack **top);

/*------------------------------------------------------*/
/* Callback routine for FindInstanceOf()		*/
/* NOTE:  This casts a (struct objlist) pointer to a	*/
/* (struct nlist) pointer for the purpose of using	*/
/* RecurseCellHashTable2().  FindInstanceOf() casts it	*/
/* back into a (struct objlist) pointer.		*/
/*------------------------------------------------------*/

struct nlist *findInstance(struct hashlist *p, void *clientdata)
{
    struct nlist *ptr;
    struct objlist *ob;
    struct nlist *tref = (struct nlist *)clientdata;

    ptr = (struct nlist *)(p->ptr);
    if (ptr->file != tref->file) return NULL;

    ob = LookupInstance(tref->name, ptr);
    return (struct nlist *)ob;
}

/*------------------------------------------------------*/
/* Routine to find the first instance of a cell		*/
/*------------------------------------------------------*/

struct objlist *FindInstanceOf(struct nlist *tc)
{
    return (struct objlist *)RecurseCellHashTable2(findInstance, (void *)tc);
}

/*------------------------------------------------------*/
/* Given a reference cell pointer tref and a port name	*/
/* portname, check if portname is a port of tref.  If	*/
/* not, then call Port() to add one.  If tref is NULL,	*/
/* then always add the port.				*/
/*------------------------------------------------------*/

void CheckPort(struct objlist *tref, char *portname)
{
    struct objlist *ob;

    if (tref != NULL) {
	for (ob = CurrentCell->cell; ob && (ob->type == PORT); ob = ob->next) {
	    if ((*matchfunc)(ob->name, portname))
		return;
	}
    }
    Port(portname);
}

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
  char inst[MAX_STR_LEN], model[MAX_STR_LEN], instname[MAX_STR_LEN], portname[MAX_STR_LEN], pkey[MAX_STR_LEN];
  struct nlist *tp;
  struct objlist *parent, *sobj, *nobj, *lobj, *pobj, *cref;

  inst[MAX_STR_LEN-1] = '\0';
  model[MAX_STR_LEN-1] = '\0';
  instname[MAX_STR_LEN-1] = '\0';
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

    /* Handle parameters by treating as a localparam or definition.	*/
    /* Currently anything other than a constant value is not handled	*/
    /* and so will flag a warning.					*/

    else if (match(nexttok, "parameter") || match(nexttok, "localparam")) {
	char *paramkey = NULL;
	char *paramval = NULL;

	// Pick up key = value pairs and store in current cell.  Look only
	// at the keyword before "=".  Then set the definition as everything
	// remaining in the line, excluding comments, until the end-of-statement

	while (nexttok != NULL)
	{
	    struct property *kl = NULL;

	    /* Parse for parameters used in expressions.  Save	*/
	    /* parameters in the "verilogparams" hash table.	*/

	    SkipTok(VLOG_DELIMITERS);
	    if ((nexttok == NULL) || (nexttok[0] == '\0')) break;
	    if (match(nexttok, "=")) {
		/* Pick up remainder of statement */
		while (nexttok != NULL) {
		    SkipTokNoNewline("X///**/X;,");
		    if (nexttok == NULL) break;
		    if (match(nexttok, ";") || match(nexttok, ",")) break;
		    if (paramval == NULL) paramval = strsave(nexttok);
		    else {
			char *paramlast;
			/* Append nexttok to paramval */
			paramlast = paramval;
			paramval = (char *)MALLOC(strlen(paramlast) + strlen(nexttok)
				+ 2);
			sprintf(paramval, "%s %s", paramlast, nexttok);
			FREE(paramlast);
		    }
		}

		kl = NewProperty();
		kl->key = strsave(paramkey);
		kl->idx = 0;
		kl->merge = MERGE_NONE;

		if (ConvertStringToInteger(paramval, &ival) == 1) {
		    kl->type = PROP_INTEGER;
		    kl->slop.ival = 0;
		    kl->pdefault.ival = ival;
		}
		else if (ConvertStringToFloat(paramval, &dval) == 1) {
		    kl->type = PROP_DOUBLE;
		    kl->slop.dval = 0.01;
		    kl->pdefault.dval = dval;
		}
		else {
		    kl->type = PROP_STRING;
		    kl->slop.dval = 0.0;
		    kl->pdefault.string = strsave(paramval);
		}

		HashPtrInstall(paramkey, kl, &verilogparams);
		FREE(paramval);
		paramval = NULL;

		if ((nexttok == NULL) || match(nexttok, ";")) break;
	    }
	    else {
		if (paramkey != NULL) FREE(paramkey);
		paramkey = strsave(nexttok);
	    }
	}
	if (paramval != NULL) FREE(paramval);
	if (paramkey != NULL) FREE(paramkey);
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
      cref = NULL;

      /* Save pointer to current cell */
      if (CurrentCell != NULL)
         parent = CurrentCell->cell;
      else
	 parent = NULL;

      /* Check for existence of the cell.  We may need to rename it. */

      snprintf(model, MAX_STR_LEN-1, "%s", nexttok);
      tp = LookupCellFile(nexttok, filenum);
      hasports = (char)0;

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
      else if (tp != NULL) {	/* Cell exists, but as a placeholder */
	 struct nlist *tptmp = NULL;
	 char ctemp[8];
	 int n = 0;

	 /* This redefines a placeholder module to an unused temporary cell name */
         while (1) {
	    sprintf(ctemp, "%d", n);
            tptmp = LookupCellFile(ctemp, filenum);
	    if (tptmp == NULL) break;
	    n++;
	 }
	 CellRehash(nexttok, ctemp, filenum);
	 tptmp = LookupCellFile(ctemp, filenum);

	 /* Create a new module definition */
	 CellDef(model, filenum);
	 tp = LookupCellFile(model, filenum);

	 /* Find an instance of this module in the netlist */
	 cref = FindInstanceOf(tp);
	 if ((cref != NULL) && (cref->name != NULL)) {
	    hasports = (char)1;
	    /* Copy ports from the original parent cell to the new parent cell */
	    for (pobj = tptmp->cell; pobj && (pobj->type == PORT); pobj = pobj->next)
		Port(pobj->name);
	 }
	 /* Remove the original cell definition */
	 FreePorts(ctemp);
	 CellDelete(ctemp, filenum);	/* This removes any PLACEHOLDER flag */
      }
      else if (tp == NULL) {	/* Completely new cell, no name conflict */
         CellDef(model, filenum);
         tp = LookupCellFile(model, filenum);
      }

      inlined_decls = (char)0;

      if (tp != NULL) {
	 struct bus wb, *nb;

	 tp->flags |= CELL_VERILOG;
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
			      CheckPort(cref, nexttok);
			   }
			}
			else {
			   if (wb.start != -1) {
			      if (wb.start > wb.end) {
				 for (i = wb.start; i >= wb.end; i--) {
				    sprintf(portname, "%s[%d]", nexttok, i);
				    CheckPort(cref, portname);
				 }
			      }
			      else {
				 for (i = wb.start; i <= wb.end; i++) {
				    sprintf(portname, "%s[%d]", nexttok, i);
				    CheckPort(cref, portname);
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
			      CheckPort(cref, nexttok);
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
	struct bus wb, *nb;
 
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
		    CheckPort(cref, nexttok);
		}
	    }
	    else if (!match(nexttok, ",")) {
		if (wb.start != -1) {
		    if (wb.start > wb.end) {
			for (i = wb.start; i >= wb.end; i--) {
			    sprintf(portname, "%s[%d]", nexttok, i);
			    CheckPort(cref, portname);
			}
		    }
		    else {
			for (i = wb.start; i <= wb.end; i++) {
			    sprintf(portname, "%s[%d]", nexttok, i);
			    CheckPort(cref, portname);
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
		    CheckPort(cref, nexttok);
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
      cref = NULL;

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
         char *toks;

	 /* Treat the parameter as a string; BUT pull everything to */
	 /* EOL, not just the current token.			    */
	 toks = GetLineAtTok();

      	 kl->type = PROP_STRING;
	 kl->pdefault.string = strsave(toks);
         kl->slop.dval = 0.0;

	 SkipNewLine(VLOG_DELIMITERS);
      }
      if (kl) HashPtrInstall(kl->key, kl, &verilogdefs);
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
    else if (match(nexttok, "real") || match(nexttok, "integer")) {
	Printf("Ignoring '%s' in module '%s'\n", nexttok, model);
	/* Do not skip to end of module, as these can be in the middle of   */
	/* I/O assignments, which need to be parsed.			    */
	while (!match(nexttok, ";")) SkipTok("X///**/X,;");
	continue;
    }
    else if (match(nexttok, "wire") || match(nexttok, "assign")) {	/* wire = node */
	struct bus wb, wb2, *nb;
	char nodename[MAX_STR_LEN], noderoot[MAX_STR_LEN];
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
		char *aptr = strvchr(nexttok, '[');
		if (aptr != NULL) {
		    *aptr = '\0';
		    /* Find object of first net in bus */
		    strcpy(noderoot, nexttok);
		    sprintf(nodename, "%s[%d]", nexttok, wb.start);
		    lhs = LookupObject(nodename, CurrentCell);
		    *aptr = '[';
		}
		else {
		    strcpy(noderoot, nexttok);
		    /* Set LHS to the start of the vector */
		    sprintf(nodename, "%s[%d]", nexttok, wb.start);
		    lhs = LookupObject(nodename, CurrentCell);
		}
	    }
	    else {
		lhs = LookupObject(nexttok, CurrentCell);
		strcpy(noderoot, nexttok);
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
	    char assignname[MAX_STR_LEN], assignroot[MAX_STR_LEN];

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
			char *aptr = strvchr(nexttok, '[');
			j = wb2.start;
			if (aptr != NULL) {
			    *aptr = '\0';
			    strcpy(assignroot, nexttok);
			    sprintf(assignname, "%s[%d]", nexttok, j);
			    rhs = LookupObject(assignname, CurrentCell);
			    *aptr = '[';
			}
			else
			    strcpy(assignroot, nexttok);
		    }
		    else {
			j = -1;
			rhs = LookupObject(nexttok, CurrentCell);
		    }
		    if ((lhs == NULL) || (rhs == NULL)) {
			if (rhs != NULL) {
			    Printf("Improper assignment;  left-hand side cannot "
					"be parsed.\n");
			    if (j != -1)
			    	Printf("Right-hand side is \"%s\".\n", assignroot);
			    else
			    	Printf("Right-hand side is \"%s\".\n", rhs->name);
			    Printf("Improper expression is \"%s\".\n", nexttok);
			    break;
			}
			if (lhs != NULL) {
			    Printf("Improper assignment;  right-hand side cannot "
					"be parsed.\n");
			    if (i != -1)
			    	Printf("Left-hand side is \"%s\".\n", noderoot);
			    else
			    	Printf("Left-hand side is \"%s\".\n", lhs->name);
			    Printf("Improper expression is \"%s\".\n", nexttok);
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
			    snprintf(nodename, MAX_STR_LEN, "%s[%d]", noderoot, i);
			else
			    strncpy(nodename, MAX_STR_LEN, lhs->name);
			if (j != -1)
			    snprintf(assignname, MAX_STR_LEN, "%s[%d]", assignroot, j);
			else
			    strncpy(assignname, MAX_STR_LEN, rhs->name);

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
    else if (match(nexttok, "reg") || match(nexttok, "always") ||
	    match(nexttok, "specify") || match(nexttok, "initial")) {
      Printf("Behavioral keyword '%s' found in source.\n", nexttok);
      Printf("Module '%s' is not structural verilog, making black-box.\n", model);
      // To be done:  Remove any contents (but may not be necessary)
      // Recast as module
      SetClass(CLASS_MODULE);
      goto skip_endmodule;
    }
    else {	/* module instances */
      char instancename[MAX_STR_LEN], modulename[MAX_STR_LEN];
      int itype, arraystart, arrayend, arraymax, arraymin;
      char ignore;

      instancename[MAX_STR_LEN-1] = '\0';
      modulename[MAX_STR_LEN-1] = '\0';

      struct portelement {
	char *name;	// Name of port in subcell
	char *net;	// Name of net connecting to port in the parent
	int   width;	// Width of port, if port is a bus
	char  flags;	// Used for marking if port was added into netlist
	struct portelement *next;
      };

      struct portelement *head, *tail, *scan, *last, *scannext;
      struct objlist *obptr;

      strncpy(modulename, nexttok, MAX_STR_LEN-1);

      /* If module name is a verilog primitive, then treat the module as a  */
      /* black box (this is not a complete list.  Preferable to use hash    */
      /* function instead of lots of strcmp() calls).			    */

      if (!strcmp(modulename, "buf") || !strcmp(modulename, "notif1") ||
	    !strcmp(modulename, "not") || !strcmp(modulename, "and") ||
	    !strcmp(modulename, "or") || !strcmp(modulename, "bufif0") ||
	    !strcmp(modulename, "bufif1") || !strcmp(modulename, "notif0")) {

         Printf("Module contains verilog primitive '%s'.\n", nexttok);
         Printf("Module '%s' is not structural verilog, making black-box.\n", model);
	 SetClass(CLASS_MODULE);
	 goto skip_endmodule;
      }

      if (!(*CellStackPtr)) {
	CellDef(fname, filenum);
	PushStack(fname, CellStackPtr);
      }
      
      SkipTokComments(VLOG_DELIMITERS);

nextinst:
      ignore = FALSE;
      head = NULL;
      tail = NULL;

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

      strncpy(instancename, nexttok, MAX_STR_LEN-1);
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
	    SkipTokComments(VLOG_PIN_CHECK_DELIMITERS);
	    if (match(nexttok, ")")) break;
	    else if (match(nexttok, ",")) continue;

	    // We need to look for pins of the type ".name(value)"

	    if (nexttok[0] != '.') {
	        Printf("Warning:  Ignoring subcircuit with no pin names "
			    "at \"%s\"\n", nexttok);
		InputParseError(stderr);
		while (nexttok != NULL) {
		    SkipTokComments(VLOG_DELIMITERS);
		    if (match(nexttok, ";")) break;
		}
		ignore = TRUE;
		break;
	    }
	    else {
	       new_port = (struct portelement *)CALLOC(1, sizeof(struct portelement));
	       new_port->name = strsave(nexttok + 1);
	       new_port->width = -1;
	       new_port->flags = PORT_NOT_FOUND;
	       SkipTokComments(VLOG_DELIMITERS);
	       if (!match(nexttok, "(")) {
	           Printf("Badly formed subcircuit pin line at \"%s\"\n", nexttok);
	           SkipNewLine(VLOG_DELIMITERS);
	       }
	       SkipTokComments(VLOG_PIN_CHECK_DELIMITERS);
	       if (match(nexttok, ")")) {
		  char localnet[MAX_STR_LEN];
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
		     char *wire_bundle = (char *)MALLOC(1);
		     char *new_wire_bundle = NULL;
		     *wire_bundle = '\0';
		     /* Wire bundle---read to "}" */
		     while (nexttok) {
			 new_wire_bundle = (char *)MALLOC(strlen(wire_bundle) +
				    strlen(nexttok) + 1);
			 /* Roundabout way to do realloc() becase there is no REALLOC() */
			 strcpy(new_wire_bundle, wire_bundle);
			 strcat(new_wire_bundle, nexttok);
			 FREE(wire_bundle);
			 wire_bundle = new_wire_bundle;
			 if (!strcmp(nexttok, "}")) break;
			 SkipTokComments(VLOG_PIN_CHECK_DELIMITERS);
		     }
		     if (!nexttok) {
			 Printf("Unterminated net in pin %s\n", wire_bundle);
		     }
		     new_port->net = wire_bundle;
		  }
		  else if (nexttok[0] == '~' || nexttok[0] == '!' || nexttok[0] == '-') {
		     /* All of these imply that the signal is logically manipulated */
		     /* in turn implying behavioral code.			    */
		     Printf("Module '%s' is not structural verilog, "
				    "making black-box.\n", model);
		     SetClass(CLASS_MODULE);
		     goto skip_endmodule;
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
      if (ignore == TRUE) continue;	/* moving along. . . */

      /* Verilog allows multiple instances of a single cell type to be chained	*/
      /* together with commas.							*/

      SkipTokComments(VLOG_DELIMITERS);
      if (match(nexttok, ",")) {
	 goto nextinst;
      }

      /* Otherwise, instance must end with a semicolon */
    
      else if (!match(nexttok, ";")) {
	 Printf("Expected to find end of instance but got \"%s\"\n", nexttok);
	 InputParseError(stderr);
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
	 char defport[MAX_STR_LEN];

	 Fprintf(stdout, "Creating placeholder cell definition for "
			"module %s.\n", modulename);
	 CellDef(modulename, filenum);
	 CurrentCell->flags |= CELL_PLACEHOLDER;
         for (scan = head; scan != NULL; scan = scan->next) {
	    // Check if net name is a wire bus or portion of a bus
	    if (GetBus(scan->net, &wb) == 0) {
		int range;

		// This takes care of three situations:
		// (1) The signal bus length matches the number of instances:
		//     apply one signal per instance.
		// (2) The signal bus length is a multiple of the number of instances:
		//     apply a signal sub-bus to each instance.
		// (3) The number of instances is a multiple of the signal bus length:
		//     apply the same signal to each instance.

		if ((arrayend - arraystart) == (wb.end - wb.start)) {
		    // Net is a bus, but net is split over arrayed instances
		    Port(scan->name);
		}
		else if (wb.start > wb.end) {
		    if ((arraystart - arrayend) > (wb.start - wb.end))
			range = (((arraystart - arrayend) + 1) /
				    ((wb.start - wb.end) + 1)) - 1;
		    else
			range = (((wb.start - wb.end) + 1) /
				    ((arraystart - arrayend) + 1)) - 1;

		    for (i = range; i >= 0; i--) {
			sprintf(defport, "%s[%d]", scan->name, i);
			Port(defport);
		    }
		}
		else {
		    if ((arrayend - arraystart) >  (wb.end - wb.start))
			range = (((arrayend - arraystart) + 1) /
				    ((wb.end - wb.start) + 1)) - 1;
		    else
			range = (((wb.end - wb.start) + 1) /
				    ((arrayend - arraystart) + 1)) - 1;

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
	    char vname[MAX_STR_LEN];
	    int j, result;
	    struct objlist *bobj;
	    char *bptr;
	    int minnet, maxnet, testidx, width;

	    width = portstart - portend;
	    if (width < 0) width = -width;
	    width++;
	    scan->width = width;

	    result = GetBus(scan->net, &wb);
	    if (result == 0) {
	       int match = 0;
	       int wblen, arraylen;

	       arraylen = arraystart - arrayend;
	       wblen = wb.start - wb.end;

	       if (arraylen < 0) arraylen = -arraylen;
	       if (wblen < 0) wblen = -wblen;

	       arraylen++;
	       wblen++;

	       if ((scan->width * arraylen) == wblen) match = 1;
	       else if (wblen == scan->width) match = 1;
	       else if (wblen == arraylen) match = 1;
	       else {
		  Fprintf(stderr, "Warning:  Net %s bus width (%d) does not match "
				"port %s bus width (%d) or array width (%d).\n",
				scan->net, wblen, scan->name, scan->width, arraylen);
	       }

	       // Net is bit-sliced across array of instances.

	       if (wb.start > wb.end) {
		  char *bptr = NULL, *cptr = NULL, cchar, *netname;
		  unsigned char is_bundle = 0;
		  struct bus wbb;

		  i = wb.start;
		  j = portstart;

		  netname = scan->net;
		  if (*netname == '{') {
		     is_bundle = 1;
		     netname++;
		     cptr = strvchr(netname, ',');
		     if (cptr == NULL) cptr = strvchr(netname, '}');
		     if (cptr == NULL) cptr = netname + strlen(netname) - 1;
		     cchar = *cptr;
		     *cptr = '\0';
		  }

		  // Remove indexed part of scan->net
		  if (GetBus(netname, &wbb) == 0) {
		     i = wbb.start;
		     if ((bptr = strvchr(netname, '[')) != NULL)
			 *bptr = '\0';
		  }
		  else
		     i = -1;

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
		     new_port->width = scan->width;

		     if (last == NULL)
			head = new_port;
		     else
			last->next = new_port;

		     new_port->next = scannext;
		     last = new_port;

		     if (j == portend) break;

		     if (portstart > portend) j--;
		     else j++;
		     if (i != -1) {
			if (wbb.start > wbb.end) i--;
			else i++;
		     }

		     if (is_bundle &&
			    ((i == -1) ||
			    ((wbb.start > wbb.end) && (i < wbb.end)) ||
			    ((wbb.start <= wbb.end) && (i > wbb.end)))) {
		         if (bptr) *bptr = '[';

			 netname = cptr + 1;
			 if (cptr) *cptr = cchar; /* Restore previous bundle delimiter */
		         cptr = strvchr(netname, ',');
			 if (cptr == NULL) cptr = strvchr(netname, '}');
			 if (cptr == NULL) cptr = netname + strlen(netname) - 1;
			 cchar = *cptr;
			 *cptr = '\0';

			 if (GetBus(netname, &wbb) == 0) {
			    i = wbb.start;
			    if ((bptr = strvchr(netname, '[')) != NULL)
				*bptr = '\0';
			 }
			 else i = -1;
		     }
		  }
		  FREE(scan);
		  scan = last;
		  if (cptr) *cptr = cchar; /* Restore bundle delimiter */
	       }
	    }
	    else if (portstart != portend) {
	       /* If the name starts with "_noconnect_", then it was an
		* auto-generated name that incorrectly assumed a single-bit
		* wide signal because it did not have a module prototype.
		* Rewrite the noconnect name as an array.  This just means
		* that the verilog had an empty list "()" and the initial
		* assumption was wrong.  If not all instances agree on the
		* size, then this will eventually generate an error.
		*/
	       if (!strncmp(scan->net, "_noconnect_", 11) &&
			(strchr(scan->net, '\[') == NULL) && (width > 1)) {
		  char *savenet = scan->net;
		  char *newnet = MALLOC(strlen(scan->net) + 12);
		  sprintf(newnet, "%s[%d:0]", scan->net, scan->width - 1);
		  FREE(savenet);
		  scan->net = newnet;
		  continue;	/* Re-process this entry */
	       }
	       else
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
	 char locinst[MAX_STR_LEN];

         if (i != -1)
	    sprintf(locinst, "%s[%d]", instancename, i);
	 else
	    strcpy(locinst, instancename);
	 Instance(modulename, locinst);
	 LinkProperties(modulename, kvlist);

         obptr = LookupInstance(locinst, CurrentCell);
         if (obptr != NULL) {
            do {
	       struct bus wb, wb2;
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
		     scan->flags |= PORT_FOUND;
		     break;
		  }
		  scan = scan->next;
	       }
	       if (scan == NULL) {
		  char localnet[MAX_STR_LEN];

		  /* Assume an implicit unconnected pin */
		  sprintf(localnet, "_noconnect_%d_", localcount++);
		  Node(localnet);
		  join(localnet, obptr->name);
		  Fprintf(stdout,
			 "Note:  Implicit pin %s in instance %s of %s in cell %s\n",
			 obpinname, locinst, modulename, CurrentCell->name);
	       }
	       else if (GetBus(scan->net, &wb) == 0) {
		   char *bptr2;
		   char *scanroot;
		   scanroot = strsave(scan->net);
		   brackptr = strvchr(scanroot, '[');
		   if (brackptr) *brackptr = '\0';

		   if (arraystart == -1) {
		       // Port may be an array
		       int range;
		       char pinname[MAX_STR_LEN];

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
		       char netname[MAX_STR_LEN];
		       int slice, portlen, siglen;

		       /* Get the array size of the port for bit slicing */
		       portlen = (scan->width < 0) ? 1 : scan->width;

		       /* Get the full array size of the connecting bus */
		       GetBus(scanroot, &wb2);
		       siglen = wb2.start - wb2.end;
		       if (siglen < 0) siglen = -siglen;
		       siglen++;

		       // If signal array is smaller than the portlength *
		       // length of instance array, then the signal wraps.

		       if (wb2.start >= wb2.end && arraystart >= arrayend) {
			   slice = wb.start - (arraystart - i) * portlen;
			   while (slice < wb2.end) slice += siglen;
		       }
		       else if (wb2.start < wb2.end && arraystart > arrayend) {
			   slice = wb.start + (arraystart - i) * portlen;
			   while (slice > wb2.end) slice -= siglen;
		       }
		       else if (wb2.start > wb2.end && arraystart < arrayend) {
			   slice = wb.start - (arraystart + i) * portlen;
			   while (slice < wb2.end) slice += siglen;
		       }
		       else { // (wb2.start < wb2.end && arraystart < arrayend)
			   slice = wb.start + (arraystart + i) * portlen;
			   while (slice > wb2.end) slice -= siglen;
		       }

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

	       /* Before exiting the loop, check if all ports in the	*/
	       /* scan list were handled.				*/

	       if ((obptr->next == NULL) || (obptr->next->type <= FIRSTPIN)) {
	          for (scan = head; scan; scan = scan->next) {
	             if (!(scan->flags & PORT_FOUND)) {
		        if (tp->flags & CELL_PLACEHOLDER) {
			   char tempname[MAX_STR_LEN];
			   int maxnode;

		           /* This pin was probably implicit in the first call	*/
		           /* and so it needs to be added to the definition.	*/

			   ReopenCellDef(modulename, filenum);
		           Port(scan->name);
			   ReopenCellDef((*CellStackPtr)->cellname, filenum);

			   /* obptr->next now gets the new port.  Update the	*/
			   /* port number, and copy class and instance name.	*/
			   nobj = GetObject();
			   sprintf(tempname, "%s%s%s", obptr->instance.name,
						SEPARATOR, scan->name);
			   nobj->name = strsave(tempname);
			   nobj->model.class = strsave(obptr->model.class);
			   nobj->instance.name = strsave(obptr->instance.name);
			   nobj->type = obptr->type + 1;
			   nobj->next = obptr->next;
			   nobj->node = -1;
			   obptr->next = nobj;
      			   HashPtrInstall(nobj->name, nobj, &(CurrentCell->objdict));

			   /* Ensure that CurrentTail is correct */
			   if (obptr == CurrentTail) CurrentTail = nobj;

	           	   if (LookupObject(scan->net, CurrentCell) == NULL)
			      Node(scan->net);
	           	   join(scan->net, nobj->name);
	             	   scan->flags |= PORT_FOUND;

			   /* Now any previous instance of the same cell must	*/
			   /* insert the same additional pin as a no-connect.	*/
			   /* NOTE:  This should be running a callback on all	*/
			   /* cells in the file, not just CurrentCell.		*/

			   for (sobj = CurrentCell->cell; sobj && (sobj != obptr);
					sobj = sobj->next) {
			      if (sobj->type == FIRSTPIN) {
				 if (match(sobj->model.class, obptr->model.class)) {
				    while (sobj->next && (sobj->next->type > FIRSTPIN))
				       sobj = sobj->next;
				    /* Stop when reaching the current instance */
				    if (sobj->type == obptr->type + 1) break;
				    nobj = GetObject();
				    sprintf(tempname, "%s%s%s", sobj->instance.name,
						SEPARATOR, scan->name);
				    nobj->name = strsave(tempname);
				    nobj->model.class = strsave(sobj->model.class);
				    nobj->instance.name = strsave(sobj->instance.name);
				    nobj->type = obptr->type + 1;
				    nobj->node = -1;
				    nobj->next = sobj->next;
				    sobj->next = nobj;
      				    HashPtrInstall(nobj->name, nobj,
							&(CurrentCell->objdict));

		  	   	    sprintf(tempname, "_noconnect_%d_", localcount++);
		  	   	    Node(tempname);
		  	   	    join(tempname, nobj->name);
		  	   	    Fprintf(stdout, "Note:  Implicit pin %s in instance "
						"%s of %s in cell %s\n",
			 			scan->name, sobj->instance.name,
						modulename, CurrentCell->name);
				 }
			      }
			   }
			   obptr = obptr->next;
		        }
		        else {
		           Fprintf(stderr, "Error:  Instance %s has pin %s which is "
					"not in the %s cell definition.\n",
					locinst, scan->name, modulename);
		        }
	             }
	          }
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

    if (strchr(fname, '.') == NULL) {
      char name[1024];
      SetExtension(name, fname, VERILOG_EXTENSION);
      if ((filenum = OpenParseFile(name, *fnum)) < 0) {
        Fprintf(stderr, "Error in Verilog file read: No file %s\n", name);
        *fnum = filenum;
        return NULL;
      }
    }    
    else {
       Fprintf(stderr, "Error in Verilog file read: No file %s\n", fname);
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
  else {
     matchfunc = match;
     matchintfunc = matchfile;
     hashfunc = hashcase;
  }

  if ((hashfile != -1) && (hashfile != *fnum)) {
      /* Started a new file, so remove all the parameters and definitions */
      RecurseHashTable(&verilogparams, freeprop);
      HashKill(&verilogparams);
      RecurseHashTable(&verilogdefs, freeprop);
      HashKill(&verilogdefs);
      hashfile = -1;
  }
  if (hashfile == -1) {
      InitializeHashTable(&verilogparams, OBJHASHSIZE);
      InitializeHashTable(&verilogdefs, OBJHASHSIZE);
      hashfile = *fnum;
  }
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

	/* If that fails, see if a standard Verilog extension	*/
	/* helps, if the file didn't have an extension.  But	*/
	/* really, we're getting desperate at this point.	*/

	if (strchr(fname, '.') == NULL) {
           SetExtension(name, fname, VERILOG_EXTENSION);
           filenum = OpenParseFile(name, parent);
	   if (filenum < 0) {
	      fprintf(stderr,"Error in Verilog file include: No file %s\n", name);
	      return;
	   }
        }
	else {
	   fprintf(stderr,"Error in Verilog file include: No file %s\n", fname);
	   return;
        }
     }
  }
  ReadVerilogFile(fname, parent, CellStackPtr, blackbox);
  CloseParseFile();
}

