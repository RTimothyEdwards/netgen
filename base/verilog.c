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
#include <ctype.h>   /* for isalnum() */
#include <limits.h>  /* for ULLONG_MAX */
#ifndef IBMPC
#include <sys/types.h>	/* for getpwnam() tilde expansion */
#include <pwd.h>
#endif

#ifdef TCL_NETGEN
#include <tcl.h>
extern Tcl_Interp *netgeninterp;
#endif

#include "netgen.h"
#include "objlist.h"
#include "netfile.h"
#include "print.h"
#include "hash.h"
#include "netcmp.h"

// See netfile.c for explanation of delimiters.  'X'
// separates single-character delimiters from two-character delimiters.
#define VLOG_DELIMITERS "X///**/#((**)X,;:(){}[]="
#define VLOG_EQUATION_DELIMITERS "X///**/#((**)X,;:(){}[]=+-*/"
#define VLOG_PIN_NAME_DELIMITERS "X///**/(**)X()"
#define VLOG_PIN_CHECK_DELIMITERS "X///**/(**)X,;(){}"
#define VLOG_INTEGER_DELIMITERS "X///**/X;[]"

// Used by portelement structure "flags" record.
#define PORT_NOT_FOUND	0
#define PORT_FOUND	1
#define PORT_SLICED	2	// Bus bit is sliced across an instance array
#define PORT_SHARED	4	// Bus bit connects to every instance of an array

// Global storage for verilog parameters
struct hashdict verilogparams;
// Global storage for verilog definitions
struct hashdict verilogdefs;
// Record file pointer that is associated with the hash tables
int hashfilep = -1;	/* for parameters */
int hashfiled = -1;	/* for definitions */

// Global storage for wire buses
struct hashdict buses;

struct bus {
    int start;
    int end;
};

// Global storage for a 'for' loop
struct _loop {
    char *loopvar;
    int start;
    int end;
    long filepos;
};

struct _loop loop;

// Verilog modules to be elaborated by yosys (see elaborate.c)
static struct extmodule *ExtModules = NULL;

// Full path of the verilog file being read (for recording the source
// of modules to be elaborated by yosys)
static char *verilogsource = NULL;

// When reading the output of yosys, the module named "renamefrom" is
// read as a cell named "renameto" (see ReadVerilogModuleAs()).
static char *renamefrom = NULL;
static char *renameto = NULL;

// Count of modules flagged for yosys while reading a file
static int extcount = 0;

