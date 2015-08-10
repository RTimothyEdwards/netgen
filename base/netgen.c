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

/* netgen.c  -- most of the netlist manipulation routines and
                embedded-language specification routines.
*/

#include "config.h"

#include <stdio.h>
#include <stdlib.h>	/* for strtof() */
#include <stdarg.h>
#include <ctype.h>	/* toupper() */
#ifdef IBMPC
#include <alloc.h>
#endif

#include "netgen.h"
#include "hash.h"
#include "objlist.h"
#include "netfile.h"
#include "print.h"
#include "netcmp.h"

int Debug = 0;
int VerboseOutput = 1;  /* by default, we get verbose output */
int IgnoreRC = 0;

int NextNode;

int Composition = NONE;
int QuickSearch = 0;

int AddToExistingDefinition = 0;  /* default: overwrite cell when reopened */

extern int errno;	/* Defined in stdlib.h */

#define MAX_STATIC_STRINGS 5
static char staticstrings[MAX_STATIC_STRINGS][200];
static int laststring;

extern struct hashlist **spiceparams;	/* From spice.c */

char *Str(char *format, ...)
{
  va_list ap;

  laststring++;
  laststring = laststring % MAX_STATIC_STRINGS;

  va_start(ap, format);
  vsprintf(staticstrings[laststring], format, ap);
  va_end(ap);
  return(staticstrings[laststring]);
}

/*--------------------------------------------------------------*/
/* Push a token on to the expression stack			*/
/*--------------------------------------------------------------*/

void PushTok(int toktype, void *tval, struct tokstack **top)
{
    struct tokstack *newstack;
    double dval;
    char *string;

    newstack = (struct tokstack *)CALLOC(1, sizeof(struct tokstack));
    newstack->toktype = toktype;

    switch (toktype) {
	case TOK_DOUBLE:
	    newstack->data.dvalue = *((double *)tval);
	    break;
	case TOK_STRING:
	    newstack->data.string = strsave((char *)tval);
	    break;
	case TOK_SGL_QUOTE:
	case TOK_DBL_QUOTE:
	case TOK_FUNC_OPEN:
	case TOK_FUNC_CLOSE:
	case TOK_GROUP_OPEN:
	case TOK_GROUP_CLOSE:
	case TOK_FUNC_IF:
	case TOK_FUNC_THEN:
	case TOK_FUNC_ELSE:
	    newstack->data.dvalue = 0.0;
	    break;
	default:
	    newstack->data.string = NULL;
	    break;
    }
    newstack->last = NULL;
    newstack->next = *top;
    if (*top != NULL)
	(*top)->last = newstack;
    *top = newstack;
}

/*--------------------------------------------------------------*/
/* Pop a token off of the expression stack, freeing the memory	*/
/* associated with it.						*/
/*--------------------------------------------------------------*/

void PopTok(struct tokstack **top)
{
    struct tokstack *stackptr;

    stackptr = *top;
    if (!stackptr) return;
    *top = stackptr->next;
    (*top)->last = NULL;

    /* Free the memory allocated to the popped entry */
    if (stackptr->toktype == TOK_STRING)
	FREE(stackptr->data.string);
    FREE(stackptr);
}

/*--------------------------------------------------------------*/
/* Make a copy of an expression 				*/
/*--------------------------------------------------------------*/

struct tokstack *CopyTokStack(struct tokstack *stack)
{
    struct tokstack *stackptr, *newstack, *newptr;

    newptr = NULL;
    if (stack == NULL) return NULL;	/* Shouldn't happen. . . */
    for (stackptr = stack; stackptr->next; stackptr = stackptr->next);
    for (; stackptr; stackptr = stackptr->last) {
	newstack = (struct tokstack *)CALLOC(1, sizeof(struct tokstack));
	newstack->last = NULL;
	newstack->toktype = stackptr->toktype;
	switch (stackptr->toktype) {
	    case TOK_STRING:
		newstack->data.string = strsave(stackptr->data.string);
		break;
	    default:
		newstack->data.dvalue = stackptr->data.dvalue;
		break;
	}
	newstack->next = newptr;
	if (newptr) newptr->last = newstack;
	newptr = newstack;
    }
    return newptr;
}

/*--------------------------------------------------------------*/
/* Get a value from a token by converting a string, potentially	*/
/* using unit suffixes, into a double floating-point value.	*/
/* Return the value in "dval", and return a result of 1 if the	*/
/* value was successfully converted, 0 if there was no value to	*/
/* convert (empty string), and -1 if unable to convert the	*/
/* string to a value (e.g., unknown parameter name).		*/
/*--------------------------------------------------------------*/

int TokGetValue(char *estr, struct nlist *parent, int glob, double *dval)
{
    struct property *kl = NULL;
    int result;

    if (*estr == '\0') return 0;

    /* Grab the last numerical value */

    if (StringIsValue(estr)) {
	result = ConvertStringToFloat(estr, dval);
	if (result == 1) return 1;
    }

    /* No numerical value found.  Try substituting parameters */

    if (glob == TRUE) {
	/* Check global parameters */
	if (spiceparams != NULL) {
	    kl = (struct property *)HashLookup(estr,
				spiceparams, OBJHASHSIZE);
	    if (kl != NULL) {
		result = ConvertStringToFloat(kl->pdefault.string, dval);
	    }
	}
    }
    else {
	/* Check local parameters */
	kl = (struct property *)HashLookup(estr,
				parent->proptab, OBJHASHSIZE);
	if (kl != NULL) {
	    switch(kl->type) {
		case PROP_STRING:
		    result = ConvertStringToFloat(kl->pdefault.string, dval);
		    break;
		case PROP_DOUBLE:
		case PROP_VALUE:
		    *dval = kl->pdefault.dval;
		    result = 1;
		    break;
		case PROP_INTEGER:
		    *dval = (double)kl->pdefault.ival;
		    result = 1;
		    break;
	    }
	}
    }
    return ((result == 0) ? -1 : 1);
}

/*--------------------------------------------------------------*/
/* Work through the property list of an instance, looking for	*/
/* properties that are marked as expressions.  For each 	*/
/* expression, parse and attempt to reduce to a simpler		*/
/* expression, preferably a single value.  "glob" is TRUE when	*/
/* reading in a netlist, and substitutions should be made from	*/
/* the global parameter list.  "glob" is FALSE when elaborating	*/
/* the netlist, and substitutions should be made from the	*/
/* property list of the parent.	 If an expression resolves to a	*/
/* single value, then replace the property type.		*/
/*--------------------------------------------------------------*/