// Name of the module being read (the cell may have a temporary name,
// "_PLACEHOLDER_", if the module was used before it was defined)
static char currentmodule[MAX_STR_LEN];

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
// arithmetic (+,-,*,/), grouping by parentheses, bit shifts, and
// if-else operators.
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

	/* Reduce (a << b) and (a >> b) */

	for (texp = start; texp; texp = texp->next) {
	    if ((texp->last != NULL) && (texp->next != NULL) && (texp->oper != '\0')) {
		if ((texp->last->oper == '\0') && (texp->next->oper == '\0')) {
		    if (texp->oper == '<') {
			/* Left shift */
			texp->last->value <<= texp->next->value;
			/* Remove two items from the stack */
			tmp = texp;
			texp = texp->last;
			texp->next = tmp->next->next;
			if (tmp->next->next) tmp->next->next->last = texp;
			FREE(tmp->next);
			FREE(tmp);
			modified = TRUE;
		    }
		    if (texp->oper == '>') {
			/* Right shift */
			texp->last->value >>= texp->next->value;
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
		/* Watch for (a)*b+c or a+b*(c);  multiplies must be solved first */
		if ((texp->last->last != NULL) && ((texp->last->last->oper == '*')
				|| (texp->last->last->oper == '/'))) {
			/* Do nothing */
		}
		else if ((texp->next->next != NULL) && ((texp->next->next->oper == '*')
				|| (texp->next->next->oper == '/'))) {
			/* Do nothing */
		}
		else if ((texp->last->oper == '\0') && (texp->next->oper == '\0')) {
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

	/* Reduce (a ? b : c) */

	for (texp = start; texp; texp = texp->next) {
	    /* There must be at least five objects on the stack */
	    if ((texp->last != NULL) && (texp->next != NULL) && (texp->oper != '\0')
			&& (texp->next->next != NULL)
			&& (texp->next->next->next != NULL)) {
		if ((texp->last->oper == '\0') && (texp->next->oper == '\0')
			&& (texp->next->next->next->oper == '\0')) {
		    if ((texp->oper == '?') && (texp->next->next->oper == ':')) {
			/* If-Else conditional */
			if (texp->last->value)
			    texp->last->value = texp->next->value;
			else
			    texp->last->value = texp->next->next->next->value;

			/* Remove four items from the stack */
			tmp = texp;
			texp = texp->last;
			texp->next = tmp->next->next->next->next;
			if (tmp->next->next->next->next)
			    tmp->next->next->next->next->last = texp;
			FREE(tmp->next->next->next);
			FREE(tmp->next->next);
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
// Find the record of a verilog module to be elaborated by yosys, or
// NULL if there is none.
//-------------------------------------------------------------------------

struct extmodule *ExtModuleLookup(char *name, int file)
{
    struct extmodule *em;

    for (em = ExtModules; em; em = em->next)
	if ((em->file == file) && !strcmp(em->name, name))
	    return em;
    return NULL;
}

//-------------------------------------------------------------------------
// The module being read (CurrentCell) has something that netgen's
// verilog reader cannot handle, such as a "generate" block that cannot
// be evaluated, or an expression that depends on parameters.  Make it a
// black box, and record it to be elaborated by yosys when the netlist
// is elaborated (see elaborate.c).  "what" describes the problem.  The
// caller should then skip to the end of the module.
//-------------------------------------------------------------------------

static void FlagExternalModule(char *what)
{
    struct extmodule *em;
    struct property *klr, *kl;

    if (CurrentCell == NULL) return;

    /* Abandon any "for" loop being read */
    if (loop.loopvar != NULL) {
	klr = (struct property *)HashLookup(loop.loopvar, &verilogparams);
	if (klr != NULL) {
	    HashDelete(loop.loopvar, &verilogparams);
	    FREE(klr->key);
	    FREE(klr);
	}
	else
	    FREE(loop.loopvar);
	loop.loopvar = NULL;
    }

    /* The output of yosys should never need this */
    if (renamefrom != NULL) {
	Printf("Module '%s' (from yosys) cannot be read (%s).\n",
		currentmodule, what);
	SetClass(CLASS_MODULE);
	return;
    }

    Printf("Module '%s' cannot be read by netgen (%s);  it will be "
		"elaborated by yosys.\n", currentmodule, what);
    SetClass(CLASS_MODULE);
    CurrentCell->flags |= CELL_EXTERNAL;
    extcount++;

    /* Note that the cell may be "_PLACEHOLDER_" until the end of the	*/
    /* module, when it is renamed to the module name.			*/
    em = ExtModuleLookup(currentmodule, CurrentCell->file);
    if (em == NULL) {
	em = (struct extmodule *)CALLOC(1, sizeof(struct extmodule));
	em->name = strsave(currentmodule);
	em->file = CurrentCell->file;
	em->next = ExtModules;
	ExtModules = em;
    }
    else if (em->source != NULL)
	FREE(em->source);
    em->source = (verilogsource != NULL) ? strsave(verilogsource) : NULL;
    em->status = EXT_PENDING;

    /* Save the definitions (`define), so that yosys reads the module	*/
    /* the same way (e.g., with or without "`ifdef USE_POWER_PINS").	*/
    if (em->defines != NULL) FREE(em->defines);
    em->defines = NULL;
#ifdef TCL_NETGEN
    if (definitions != NULL) {
	Tcl_Obj *dlist = Tcl_NewListObj(0, NULL);
	char dstr[MAX_STR_LEN];

	Tcl_IncrRefCount(dlist);
	for (kl = (struct property *)HashFirst(definitions); kl;
		    kl = (struct property *)HashNext(definitions)) {
	    if (kl->type == PROP_INTEGER)
		snprintf(dstr, MAX_STR_LEN, "%s=%d", kl->key, kl->pdefault.ival);
	    else if (kl->type == PROP_DOUBLE)
		snprintf(dstr, MAX_STR_LEN, "%s=%.15g", kl->key, kl->pdefault.dval);
	    else if ((kl->type == PROP_STRING) && (kl->pdefault.string != NULL) &&
			(strpbrk(kl->pdefault.string, " \t\n\r\"") == NULL))
		snprintf(dstr, MAX_STR_LEN, "%s=%s", kl->key, kl->pdefault.string);
	    else
		continue;	/* The value cannot be passed to yosys */
	    Tcl_ListObjAppendElement(netgeninterp, dlist,
			Tcl_NewStringObj(dstr, -1));
	}
	em->defines = strsave(Tcl_GetString(dlist));
	Tcl_DecrRefCount(dlist);
    }
#endif
    if (em->params != NULL) FREE(em->params);
    em->params = NULL;
}

//-------------------------------------------------------------------------
// Return TRUE if the module being read has parameters (in its header or
// body, or local parameters), or is in a generate loop.
//-------------------------------------------------------------------------

static int ModuleHasParams()
{
    if (CurrentCell == NULL) return FALSE;
    if (loop.loopvar != NULL) return TRUE;
    if (HashFirst(&(CurrentCell->propdict)) != NULL) return TRUE;
    if ((CurrentCell->paramdict.hashtab != NULL) &&
		(HashFirst(&(CurrentCell->paramdict)) != NULL)) return TRUE;
    return FALSE;
}

//-------------------------------------------------------------------------
// Set the full path of the verilog file being read, returning the
// previous one (to be restored with RestoreVerilogSource()).
//-------------------------------------------------------------------------

static char *SetVerilogSource(char *path)
{
    char *prev = verilogsource;
    char *full;

    full = realpath(path, NULL);
    if (full != NULL) {
	verilogsource = strsave(full);
	free(full);
    }
    else
	verilogsource = strsave(path);
    return prev;
}

static void RestoreVerilogSource(char *prev)
{
    if (verilogsource != NULL) FREE(verilogsource);
    verilogsource = prev;
}

//-------------------------------------------------------------------------
// Convert one verilog based constant (e.g., "32'h1209_2025", "8'sd5",
// or "'b1010") at the start of string "s" to its value.  Return the
// number of characters of "s" that make up the constant, or 0 if "s"
// does not start with a based constant or the constant cannot be
// converted (it has x or z bits, or does not fit in 64 bits).  The
// value is returned in "vptr".  A signed constant whose top bit is set
// is negative;  then "negptr" is set to 1 and "vptr" is the magnitude.
//-------------------------------------------------------------------------

static int ConvertBasedConstant(char *s, unsigned long long *vptr, int *negptr)
{
    char *p = s;
    int size = 0, sized = 0, issigned = 0, base, digit, ndigits = 0;
    unsigned long long value = 0;

    while (isdigit(*p) || (sized && (*p == '_'))) {
	if (*p != '_') {
	    if (size < 1000000) size = size * 10 + (*p - '0');
	    sized = 1;
	}
	p++;
    }
    while (isspace(*p)) p++;
    if (*p != '\'') return 0;
    p++;
    if ((*p == 's') || (*p == 'S')) {
	issigned = 1;
	p++;
    }
    switch (tolower(*p)) {
	case 'b': base = 2; break;
	case 'o': base = 8; break;
	case 'd': base = 10; break;
	case 'h': base = 16; break;
	default: return 0;
    }
    p++;
    while (isspace(*p)) p++;
    while (isalnum(*p) || (*p == '_') || (*p == '?')) {
	if (*p == '_') {
	    p++;
	    continue;
	}
	if (isdigit(*p))
	    digit = *p - '0';
	else if (isxdigit(*p))
	    digit = tolower(*p) - 'a' + 10;
	else
	    return 0;		/* x, z, or ? bits, or not a digit */
	if (digit >= base) return 0;
	if (value > (ULLONG_MAX - digit) / base) return 0;	/* Too large */
	value = value * base + digit;
	ndigits++;
	p++;
    }
    if (ndigits == 0) return 0;
    if (sized && (size == 0)) return 0;

    *negptr = 0;
    if (sized && (size < 64)) {
	value &= ((1ULL << size) - 1);
	if (issigned && ((value >> (size - 1)) & 1)) {
	    *negptr = 1;
	    value = (1ULL << size) - value;
	}
    }
    *vptr = value;
    return (int)(p - s);
}

//-------------------------------------------------------------------------
// Return a copy of string "str" in which every verilog based constant
// (see above) is replaced by its decimal value, so that the value can
// be used like any other number.  Text in double quotes is not changed.
// Return NULL if "str" has no constants to replace.  The returned
// string must be freed by the caller.
//-------------------------------------------------------------------------

static char *ConvertVerilogConstants(char *str)
{
    char *s, *dptr, *newstr;
    char prev, quoted = 0;
    int n, neg, changed = 0;
    unsigned long long value;

    if (str == NULL) return NULL;
    if (strchr(str, '\'') == NULL) return NULL;

    /* Each constant is at least 3 characters and becomes at most	*/
    /* 23 characters ("(-" + 20 digits + ")").			*/
    newstr = (char *)MALLOC(8 * strlen(str) + 24);
    dptr = newstr;
    s = str;
    while (*s != '\0') {
	if (*s == '\"') quoted = !quoted;
	prev = (s == str) ? ' ' : *(s - 1);
	if (!quoted && (isdigit(*s) || (*s == '\'')) && !isalnum(prev)
		&& (prev != '_') && (prev != '$') && (prev != '.')
		&& (prev != '\'')) {
	    n = ConvertBasedConstant(s, &value, &neg);
	    if (n > 0) {
		if (!neg)
		    dptr += sprintf(dptr, "%llu", value);
		else if ((s == str) && (*(s + n) == '\0'))
		    dptr += sprintf(dptr, "-%llu", value);
		else
		    dptr += sprintf(dptr, "(-%llu)", value);
		s += n;
		changed = 1;
		continue;
	    }
	}
	*dptr++ = *s++;
    }
    *dptr = '\0';

    if (changed == 0) {
	FREE(newstr);
	return NULL;
    }
    return newstr;
}

//-------------------------------------------------------------------------
// Read an integer from token "tok", which may be a verilog based
// constant.  Return value is the same as sscanf().
//-------------------------------------------------------------------------

static int ScanInteger(char *tok, int *vptr)
{
    char *cval;
    int result;

    cval = ConvertVerilogConstants(tok);
    result = sscanf((cval != NULL) ? cval : tok, "%d", vptr);
    if (cval != NULL) FREE(cval);
    return result;
}

//-------------------------------------------------------------------------
// Parse an expression which must resolve to a single integer.
// If it does, return "1" and put the integer in the return
// pointer "iptr".  If not, return "0" and iptr is undefined.
// The expression may use parameters, standard arithmetic,
// and parenthetic grouping.  This routine does not tokenize input
// but assumes that the entire expression is in the string "expr".
//-------------------------------------------------------------------------

static int ParseIntegerExpr(char *expr, int *iptr)
{
    int result, value;
    char *sptr, *cptr, savec;
    struct property *kl = NULL;
    struct expr_stack *stack, *newexp;

    stack = NULL;
    savec = '\0';
    sptr = expr;
    result = 1;

    while (sptr && (*sptr != '\0')) {
	// Move sptr to first non-space character
	while (isspace(*sptr)) sptr++;

	// Tokenize.  Look ahead to next delimeter and truncate string there.
	cptr = sptr + 1;
	if (isalnum(*sptr) || (*sptr == '_') || (*sptr == '$')) {
	    while (*cptr != '\0') {
		if (isalnum(*cptr)) cptr++;
		else if ((*cptr == '_') || (*cptr == '$')) cptr++;
		else break;
	    }
	}
	savec = *cptr;
	*cptr = '\0';

	if (match(sptr, "+") || match(sptr, "-")
	    	|| match(sptr, "*") || match(sptr, "/")
	    	|| match(sptr, "(") || match(sptr, ")")
	    	|| match(sptr, "<<") || match(sptr, ">>")
	    	|| match(sptr, "?") || match(sptr, ":")) {
	    newexp = (struct expr_stack *)MALLOC(sizeof(struct expr_stack));
	    /* Note that "oper" is one character and that "<<" and ">>"
	     * become '<' and '>', respectively.  The less-than and greater-
	     * than operators are not (yet) handled but will need to be
	     * recast to some other character in "oper".
	     */
	    newexp->oper = *sptr;
	    newexp->value = 0;
	    newexp->next = NULL;
	    newexp->last = stack;
	    if (stack) stack->next = newexp;
	    stack = newexp;

	    sptr = cptr;
	    *cptr = savec;
	    continue;
	}
	if ((result = sscanf(sptr, "%d", &value)) != 1) {

	    // Is name in the parameter list?
	    kl = (struct property *)HashLookup(sptr, &verilogparams);
	    if (kl == NULL) {
		Printf("Value %s in expression is not a number or a parameter.\n",
				sptr);
		value = 0;
		break;
	    }
	    else {
		if (kl->type == PROP_STRING) {
		    result = sscanf(kl->pdefault.string, "%d", &value);
		    if (result != 1) {
		        Printf("Parameter %s has value %s that cannot be parsed"
				" as an integer.\n",
				sptr, kl->pdefault.string);
		    	value = 0;
			break;
		    }
		}
		else if (kl->type == PROP_INTEGER) {
		    value = kl->pdefault.ival;
		    result = 1;		// Assert valid result
		}
		else if (kl->type == PROP_DOUBLE) {
		    value = (int)kl->pdefault.dval;
		    if ((double)value != kl->pdefault.dval) {
		        Printf("Parameter %s has value %g that cannot be parsed"
				" as an integer.\n",
				sptr, kl->pdefault.dval);
		    	value = 0;
			break;
		    }
		    result = 1;		// Assert valid result
		}
		else {
		    Printf("Parameter %s has unknown type; don't know how"
				" to parse.\n", sptr);
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

	/* Move to next token */
	sptr = cptr;
	*cptr = savec;
    }

    if (result != 0) {
	if (stack == NULL) {
	    Printf("Empty array found.\n");
	    result = 0;
	}
	else if (EvalExpr(&stack, iptr) != 1) {
	    Printf("Bad expression found in array.\n");
	    result = 0;
	}
    }

    /* In case of error, stack may need cleaning up */
    while (stack != NULL) {
	newexp = stack;
	stack = stack->last;
	FREE(newexp);
    }
    return result;
}

//-------------------------------------------------------------------------
// Front end to ParseIntegerExpr() above, which first converts any
// verilog based constants (e.g., "4'd3") in the expression to numbers.
//-------------------------------------------------------------------------

int ParseIntegerExpression(char *expr, int *iptr)
{
    char *cexpr;
    int result;

    cexpr = ConvertVerilogConstants(expr);
    if (cexpr == NULL) return ParseIntegerExpr(expr, iptr);
    result = ParseIntegerExpr(cexpr, iptr);
    FREE(cexpr);
    return result;
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
	    		|| match(nexttok, "(") || match(nexttok, ")")
	    		|| match(nexttok, "<<") || match(nexttok, ">>")) {
		newexp = (struct expr_stack *)MALLOC(sizeof(struct expr_stack));
		newexp->oper = *nexttok;
		newexp->value = 0;
		newexp->next = NULL;
		newexp->last = stack;
		if (stack) stack->next = newexp;
		stack = newexp;
		continue;
	    }
	    if ((result = ScanInteger(nexttok, &value)) != 1) {

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
// a string instead of the input tokenizer, and expressions are not
// allowed.
//--------------------------------------------------------------------

int GetBus(char *astr, struct bus *wb)
{
    char *colonptr, *brackstart, *brackend, *sigend, sdelim, *aastr;
    int result, start, end;
    struct property *kl = NULL;
    struct expr_stack *stack, *newexp;

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
	result = ParseIntegerExpression(brackstart + 1, &start);
	if (colonptr) *colonptr = ':';
	if (result != 1) {
	    Printf("Badly formed array notation \"%s\"\n", astr);
	    *brackend = ']';
	    return 1;
	}
	if (colonptr)
	    result = ParseIntegerExpression(colonptr + 1, &end);
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

//--------------------------------------------------------------------
// Output a Verilog Module.  Note that since Verilog does not describe
// low-level devices like transistors, capacitors, etc., then this
// format is limited to black-box subcircuits.  Cells containing any
// such low-level devices are ignored.
//--------------------------------------------------------------------

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
  char inst[MAX_STR_LEN], model[MAX_STR_LEN], portname[MAX_STR_LEN], pkey[MAX_STR_LEN];
  struct nlist *tp, *tpsave;
  struct objlist *parent, *sobj, *nobj, *lobj, *pobj, *cref;

  inst[MAX_STR_LEN-1] = '\0';
  model[MAX_STR_LEN-1] = '\0';
  in_module = (char)0;
  in_param = (char)0;

  loop.loopvar = NULL;

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
	int islocal = match(nexttok, "localparam");

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
		if (paramval == NULL) paramval = strsave("");
		else {
		    /* Change based constants such as 32'h1f to numbers */
		    char *cval = ConvertVerilogConstants(paramval);
		    char *sptr, *dptr, quoted = 0;

		    if (cval != NULL) {
			FREE(paramval);
			paramval = cval;
		    }

		    /* Remove white space (except in quoted strings), as	*/
		    /* the expression evaluator does not expect it (e.g.,	*/
		    /* "L * 2" must be "L*2").				*/
		    for (sptr = dptr = paramval; *sptr != '\0'; sptr++) {
			if (*sptr == '\"') quoted = !quoted;
			if (!quoted && isspace(*sptr)) continue;
			*dptr++ = *sptr;
		    }
		    *dptr = '\0';
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

		/* Inside a module, a "parameter" is also a parameter of	*/
		/* the module that an instance may override (the same as	*/
		/* one declared in the module header), and a "localparam"	*/
		/* is a local parameter of the module (the same as .param	*/
		/* inside a SPICE subcircuit).  These are used to resolve	*/
		/* parameters passed down through the hierarchy (see	*/
		/* elaborate.c).						*/

		if (in_module && (CurrentCell != NULL)) {
		    if (islocal) {
			struct property *klc;
			struct hashdict *pdict = &(CurrentCell->paramdict);

			if (pdict->hashtab == NULL)
			    InitializeHashTable(pdict, TINYHASHSIZE);
			klc = (struct property *)HashLookup(paramkey, pdict);
			if (klc != NULL) {
			    FREE(klc->key);
			    if (klc->pdefault.string) FREE(klc->pdefault.string);
			    FREE(klc);
			}
			klc = NewProperty();
			klc->key = strsave(paramkey);
			klc->idx = 0;
			klc->type = PROP_STRING;
			klc->slop.dval = 0.0;
			klc->pdefault.string = strsave(paramval);
			HashPtrInstall(paramkey, klc, pdict);
		    }
		    else if (ConvertStringToFloat(paramval, &dval) == 1)
			PropertyDouble(CurrentCell->name, filenum, paramkey, 0.01, dval);
		    else
			PropertyString(CurrentCell->name, filenum, paramkey, 0, paramval);
		}

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

      /* When reading the output of yosys, read the module as a cell	*/
      /* with a different name (see ReadVerilogModuleAs()).		*/
      if ((renamefrom != NULL) && match(nexttok, renamefrom))
	 nexttok = renameto;
      strncpy(currentmodule, nexttok, MAX_STR_LEN - 1);
      currentmodule[MAX_STR_LEN - 1] = '\0';

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
      tpsave = NULL;
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
	 tpsave = tp;
	 CellDef("_PLACEHOLDER_", filenum);
	 tp = LookupCellFile("_PLACEHOLDER_", filenum);
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

		    char *cval;

		    // The parameter value is the next token.
		    SkipTokComments(VLOG_DELIMITERS); /* get the next token */

		    // Change based constants such as 32'h1f to numbers
		    cval = ConvertVerilogConstants(nexttok);
		    eqptr = (cval != NULL) ? cval : nexttok;

		    // Try first as a double, otherwise it's a string
		    // Double value's slop defaults to 1%.
		    if (ConvertStringToFloat(eqptr, &dval) == 1)
		        PropertyDouble(tp->name, filenum, pkey, 0.01, dval);
		    else
			PropertyString(tp->name, filenum, pkey, 0, eqptr);
		    if (cval != NULL) FREE(cval);
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
	       /* Finish the module (black box) as usual, which also	*/
	       /* replaces a placeholder cell for it, if there is one.	*/
	       if (in_module && (CurrentCell != NULL)) goto end_module;
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

end_module:
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

      if (tpsave != NULL) {
	 struct nlist *tpplace;
	 char *savename;
	 int lnum, pnum, ltest;
	 unsigned char valid, numbered;
	 struct objlist *lobj, *pobj;

	 /* Handle a placeholder from a verilog file that has been replaced
	  * by a netlist with pins in a different order.  The pins need to
	  * be matched, corrected in the original cell and all instances,
	  * and the new cell deleted.
	  */

	 Printf("Verilog placeholder module %s replaced by module definition\n",
		tpsave->name);
         tpplace = LookupCellFile("_PLACEHOLDER_", filenum);

	 /* If tpsave was generated from an instance in a SPICE netlist that
	  * did not have a black-box subcircuit definition, then the pins
	  * will all be labeled 1, 2, 3, etc.  If so, then assume that the
	  * verilog pins are in order, and rename the placeholder pins.
	  * If the number of pins does not match, or if the pins are not
	  * labeled as ascending integers, then leave the cell alone.
	  * In either case, output a warning message.
	  */

	 /* Get the number of ports in the placeholder */
	 pnum = 0;
	 for (pobj = tpplace->cell; pobj; pobj = pobj->next) {
	    if (pobj->type != PORT) break;
	    pnum++;
	 }

	 /* Get the number of ports in the saved cell and make	*/
	 /* sure that it equals the number of ports in the	*/
	 /* placeholder, and that all of the ports in the saved	*/
	 /* cell are integers in ascending order.		*/

	 valid = TRUE;
	 numbered = TRUE;
	 lnum = 0;
	 for (lobj = tpsave->cell; lobj; lobj = lobj->next) {
	    if (lobj->type != PORT) break;
	    lnum++;
	    if (sscanf(lobj->name, "%d", &ltest) != 1) break;
	    if (ltest != lnum) break;
	 }
	 if ((lobj != NULL) && (lobj->type == PORT)) {
	    valid = FALSE;	/* Pins are not integers in ascending order */
	    numbered = FALSE;
	 }
	 if (pnum != lnum) valid = FALSE;	/* Different number of pins */

	 if (valid == TRUE) {
	    Printf("Replacing pins of placeholder cell %s from cell definition.\n",
		tpsave->name);
	    pobj = tpplace->cell;
	    for (lobj = tpsave->cell; lobj; lobj = lobj->next) {
		if (lobj->type != PORT) break;
		if (pobj == NULL) break;	/* should not happen */
		FREE(lobj->name);
		lobj->name = (char *)MALLOC(strlen(pobj->name) + 1);
		strcpy(lobj->name, pobj->name);
		pobj = pobj->next;
	    }
	 }
	 else if (numbered == TRUE) {
	    /* A SPICE placeholder with a different number of pins */
	    Printf("Placeholder pins of cell %s are not compatible and"
		" will be left unchanged\n", tpsave->name);
	 }
	 /* Otherwise, the placeholder has named pins (from verilog	*/
	 /* instances), which are matched by name below.		*/

         /* MatchPins is part of netcmp and normally Circuit2 is the
          * circuit being matched, so set Circuit2 to the original
          * verilog black-box cell, and MatchPins() will force its
          * pins to be rearranged to match the module definition just
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
      char *cval, *vptr;

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

      /* Change based constants such as 32'h1f to numbers */
      cval = ConvertVerilogConstants(nexttok);
      vptr = (cval != NULL) ? cval : nexttok;

      if ((vptr == NULL) || (vptr[0] == '\0')) {
	 // Let "`define X" be equivalent to "`define X 1".  Use integer value.
	 kl->type = PROP_INTEGER;
	 kl->pdefault.ival = 1;
         kl->slop.ival = 0;
      }
      else if (ConvertStringToInteger(vptr, &ival) == 1) {
	 /* Parameter parses as an integer */
      	 kl->type = PROP_INTEGER;
	 kl->pdefault.ival = ival;
         kl->slop.ival = 0;		// Exact match default
      }
      else if (ConvertStringToFloat(vptr, &dval) == 1) {
	 /* Parameter parses as a floating-point number */
      	 kl->type = PROP_DOUBLE;
	 kl->pdefault.dval = dval;
         kl->slop.dval = 0.01;		// One percent default
      }
      else {
         char *toks, *ctoks;

	 /* Treat the parameter as a string; BUT pull everything to */
	 /* EOL, not just the current token.			    */
	 toks = GetLineAtTok();
	 ctoks = ConvertVerilogConstants(toks);

      	 kl->type = PROP_STRING;
	 kl->pdefault.string = (ctoks != NULL) ? ctoks : strsave(toks);
         kl->slop.dval = 0.0;

	 SkipNewLine(VLOG_DELIMITERS);
      }
      if (cval != NULL) FREE(cval);
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
    else if (match(nexttok, "end")) {
	/* Handle a 'for' loop */
	if (loop.loopvar != NULL) {
	    int loopval;
	    struct property *klr;

	    klr = (struct property *)HashLookup(loop.loopvar, &verilogparams);
	    loopval = klr->pdefault.ival;

	    /* loop.end is the last value of the loop variable.  If the	*/
	    /* block was just read for the last value, then the loop is	*/
	    /* done.  Otherwise, read the block again for the next value.	*/
	    if (loopval == loop.end)
	    {
		HashDelete(loop.loopvar, &verilogparams);
		FREE(klr);
		FREE(loop.loopvar);
		loop.loopvar = NULL;
	    }
	    else
	    {
		if (loopval < loop.end) loopval++;
		else loopval--;
		klr->pdefault.ival = loopval;
		SeekFile(loop.filepos);
	    }
	}
	continue;
    }
    else if (match(nexttok, "begin") || match(nexttok, "generate")) {
	/* 'generate' section or 'for' loop start is ignored */
	continue;
    }
    else if (match(nexttok, ":")) {
	/* Block label after 'begin' or 'end' (e.g., "begin : name"):	*/
	/* skip the label.						*/
	SkipTokNoNewline(VLOG_DELIMITERS);
	continue;
    }
    else if (match(nexttok, "generate") || match(nexttok, "endgenerate")) {
	/* 'generate' section is ignored */
	continue;
    }
    else if (match(nexttok, "genvar")) {
	while (!match(nexttok, ";")) SkipTok("X///**/X,;");
	continue;
    }
    else if (match(nexttok, "for")) {
	char limittype = '\0';
	char *paramkey = NULL;
	struct property *kl = NULL, *klr;
	/* Parse out the loop variable and count.  Set the loop
	 * variable as a parameter.  Find the beginning of the
	 * loop block and record the file position so that the
	 * block can be re-parsed for each loop iteration.
	 */
	SkipTokNoNewline(VLOG_DELIMITERS);
	if ((nexttok == NULL) || !match(nexttok, "(")) {
	    Printf("Badly formed 'for' loop.\n");
	    FREE(paramkey);
	    goto bad_for_loop;
	}
	SkipTokNoNewline(VLOG_DELIMITERS);
	/* Next token must be the loop variable */
	if (nexttok == NULL) goto bad_for_loop;
	paramkey = strsave(nexttok);
	SkipTokNoNewline(VLOG_DELIMITERS);
	if (!match(nexttok, "=")) {
	    Printf("Badly formed 'for' loop.\n");
	    FREE(paramkey);
	    goto bad_for_loop;
	}
	SkipTokNoNewline(VLOG_INTEGER_DELIMITERS);
	if (ParseIntegerExpression(nexttok, &ival) == 0) {
	    FREE(paramkey);
	    goto bad_for_loop;
	}

	kl = NewProperty();
	kl->type = PROP_INTEGER;
	kl->slop.ival = 0;
	kl->pdefault.ival = ival;
	kl->key = paramkey;
	kl->idx = 0;
	kl->merge = MERGE_NONE;

	SkipTokNoNewline(VLOG_DELIMITERS);
	if (!match(nexttok, ";")) {
	    Printf("Badly formed 'for' loop.\n");
	    FREE(paramkey);
	    FREE(kl);
	    goto bad_for_loop;
	}

	/* Assuming a standard 'for' loop here */
	SkipTokNoNewline(VLOG_DELIMITERS);
	if (!match(nexttok, paramkey)) {
	    Printf("Don't know how to parse this 'for' loop!\n");
	    FREE(paramkey);
	    FREE(kl);
	    goto bad_for_loop;
	}
	SkipTokNoNewline(VLOG_DELIMITERS);
	if (strlen(nexttok) == 1) limittype = *nexttok;
	SkipTokNoNewline(VLOG_INTEGER_DELIMITERS);
	if (ParseIntegerExpression(nexttok, &ival) == 0) {
	    FREE(kl);
	    FREE(paramkey);
	    goto bad_for_loop;
	}

	/* Loops will stop after the last value, so if value is > or <,
	 * then adjust the end value accordingly.
	 */
	if (limittype == '<')
	    ival--;
	else if (limittype == '>')
	    ival++;

	SkipTokNoNewline(VLOG_DELIMITERS);
	if (!match(nexttok, ";")) {
	    Printf("Badly formed 'for' loop.\n");
	    FREE(paramkey);
	    FREE(kl);
	    goto bad_for_loop;
	}

	/* Assume a standard 'for' loop and skip to the block begin	*/
	/* To do:  Parse out the loop increment value.			*/
	while (1) {
	    SkipTokNoNewline(VLOG_DELIMITERS);
	    if (EndParseFile()) break;
	    else if (match(nexttok, "begin")) break;
	}
	if (EndParseFile()) {
	    Printf("Badly formed 'for' loop:  No begin/end block.\n");
	    FREE(paramkey);
	    FREE(kl);
	    goto bad_for_loop;
	}

	/* Save the loop variable, ending value, and file position */
	loop.loopvar = paramkey;
	loop.start = kl->pdefault.ival;
	loop.end = ival;
	loop.filepos = TellFile();

	/* 'for' loop has been completely parsed, so save the loop variable */
	HashPtrInstall(paramkey, kl, &verilogparams);
	continue;

bad_for_loop:
	/* The loop could not be read.  Previously, this ended reading	*/
	/* of the file.  Instead, have yosys elaborate the module, and	*/
	/* continue after the end of the module.			*/
	if (in_module && (CurrentCell != NULL)) {
	    FlagExternalModule("'for' loop");
	    goto skip_endmodule;
	}
	break;
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
	    char *aptr = strvchr(nexttok, '[');

	    /* This is a repeat of code to handle reading of pins, and
	     * makes sure that all input has been read to the closing
	     * bracket.  There needs to be a single routine to do this,
	     * or (better) rewrite the parser with lex instead of trying
	     * to enumerate all the syntax cases separately.
	     */
	    if ((aptr != NULL) && (strvchr(nexttok, ']') == NULL))
	    {
		/* If a bus expressions has whitespace, then concatenate
		 * to the closing ']'.
		 */
		char *array_expr = (char *)MALLOC(1);
		char *new_array_expr = NULL;
		*array_expr = '\0';
		/* Read to "]" */
		while (nexttok) {
		    new_array_expr = (char *)MALLOC(strlen(array_expr) +
				    strlen(nexttok) + 1);
		    /* Roundabout way to do realloc() becase there is no REALLOC() */
		    strcpy(new_array_expr, array_expr);
		    strcat(new_array_expr, nexttok);
		    FREE(array_expr);
		    array_expr = new_array_expr;
		    if (strchr(nexttok, ']')) break;
		    SkipTokComments(VLOG_PIN_CHECK_DELIMITERS);
		}
		if (!nexttok) {
		    Printf("Unterminated array in assignment %s\n", array_expr);
		}
	    }

	    if (((aptr == NULL) && (GetBusTok(&wb) == 0)) ||
			((aptr != NULL) && (GetBus(aptr, &wb) == 0))) {
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
		/* Handle the case in which an assignment is made
		 * without first declaring a wire for the signal,
		 * which is considered valid syntax (patch by
		 * Sylvain Munaut).
		 */
		if (lhs == NULL) {
		    Node(nexttok);
		    lhs = LookupObject(nexttok, CurrentCell);
		}
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
	//    "assign" using if-else constructs or bit shifts.
	//   ("assign" using any other boolean arithmetic is not structural verilog.)
	//    "assign a = {x{b}}" creates a bus by repeating a component.

	if (nexttok && match(nexttok, "=")) {
	    char assignname[MAX_STR_LEN], assignroot[MAX_STR_LEN];
	    int multiplier = 1;

	    i = wb.start;
	    while (1) {
		SkipTokComments(VLOG_PIN_CHECK_DELIMITERS);
		if (!nexttok) break;

		if (match(nexttok, "{")) {
		    /* RHS is a bundle */
		    /* Make provisional multiplier active.  NOTE:	*/
		    /* this is not going to properly handle complex	*/
		    /* multipliers like {2{a,b}}.  Needs rework.	*/
		    if (multiplier < 0) multiplier = -multiplier;
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
		    char *aptr = strvchr(nexttok, '[');
		    if (((aptr == NULL) && (GetBusTok(&wb2) == 0)) ||
				((aptr != NULL) && (GetBus(aptr, &wb2) == 0))) {
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

			/* Check if rhs starts with a signal multiplier */
			if (rhs == NULL) {
			    if (ConvertStringToInteger(nexttok, &multiplier) == 1) {
				/* Set multiplier to a negative value to
				 * indicate that it is provisional, waiting on
				 * signal specification to follow.
				 */
				multiplier = -multiplier;
				continue;
			    }
			}
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
			if ((lhs != NULL) || (multiplier < 0)) {
			    Printf("Improper assignment;  right-hand side cannot "
					"be parsed.\n");
			    if (i != -1)
			    	Printf("Left-hand side is \"%s\".\n", noderoot);
			    else
			    	Printf("Left-hand side is \"%s\".\n", lhs->name);
			    Printf("Improper expression is \"%s\".\n", nexttok);
			    /* An expression in a module with parameters or	*/
			    /* in a generate loop may resolve to a simple	*/
			    /* connection for given parameter values (e.g.,	*/
			    /* "assign a = P ? b : c"), so have yosys		*/
			    /* elaborate the module.  Otherwise, it is		*/
			    /* probably behavioral verilog.			*/
			    if (ModuleHasParams()) {
				FlagExternalModule("expression in 'assign'");
				goto skip_endmodule;
			    }
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
			else if (lhs != NULL)
			    strncpy(nodename, lhs->name, MAX_STR_LEN - 1);
			else {
			    Printf("Error:  Improper node name \"%s\".\n", noderoot);
			    goto skip_endmodule;
			}
			if (j != -1)
			    snprintf(assignname, MAX_STR_LEN, "%s[%d]", assignroot, j);
			else if (rhs != NULL)
			    strncpy(assignname, rhs->name, MAX_STR_LEN - 1);
			else {
			    Printf("Error:  Improper assignment name \"%s\".\n",
					assignroot);
			    goto skip_endmodule;
			}

			join(nodename, assignname);

			if (i == wb.end) break;
			i += (wb.end > wb.start) ? 1 : -1;

			if (j == wb2.end) {
			    if (multiplier <= 1)
			    	break;
			    else
				multiplier--;
			}
			else
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
    else if (in_module && (match(nexttok, "if") || match(nexttok, "case"))) {
      /* Conditional generate blocks depend on parameter values, and	*/
      /* are not handled by netgen;  have yosys elaborate the module.	*/
      char what[64];
      snprintf(what, 63, "generate '%s'", nexttok);
      FlagExternalModule(what);
      goto skip_endmodule;
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
		    /* The value may be an expression of several tokens,	*/
		    /* including parentheses (e.g., "(L + 1) * 2").  Collect	*/
		    /* everything up to the matching close parenthesis.		*/
		    char *pval = NULL, *ptmp;
		    int depth = 0;

		    while (nexttok != NULL) {
			if (match(nexttok, "(")) depth++;
			else if (match(nexttok, ")")) {
			    if (depth == 0) break;
			    depth--;
			}
			if (pval == NULL)
			    pval = strsave(nexttok);
			else {
			    ptmp = (char *)MALLOC(strlen(pval) + strlen(nexttok) + 1);
			    sprintf(ptmp, "%s%s", pval, nexttok);
			    FREE(pval);
			    pval = ptmp;
			}
	                SkipTokComments(VLOG_DELIMITERS);
		    }
		    if (nexttok == NULL) {
			Printf("Error: Expecting end of parameter value, "
				"got end of file.\n");
		    }
		    if (pval != NULL) {
			/* Change based constants such as 32'h1f to numbers */
			ptmp = ConvertVerilogConstants(pval);
			if (ptmp != NULL) {
			    FREE(pval);
			    pval = ptmp;
			}
	                AddProperty(&kvlist, paramname, pval);
			FREE(pval);
		    }
		}
		FREE(paramname);
	    }
	    else {
		/* Parameter values given by position (e.g., "#(3)") are	*/
		/* not handled, since the order of the module's parameters	*/
		/* is not known.  Do not ignore them silently.		*/
		Printf("Warning: Parameter value \"%s\" given by position is not "
			"supported (use \".name(value)\");  ignored.\n", nexttok);
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
      else if (loop.loopvar != NULL) {
	 /* Instances created within a generate block for loop have an
	  * implicit array (of one instance, named with the index when
	  * the instance is made below).
	  */
	 int loopval;
	 struct property *klr;

	 klr = (struct property *)HashLookup(loop.loopvar, &verilogparams);
	 loopval = klr->pdefault.ival;

         arraystart = arrayend = loopval;
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
			 if (!strcmp(nexttok, "}"))
			 {
			    /* If a "bundle" has only one component, then it is
			     * not really a bundle and should be treated as a
			     * single wire.
			     */
			    if (strchr(wire_bundle, ',') == NULL) {
				int blen = strlen(wire_bundle + 1) - 1;
				memmove(wire_bundle, wire_bundle + 1, blen);
				*(wire_bundle + blen) = '\0';
			    }
			    break;
			 }
			 SkipTokComments(VLOG_PIN_CHECK_DELIMITERS);
		     }
		     if (!nexttok) {
			 Printf("Unterminated net in pin %s\n", wire_bundle);
		     }
		     new_port->net = wire_bundle;
		  }
		  else if ((strchr(nexttok, '[') != NULL) &&
				(strchr(nexttok, ']') == NULL)) {
		     /* If a bus expressions has whitespace, then treat it like
		      * a bundle, above, concatenating to the closing ']'.
		      */
		     char *array_expr = (char *)MALLOC(1);
		     char *new_array_expr = NULL;
		     *array_expr = '\0';
		     /* Read to "]" */
		     while (nexttok) {
			 new_array_expr = (char *)MALLOC(strlen(array_expr) +
				    strlen(nexttok) + 1);
			 /* Roundabout way to do realloc() becase there is no REALLOC() */
			 strcpy(new_array_expr, array_expr);
			 strcat(new_array_expr, nexttok);
			 FREE(array_expr);
			 array_expr = new_array_expr;
			 if (strchr(nexttok, ']')) break;
			 SkipTokComments(VLOG_PIN_CHECK_DELIMITERS);
		     }
		     if (!nexttok) {
			 Printf("Unterminated net in pin %s\n", array_expr);
		     }
		     new_port->net = array_expr;
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
		int siglen, arraylen, portlen;

		// Work out the port width from the signal width and the
		// number of instances.  The array and the signal may be
		// declared in either direction, independently.
		// (1) The signal width is a multiple of the number of
		//     instances:  each instance gets a slice of the signal
		//     (one bit each if the widths are the same).
		// (2) Otherwise, each instance gets the whole signal.

		siglen = wb.start - wb.end;
		if (siglen < 0) siglen = -siglen;
		siglen++;
		arraylen = arraystart - arrayend;
		if (arraylen < 0) arraylen = -arraylen;
		arraylen++;

		if ((siglen % arraylen) == 0)
		    portlen = siglen / arraylen;
		else
		    portlen = siglen;

		if (portlen == 1) {
		    Port(scan->name);
		}
		else if (wb.start > wb.end) {
		    for (i = portlen - 1; i >= 0; i--) {
			sprintf(defport, "%s[%d]", scan->name, i);
			Port(defport);
		    }
		}
		else {
		    for (i = 0; i < portlen; i++) {
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
	 int portstart, portend, portnum, portmin, portmax, portlast;
	 int portdir, monotonic;
	 char odelim = '[', cdelim = ']';

	 scannext = scan->next;
	 portstart = -1;
	 portmin = portmax = portlast = -1;
	 portdir = 0;
	 monotonic = TRUE;

	 /* NOTE:  The cell may have been read from SPICE or CDL, in which	*/
	 /* case the port may be a bus bit using angle brackets (e.g.,	*/
	 /* "A<0>") instead of square brackets.  Accept either.		*/

	 for (obptr = tp->cell; obptr && obptr->type == PORT; obptr = obptr->next) {
	    char *delimiter, dchar;
	    delimiter = strrchr(obptr->name, '[');
	    if (delimiter == NULL) delimiter = strrchr(obptr->name, '<');
	    if (delimiter != NULL) {
	       dchar = *delimiter;
	       *delimiter = '\0';
	       if ((*matchfunc)(obptr->name, scan->name)) {
		  if (sscanf(delimiter + 1, "%d", &portnum) == 1) {
		     if (portstart == -1) {
			portstart = portend = portnum;
			portmin = portmax = portnum;
			odelim = dchar;
			cdelim = (dchar == '<') ? '>' : ']';
		     }
		     else {
		        portend = portnum;
			if (portnum < portmin) portmin = portnum;
			if (portnum > portmax) portmax = portnum;

			/* Check that the indexes run in one direction */
			if (portdir == 0)
			   portdir = (portnum > portlast) ? 1 : -1;
			else if (((portdir > 0) && (portnum < portlast)) ||
				((portdir < 0) && (portnum > portlast)))
			   monotonic = FALSE;
		     }
		     portlast = portnum;
		  }
	       }
	       *delimiter = dchar;
	    }
	 }

	 /* If the bus bits are not listed in order in the cell (e.g., a	*/
	 /* SPICE netlist with pins sorted alphabetically, so that "A[10]"	*/
	 /* comes before "A[2]"), then the first and last entries do not	*/
	 /* give the bus range.  Use the full range, and assume the usual	*/
	 /* verilog convention of [max:min].				*/

	 if ((portstart != -1) && (monotonic == FALSE)) {
	    portstart = portmax;
	    portend = portmin;
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

	       // Expand the port into one entry per bit.  If the net is
	       // the same width as the port, then every instance of an
	       // array gets the whole net.  Otherwise, the net is
	       // bit-sliced across the array of instances.  The net may
	       // be declared in either direction.

	       if (wb.start != -1) {
		  char *bptr = NULL, *cptr = NULL, cchar, *netname;
		  unsigned char is_bundle = 0;
		  struct bus wbb;
		  char portflags;

		  portflags = (wblen == scan->width) ? PORT_SHARED : PORT_SLICED;

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
	             sprintf(vname, "%s%c%d%c", scan->name, odelim, j, cdelim);
	             new_port->name = strsave(vname);
		     if (i == -1)
			 sprintf(vname, "%s", netname); 
		     else
			 sprintf(vname, "%s[%d]", netname, i); 
	             new_port->net = strsave(vname);
		     new_port->width = scan->width;
		     new_port->flags = portflags;

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
	 /* Position of the instance in the array, counted from the	*/
	 /* first (left) index, in whichever direction it runs.	*/
	 int arraypos = (arraystart > arrayend) ? arraystart - i : i - arraystart;

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
		  if ((*matchfunc)(obpinname, scan->name)) {
		     scan->flags |= PORT_FOUND;
		     break;
		  }
		  scan = scan->next;
	       }
	       if (scan == NULL) {
		  char localnet[MAX_STR_LEN];

		  /* Assume an implicit unconnected pin, unless there are no pins */
		  if (strcmp(obpinname, "(no pins)")) {
		     sprintf(localnet, "_noconnect_%d_", localcount++);
		     Node(localnet);
		     join(localnet, obptr->name);
		     Fprintf(stdout,
			 "Note:  Implicit pin %s in instance %s of %s in cell %s\n",
			 obpinname, locinst, modulename, CurrentCell->name);
		  }
	       }
	       else if (GetBus(scan->net, &wb) == 0) {
		   char *bptr2;
		   char *scanroot;
		   int isbundle = FALSE;
		   /* Skip over a bundle delimiter */
		   if (*scan->net == '{') {
		      scanroot = strsave(scan->net + 1);
		      isbundle = TRUE;
		   }
		   else
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
		       char *spos = scanroot;
		       int slice, portlen, siglen, netlen, offset;

		       /* Get the array size of the port for bit slicing */
		       portlen = (scan->width < 0) ? 1 : scan->width;

		       /* Get the full array size of the connecting bus */
		       GetBus(scanroot, &wb2);
		       siglen = wb2.start - wb2.end;
		       if (siglen < 0) siglen = -siglen;
		       siglen++;

		       /* A net that is the same width as the port connects	*/
		       /* to every instance in the array.  Otherwise, each	*/
		       /* instance gets its own slice of the net.  Ports that	*/
		       /* were expanded into bits above are already marked.	*/

		       if (scan->flags & PORT_SHARED)
			   offset = 0;
		       else if (scan->flags & PORT_SLICED)
			   offset = arraypos * portlen;
		       else {
			   netlen = wb.start - wb.end;
			   if (netlen < 0) netlen = -netlen;
			   netlen++;
			   offset = (netlen == portlen) ? 0 : arraypos * portlen;
		       }

		       // If this is a bundle, then count out to the current
		       // slice and read off the index.  NOTE:  Need to
		       // handle multiple bits per bundle entry!
		       if (isbundle) {
			   int j;
			   if (brackptr) *brackptr = '[';
			   for (j = 0; j < offset; j++) {
			       spos = strvchr(spos, ',');
			       if (spos == NULL) break;
			       spos++;
			   }
			   if (spos != NULL) {
			       brackptr = strvchr(spos, '[');
			       *brackptr = '\0';
			       sscanf(brackptr + 1, "%d", &slice);
			   }
			   else spos = scanroot;  /* should emit an error here */
		       }
		       else {	/* not a bundle */

		          // If signal array is smaller than the portlength *
		          // length of instance array, then the signal wraps.

		          // The first instance of the array gets the bits
		          // starting at wb.start, and each following instance
		          // gets the next bits in the direction of the signal.

		          if (wb2.start >= wb2.end) {
			      slice = wb.start - offset;
			      while (slice < wb2.end) slice += siglen;
		          }
		          else {
			      slice = wb.start + offset;
			      while (slice > wb2.end) slice -= siglen;
		          }
			  spos = scanroot;
		       }

		       sprintf(netname, "%s[%d]", spos, slice);
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
  char name[1024], *openname, *prevsource;

  // Make sure CurrentCell is clear
  CurrentCell = NULL;
  openname = fname;

  if ((filenum = OpenParseFile(fname, *fnum)) < 0) {

    if (strchr(fname, '.') == NULL) {
      SetExtension(name, fname, VERILOG_EXTENSION);
      if ((filenum = OpenParseFile(name, *fnum)) < 0) {
        Fprintf(stderr, "Error in Verilog file read: No file %s\n", name);
        *fnum = filenum;
        return NULL;
      }
      openname = name;
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

  if ((hashfilep != -1) && (hashfilep != *fnum)) {
      /* Started a new file, so remove all the parameters */
      RecurseHashTable(&verilogparams, freeprop);
      HashKill(&verilogparams);
      hashfilep = -1;
  }
  if ((hashfiled != -1) && (hashfiled != *fnum)) {
      /* Started a new file, so remove all the definitions */
      RecurseHashTable(&verilogdefs, freeprop);
      HashKill(&verilogdefs);
      hashfiled = -1;
  }
  if (hashfilep == -1) {
      InitializeHashTable(&verilogparams, OBJHASHSIZE);
      hashfilep = filenum;
  }
  if (hashfiled == -1) {
      InitializeHashTable(&verilogdefs, OBJHASHSIZE);
      hashfiled = filenum;
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

  prevsource = SetVerilogSource(openname);
  extcount = 0;
  ReadVerilogFile(fname, filenum, &CellStack, blackbox);
  CloseParseFile();
  RestoreVerilogSource(prevsource);

  if (extcount > 0)
     Printf("Note:  %d module%s will be elaborated by yosys (see above).\n",
		extcount, (extcount == 1) ? "" : "s");

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
  char name[MAX_STR_LEN], *openname, *prevsource;

  /* If fname does not begin with "/", then assume that it is	*/
  /* in the same relative path as its parent.			*/
  
  openname = fname;
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
	if (filenum >= 0) openname = name;
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
	   openname = name;
        }
	else {
	   fprintf(stderr,"Error in Verilog file include: No file %s\n", fname);
	   return;
        }
     }
     else
	openname = fname;
  }
  prevsource = SetVerilogSource(openname);
  ReadVerilogFile(fname, parent, CellStackPtr, blackbox);
  CloseParseFile();
  RestoreVerilogSource(prevsource);
}


/*----------------------------------------------------------------------*/
/* Read a verilog file written by yosys (see elaborate.c) into netlist	*/
/* "filenum", reading module "module" as a cell named "newname".  The	*/
/* file should contain only that module.  Returns the new cell, or	*/
/* NULL if the file could not be read, or the module was not found or	*/
/* could not be read as a netlist (e.g., it is behavioral).  Unlike	*/
/* ReadVerilogTop(), this does not make a cell for the file itself.	*/
/*----------------------------------------------------------------------*/

struct nlist *ReadVerilogModuleAs(char *fname, int filenum, char *module,
		char *newname)
{
  struct cellstack *CellStack = NULL;
  struct nlist *tp, *savecell;
  struct hashdict *savedefs;
  int fnum;

  savecell = CurrentCell;
  savedefs = definitions;
  CurrentCell = NULL;

  if (LookupCellFile(newname, filenum) != NULL) {
     Fprintf(stderr, "Cannot read module %s as %s:  cell already exists.\n",
		module, newname);
     return NULL;
  }

  if ((fnum = OpenParseFile(fname, filenum)) < 0) {
     Fprintf(stderr, "Cannot read file %s\n", fname);
     return NULL;
  }

  /* Use the verilog parameter and definition tables of this netlist	*/
  /* (the output of yosys has no parameters or definitions of its own).	*/
  if ((hashfilep != -1) && (hashfilep != filenum)) {
     RecurseHashTable(&verilogparams, freeprop);
     HashKill(&verilogparams);
     hashfilep = -1;
  }
  if (hashfilep == -1) {
     InitializeHashTable(&verilogparams, OBJHASHSIZE);
     hashfilep = filenum;
  }
  if ((hashfiled != -1) && (hashfiled != filenum)) {
     RecurseHashTable(&verilogdefs, freeprop);
     HashKill(&verilogdefs);
     hashfiled = -1;
  }
  if (hashfiled == -1) {
     InitializeHashTable(&verilogdefs, OBJHASHSIZE);
     hashfiled = filenum;
  }
  definitions = &verilogdefs;

  renamefrom = module;
  renameto = newname;
  ReadVerilogFile(fname, filenum, &CellStack, 0);
  CloseParseFile();
  renamefrom = renameto = NULL;

  while (CellStack != NULL) PopStack(&CellStack);
  definitions = savedefs;

  /* Remove the cell made for anything outside of a module, if any */
  if (LookupCellFile(fname, filenum) != NULL) CellDelete(fname, filenum);

  if (savecell != NULL)
     ReopenCellDef(savecell->name, savecell->file);
  else
     CurrentCell = NULL;

  tp = LookupCellFile(newname, filenum);
  if (tp == NULL)
     Fprintf(stderr, "Module %s not found in file %s\n", module, fname);
  return tp;
}