int ReduceExpressions(struct objlist *instprop,
        struct nlist *parent, int glob) {

    struct tokstack *expstack, *stackptr, *lptr, *nptr;
    struct valuelist *kv;
    struct property *kl = NULL;
    char *estr, *tstr, *sstr;
    int toktype, functype, i, result, modified, numlast;
    double dval;

    if (instprop == NULL) return 0;	// Nothing to do
    if (instprop->type != PROPERTY) return -1;	// Shouldn't happen

    for (i = 0;; i++) {

	kv = &(instprop->instance.props[i]);

	if (kv->type == PROP_EXPRESSION) {
	    expstack = kv->value.stack;
	}
	else if (kv->type == PROP_STRING) {
	    expstack = NULL;
	    estr = kv->value.string;
	    tstr = estr;

	    numlast = 0;
	    while (*tstr != '\0') {
		switch(*tstr) {
		    
		    case '+':
			if (numlast == 0) {
			    /* This is part of a number */
			    dval = strtod(estr, &sstr);
			    if (sstr > estr && sstr > tstr) {
			        tstr = sstr - 1;
			        numlast = 1;
			    }
			    break;
			}
			/* Not a number, so must be arithmetic */
			*tstr = '\0';
			result = TokGetValue(estr, parent, glob, &dval);
			if (result == 1) PushTok(TOK_DOUBLE, &dval, &expstack);
			else if (result == -1) PushTok(TOK_STRING, estr, &expstack);
			PushTok(TOK_PLUS, NULL, &expstack);
			estr = tstr + 1;
			numlast = 0;
			break;

		    case '-':
			if (numlast == 0) {
			    /* This is part of a number */
			    dval = strtod(estr, &sstr);
			    if (sstr > estr && sstr > tstr) {
			        tstr = sstr - 1;
			        numlast = 1;
			    }
			    break;
			}
			/* Not a number, so must be arithmetic */
			*tstr = '\0';
			result = TokGetValue(estr, parent, glob, &dval);
			if (result == 1) PushTok(TOK_DOUBLE, &dval, &expstack);
			else if (result == -1) PushTok(TOK_STRING, estr, &expstack);
			PushTok(TOK_MINUS, NULL, &expstack);
			estr = tstr + 1;
			numlast = 0;
			break;

		    case '1': case '2': case '3': case '4': case '5':
		    case '6': case '7': case '8': case '9': case '0':
			/* Numerical value.  Use strtod() to capture */
			if (numlast == 1) break;
			dval = strtod(estr, &sstr);
			if (sstr > estr && sstr > tstr) {
			    tstr = sstr - 1;
			    numlast = 1;
			}
			break;

		    case '/':
			*tstr = '\0';
			result = TokGetValue(estr, parent, glob, &dval);
			if (result == 1) PushTok(TOK_DOUBLE, &dval, &expstack);
			else if (result == -1) PushTok(TOK_STRING, estr, &expstack);
			PushTok(TOK_DIVIDE, NULL, &expstack);
			estr = tstr + 1;
			numlast = 0;
			break;

		    case '*':
			*tstr = '\0';
			result = TokGetValue(estr, parent, glob, &dval);
			if (result == 1) PushTok(TOK_DOUBLE, &dval, &expstack);
			else if (result == -1) PushTok(TOK_STRING, estr, &expstack);
			PushTok(TOK_MULTIPLY, NULL, &expstack);
			estr = tstr + 1;
			numlast = 0;
			break;

		    case '(':
			*tstr = '\0';

			/* Check for predefined function keywords */

			if (!strcmp(estr, "IF")) {
			    PushTok(TOK_FUNC_IF, NULL, &expstack);
			}
			else {
			    /* Treat as a parenthetical grouping */

			    result = TokGetValue(estr, parent, glob, &dval);
			    if (result == 1) PushTok(TOK_DOUBLE, &dval, &expstack);
			    else if (result == -1) PushTok(TOK_STRING, estr, &expstack);
			    PushTok(TOK_FUNC_OPEN, NULL, &expstack);
			}
			estr = tstr + 1;
			numlast = 0;
			break;

		    case ')':
			*tstr = '\0';
			if (expstack == NULL) break;
			switch (expstack->toktype) {
			    case TOK_FUNC_THEN:
				PushTok(TOK_FUNC_ELSE, NULL, &expstack);
				break;
			    default:
				PushTok(TOK_FUNC_CLOSE, NULL, &expstack);
				break;
			}
			numlast = 1;
			estr = tstr + 1;
			break;

		    case '\'':
			*tstr = '\0';
			result = TokGetValue(estr, parent, glob, &dval);
			if (result == 1) PushTok(TOK_DOUBLE, &dval, &expstack);
			else if (result == -1) PushTok(TOK_STRING, estr, &expstack);
			PushTok(TOK_SGL_QUOTE, NULL, &expstack);
			estr = tstr + 1;
			numlast = 0;
			break;

		    case '"':
			*tstr = '\0';
			result = TokGetValue(estr, parent, glob, &dval);
			if (result == 1) PushTok(TOK_DOUBLE, &dval, &expstack);
			else if (result == -1) PushTok(TOK_STRING, estr, &expstack);
			PushTok(TOK_DBL_QUOTE, NULL, &expstack);
			estr = tstr + 1;
			numlast = 0;
			break;

		    case '{':
			*tstr = '\0';
			result = TokGetValue(estr, parent, glob, &dval);
			if (result == 1) PushTok(TOK_DOUBLE, &dval, &expstack);
			else if (result == -1) PushTok(TOK_STRING, estr, &expstack);
			PushTok(TOK_GROUP_OPEN, NULL, &expstack);
			estr = tstr + 1;
			numlast = 0;
			break;

		    case '}':
			*tstr = '\0';
			result = TokGetValue(estr, parent, glob, &dval);
			if (result == 1) PushTok(TOK_DOUBLE, &dval, &expstack);
			PushTok(TOK_GROUP_CLOSE, NULL, &expstack);
			estr = tstr + 1;
			numlast = 1;
			break;

		    case '!':
			if (*(tstr + 1) == '=') {
			    *tstr = '\0';
			    result = TokGetValue(estr, parent, glob, &dval);
			    if (result == 1) PushTok(TOK_DOUBLE, &dval, &expstack);
			    else if (result == -1) PushTok(TOK_STRING, estr, &expstack);
			    PushTok(TOK_NE, NULL, &expstack);
			}
			numlast = 0;
			break;

		    case '=':
			if (*(tstr + 1) == '=') {
	 		    *tstr = '\0';
	 		    result = TokGetValue(estr, parent, glob, &dval);
	 		    if (result == 1) PushTok(TOK_DOUBLE, &dval, &expstack);
	 		    else if (result == -1) PushTok(TOK_STRING, estr, &expstack);
			    PushTok(TOK_EQ, NULL, &expstack);
			    numlast = 0;
			}
			break;

		    case '>':
			*tstr = '\0';
			result = TokGetValue(estr, parent, glob, &dval);
			if (result == 1) PushTok(TOK_DOUBLE, &dval, &expstack);
			else if (result == -1) PushTok(TOK_STRING, estr, &expstack);

			if (*(tstr + 1) == '=') {
			    PushTok(TOK_GE, NULL, &expstack);
			    tstr++;
			}
			else
			    PushTok(TOK_GT, NULL, &expstack);
			estr = tstr + 1;
			numlast = 0;
			break;

		    case '<':
			*tstr = '\0';
			result = TokGetValue(estr, parent, glob, &dval);
			if (result == 1) PushTok(TOK_DOUBLE, &dval, &expstack);
			else if (result == -1) PushTok(TOK_STRING, estr, &expstack);

			if (*(tstr + 1) == '=') {
			    PushTok(TOK_LE, NULL, &expstack);
			    tstr++;
			}
			else
			    PushTok(TOK_LT, NULL, &expstack);
			estr = tstr + 1;
			numlast = 0;
			break;

		    case ',':
			*tstr = '\0';
			result = TokGetValue(estr, parent, glob, &dval);
			if (result == 1) PushTok(TOK_DOUBLE, &dval, &expstack);
			else if (result == -1) PushTok(TOK_STRING, estr, &expstack);
			if (expstack == NULL) break;
			lptr = expstack;
			while (lptr->next) {
			    lptr = lptr->next;
			    if (lptr->toktype == TOK_FUNC_THEN) {
				PushTok(TOK_FUNC_ELSE, NULL, &expstack);
				break;
			    }
			    else if (lptr->toktype == TOK_FUNC_IF) {
				PushTok(TOK_FUNC_THEN, NULL, &expstack);
				break;
			    }
			}
			estr = tstr + 1;
			numlast = 0;
			break;

		    default:
			break;
		}
		tstr++;
	    }
	    result = TokGetValue(estr, parent, glob, &dval);
	    if (result == 1) PushTok(TOK_DOUBLE, &dval, &expstack);
	    else if (result == -1) PushTok(TOK_STRING, estr, &expstack);

	    FREE(kv->value.string);
	    kv->value.stack = expstack;
	    kv->type = PROP_EXPRESSION;
	}

	// Find the beginning of the expression, which is the bottom of
	// the stack.
	for (stackptr = kv->value.stack; stackptr != NULL &&
			stackptr->next != NULL; stackptr = stackptr->next);

	// For each pass, start at the bottom and work forward
	expstack = stackptr;

	modified = 1;
	while (modified) {
	    double dval1, dval2;

	    modified = 0;

	    // Reduce conditionals

	    for (stackptr = expstack; stackptr != NULL; stackptr = stackptr->last) {
		switch (stackptr->toktype) {
		    case TOK_LE:
		    case TOK_LT:
		    case TOK_GE:
		    case TOK_GT:
		    case TOK_EQ:
		    case TOK_NE:
			lptr = stackptr->last;
			nptr = stackptr->next;
			if (lptr && nptr && (lptr->toktype == TOK_DOUBLE) &&
					(nptr->toktype == TOK_DOUBLE)) {
			    
			    switch (stackptr->toktype) {
				case TOK_LE:
				    if (nptr->data.dvalue <= lptr->data.dvalue) {
					stackptr->data.dvalue = 1.0;
				    }
				    else {
					stackptr->data.dvalue = 0.0;
				    }
				    break;
				case TOK_LT:
				    if (nptr->data.dvalue < lptr->data.dvalue) {
					stackptr->data.dvalue = 1.0;
				    }
				    else {
					stackptr->data.dvalue = 0.0;
				    }
				    break;
				case TOK_GE:
				    if (nptr->data.dvalue >= lptr->data.dvalue) {
					stackptr->data.dvalue = 1.0;
				    }
				    else {
					stackptr->data.dvalue = 0.0;
				    }
				    break;
				case TOK_GT:
				    if (nptr->data.dvalue > lptr->data.dvalue) {
					stackptr->data.dvalue = 1.0;
				    }
				    else {
					stackptr->data.dvalue = 0.0;
				    }
				    break;
				case TOK_EQ:
				    if (nptr->data.dvalue == lptr->data.dvalue) {
					stackptr->data.dvalue = 1.0;
				    }
				    else {
					stackptr->data.dvalue = 0.0;
				    }
				    break;
				case TOK_NE:
				    if (nptr->data.dvalue != lptr->data.dvalue) {
					stackptr->data.dvalue = 1.0;
				    }
				    else {
					stackptr->data.dvalue = 0.0;
				    }
				    break;
			    }
			    modified = 1;
			    stackptr->toktype = TOK_DOUBLE;
			    stackptr->last = lptr->last;
			    if (lptr->last) lptr->last->next = stackptr;
			    else kv->value.stack = stackptr;
			    stackptr->next = nptr->next;
			    if (nptr->next) nptr->next->last = stackptr;
			    if (expstack == nptr) expstack = stackptr;

			    FREE(nptr);
			    FREE(lptr);
			}
		}
	    }

	    // Reduce IF(a,b,c)

	    for (stackptr = expstack; stackptr != NULL; stackptr = stackptr->last) {
		struct tokstack *ifptr, *thenptr;
		if (stackptr->toktype == TOK_FUNC_IF) {
		    ifptr = stackptr->last;
		    if (ifptr->toktype == TOK_DOUBLE) {
			stackptr->toktype = TOK_FUNC_OPEN;
			if (ifptr->data.dvalue == 0.0) {
			    /* Keep ELSE value, remove IF and THEN */
			    for (thenptr = ifptr; thenptr->toktype !=
					TOK_FUNC_ELSE; ) {
				lptr = thenptr->last;
				nptr = thenptr->next;
				lptr->next = nptr;
				nptr->last = lptr;
				if (thenptr->toktype == TOK_STRING)
				    FREE(thenptr->data.string);
				FREE(thenptr);
				thenptr = lptr;
			    }
			    /* Free the TOK_FUNC_ELSE record */
			    lptr = thenptr->last;
			    nptr = thenptr->next;
			    lptr->next = nptr;
			    nptr->last = lptr;
			    FREE(thenptr);
			    modified = 1;
			}
			else {
			    /* Keep THEN value, remove IF and ELSE */
			    /* Free the conditional result value record */
			    lptr = ifptr->last;
			    nptr = ifptr->next;
			    lptr->next = nptr;
			    nptr->last = lptr;
			    FREE(ifptr);
			    thenptr = nptr;

			    /* Free the TOK_FUNC_THEN record */
			    lptr = thenptr->last;
			    nptr = thenptr->next;
			    lptr->next = nptr;
			    nptr->last = lptr;
			    FREE(thenptr);

			    /* Free to end of IF block */
			    for (thenptr = nptr->last; thenptr->toktype !=
					TOK_FUNC_CLOSE; ) {
				lptr = thenptr->last;
				nptr = thenptr->next;
				lptr->next = nptr;
				nptr->last = lptr;
				if (thenptr->toktype == TOK_STRING)
				    FREE(thenptr->data.string);
				FREE(thenptr);
				thenptr = lptr;
			    }
			    modified = 1;
			}
		    }
		}
	    }
		 
	    // Reduce (value) * (value) and (value) / (value)

	    for (stackptr = expstack; stackptr != NULL; stackptr = stackptr->last) {
		switch (stackptr->toktype) {
		    case TOK_MULTIPLY:
		    case TOK_DIVIDE:
			lptr = stackptr->last;
			nptr = stackptr->next;
			if (lptr && nptr && (lptr->toktype == TOK_DOUBLE) &&
					(nptr->toktype == TOK_DOUBLE)) {

			    if (stackptr->toktype == TOK_MULTIPLY)
				stackptr->data.dvalue = nptr->data.dvalue *
					lptr->data.dvalue;
			    else
				stackptr->data.dvalue = nptr->data.dvalue /
					lptr->data.dvalue;

			    modified = 1;
			    stackptr->toktype = TOK_DOUBLE;
			    stackptr->last = lptr->last;
			    if (lptr->last) lptr->last->next = stackptr;
			    else kv->value.stack = stackptr;
			    stackptr->next = nptr->next;
			    if (nptr->next) nptr->next->last = stackptr;
			    if (expstack == nptr) expstack = stackptr;

			    FREE(nptr);
			    FREE(lptr);
			}
		}
	    }

	    // Reduce (value) + (value) and (value) - (value)

	    for (stackptr = expstack; stackptr != NULL; stackptr = stackptr->last) {
		switch (stackptr->toktype) {
		    case TOK_PLUS:
		    case TOK_MINUS:
			lptr = stackptr->last;
			nptr = stackptr->next;
			if (lptr && nptr && (lptr->toktype == TOK_DOUBLE) &&
					(nptr->toktype == TOK_DOUBLE)) {

			    if (stackptr->toktype == TOK_PLUS)
				stackptr->data.dvalue = nptr->data.dvalue +
					lptr->data.dvalue;
			    else
				stackptr->data.dvalue = nptr->data.dvalue -
					lptr->data.dvalue;

			    modified = 1;
			    stackptr->toktype = TOK_DOUBLE;
			    stackptr->last = lptr->last;
			    if (lptr->last) lptr->last->next = stackptr;
			    else kv->value.stack = stackptr;
			    stackptr->next = nptr->next;
			    if (nptr->next) nptr->next->last = stackptr;
			    if (expstack == nptr) expstack = stackptr;

			    FREE(nptr);
			    FREE(lptr);
			}
		}
	    }

	    // Reduce {value}, (value), and 'value'

	    for (stackptr = expstack; stackptr != NULL; stackptr = stackptr->last) {
		switch (stackptr->toktype) {
		    case TOK_DOUBLE:
			lptr = stackptr->last;
			nptr = stackptr->next;
			if (lptr && nptr &&
				(((nptr->toktype == TOK_FUNC_OPEN) &&
					(lptr->toktype == TOK_FUNC_CLOSE)) ||
				((nptr->toktype == TOK_GROUP_OPEN) &&
					(lptr->toktype == TOK_GROUP_CLOSE)) ||
				((nptr->toktype == TOK_DBL_QUOTE) &&
					(lptr->toktype == TOK_DBL_QUOTE)) ||
				((nptr->toktype == TOK_SGL_QUOTE) &&
					(lptr->toktype == TOK_SGL_QUOTE)))) {

			    modified = 1;
			    stackptr->last = lptr->last;
			    if (lptr->last) lptr->last->next = stackptr;
			    else kv->value.stack = stackptr;
			    stackptr->next = nptr->next;
			    if (nptr->next) nptr->next->last = stackptr;
			    if (expstack == nptr) expstack = stackptr;

			    FREE(nptr);
			    FREE(lptr);
			}
			break;
		}
	    }

	    // Replace value if string can be substituted with a number

	    for (stackptr = expstack; stackptr != NULL; stackptr = stackptr->last) {
		switch (stackptr->toktype) {
		    case TOK_STRING:
			result = TokGetValue(stackptr->data.string, parent,
				glob, &dval);
			if (result == 1) {
			    stackptr->toktype = TOK_DOUBLE;
			    FREE(stackptr->data.string);
			    stackptr->data.dvalue = dval;
			    modified = 1;
			}
			break;
		}
	    }
	}

	// Replace the expression with the reduced expression or
	// value.

	expstack = kv->value.stack;	// Now pointing at the end

	if (expstack && expstack->next == NULL) {
	    if (expstack->toktype == TOK_DOUBLE) {
		kv->type = PROP_DOUBLE;
		kv->value.dval = expstack->data.dvalue;
	    }
	    else if (expstack->toktype == TOK_STRING) {
		kv->type = PROP_STRING;
		kv->value.string = strsave(expstack->data.string);
	    }
	}
	else {
	    // Still an expression;  do nothing
	}

	// Free up the stack if it's not being used

	if (kv->type != PROP_EXPRESSION)
	{
	    while (expstack != NULL) {
		nptr = expstack->next;
		if (expstack->toktype == TOK_STRING)
		    FREE(expstack->data.string);
		FREE(expstack);
		expstack = nptr;
	    }
	}

        if (kv->type == PROP_ENDLIST)
	    break;
    }

    return 0;
}

/*----------------------------------------------------------------------*/
/* Delete a property from the master cell record.			*/
/*----------------------------------------------------------------------*/

int
PropertyDelete(char *name, int fnum, char *key)
{
    struct property *kl = NULL;
    struct nlist *tc;
    int result;

    if ((fnum == -1) && (Circuit1 != NULL) && (Circuit2 != NULL)) {
	result = PropertyDelete(name, Circuit1->file, key);
	result = PropertyDelete(name, Circuit2->file, key);
	return result;
    }

    tc = LookupCellFile(name, fnum);
    if (tc == NULL) {
	Printf("No device %s found for PropertyDelete()\n", name);
	return -1;
    }
    else if (key == NULL) {

	/* key == NULL means delete all properties. */

	RecurseHashTable(tc->proptab, OBJHASHSIZE, freeprop);
	HashKill(tc->proptab, OBJHASHSIZE);
	FREE(tc->proptab);
	tc->proptab = (struct hashlist **)CALLOC(OBJHASHSIZE,
                 sizeof(struct hashlist *));
    }
    else {
	kl = (struct property *)HashLookup(key, tc->proptab, OBJHASHSIZE);
	if (kl != NULL) {
	    if (kl->type == PROP_STRING || kl->type == PROP_EXPRESSION)
		FREE(kl->pdefault.string);
	    FREE(kl->key);
	    HashDelete(key, tc->proptab, OBJHASHSIZE);
	}
	else {
	    Printf("No property %s found for device %s\n", key, name);
	    return -1;
	}
    }
    return 0;
}

/*----------------------------------------------------------------------*/
/* Set the tolerance of a property in the master cell record.		*/
/*----------------------------------------------------------------------*/

int
PropertyTolerance(char *name, int fnum, char *key, int ival, double dval)
{
    struct property *kl = NULL;
    struct nlist *tc;
    int result;

    if ((fnum == -1) && (Circuit1 != NULL) && (Circuit2 != NULL)) {
	result = PropertyTolerance(name, Circuit1->file, key, ival, dval);
	result = PropertyTolerance(name, Circuit2->file, key, ival, dval);
	return result;
    }

    tc = LookupCellFile(name, fnum);
    if (tc == NULL) {
	Printf("No device %s found for PropertyTolerance()\n", name);
	return -1;
    }

    kl = (struct property *)HashLookup(key, tc->proptab, OBJHASHSIZE);
    if (kl == NULL) {
	Printf("No property %s found for device %s\n", key, name);
	return -1;
    }
    else {
        switch (kl->type) {
	    case PROP_DOUBLE:
	    case PROP_VALUE:
		kl->slop.dval = dval;
		break;
	    case PROP_INTEGER:
	    case PROP_STRING:
	    case PROP_EXPRESSION:
		kl->slop.ival = ival;
		break;
	}
    }
    return 0;
}

/*----------------------------------------------------------------------*/
/* Add a new value property to the indicated cell			*/
/* Value properties are used for resistors and capacitors in SPICE	*/
/* netlists where the value is not syntactically like other properties.	*/
/* For the purpose of netgen, it is treated like a PROP_DOUBLE except	*/
/* when reading and writing netlist files.				*/
/*----------------------------------------------------------------------*/

struct property *PropertyValue(char *name, int fnum, char *key,
		double slop, double pdefault)
{
   struct property *kl = NULL;
   struct nlist *tc;

   if ((fnum == -1) && (Circuit1 != NULL) && (Circuit2 != NULL)) {
      PropertyValue(name, Circuit1->file, key, slop, pdefault);
      PropertyValue(name, Circuit2->file, key, slop, pdefault);
      return;
   }

   tc = LookupCellFile(name, fnum);
   if (tc == NULL) 
      Printf("No device %s found for PropertyValue()\n", name);
   else if ((kl = (struct property *)HashLookup(key, tc->proptab,
		OBJHASHSIZE)) != NULL) {
      Printf("Device %s already has property named \"%s\"\n", name, key);
   }
   else {
      kl = NewProperty();
      kl->key = strsave(key);
      kl->idx = 0;
      kl->type = PROP_VALUE;
      kl->slop.dval = slop;
      kl->pdefault.dval = pdefault;
      HashPtrInstall(kl->key, kl, tc->proptab, OBJHASHSIZE);
   }
   return kl;
}

/*----------------------------------------------------------------------*/
/* Add a new double-valued property key to the current cell		*/
/*----------------------------------------------------------------------*/

struct property *PropertyDouble(char *name, int fnum, char *key,
			double slop, double pdefault)
{
   struct property *kl = NULL;
   struct nlist *tc;

   if ((fnum == -1) && (Circuit1 != NULL) && (Circuit2 != NULL)) {
      PropertyDouble(name, Circuit1->file, key, slop, pdefault);
      PropertyDouble(name, Circuit2->file, key, slop, pdefault);
      return;
   }

   tc = LookupCellFile(name, fnum);
   if (tc == NULL) 
      Printf("No device %s found for PropertyDouble()\n", name);
   else if ((kl = (struct property *)HashLookup(key, tc->proptab,
		OBJHASHSIZE)) != NULL) {
      Printf("Device %s already has property named \"%s\"\n", name, key);
   }
   else {
      kl = NewProperty();
      kl->key = strsave(key);
      kl->idx = 0;
      kl->type = PROP_DOUBLE;
      kl->slop.dval = slop;
      kl->pdefault.dval = pdefault;
      HashPtrInstall(kl->key, kl, tc->proptab, OBJHASHSIZE);
   }
   return kl;
}

/*----------------------------------------------------------------------*/

struct property *PropertyInteger(char *name, int fnum, char *key,
			int slop, int pdefault)
{
   struct property *kl = NULL;
   struct nlist *tc;

   if ((fnum == -1) && (Circuit1 != NULL) && (Circuit2 != NULL)) {
      PropertyInteger(name, Circuit1->file, key, slop, pdefault);
      PropertyInteger(name, Circuit2->file, key, slop, pdefault);
      return;
   }

   tc = LookupCellFile(name, fnum);
   if (tc == NULL) 
      Printf("No device %s found for PropertyInteger()\n", name);
   else if ((kl = (struct property *)HashLookup(key, tc->proptab,
		OBJHASHSIZE)) != NULL) {
      Printf("Device %s already has property named \"%s\"\n", name, key);
   }
   else {
      kl = NewProperty();
      kl->key = strsave(key);
      kl->idx = 0;
      kl->type = PROP_INTEGER;
      kl->slop.ival = slop;
      kl->pdefault.ival = pdefault;
      HashPtrInstall(kl->key, kl, tc->proptab, OBJHASHSIZE);
   }
   return kl;
}

/*----------------------------------------------------------------------*/

struct property *PropertyString(char *name, int fnum, char *key, int range,
		char *pdefault)
{
   struct property *kl = NULL;
   struct nlist *tc;

   if ((fnum == -1) && (Circuit1 != NULL) && (Circuit2 != NULL)) {
      PropertyString(name, Circuit1->file, key, range, pdefault);
      PropertyString(name, Circuit2->file, key, range, pdefault);
      return;
   }

   tc = LookupCellFile(name, fnum);
   if (tc == NULL) 
      Printf("No device %s found for PropertyString()\n", name);
   else if ((kl = (struct property *)HashLookup(key, tc->proptab,
		OBJHASHSIZE)) != NULL) {
      Printf("Device %s already has property named \"%s\"\n", name, key);
   }
   else {
      kl = NewProperty();
      kl->key = strsave(key);
      kl->idx = 0;
      kl->type = PROP_STRING;
      kl->slop.ival = (range >= 0) ? range : 0;
      if (pdefault != NULL)
         kl->pdefault.string = strsave(pdefault);
      else
         kl->pdefault.string = NULL;
      HashPtrInstall(kl->key, kl, tc->proptab, OBJHASHSIZE);
   }
   return kl;
}

/*----------------------------------------------------------------------*/
/* Find all instances of type "instance" in cell "name".  For each one,	*/
/* determine if it can be combined in parallel with other devices.	*/
/* Capacitors add area or value.  Resistors add width, or run a		*/
/* parallel calculation on value.  Transistors add width.		*/
/* If "aggressive" is FALSE, then combinations may be made only if	*/
/* the devices have the same properties (except "M"), and will combine	*/
/* by incrementing "M".  If "aggressive" is TRUE, then the device	*/
/* properties (width, area, etc.) will be modified in combination.	*/
/* Devices with pin permutations are parallel if nodes match when	*/
/* permuted.								*/
/*----------------------------------------------------------------------*/

void CombineParallel(char *name, int fnum, char *instance, int aggressive)
{
    struct nlist *tc, *tp;
    struct objlist *ob1, *ob2;

    if ((tc = LookupCellFile(name, fnum)) == NULL) return;
    if ((tp = LookupCellFile(instance, fnum)) == NULL) return;

    for (ob1 = tc->cell; ob1; ob1 = ob1->next) {
	if (ob1->type == FIRSTPIN && (*matchfunc)(ob1->model.class, tc->name)) {
	    for (ob2 = ob1->next; ob2; ob2 = ob2->next) {
		if (ob2->type == FIRSTPIN && (*matchfunc)(ob2->model.class,
				tc->name)) {
		    /* To-do:  Handle permutations! */
		    while (ob1->node == ob2->node) {
			ob1 = ob1->next;
			ob2 = ob2->next;
			if (ob1 == NULL || ob1->type <= FIRSTPIN) break;
			if (ob2 == NULL || ob2->type <= FIRSTPIN) break;
		    }
		    if ((ob1 && ob1->type == PROPERTY) &&
				(ob2 && ob2->type == PROPERTY)) {
			if (PropertyMatch(ob1, ob2, FALSE)) {
			    /* WIP */
			}
		    }
		}
	    }
	}
    }
}

/*----------------------------------------------------------------------*/
/* Declare the element class of the current cell			*/
/*----------------------------------------------------------------------*/

void SetClass(unsigned char class)
{
	if (CurrentCell == NULL) 
	  Printf("No current cell for SetClass()\n");
	else
	  CurrentCell->class = class;
}

/*----------------------------------------------------------------------*/

void ReopenCellDef(char *name, int fnum)
{ 	
  struct objlist *ob;

  if (Debug) Printf("Reopening cell definition: %s\n",name);
  GarbageCollect();
  if ((CurrentCell = LookupCellFile(name, fnum)) == NULL) {
    Printf("Undefined cell: %s\n", name);
    return;
  }
  /* cell exists, so append to the end of it */
  NextNode = 1;
  CurrentTail = CurrentCell->cell;
  for (ob = CurrentTail; ob != NULL; ob = ob->next) {
    CurrentTail = ob;
    if (ob->node >= NextNode) NextNode = ob->node + 1;
  }
}

/*----------------------------------------------------------------------*/

void CellDef(char *name, int fnum)
{
    struct nlist *np;

	if (Debug) Printf("Defining cell: %s\n",name);
	GarbageCollect();
	if ((CurrentCell = LookupCellFile(name, fnum)) != NULL) {
	  if (AddToExistingDefinition) {
	    ReopenCellDef(name, fnum);
	    return ;
	  }
	  else {
	    Printf("Cell: %s exists already, and will be overwritten.\n", name);
	    CellDelete(name, fnum);
	  }
	}
	/* install a new cell in lookup table (hashed) */
	np = InstallInCellHashTable(name, fnum);
	CurrentCell = LookupCellFile(name, fnum);
	CurrentCell->class = CLASS_SUBCKT;	/* default */
	CurrentCell->flags = 0;

	LastPlaced = NULL;
	CurrentTail = NULL;
	FreeNodeNames(CurrentCell);
	NextNode = 1;
}

/*----------------------------------------------------------------------*/
/* Same as CellDef() above, but mark cell as case-insensitive.		*/
/* This routine is used only by	the ReadSpice() function.		*/
/*----------------------------------------------------------------------*/

void CellDefNoCase(char *name, int file)
{
   CellDef(name, file);
   CurrentCell->file = file;
   CurrentCell->flags |= CELL_NOCASE;
}

/*----------------------------------------------------------------------*/

int IsIgnored(char *name, int file)
{
    struct IgnoreList *ilist;
    char *nptr = name;

    for (ilist = ClassIgnore; ilist; ilist = ilist->next)
    {
	if ((file == -1) || (ilist->file == -1) || (file == ilist->file)) 
	    if ((*matchfunc)(ilist->class, nptr))
		return 1;
    }
   return 0;
}

/*----------------------------------------------------------------------*/

void Port(char *name)
{
	struct objlist *tp;
	
	if (Debug) Printf("   Defining port: %s\n",name);
	if ((tp = GetObject()) == NULL) {
	  perror("Failed GetObject in Port");
	  return;
	}
	tp->type = PORT;  /* port type */
	if (name == NULL) {
	   // Name becomes "no pins" and shows up in the pin matching
	   // output for both devices.
	   tp->name = strsave("(no pins)");
	   tp->model.port = PROXY;
	}
	else {
	   tp->name = strsave(name);
	   tp->model.port = PORT;
	}
	tp->instance.name = NULL;
	tp->node = -1;  /* null node */
	tp->next = NULL;
	AddToCurrentCell (tp);
}

/*----------------------------------------------------------------------*/

int CountPorts(char *name, int fnum)
{
    struct nlist *tc;
    struct objlist *ob;
    int ports = 0;

    tc = LookupCellFile(name, fnum);
    if (tc != NULL) {
	for (ob = tc->cell; ob; ob = ob->next) {
	    if (ob->type != PORT) break;
	    ports++;
	}
    } 
    return ports;
}

/*----------------------------------------------------------------------*/

void Node(char *name)
{
	struct objlist *tp;
	
	if (Debug) Printf("   Defining internal node: %s\n",name);
	if ((tp = GetObject()) == NULL) {
	  perror("Failed GetObject in Node");
	  return;
	}
	tp->name = strsave(name);
	tp->type = NODE;  /* internal node type */
	tp->model.class = NULL;
	tp->instance.name = NULL;
	tp->node = -1;  /* null node */
	tp->next = NULL;
	AddToCurrentCell (tp);
}

/*----------------------------------------------------------------------*/

void Global(char *name)
{
    struct objlist *tp;

    // Check if "name" is already in the current cell as a global node
    // or a port.  If it is, then we're done.  Otherwise, add "name" as
    // a new global in CurrentCell.

    for (tp = CurrentCell->cell; tp; tp = tp->next)
	if (tp->type == GLOBAL || tp->type == UNIQUEGLOBAL || tp->type == PORT)
	    if ((*matchfunc)(tp->name, name))
		return;
	
    if (Debug) Printf("   Defining global node: %s\n",name);
    if ((tp = GetObject()) == NULL) {
	perror("Failed GetObject in Global");
	return;
    }
    tp->name = strsave(name);
    tp->type = GLOBAL;		/* internal node type */
    tp->model.class = NULL;
    tp->instance.name = NULL;
    tp->node = -1;		/* null node */
    tp->next = NULL;
    AddToCurrentCell (tp);
}

/*----------------------------------------------------------------------*/

void UniqueGlobal(char *name)
{
	struct objlist *tp;
	
	if (Debug) Printf("   Defining unique global node: %s\n",name);
	if ((tp = GetObject()) == NULL) {
	  perror("Failed GetObject in UniqueGlobal");
	  return;
	}

	tp->name = strsave(name);
	tp->type = UNIQUEGLOBAL;  /* internal node type */
	tp->model.class = NULL;
	tp->instance.name = NULL;
	tp->node = -1;  /* null node */
	tp->next = NULL;
	AddToCurrentCell (tp);
}

/*----------------------------------------------------------------------*/

void Instance(char *model, char *instancename)
{
  struct objlist *tp, *tp2;
  struct nlist *instanced_cell;
  int portnum;
  char tmpname[512], tmpname2[512];
  int firstobj, fnum;
	
  if (Debug) Printf("   Instance: %s of class: %s\n",
		    instancename, model);
  if (CurrentCell == NULL) {
    Printf("No current cell for Instance(%s,%s)\n", model,instancename);
    return;
  }
  fnum = CurrentCell->file;
  if (IsIgnored(model, fnum)) {
    Printf("Class '%s' instanced in input but is being ignored.\n", model);
    return;
  }
  instanced_cell = LookupCellFile(model, fnum);
  if (instanced_cell == NULL) {
    Printf("Attempt to instance undefined model '%s'\n", model);
    return;
  }
  /* class exists */
  instanced_cell->number++;		/* one more allocated */
  portnum = 1;
  firstobj = 1;
  for (tp2 = instanced_cell->cell; tp2 != NULL; tp2 = tp2->next) 
    if (IsPort(tp2)) {
      /* it is a port */
      tp = GetObject();
      if (tp == NULL) {
	perror("Failed GetObject in Instance()");
	return;
      }
      strcpy(tmpname,instancename);
      strcat(tmpname,SEPARATOR);
      strcat(tmpname,tp2->name);
      tp->name = strsave(tmpname);
      tp->model.class = strsave(model);
      tp->instance.name = strsave(instancename);
      tp->type = portnum++;	/* instance type */
      tp->node = -1;		/* null node */
      tp->next = NULL;
      AddToCurrentCell (tp);
      if (firstobj) {
	AddInstanceToCurrentCell(tp);
	firstobj = 0;
      }
    }
  /* now run through list of new objects, processing global ports */
  for (tp2 = instanced_cell->cell; tp2 != NULL; tp2 = tp2->next) { 
    /* check to see if it is a global port */
    if (tp2->type == GLOBAL) {
      if (Debug) Printf("   processing global port: %s\n",
			tp2->name);
      strcpy(tmpname,instancename);
      strcat(tmpname,SEPARATOR);
      strcat(tmpname,tp2->name);
      /* see if element already exists */
      if (LookupObject(tp2->name,CurrentCell) != NULL)
	join(tp2->name, tmpname);
      else {
	/* define global node if not already there */
	Global(tp2->name);
	join(tp2->name, tmpname);
      }
    }
    else if (tp2->type == UNIQUEGLOBAL) {
      if (Debug) Printf("   processing unique global port: %s\n",
			tp2->name);
      strcpy(tmpname,CurrentCell->name);
      strcat(tmpname,INSTANCE_DELIMITER);
      strcat(tmpname,instancename);
      strcat(tmpname,SEPARATOR);
      strcat(tmpname,tp2->name);
      /* make this element UniqueGlobal */
      UniqueGlobal(tmpname);
      strcpy(tmpname2,instancename);
      strcat(tmpname2,SEPARATOR);
      strcat(tmpname2,tp2->name);
      Connect(tmpname,tmpname2);
    }
  }
  /* now run through list of new objects, checking for shorted ports */
  for (tp2 = instanced_cell->cell; tp2 != NULL; tp2 = tp2->next) {
    /* check to see if it is a unique port */
    /* remember to NOT consider unconnected ports (node = -1) 
       as being shorted out */

    if (IsPort(tp2)) {
      struct objlist *ob;

      ob = LookupObject(tp2->name, instanced_cell);
      if (ob->node != -1 && !(*matchfunc)(tp2->name, 
			NodeAlias(instanced_cell, ob))) {
	if (Debug) Printf("shorted ports found on Instance\n");
	strcpy(tmpname,instancename);
	strcat(tmpname,SEPARATOR);
	strcat(tmpname,tp2->name);
	strcpy(tmpname2,instancename);
	strcat(tmpname2,SEPARATOR);
	strcat(tmpname2, NodeAlias(instanced_cell, ob));
	join(tmpname,tmpname2);
      }
    }
  }
}

/*----------------------------------------------------------------------*/

char *Next(char *name)
{
    int filenum = CurrentCell->file;

	/* generate a unique instance name with 'name') */
	char buffer[1024];
	int n;
	
	n = 0;
	if (QuickSearch) {
	  struct nlist *tp;
	  tp = LookupCellFile(name, filenum);
	  if (tp != NULL)
	    n = tp->number; /* was +1, but would miss #2 */
	}
	do {
	  n++;
	  sprintf(buffer, "%s%d", name, n);
	} while (LookupInstance(buffer,CurrentCell) != NULL);
	return (strsave(buffer));
}

/*
 *---------------------------------------------------------------------
 * This procedure provides a versatile interface to Instance/Connect.
 * Cell() accepts a variable length list of arguments, in either of
 * two forms:  (i) named arguments -- take the form "port=something"
 * (ii) unnamed arguments, which are bound to ports in the order they
 * appear. Arguments are read until all cell ports have been connected,
 * or until a NULL is encountered.
 *
 * Returns the name of the instance, which remains valid at least
 * until the next call to Cell().
 *---------------------------------------------------------------------
 */

char *Cell(char *inststr, char *model, ...)
{
  va_list ap;
  char *nodelist;
  char tmpname[512];
  struct nlist *instanced_cell;
  struct objlist *head, *tp, *tp2;
  struct objlist *namedporthead, *namedportp, *namedlisthead, *namedlistp;
  int portnum, portlist, done;
  char namedport[512]; /* tmp buffers */
  int filenum;

  static char *instancename = NULL;
  char *instnameptr;

  if (CurrentCell == NULL) {
     Printf("No current cell defined for call to Cell().\n");
     return NULL;
  }
  else
     filenum = CurrentCell->file;
	
  if (Debug) Printf("   calling cell: %s\n",model);
  if (IsIgnored(model, filenum)) {
    Printf("Class '%s' instanced in input but is being ignored.\n", model);
    return NULL;
  }
  instanced_cell = LookupCellFile(model, filenum);
  if (instanced_cell == NULL) {
    Printf("Attempt to instance undefined class '%s'\n", model);
    return NULL;
  }
  /* class exists */
  tp2 = instanced_cell->cell;
  portnum = 0;
  while (tp2 != NULL) {
    if (IsPort(tp2)) portnum++;
    tp2 = tp2->next;
  }
	
  /* now generate lists of nodes using variable length parameter list */
  va_start(ap, model);
  head = NULL;
  namedporthead = namedlisthead = NULL;
  done = 0;
  portlist = 0;
  while (!done && portlist < portnum) {
    struct objlist *tmp;
    char *equals;

    nodelist = va_arg(ap, char *);
    if (nodelist == NULL) break; /* out of while loop */

    if (strchr(nodelist,'=') != NULL) {
      /* we have a named element */
      struct objlist *tmpport, *tmpname;
      struct nlist *oldCurCell;
      int ports;

      strcpy(namedport, nodelist);
      equals = strchr(namedport, '=');
      *equals = '\0';
      equals++;  /* point to first char of node */

      /* need to get list out of cell: 'model' */
      oldCurCell = CurrentCell;
      CurrentCell = instanced_cell;
      tmpport = List(namedport);
      CurrentCell = oldCurCell;
      tmpname = List(equals);

      if ((ports = ListLen(tmpport)) != ListLen(tmpname)) {
	Printf("List %s has %d elements, list %s has %d\n",
	       namedport, ListLen(tmpport), equals, ListLen(tmpname));
	done = 1;
      }
      else if (tmpport == NULL) {
	Printf("List %s has no elements\n", namedport);
	done = 1;
      }
      else if (tmpname == NULL) {
	Printf("List %s has no elements\n", equals);
	done = 1;
      }
      else {
	portlist += ports;
	namedporthead = ListCat(namedporthead, tmpport);
	namedlisthead = ListCat(namedlisthead, tmpname);
      }
    }
    else {
      /* unnamed element, so add it to the list */
      tmp = List(nodelist);
      if (tmp == NULL) {
	Printf("No such pin '%s' in Cell(%s); Current cell = %s\n",
             nodelist, model, CurrentCell->name);
	done = 1;
      }
      else {
	portlist += ListLen(tmp);
	head = ListCat(head, tmp);
      }
    }
  }
  va_end(ap);

  if (inststr == NULL) {
    if (instancename != NULL)
       FreeString(instancename);
    QuickSearch = 1;
    instancename = Next(model);
    QuickSearch = 0;
    instnameptr = instancename;
  }
  else
     instnameptr = inststr;

  Instance(model, instnameptr);
  tp = head;
  for (tp2 = instanced_cell->cell; tp2 != NULL; tp2 = tp2->next) {
    if (IsPort(tp2)) {
      strcpy(tmpname, instnameptr);
      strcat(tmpname, SEPARATOR);
      strcat(tmpname, tp2->name);
      namedlistp = namedlisthead;
      namedportp = namedporthead;
      while (namedportp != NULL) {
	if ((*matchfunc)(namedportp->name, tp2->name)) {
	  join(namedlistp->name, tmpname);
	  break; /* out of while loop */
	}
	namedlistp = namedlistp->next;
	namedportp = namedportp->next;
      }
      if (namedportp == NULL) {
	/* port was NOT a named port, so connect to unnamed list */
	if (tp == NULL) {
	  Printf( "Not enough ports in Cell().\n");
	  break; /* out of for loop */
	}
	else {
	  join(tp->name, tmpname);
	  tp = tp->next;
	}
      }
    }
  }
  return instnameptr;
}

/*----------------------------------------------------------------------*/
/* These default classes correspond to .sim file format types and other	*/
/* basic classes, and may be used by any netlist-reading routine to	*/
/* define basic types.	The classes are only defined when called (in	*/
/* contrast to netgen v. 1.3 and earlier, where they were pre-defined)	*/
/*----------------------------------------------------------------------*/

char *P(char *fname, char *inststr, char *gate, char *drain, char *source)
{
    int fnum = CurrentCell->file;

    if (LookupCellFile("p", fnum) == NULL) {
       CellDef("p", fnum);
       Port("drain");
       Port("gate");
       Port("source");
       PropertyDouble("p", fnum, "length", 0.01, 0.0);
       PropertyDouble("p", fnum, "width", 0.01, 0.0);
       SetClass(CLASS_PMOS);
       EndCell();
       if (fname) ReopenCellDef(fname, fnum);  /* Reopen */
    }
    return Cell(inststr, "p", drain, gate, source);
}

/*----------------------------------------------------------------------*/

char *P4(char *fname, char *inststr, char *drain, char *gate, char *source, char *bulk)
{
    int fnum = CurrentCell->file;

    if (LookupCellFile("p4", fnum) == NULL) {
       CellDef("p4", fnum);
       Port("drain");
       Port("gate");
       Port("source");
       Port("well");
       PropertyDouble("p4", fnum, "length", 0.01, 0.0);
       PropertyDouble("p4", fnum, "width", 0.01, 0.0);
       SetClass(CLASS_PMOS4);
       EndCell();
       if (fname) ReopenCellDef(fname, fnum);  /* Reopen */
    }
    return Cell(inststr, "p4", drain, gate, source, bulk);
}

/*----------------------------------------------------------------------*/

char *N(char *fname, char *inststr, char *gate, char *drain, char *source)
{
    int fnum = CurrentCell->file;

    if (LookupCellFile("n", fnum) == NULL) {
       CellDef("n", fnum);
       Port("drain");
       Port("gate");
       Port("source");
       PropertyDouble("n", fnum, "length", 0.01, 0.0);
       PropertyDouble("n", fnum, "width", 0.01, 0.0);
       SetClass(CLASS_NMOS);
       EndCell();
       if (fname) ReopenCellDef(fname, fnum);  /* Reopen */
    }
    return Cell(inststr, "n", drain, gate, source);
}

/*----------------------------------------------------------------------*/

char *N4(char *fname, char *inststr, char *drain, char *gate, char *source, char *bulk)
{
    int fnum = CurrentCell->file;

    if (LookupCellFile("n4", fnum) == NULL) {
       CellDef("n4", fnum);
       Port("drain");
       Port("gate");
       Port("source");
       Port("bulk");
       PropertyDouble("n4", fnum, "length", 0.01, 0.0);
       PropertyDouble("n4", fnum, "width", 0.01, 0.0);
       SetClass(CLASS_NMOS4);
       EndCell();
       if (fname) ReopenCellDef(fname, fnum);  /* Reopen */
    }
    return Cell(inststr, "n4", drain, gate, source, bulk);
}

/*----------------------------------------------------------------------*/

char *E(char *fname, char *inststr, char *top, char *bottom_a, char *bottom_b)
{
    int fnum = CurrentCell->file;

    if (LookupCellFile("e", fnum) == NULL) {
       CellDef("e", fnum);
       Port("top");
       Port("bottom_a");
       Port("bottom_b");
       PropertyDouble("e", fnum, "length", 0.01, 0.0);
       PropertyDouble("e", fnum, "width", 0.01, 0.0);
       SetClass(CLASS_ECAP);
       EndCell();
       if (fname) ReopenCellDef(fname, fnum);  /* Reopen */
    }
    return Cell(inststr, "e", top, bottom_a, bottom_b);
}

/*----------------------------------------------------------------------*/

char *B(char *fname, char *inststr, char *collector, char *base, char *emitter)
{
    int fnum = CurrentCell->file;

    if (LookupCellFile("b", fnum) == NULL) {
       CellDef("b", fnum);
       Port("collector");
       Port("base");
       Port("emitter");
       SetClass(CLASS_NPN);
       EndCell();
       if (fname) ReopenCellDef(fname, fnum);  /* Reopen */
    }
    return Cell(inststr, "b", collector, base, emitter);
}

/*----------------------------------------------------------------------*/

char *Res(char *fname, char *inststr, char *end_a, char *end_b)
{
    int fnum = CurrentCell->file;

    if (LookupCellFile("r", fnum) == NULL) {
       CellDef("r", fnum);
       Port("end_a");
       Port("end_b");
       PropertyDouble("r", fnum, "value", 0.01, 0.0);
       SetClass(CLASS_RES);
       EndCell();
       if (fname) ReopenCellDef(fname, fnum);  /* Reopen */
    }
    return Cell(inststr, "r", end_a, end_b);
}

/*----------------------------------------------------------------------*/

char *Res3(char *fname, char *inststr, char *rdummy, char *end_a, char *end_b)
{
    int fnum = CurrentCell->file;

    if (LookupCellFile("r3", fnum) == NULL) {
       CellDef("r3", fnum);
       Port("dummy");
       Port("end_a");
       Port("end_b");
       PropertyDouble("r3", fnum, "value", 0.01, 0.0);
       SetClass(CLASS_RES3);
       EndCell();
       if (fname) ReopenCellDef(fname, fnum);  /* Reopen */
    }
    return Cell(inststr, "r3", rdummy, end_a, end_b);
}

/*----------------------------------------------------------------------*/

char *XLine(char *fname, char *inststr, char *node1, char *node2,
		char *node3, char *node4)
{
    int fnum = CurrentCell->file;

    if (LookupCellFile("t", fnum) == NULL) {
       CellDef("t", fnum);
       Port("node1");
       Port("node2");
       Port("node3");
       Port("node4");
       SetClass(CLASS_XLINE);
       EndCell();
       if (fname) ReopenCellDef(fname, fnum);  /* Reopen */
    }
    return Cell(inststr, "t", node1, node2, node3, node4);
}

/*----------------------------------------------------------------------*/

char *Cap(char *fname, char *inststr, char *top, char *bottom)
{
    int fnum = CurrentCell->file;

    if (LookupCellFile("c", fnum) == NULL) {
       CellDef("c", fnum);
       Port("top");
       Port("bottom");
       PropertyDouble("c", fnum, "value", 0.01, 0.0);
       SetClass(CLASS_CAP);
       EndCell();
       if (fname) ReopenCellDef(fname, fnum);  /* Reopen */
    }
    return Cell(inststr, "c", top, bottom);
}

/*----------------------------------------------------------------------*/

char *Cap3(char *fname, char *inststr, char *top, char *bottom, char *cdummy)
{
    int fnum = CurrentCell->file;

    if (LookupCellFile("c3", fnum) == NULL) {
       CellDef("c3", fnum);
       Port("top");
       Port("bottom");
       Port("dummy");
       PropertyDouble("c3", fnum, "value", 0.01, 0.0);
       SetClass(CLASS_CAP3);
       EndCell();
       if (fname) ReopenCellDef(fname, fnum);  /* Reopen */
    }
    return Cell(inststr, "c3", top, bottom, cdummy);
}

/*----------------------------------------------------------------------*/

char *Inductor(char *fname, char *inststr, char *end_a, char *end_b)
{
    int fnum = CurrentCell->file;

    if (LookupCellFile("l", fnum) == NULL) {
       CellDef("l", fnum);
       Port("end_a");
       Port("end_b");
       PropertyDouble("l", fnum, "value", 0.01, 0.0);
       SetClass(CLASS_INDUCTOR);
       EndCell();
       if (fname) ReopenCellDef(fname, fnum);  /* Reopen */
    }
    return Cell(inststr, "l", end_a, end_b);
}

/*----------------------------------------------------------------------*/
/* Determine if two property keys are matching strings			*/
/* Return 1 on match, 0 on failure to match				*/
/*----------------------------------------------------------------------*/

int PropertyKeyMatch(char *key1, char *key2)
{
   /* For now, an unsophisticated direct string match */

   if (!strcasecmp(key1, key2)) return 1;
   return 0;
}

/*----------------------------------------------------------------------*/
/* Determine if two property values are matching.			*/
/* Return 1 on match, 0 on failure to match				*/
/*----------------------------------------------------------------------*/

int PropertyValueMatch(char *value1, char *value2)
{
   /* For now, an unsophisticated direct string match */

   if (!strcasecmp(value1, value2)) return 1;
   return 0;
}

/*----------------------------------------------------------------------*/
/* Add a key:value property pair to the list of property pairs		*/
/*----------------------------------------------------------------------*/

void AddProperty(struct keyvalue **topptr, char *key, char *value)
{
    struct keyvalue *kv;

    if (Debug) Printf("   Defining key:value property pair: %s:%s\n", key, value);
    if ((kv = NewKeyValue()) == NULL) {
	perror("Failed NewKeyValue in Property");
	return;
    }
    kv->key = strsave(key);
    kv->value = strsave(value);
    kv->next = *topptr;
    *topptr = kv;
}

/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/

void AddScaledProperty(struct keyvalue **topptr, char *key, char *value, double scale)
{
    struct keyvalue *kv;

    if (Debug) Printf("   Defining key:value property pair: %s:%s\n", key, value);
    if ((kv = NewKeyValue()) == NULL) {
	perror("Failed NewKeyValue in Property");
	return;
    }
    kv->key = strsave(key);
    kv->value = strsave(value);
    kv->next = *topptr;
    *topptr = kv;
}

/*----------------------------------------------------------------------*/
/* Free up a property list 						*/
/*----------------------------------------------------------------------*/

void DeleteProperties(struct keyvalue **topptr)
{
    struct keyvalue *kv, *nextkv;
    
    kv = *topptr;
    while (kv != NULL)
    {
	nextkv = kv->next;
	FreeString(kv->key);
	FreeString(kv->value);
	FREE(kv);
	kv = nextkv;
    }
    *topptr = NULL;
}

/*----------------------------------------------------------------------*/
/* LinkProperties() ---							*/
/*									*/
/* Add a list of properties to the current cell	(instance).		*/
/* This just keeps a record of key:value pairs;  it does not attempt	*/
/* to relate them to the cell that is being instanced.  Because this	*/
/* is used to link the same properties multiple times for parallel	*/
/* devices, copy the list (a refcount would work better. . .)		*/
/*									*/
/* If "isdefault" is "true", then the record is installed in the	*/
/* object hash under the name "defaults";  otherwise, it gets the	*/
/* name "properties" and is not added to the object hash.		*/
/*----------------------------------------------------------------------*/

struct objlist *LinkProperties(char *model, struct keyvalue *topptr)
{
    int filenum = -1;
    struct nlist *cell;
    struct objlist *tp;
    struct keyvalue *kv;
    struct valuelist *newkv;
    int entries;

    if (topptr == NULL) return NULL;

    if (CurrentCell == NULL) {
	Printf("LinkProperties() called with no current cell.\n");
	return NULL;
    }
    else
	filenum = CurrentCell->file;

    if (IsIgnored(model, filenum)) {
	Printf("Class '%s' instanced in input but is being ignored.\n", model);
	return NULL;
    }
    cell = LookupCellFile(model, filenum);
    if (cell == NULL) {
	Printf("No cell '%s' found to link properties to.\n", model);
	return NULL;
    }

    tp = GetObject();
    tp->type = PROPERTY;
    tp->name = strsave("properties");
    tp->node = -2;		/* Don't report as disconnected node */
    tp->next = NULL;
    tp->model.class = strsave(model);

    /* Save a copy of the key:value pairs in tp->instance.props */

    for (entries = 0, kv = topptr; kv != NULL; kv = kv->next, entries++);
    tp->instance.props = NewPropValue(entries + 1);

    for (entries = 0, kv = topptr; kv != NULL; kv = kv->next, entries++)
    {
	newkv = &(tp->instance.props[entries]);
	newkv->key = strsave(kv->key);
	/* No promotion to types other than string at this point */
	newkv->type = PROP_STRING;
	newkv->value.string = strsave(kv->value);
    }

    /* Final entry marks the end of the list */
    newkv = &(tp->instance.props[entries]);
    newkv->key = NULL;
    newkv->type = PROP_ENDLIST;
    newkv->value.ival = 0;

    AddToCurrentCellNoHash(tp);
    return tp;
}

/*----------------------------------------------------------------------*/
/* PromoteProperty() ---						*/
/*									*/
/* Instances have all properties recorded as strings.  If the cell	*/
/* record has an integer or double type, then attempt to convert the	*/
/* instance record into that type.					*/
/*									*/
/* Do not attempt to promote properties that cannot be promoted		*/
/* without altering the content.  Fractional values cannot be converted	*/
/* to integers, and strings that are not numbers must be left as	*/
/* strings. Return 1 if the property was promoted successfully, and 0	*/
/* if no promotion was possible, and -1 if passed a bad argument.	*/
/*----------------------------------------------------------------------*/

int PromoteProperty(struct property *prop, struct valuelist *vl)
{
    char tstr[256];
    int ival, result;
    double dval;

    if (prop == NULL || vl == NULL) return -1;
    if (prop->type == vl->type) return 1;	/* Nothing to do */
    result = 0;
    switch (prop->type) {
	case PROP_STRING:
	    switch (vl->type) {
		case PROP_INTEGER:
		    vl->type = PROP_STRING;
		    sprintf(tstr, "%d", vl->value.ival);
		    vl->value.string = strsave(tstr);
		    result = 1;
		    break;
		case PROP_DOUBLE:
		case PROP_VALUE:
		    vl->type = PROP_STRING;
		    sprintf(tstr, "%g", vl->value.dval);
		    vl->value.string = strsave(tstr);
		    result = 1;
		    break;
	    }
	    break;
	case PROP_INTEGER:
	    switch (vl->type) {
		case PROP_STRING:
		    if (StringIsValue(vl->value.string)) {
			result = ConvertStringToFloat(vl->value.string, &dval);
			if (result != 0) {
			    if ((double)((int)dval) == dval) {
				vl->type = PROP_INTEGER;
				FREE(vl->value.string);
				vl->value.ival = (int)dval;
				result = 1;
			    }
			}
		    }
		    break;
		case PROP_DOUBLE:
		case PROP_VALUE:
		    vl->type = PROP_INTEGER;
		    dval = vl->value.dval;
		    if ((double)((int)dval) == dval) {
			vl->value.ival = (int)dval;
			result = 1;
		    }
		    break;
	    }
	    break;
	case PROP_DOUBLE:
	case PROP_VALUE:
	    switch (vl->type) {
		case PROP_STRING:
		    if (StringIsValue(vl->value.string)) {
			result = ConvertStringToFloat(vl->value.string, &dval);
			if (result != 0) {
			   vl->type = PROP_DOUBLE;
			   FREE(vl->value.string);
			   vl->value.dval = dval;
			   result = 1;
			}
		    }
		    break;
		case PROP_INTEGER:
		    vl->type = PROP_DOUBLE;
		    vl->value.dval = (double)vl->value.ival;
		    result = 1;
		    break;
	    }
	    break;
    }
    return result;
}

/*----------------------------------------------------------------------*/
/* Structure used by resolveprops()					*/
/*----------------------------------------------------------------------*/

typedef struct _propdata {
    struct nlist *cell;
    int entries;
} PropData;

/*----------------------------------------------------------------------*/
/* resolveprops() ---							*/
/*									*/
/* Match instance property lists to the cell it's an instance of	*/
/*----------------------------------------------------------------------*/

struct nlist *resolveprops(struct hashlist *p, void *clientdata)
{
   struct nlist *ptr, *pmod;
   struct objlist *ob;
   struct valuelist *vl, vtemp, *vlnew;
   struct property *prop;
   struct nlist *tc;
   int entries, i, j;
   PropData *pdp = (PropData *)clientdata;

   tc = pdp->cell;
   entries = pdp->entries;

   ptr = (struct nlist *)(p->ptr);
   if (ptr->file != tc->file) return NULL;

   for (ob = ptr->cell; ob; ob = ob->next) {
      if (ob->type == PROPERTY) {
	 if ((*matchfunc)(ob->model.class, tc->name)) {
	    /* Check length of the record, resize if necessary */
	    for (i = 0;; i++) {
	       vl = &(ob->instance.props[i]);
	       if (vl->type == PROP_ENDLIST) break;
	    }
	    if (i > entries) {
	       Printf("Warning: Instance defines more properties than cell.\n");
	       Printf("This shouldn't happen.\n");
	    }

	    /* Create new structure in the correct order, and replace	*/
	    /* the old property structure with it.			*/

	    vlnew = NewPropValue(entries + 1);
	    for (i = 0;; i++) {
	       vl = &(ob->instance.props[i]);
	       if (vl->type == PROP_ENDLIST) break;
	       prop = (struct property *)HashLookup(vl->key, tc->proptab, OBJHASHSIZE);

	       /* Warning:  prop should never be null, but condition	*/
	       /* should be handled.					*/

	       if (prop != NULL) {
		  j = prop->idx;
		  vlnew[j].key = vl->key;
		  vlnew[j].type = vl->type;
		  vlnew[j].value = vl->value;
	       }
	    }
	    vlnew[entries].key = NULL;
	    vlnew[entries].type = PROP_ENDLIST;
	    vlnew[entries].value.ival = 0;
	    FREE(ob->instance.props);
	    ob->instance.props = vlnew;
	 }
      }
   }
   return ptr;
}

/*----------------------------------------------------------------------*/
/* ResolveProperties() ---						*/
/*									*/
/* This routine does the greatest part of the work for the property-	*/
/* handling mechanism.  It determines which properties are common to	*/
/* two models, and arranges the lists of properties in both models	*/
/* such that the lists are in the same order.  Properties that exist in	*/
/* one cell but not the other are floated to the end of the list.  All	*/
/* instances of both models are checked and their property lists also	*/
/* arranged in order.  							*/
/*----------------------------------------------------------------------*/

void ResolveProperties(char *name1, int file1, char *name2, int file2)
{
    PropData pdp;
    struct property *kl1, *kl2;
    struct nlist *tp1, *tp2;
    int i;

    struct valuelist *kv, *newkv;
    struct valuelist *vl, *lastvl;
    int isnum, filenum;

    if ((tp1 = LookupCellFile(name1, file1)) == NULL) return;
    if ((tp2 = LookupCellFile(name2, file2)) == NULL) return;

    /* Find all properties defined in the cell tp1, and index them in	*/
    /* numerical order.  For each property, find the equivalent		*/
    /* property in the cell to be matched, and give them both the same	*/
    /* index.  If the property does not exist in the second cell, then	*/
    /* create it.							*/

    kl1 = (struct property *)HashFirst(tp1->proptab, OBJHASHSIZE);
    /* If indexes are not zero, then properties have already been matched. */
    if (kl1 == NULL) return;	/* Cell has no properties */
    if (kl1->idx != 0) return;
    i = 1;

    while (kl1 != NULL) {
	kl1->idx = i;

	kl2 = (struct property *)HashLookup(kl1->key, tp2->proptab, OBJHASHSIZE);
	if (kl2 == NULL) {
	    /* No such property in tp2 */
	    switch (kl1->type) {
		case PROP_STRING:
		    kl2 = PropertyString(tp2->name, tp2->file, kl1->key,
				kl1->slop.ival, kl1->pdefault.string); 
		    break;
		case PROP_INTEGER:
		    kl2 = PropertyInteger(tp2->name, tp2->file, kl1->key,
				kl1->slop.ival, kl1->pdefault.ival); 
		    break;
		case PROP_DOUBLE:
		    kl2 = PropertyDouble(tp2->name, tp2->file, kl1->key,
				kl1->slop.dval, kl1->pdefault.dval); 
		    break;
		case PROP_VALUE:
		    kl2 = PropertyValue(tp2->name, tp2->file, kl1->key,
				kl1->slop.dval, kl1->pdefault.dval); 
		    break;
	    }
	}
	if (kl2 != NULL) kl2->idx = i;
	kl1 = (struct property *)HashNext(tp1->proptab, OBJHASHSIZE);
	i++;
    }

    /* Now check tp2 for properties not in tp1 */

    kl2 = (struct property *)HashFirst(tp2->proptab, OBJHASHSIZE);

    while (kl2 != NULL) {
	kl1 = (struct property *)HashLookup(kl2->key, tp1->proptab, OBJHASHSIZE);
	if (kl1 == NULL) {
	    /* No such property in tp1 */
	    switch (kl2->type) {
		case PROP_STRING:
		    kl1 = PropertyString(tp1->name, tp1->file, kl2->key,
				kl2->slop.ival, kl2->pdefault.string); 
		    break;
		case PROP_INTEGER:
		    kl1 = PropertyInteger(tp1->name, tp1->file, kl2->key,
				kl2->slop.ival, kl2->pdefault.ival); 
		    break;
		case PROP_DOUBLE:
		    kl1 = PropertyDouble(tp1->name, tp1->file, kl2->key,
				kl2->slop.dval, kl2->pdefault.dval); 
		    break;
		case PROP_VALUE:
		    kl1 = PropertyValue(tp1->name, tp1->file, kl2->key,
				kl2->slop.dval, kl2->pdefault.dval); 
		    break;
	    }
	}
	if (kl1 != NULL) kl1->idx = i;
	kl2 = (struct property *)HashNext(tp1->proptab, OBJHASHSIZE);
	i++;
    }

    /* Now that the properties of the two cells are ordered, find all	*/
    /* instances of both cells, and order their properties to match.	*/

    pdp.cell = tp1;
    pdp.entries = i;
    RecurseCellHashTable2(resolveprops, (void *)(&pdp));
    pdp.cell = tp2;
    RecurseCellHashTable2(resolveprops, (void *)(&pdp));
}

/*--------------------------------------------------------------*/
/* Copy properties from one object to another (used when	*/
/* flattening cells).						*/
/*--------------------------------------------------------------*/

void CopyProperties(struct objlist *obj_to, struct objlist *obj_from)
{
   int i;
   struct valuelist *kv, *kvcopy, *kvcur;
   
   if (obj_from->instance.props != NULL) {
      for (i = 0;; i++) {
         kv = &(obj_from->instance.props[i]);
         if (kv->type == PROP_ENDLIST)
	    break;
      }
      kvcopy = NewPropValue(i + 1);

      for (i = 0;; i++) {
	 kv = &(obj_from->instance.props[i]);
	 kvcur = &(kvcopy[i]);
         kvcur->type = kv->type;
	 if (kv->type == PROP_ENDLIST) break;
         kvcur->key = strsave(kv->key);
         switch (kvcur->type) {
	    case PROP_STRING:
      	        kvcur->value.string = strsave(kv->value.string);
	        break;
	    case PROP_INTEGER:
      	        kvcur->value.ival = kv->value.ival;
	        break;
	    case PROP_DOUBLE:
	    case PROP_VALUE:
      	        kvcur->value.dval = kv->value.dval;
	        break;
	    case PROP_EXPRESSION:
      	        kvcur->value.stack = CopyTokStack(kv->value.stack);
	        break;
         }
      }
      kvcur->key = NULL;
      kvcur->value.ival = 0;

      obj_to->instance.props = kvcopy;
      obj_to->model.class = strsave(obj_from->model.class);
   }
}

/*--------------------------------------------------------------*/
/* Convert a string to an integer.				*/
/* At the moment, we do nothing with error conditions.		*/
/*--------------------------------------------------------------*/

int ConvertStringToInteger(char *string, int *ival)
{
   long lval;
   char *eptr = NULL;

   lval = strtol(string, &eptr, 10);
   if (eptr > string) {
      *ival = (int)lval;
      return 1;
   }
   else if (eptr == string)
      return 0;		/* No conversion */
}

/*--------------------------------------------------------------*/
/* Check if a string is a valid number (with optional metric	*/
/* unit suffix).						*/
/* Returns 1 if the string is a proper value, 0 if not.		*/
/*--------------------------------------------------------------*/

int StringIsValue(char *string)
{
   double fval;
   char *eptr = NULL;

   fval = strtod(string, &eptr);
   if (eptr > string)
   {
      while (isspace(*eptr)) eptr++;
      switch (tolower(*eptr)) {
	 case 'g':	/* giga */
	 case 'k':	/* kilo */
	 case 'c':	/* centi */
	 case 'm':	/* milli */
	 case 'u':	/* micro */
	 case 'n':	/* nano */
	 case 'p':	/* pico */
	 case 'f':	/* femto */
	 case 'a':	/* atto */
	 case '\0':	/* no units */
	    return 1;
      }
   }
   return 0;
}

/*--------------------------------------------------------------*/
/* Convert a string with possible metric notation into a float.	*/
/* This follows SPICE notation with case-insensitive prefixes,	*/
/* using "meg" to distinguish 1x10^6 from "m" 1x10^-3		*/
/*								*/
/* Put the result in "dval".  Return 1 if successful, 0 if	*/
/* unsuccessful.						*/
/*--------------------------------------------------------------*/

int ConvertStringToFloat(char *string, double *dval)
{
   long double fval;
   char *eptr = NULL;

   fval = strtold(string, &eptr);
   if (eptr > string)
   {
      while (isspace(*eptr)) eptr++;
      switch (tolower(*eptr)) {
	 case 'g':	/* giga */
	    fval *= 1.0e9L;
	    eptr++;
	    break;
	 case 'k':	/* kilo */
	    fval *= 1.0e3L;
	    eptr++;
	    break;
	 case 'c':	/* centi */
	    fval *= 1.0e-2L;
	    eptr++;
	    break;
	 case 'm':	/* milli */
	    if (tolower(*(eptr + 1)) == 'e' &&
			tolower(*(eptr + 2)) == 'g') {
	       fval *= 1.0e6L;
	       eptr += 2;
	    }
	    else
	       fval *= 1.0e-3L;
	    eptr++;
	    break;
	 case 'u':	/* micro */
	    fval *= 1.0e-6L;
	    eptr++;
	    break;
	 case 'n':	/* nano */
	    fval *= 1.0e-9L;
	    eptr++;
	    break;
	 case 'p':	/* pico */
	    fval *= 1.0e-12L;
	    eptr++;
	    break;
	 case 'f':	/* femto */
	    fval *= 1.0e-15L;
	    eptr++;
	    break;
	 case 'a':	/* atto */
	    fval *= 1.0e-18L;
	    eptr++;
	    break;
	 default:
	    break;	/* No units, no adjustment */
      }
      if (*eptr != '\0') {
	 switch (tolower(*eptr)) {
	     case 'f':	/* Farads */
		if (!strncasecmp(eptr, "farad", 5)) {
		    eptr += 5;
		    if (tolower(*eptr) == 's') eptr++;
		}
		else eptr++;
		if (*eptr != '\0') return 0; 	/* Unknown units */
		break;
	     case 'm':	/* Meters */
		if (!strncasecmp(eptr, "meter", 5)) {
		    eptr += 5;
		    if (tolower(*eptr) == 's') eptr++;
		}
		else eptr++;
		if (*eptr != '\0') return 0; 	/* Unknown units */
		break;
	     case 'h':	/* Henrys */
		if (!strncasecmp(eptr, "henr", 4)) {
		    eptr += 4;
		    if (tolower(*eptr) == 'y') {
			eptr++;
		        if (*eptr == 's') eptr++;
		    }
		    else if (!strncasecmp(eptr, "ies", 3))
			eptr += 3;
		}
		else eptr++;
		if (*eptr != '\0') return 0; 	/* Unknown units */
		break;
	     case 's':	/* Seconds */
		if (!strncasecmp(eptr, "second", 6)) {
		    eptr += 6;
		    if (tolower(*eptr) == 's') eptr++;
		}
		else eptr++;
		if (*eptr != '\0') return 0; 	/* Unknown units */
		break;
	     case 'o':	/* Ohms */
		if (!strncasecmp(eptr, "ohm", 3)) {
		    eptr += 3;
		    if (tolower(*eptr) == 's') eptr++;
		}
		else eptr++;
		if (*eptr != '\0') return 0; 	/* Unknown units */
		break;
	     case 'v':	/* Volts */
		if (!strncasecmp(eptr, "volt", 4)) {
		    eptr += 4;
		    if (tolower(*eptr) == 's') eptr++;
		}
		else eptr++;
		if (*eptr != '\0') return 0; 	/* Unknown units */
		break;
	     case 'a':	/* Amps */
		if (!strncasecmp(eptr, "amp", 3)) {
		    eptr += 3;
		    if (tolower(*eptr) == 's') eptr++;
		}
		else eptr++;
		if (*eptr != '\0') return 0; 	/* Unknown units */
		break;
	     default:
		return 0;	/* Unknown units;  no conversion */
	 }
      }
   }
   else if (eptr == string) return 0;	/* No conversion */
   *dval = (double)fval;
   return 1;
}

/*--------------------------------------------------------------*/
/* Convert a string into a double, scale it, and pass it back	*/
/* as another string value.					*/
/*--------------------------------------------------------------*/

char *ScaleStringFloatValue(char *vstr, double scale)
{
   static char newstr[32];
   double fval, afval;
   int result;

   result = ConvertStringToFloat(vstr, &fval);
   if (result == 1) {
      fval *= scale;
   
      snprintf(newstr, 31, "%g", fval);
      return newstr;
   }
   else
      return vstr;
}

/*----------------------------------------------------------------------*/
/* Workhorse subroutine for the Connect() function			*/
/*----------------------------------------------------------------------*/

void join(char *node1, char *node2)
{
	struct objlist *tp1, *tp2, *tp3;
	int nodenum, oldnode;

	if (CurrentCell == NULL) {
		Printf( "No current cell for join(%s,%s)\n",
			node1,node2);
		return;
	}
	tp1 = LookupObject(node1, CurrentCell);
	if (tp1 == NULL) {
		Printf("No node '%s' found in current cell '%s'\n",
			node1, CurrentCell->name);
		return;
	}
	tp2 = LookupObject(node2, CurrentCell);
	if (tp2 == NULL) {
		Printf("No node '%s' found in current cell '%s'\n",
			node2, CurrentCell->name);
		return;
	}
	if (Debug) Printf("         joining: %s == %s (",
		           tp1->name,tp2->name);
	
	/* see if either node has an assigned node number */
	if ((tp1->node == -1) && (tp2->node == -1)) {
		tp1->node = NextNode;
		tp2->node = NextNode++;
		if (Debug) Printf("New ");
	}
	else if (tp1->node == -1) tp1->node = tp2->node;
	else if (tp2->node == -1) tp2->node = tp1->node;
	else {
		if (tp1->node < tp2->node) {
			nodenum = tp1->node;
			oldnode = tp2->node;
		} else {
			nodenum = tp2->node;
			oldnode = tp1->node;
		}
		/* now search through entire list, updating nodes as needed */
		for (tp3 = CurrentCell->cell; tp3 != NULL; tp3 = tp3->next) 
			if (tp3->node == oldnode)  tp3->node = nodenum;
	}
	if (Debug) Printf("Node = %d)\n",tp1->node);
}

/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/

void Connect(char *tplt1, char *tplt2)
{
	struct objlist *list1, *list2;
	int n1, n2;  /* lengths of two lists */

	if (Debug) Printf("      Connect(%s,%s)\n",tplt1,tplt2);
	if (CurrentCell == NULL) {
		Printf( "No current cell for Connect(%s,%s)\n",
			tplt1,tplt2);
		return;
	}
	list1 = List(tplt1);
	n1 = ListLen(list1);
	list2 = List(tplt2);
	n2 = ListLen(list2);
	if (n1==n2) {
	  while (list1 != NULL) {
	    join(list1->name,list2->name);
	    list1 = list1->next;
	    list2 = list2->next;
	  }
	}
	else if (n1==1 && n2>0) {
		while (list2 != NULL) {
			join(list1->name,list2->name);
			list2 = list2->next;
		}
	}
	else if (n2==1 && n1>0) {
		while (list1 != NULL) {
			join(list1->name,list2->name);
			list1 = list1->next;
		}
	}
	else Printf("Unequal element lists: '%s' has %d, '%s' has %d.\n",
		    tplt1,n1,tplt2,n2);
}
		
/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/

void PortList(char *prefix, char *list_template)
{
  struct objlist *list;
  char buffer[1024];
  int buflen;
  int i;
	
  for (list = List(list_template); list != NULL; list = list->next) {
    strcpy(buffer,prefix);
    strcat(buffer,list->name);
    buflen = strlen(buffer);
    for (i=0; i < buflen; i++)
      if (buffer[i] == SEPARATOR[0]) buffer[i] = PORT_DELIMITER[0];
    Port(buffer);
    join(buffer,list->name);
  }
}
		
/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/

void Place(char *name)
{
  char *freename;
  char buffer1[1024], buffer2[1024];
  char prefix[20];
	
  QuickSearch = (LastPlaced != NULL);
  freename = Next(name);
  Instance(name,freename);
  if (Composition == HORIZONTAL) {
    sprintf(buffer2,"%s%s%s%s%s", freename, SEPARATOR, "W", PORT_DELIMITER, "*");
    if (LastPlaced != NULL) {
      sprintf(buffer1,"%s%s%s%s%s", 
	      LastPlaced->instance.name, SEPARATOR, "E", PORT_DELIMITER, "*");
      Connect (buffer1,buffer2);
    }
    else {  /* promote left-hand ports */
      sprintf(prefix,"%s%s","W", PORT_DELIMITER);
      PortList(prefix,buffer2);  
    }
    buffer2[strlen(buffer2)-3] = 'N';
    sprintf(prefix,"%s%s", "N", PORT_DELIMITER);
    PortList(prefix,buffer2);
    buffer2[strlen(buffer2)-3] = 'S';
    sprintf(prefix,"%s%s", "S", PORT_DELIMITER);
    PortList(prefix,buffer2);
  }
  else if (Composition == VERTICAL) {
    sprintf(buffer2,"%s%s%s%s%s",
	    freename, SEPARATOR, "S", PORT_DELIMITER, "*");
    if (LastPlaced != NULL) {
      sprintf(buffer1,"%s%s%s%s%s",
	      LastPlaced->instance.name, SEPARATOR, "N", PORT_DELIMITER, "*");
      Connect (buffer1,buffer2);
    }
    else { /* promote bottom ports */
      sprintf(prefix,"%s%s","S", PORT_DELIMITER);
      PortList(prefix,buffer2);  
    }
    buffer2[strlen(buffer2)-3] = 'E';
    sprintf(prefix,"%s%s", "E", PORT_DELIMITER);
    PortList(prefix,buffer2);
    buffer2[strlen(buffer2)-3] = 'W';
    sprintf(prefix,"%s%s", "W", PORT_DELIMITER);
    PortList(prefix,buffer2);
  }
  LastPlaced = LookupInstance(freename,CurrentCell);
  QuickSearch = 0;
  FreeString(freename);
}

/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/

void Array(char *Cell, int num)
{
        int i;

	for (i=0; i<num; i++) {
	  if (Debug) Printf(".");
	  Place(Cell);
	}
}

	
/* if TRUE, always connect all nodes upon EndCell() */
int NoDisconnectedNodes = 0;

/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/

void ConnectAllNodes(char *model, int file)
/* Within the definition of 'model', traverse the object list
   and connect all the otherwise disconnected nodes (i.e., those
   with node==-1) to unique node numbers */
{
  int nodenum;
  struct nlist *tp;
  struct objlist *ob;

  if ((tp = LookupCellFile(model, file)) == NULL) {
    Printf("Cell: %s does not exist.\n", model);
    return;
  }
  
  nodenum = 0;
  for (ob = tp->cell; ob != NULL; ob = ob->next)
    if (ob->node >= nodenum) nodenum = ob->node + 1;

  for (ob = tp->cell; ob != NULL; ob = ob->next)
    if (ob->node == -1) ob->node = nodenum++;
}

/*----------------------------------------------------------------------*/
/*----------------------------------------------------------------------*/

void EndCell(void)
{
  char buffer1[1024];
  char prefix[10];
	
  if (CurrentCell == NULL) return;
  
  if (Composition == HORIZONTAL) {
    if (LastPlaced != NULL) {
      sprintf(buffer1,"%s%s%s%s%s",
	      LastPlaced->instance.name, SEPARATOR, "E", PORT_DELIMITER, "*");
      sprintf(prefix,"%s%s", "E", PORT_DELIMITER);
      PortList(prefix,buffer1);
    }
  }
  else if (Composition == VERTICAL) { /* vcomposing */
    if (LastPlaced != NULL) {
      sprintf(buffer1,"%s%s%s%s%s",
	      LastPlaced->instance.name, SEPARATOR, "N", PORT_DELIMITER, "*");
      sprintf(prefix,"%s%s", "N", PORT_DELIMITER);
      PortList(prefix,buffer1);
    }
  }
  LastPlaced = NULL;
  CacheNodeNames(CurrentCell);
  if (NoDisconnectedNodes)  ConnectAllNodes(CurrentCell->name, CurrentCell->file);
  CurrentCell = NULL;
  CurrentTail = NULL;
}
