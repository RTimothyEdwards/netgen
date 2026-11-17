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
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. */

/* elaborate.c --- Resolve parameters passed through a subcircuit	*/
/* hierarchy.								*/
/*									*/
/* When a SPICE netlist is read, parameter expressions are reduced	*/
/* only as far as global parameters allow.  References to a		*/
/* subcircuit's own parameters (declared on the .subckt line, or by	*/
/* .param inside the subcircuit) are left unresolved, because their	*/
/* values depend on what each instance of the subcircuit passes to it.	*/
/*									*/
/* Elaboration resolves them, working from the top of the hierarchy	*/
/* down, so that the parameters of each cell's instances are known	*/
/* (as numbers) before the cells they instance are processed.  For	*/
/* each subcircuit, the full set of parameter values given to each	*/
/* instance (values passed by the instance, plus defaults) is		*/
/* computed.  If every instance has the same set, then the subcircuit	*/
/* is resolved in place and keeps its name, so that it can be matched	*/
/* to a cell of the same name in the other netlist.  If instances have	*/
/* different sets, then a copy of the subcircuit is made for each	*/
/* unique set, named "<name>@1", "<name>@2", etc., and each instance is	*/
/* changed to use the copy for its set.  The copies do not match any	*/
/* cell in the other netlist, so they are flattened during comparison	*/
/* (along with the corresponding cell in the other netlist), and their	*/
/* contents are compared at the next level up.				*/
/*									*/
/* Elaboration is done once per netlist (file number), and is done	*/
/* again if another netlist is read into the same file number.	It	*/
/* is run by the "elaborate" command, and automatically before		*/
/* flattening or comparing.						*/

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef TCL_NETGEN
#include <tcl.h>
#endif

#include "netgen.h"
#include "hash.h"
#include "objlist.h"
#include "print.h"
#include "flatten.h"

/* Instance of a cell, as found in a parent cell */

struct elabinst {
    struct nlist *parent;	/* Cell containing the instance */
    struct objlist *first;	/* First pin record of the instance */
    struct objlist *prop;	/* Property record of the instance, or NULL */
    char *signature;		/* Parameter values, as a string */
    struct elabinst *next;
};

/* Information kept for each cell during elaboration */

struct elabcell {
    struct nlist *cell;
    struct elabinst *insts;	/* Instances of this cell */
    struct elabinst *lastinst;	/* Last entry in "insts" */
    int indegree;		/* Unprocessed parent instances (topological sort) */
    unsigned char dead;		/* Cell has been replaced by copies */
    struct elabcell *next;	/* Next cell in processing order */
};

/* Record of the subcircuit copies made for each netlist, kept so	*/
/* that the "elaborate" command can report them (e.g., to the log	*/
/* file of an LVS run) after elaboration has been done.		*/

struct elabfile {
    int file;
    int done;
    struct elabrec *records;
    struct elabfile *next;
};

static struct elabfile *ElabFiles = NULL;

static struct hashdict elabdict;	/* name -> struct elabcell */
static int elabfile = -1;		/* File being elaborated */

/*----------------------------------------------------------------------*/
/* Find or create the record of a file's elaboration state.		*/
/*----------------------------------------------------------------------*/

static struct elabfile *GetElabFile(int file, int create)
{
    struct elabfile *ef;

    for (ef = ElabFiles; ef; ef = ef->next)
	if (ef->file == file) return ef;
    if (!create) return NULL;

    ef = (struct elabfile *)CALLOC(1, sizeof(struct elabfile));
    ef->file = file;
    ef->done = FALSE;
    ef->records = NULL;
    ef->next = ElabFiles;
    ElabFiles = ef;
    return ef;
}

static void FreeElabRecords(struct elabfile *ef)
{
    struct elabrec *er;

    while (ef->records != NULL) {
	er = ef->records;
	ef->records = er->next;
	FREE(er->cellname);
	FREE(er->variant);
	FREE(er->params);
	FREE(er);
    }
}

/*----------------------------------------------------------------------*/
/* Mark a file as needing (re-)elaboration.  Called whenever a netlist	*/
/* is read into the file.						*/
/*----------------------------------------------------------------------*/

void ElaborateReset(int file)
{
    struct elabfile *ef;

    ef = GetElabFile(file, FALSE);
    if (ef != NULL) {
	ef->done = FALSE;
	FreeElabRecords(ef);
    }
}

/*----------------------------------------------------------------------*/
/* Return the list of subcircuit copies made when the file was		*/
/* elaborated (NULL if none, or not yet elaborated).			*/
/*----------------------------------------------------------------------*/

struct elabrec *ElaborateRecords(int file)
{
    struct elabfile *ef;

    ef = GetElabFile(file, FALSE);
    return (ef == NULL) ? NULL : ef->records;
}

/*----------------------------------------------------------------------*/
/* Look up the elaboration record of a cell (by name).		*/
/*----------------------------------------------------------------------*/

static struct elabcell *LookupElabCell(char *name)
{
    return (struct elabcell *)HashLookup(name, &elabdict);
}

/*----------------------------------------------------------------------*/
/* Return TRUE if a cell needs to be elaborated:  It is a subcircuit	*/
/* in the file being elaborated and has contents.			*/
/*----------------------------------------------------------------------*/

static int IsElabCell(struct nlist *tc)
{
    if (tc == NULL) return FALSE;
    if (tc->file != elabfile) return FALSE;
    if ((tc->class != CLASS_SUBCKT) && (tc->class != CLASS_MODULE)) return FALSE;
    return HasContents(tc);
}

/*----------------------------------------------------------------------*/
/* Add an instance to the list of instances of the cell it instances.	*/
/* "ob" is the first pin record of the instance in cell "parent".	*/
/* If "count" is TRUE, count the instance in the cell's "indegree",	*/
/* which is used to process cells in order (each cell after all cells	*/
/* that instance it);  instances in copies of a cell, made during	*/
/* elaboration, are not counted, as the original cell's were.		*/
/*----------------------------------------------------------------------*/

static void AddElabInst(struct nlist *parent, struct objlist *ob, int count)
{
    struct elabcell *ec;
    struct elabinst *ei;
    struct objlist *pob;

    ec = LookupElabCell(ob->model.class);
    if (ec == NULL) return;

    /* The property record, if any, follows the last pin record */
    for (pob = ob->next; pob && (pob->type > FIRSTPIN); pob = pob->next);
    if ((pob != NULL) && (pob->type != PROPERTY)) pob = NULL;

    ei = (struct elabinst *)CALLOC(1, sizeof(struct elabinst));
    ei->parent = parent;
    ei->first = ob;
    ei->prop = pob;
    ei->signature = NULL;
    ei->next = NULL;

    /* Keep instances in order, so copies are numbered in order */
    if (ec->lastinst) ec->lastinst->next = ei;
    else ec->insts = ei;
    ec->lastinst = ei;

    if (count) ec->indegree++;
}

/*----------------------------------------------------------------------*/
/* Callback to create the elaboration record of each cell in the file.	*/
/*----------------------------------------------------------------------*/

static int makeelabcell(struct hashlist *p, int file)
{
    struct nlist *tc = (struct nlist *)(p->ptr);
    struct elabcell *ec;

    if (tc->file != file) return 0;
    if (!IsElabCell(tc)) return 0;

    ec = (struct elabcell *)CALLOC(1, sizeof(struct elabcell));
    ec->cell = tc;
    ec->insts = NULL;
    ec->lastinst = NULL;
    ec->indegree = 0;
    ec->dead = FALSE;
    ec->next = NULL;
    HashPtrInstall(tc->name, ec, &elabdict);
    return 0;
}

/*----------------------------------------------------------------------*/
/* Callback to list the instances in each cell of the file.		*/
/*----------------------------------------------------------------------*/

static int findelabinsts(struct hashlist *p, int file)
{
    struct nlist *tc = (struct nlist *)(p->ptr);
    struct objlist *ob;

    if (tc->file != file) return 0;
    if (LookupElabCell(tc->name) == NULL) return 0;

    for (ob = tc->cell; ob; ob = ob->next)
	if (ob->type == FIRSTPIN)
	    AddElabInst(tc, ob, TRUE);
    return 0;
}

/*----------------------------------------------------------------------*/
/* Callback to collect cells having no instances (the roots of the	*/
/* hierarchy) into a list.						*/
/*----------------------------------------------------------------------*/

static struct elabcell *ElabRoots;

static int findelabroots(struct hashlist *p)
{
    struct elabcell *ec = (struct elabcell *)(p->ptr);

    if (ec->indegree == 0) {
	ec->next = ElabRoots;
	ElabRoots = ec;
    }
    return 0;
}

/*----------------------------------------------------------------------*/
/* Callback to free elaboration records.				*/
/*----------------------------------------------------------------------*/

static int freeelabcell(struct hashlist *p)
{
    struct elabcell *ec = (struct elabcell *)(p->ptr);
    struct elabinst *ei;

    while (ec->insts != NULL) {
	ei = ec->insts;
	ec->insts = ei->next;
	if (ei->signature) FREE(ei->signature);
	FREE(ei);
    }
    FREE(ec);
    return 0;
}

/*----------------------------------------------------------------------*/
/* Compare function for sorting parameter names.			*/
/*----------------------------------------------------------------------*/

static int paramcompare(const void *a, const void *b)
{
    return strcasecmp(*(char **)a, *(char **)b);
}

/*----------------------------------------------------------------------*/
/* Get the names of the parameters of a cell (the keys of its property	*/
/* dictionary, other than "M", which is the instance multiplier),	*/
/* sorted by name.  Returns the number of parameters;  the list must	*/
/* be freed by the caller (the names are not copied).			*/
/*----------------------------------------------------------------------*/

static int GetCellParams(struct nlist *tc, char ***keysptr)
{
    struct property *kl;
    char **keys;
    int n = 0, i;

    for (kl = (struct property *)HashFirst(&(tc->propdict)); kl;
		kl = (struct property *)HashNext(&(tc->propdict)))
	n++;

    keys = (char **)MALLOC((n + 1) * sizeof(char *));
    i = 0;
    for (kl = (struct property *)HashFirst(&(tc->propdict)); kl;
		kl = (struct property *)HashNext(&(tc->propdict))) {
	if ((*matchfunc)(kl->key, "M")) continue;
	keys[i++] = kl->key;
    }
    qsort(keys, i, sizeof(char *), paramcompare);
    *keysptr = keys;
    return i;
}

/*----------------------------------------------------------------------*/
/* Get the value of parameter "key" for an instance of cell "tc" with	*/
/* property record "prop":  the value passed by the instance, or else	*/
/* the cell's default (which may be an expression, evaluated in the	*/
/* scope of the cell with the values passed by the instance).		*/
/* Returns 1 if the value is a number (in *dval), -1 if not.		*/
/*----------------------------------------------------------------------*/

static int GetInstParam(struct nlist *tc, struct objlist *prop, char *key,
		double *dval)
{
    struct valuelist *kv;
    struct property *kl;
    int i;

    if (prop != NULL) {
	for (i = 0; ; i++) {
	    kv = &(prop->instance.props[i]);
	    if (kv->type == PROP_ENDLIST) break;
	    if (!(*matchfunc)(kv->key, key)) continue;
	    switch (kv->type) {
		case PROP_DOUBLE:
		case PROP_VALUE:
		    *dval = kv->value.dval;
		    return 1;
		case PROP_INTEGER:
		    *dval = (double)kv->value.ival;
		    return 1;
		case PROP_STRING:
		    if (kv->value.string &&
				(ConvertStringToFloat(kv->value.string, dval) == 1))
			return 1;
		    return -1;
		default:
		    return -1;	/* Unresolved expression */
	    }
	}
    }

    /* Not passed by the instance;  use the default */
    kl = (struct property *)HashLookup(key, &(tc->propdict));
    if (kl == NULL) return -1;
    switch (kl->type) {
	case PROP_DOUBLE:
	case PROP_VALUE:
	    *dval = kl->pdefault.dval;
	    return 1;
	case PROP_INTEGER:
	    *dval = (double)kl->pdefault.ival;
	    return 1;
	case PROP_STRING:
	    return ParamStringValue(kl, tc, prop, PARAM_ELABORATE, dval);
    }
    return -1;
}

/*----------------------------------------------------------------------*/
/* Compute the parameter values for each instance of a cell, and save	*/
/* them in the instance record as a string ("key=value key=value").	*/
/* Parameters that cannot be resolved are written as "key=?".		*/
/*----------------------------------------------------------------------*/

static void SetSignatures(struct elabcell *ec, char **keys, int nkeys)
{
    struct elabinst *ei;
    struct elabcell *pc;
    char *sig, valstr[64];
    int i, len;
    double dval;

    for (ei = ec->insts; ei; ei = ei->next) {
	pc = LookupElabCell(ei->parent->name);
	if ((pc != NULL) && pc->dead) continue;

	len = 1;
	for (i = 0; i < nkeys; i++) len += strlen(keys[i]) + 2 + 32;
	sig = (char *)MALLOC(len);
	*sig = '\0';
	for (i = 0; i < nkeys; i++) {
	    if (GetInstParam(ec->cell, ei->prop, keys[i], &dval) == 1)
		snprintf(valstr, 63, "%.10g", dval);
	    else
		strcpy(valstr, "?");
	    if (i > 0) strcat(sig, " ");
	    strcat(sig, keys[i]);
	    strcat(sig, "=");
	    strcat(sig, valstr);
	}
	if (ei->signature) FREE(ei->signature);
	ei->signature = sig;
    }
}

/*----------------------------------------------------------------------*/
/* Make a property record holding the parameter values of an instance,	*/
/* to be used as the context in which a cell's contents are resolved.	*/
/* Parameters that could not be resolved are left out, so that the	*/
/* cell's default is used.  Free the result with FreeObject().		*/
/*----------------------------------------------------------------------*/

static struct objlist *MakeContext(struct elabcell *ec, struct elabinst *ei,
		char **keys, int nkeys)
{
    struct objlist *ctx;
    struct valuelist *kv;
    double dval;
    int i, n;

    ctx = GetObject();
    ctx->type = PROPERTY;
    ctx->name = strsave("properties");
    ctx->model.class = strsave(ec->cell->name);
    ctx->instance.props = NewPropValue(nkeys + 1);
    ctx->node = -2;
    ctx->next = NULL;

    n = 0;
    for (i = 0; i < nkeys; i++) {
	if (GetInstParam(ec->cell, ei->prop, keys[i], &dval) != 1) continue;
	kv = &(ctx->instance.props[n++]);
	kv->key = strsave(keys[i]);
	kv->type = PROP_DOUBLE;
	kv->value.dval = dval;
    }
    kv = &(ctx->instance.props[n]);
    kv->key = NULL;
    kv->type = PROP_ENDLIST;
    kv->value.ival = 0;
    return ctx;
}

/*----------------------------------------------------------------------*/
/* Resolve all parameter expressions in the contents of a cell, using	*/
/* the parameter values in "ctx" (or the cell's defaults, if NULL).	*/
/*----------------------------------------------------------------------*/

static void ResolveCell(struct nlist *tc, struct objlist *ctx)
{
    struct objlist *ob;

    for (ob = tc->cell; ob; ob = ob->next)
	if (ob->type == PROPERTY)
	    ReduceExpressions(ob, ctx, tc, PARAM_ELABORATE);
}

/*----------------------------------------------------------------------*/
/* Make a copy of a property dictionary entry.				*/
/*----------------------------------------------------------------------*/

static struct property *CopyProperty(struct property *kl)
{
    struct property *klc;

    klc = NewProperty();
    klc->key = strsave(kl->key);
    klc->idx = kl->idx;
    klc->type = kl->type;
    klc->merge = kl->merge;
    klc->pin = (kl->pin) ? strsave(kl->pin) : NULL;
    klc->slop = kl->slop;
    switch (kl->type) {
	case PROP_STRING:
	    klc->pdefault.string = (kl->pdefault.string) ?
			strsave(kl->pdefault.string) : NULL;
	    break;
	case PROP_EXPRESSION:
	    klc->pdefault.stack = CopyTokStack(kl->pdefault.stack);
	    break;
	default:
	    klc->pdefault = kl->pdefault;
	    break;
    }
    return klc;
}

/*----------------------------------------------------------------------*/
/* Make a copy of cell "tc" named "name" in the same file.		*/
/*----------------------------------------------------------------------*/

static struct nlist *CloneCell(struct nlist *tc, char *name)
{
    struct nlist *nc, *savecell;
    struct objlist *ob, *nob, *tail;
    struct property *kl;
    struct Permutation *perm, *nperm, *ptail;

    savecell = CurrentCell;
    CellDef(name, tc->file);
    nc = CurrentCell;

    nc->class = tc->class;
    nc->flags = tc->flags & ~(CELL_TOP | CELL_MATCHED | CELL_PROPSMATCHED |
		CELL_DUPLICATE);

    /* Copy pin permutations */
    ptail = NULL;
    for (perm = tc->permutes; perm; perm = perm->next) {
	nperm = (struct Permutation *)CALLOC(1, sizeof(struct Permutation));
	nperm->pin1 = strsave(perm->pin1);
	nperm->pin2 = strsave(perm->pin2);
	nperm->next = NULL;
	if (ptail) ptail->next = nperm;
	else nc->permutes = nperm;
	ptail = nperm;
    }

    /* Copy parameter defaults and local parameters */
    for (kl = (struct property *)HashFirst(&(tc->propdict)); kl;
		kl = (struct property *)HashNext(&(tc->propdict)))
	HashPtrInstall(kl->key, CopyProperty(kl), &(nc->propdict));

    if (tc->paramdict.hashtab != NULL) {
	InitializeHashTable(&(nc->paramdict), TINYHASHSIZE);
	for (kl = (struct property *)HashFirst(&(tc->paramdict)); kl;
		kl = (struct property *)HashNext(&(tc->paramdict)))
	    HashPtrInstall(kl->key, CopyProperty(kl), &(nc->paramdict));
    }

    /* Copy the contents */
    tail = NULL;
    for (ob = tc->cell; ob; ob = ob->next) {
	nob = GetObject();
	*nob = *ob;
	nob->next = NULL;
	nob->name = (ob->name) ? strsave(ob->name) : NULL;

	if (ob->type == PROPERTY) {
	    nob->instance.props = NULL;
	    nob->model.class = NULL;
	    CopyProperties(nob, ob);
	}
	else {
	    if (ob->type != PORT)
		nob->model.class = (ob->model.class) ?
			strsave(ob->model.class) : NULL;
	    nob->instance.name = (ob->instance.name) ?
			strsave(ob->instance.name) : NULL;
	}

	if (tail) tail->next = nob;
	else nc->cell = nob;
	tail = nob;

	if (nob->type != PROPERTY)
	    HashPtrInstall(nob->name, nob, &(nc->objdict));
	if (nob->type == FIRSTPIN)
	    HashPtrInstall(nob->instance.name, nob, &(nc->instdict));
    }
    CacheNodeNames(nc);

    if (savecell != NULL)
	ReopenCellDef(savecell->name, savecell->file);
    else
	CurrentCell = NULL;

    return nc;
}

/*----------------------------------------------------------------------*/
/* Change an instance to instance a different cell.			*/
/*----------------------------------------------------------------------*/

static void RetargetInstance(struct elabinst *ei, char *name)
{
    struct objlist *ob;

    ob = ei->first;
    do {
	FREE(ob->model.class);
	ob->model.class = strsave(name);
	ob = ob->next;
    } while (ob && (ob->type > FIRSTPIN));

    if (ei->prop != NULL) {
	FREE(ei->prop->model.class);
	ei->prop->model.class = strsave(name);
    }
}

/*----------------------------------------------------------------------*/
/* Remove the cell's parameters (other than "M") from the property	*/
/* records of its instances.  After elaboration, the values are part	*/
/* of the contents of the cell (or of its copy), so they are no longer	*/
/* needed;  and if they were left, they would be compared against the	*/
/* properties of instances in the other netlist, which normally has	*/
/* no such parameters.							*/
/*----------------------------------------------------------------------*/

static void StripInstParams(struct elabcell *ec, char **keys, int nkeys)
{
    struct elabinst *ei;
    struct elabcell *pc;
    struct valuelist *kv, *kvd;
    struct tokstack *stackptr, *nptr;
    int i, j, k;

    for (ei = ec->insts; ei; ei = ei->next) {
	if (ei->prop == NULL) continue;
	pc = LookupElabCell(ei->parent->name);
	if ((pc != NULL) && pc->dead) continue;

	j = 0;
	for (i = 0; ; i++) {
	    kv = &(ei->prop->instance.props[i]);
	    if (kv->type == PROP_ENDLIST) break;
	    for (k = 0; k < nkeys; k++)
		if ((*matchfunc)(kv->key, keys[k])) break;
	    if (k < nkeys) {
		/* Parameter of the cell:  remove it */
		FREE(kv->key);
		if (kv->type == PROP_STRING) {
		    if (kv->value.string) FREE(kv->value.string);
		}
		else if (kv->type == PROP_EXPRESSION) {
		    stackptr = kv->value.stack;
		    while (stackptr != NULL) {
			nptr = stackptr->next;
			if (stackptr->toktype == TOK_STRING)
			    FREE(stackptr->data.string);
			FREE(stackptr);
			stackptr = nptr;
		    }
		}
		continue;
	    }
	    if (j != i) {
		kvd = &(ei->prop->instance.props[j]);
		*kvd = *kv;
	    }
	    j++;
	}
	/* Move the end-of-list record */
	kvd = &(ei->prop->instance.props[j]);
	kvd->key = NULL;
	kvd->type = PROP_ENDLIST;
	kvd->value.ival = 0;
    }
}

/*----------------------------------------------------------------------*/
/* Elaborate one cell, after all of the cells that instance it have	*/
/* been elaborated.  Copies made are recorded in "ef".			*/
/*----------------------------------------------------------------------*/

static void ElaborateCell(struct elabcell *ec, struct elabfile *ef)
{
    struct elabinst *ei, *ej;
    struct elabcell *pc, *ncc;
    struct nlist *tc = ec->cell, *nc;
    struct objlist *ctx, *ob;
    struct elabrec *er, *ertail;
    char **keys, vname[MAX_STR_LEN];
    int nkeys, nsets, idx, live;

    /* Count instances in cells that have not been replaced by copies */
    live = 0;
    for (ei = ec->insts; ei; ei = ei->next) {
	pc = LookupElabCell(ei->parent->name);
	if ((pc == NULL) || !pc->dead) live++;
    }

    nkeys = GetCellParams(tc, &keys);

    /* A cell with no instances (top level) or no parameters is	*/
    /* resolved with its own default values.			*/

    if ((live == 0) || (nkeys == 0)) {
	ResolveCell(tc, NULL);
	FREE(keys);
	return;
    }

    SetSignatures(ec, keys, nkeys);

    /* Count the unique sets of parameter values */
    nsets = 0;
    for (ei = ec->insts; ei; ei = ei->next) {
	if (ei->signature == NULL) continue;	/* In a replaced cell */
	for (ej = ec->insts; ej != ei; ej = ej->next)
	    if (ej->signature && !strcmp(ej->signature, ei->signature))
		break;
	if (ej == ei) nsets++;
    }

    if (nsets == 1) {
	/* All instances have the same parameters:  resolve in place */
	for (ei = ec->insts; ei && (ei->signature == NULL); ei = ei->next);
	ctx = MakeContext(ec, ei, keys, nkeys);
	ResolveCell(tc, ctx);
	FreeObject(ctx);
	StripInstParams(ec, keys, nkeys);
	FREE(keys);
	return;
    }

    /* Make a copy of the cell for each unique set of parameters */

    Printf("Subcircuit %s has %d unique sets of parameters:\n", tc->name, nsets);
    for (ertail = ef->records; ertail && ertail->next; ertail = ertail->next);

    idx = 1;
    for (ei = ec->insts; ei; ei = ei->next) {
	if (ei->signature == NULL) continue;
	for (ej = ec->insts; ej != ei; ej = ej->next)
	    if (ej->signature && !strcmp(ej->signature, ei->signature))
		break;
	if (ej != ei) continue;		/* Set was already handled */

	/* Find an unused name */
	do {
	    snprintf(vname, MAX_STR_LEN - 1, "%s@%d", tc->name, idx++);
	} while (LookupCellFile(vname, tc->file) != NULL);

	nc = CloneCell(tc, vname);
	ctx = MakeContext(ec, ei, keys, nkeys);
	ResolveCell(nc, ctx);
	FreeObject(ctx);

	/* Change all instances with this set to use the copy */
	nc->number = 0;
	for (ej = ei; ej; ej = ej->next) {
	    if (ej->signature && !strcmp(ej->signature, ei->signature)) {
		RetargetInstance(ej, vname);
		nc->number++;
		tc->number--;
	    }
	}

	/* The copy's instances of other cells are new instances */
	ncc = (struct elabcell *)CALLOC(1, sizeof(struct elabcell));
	ncc->cell = nc;
	HashPtrInstall(nc->name, ncc, &elabdict);
	for (ob = nc->cell; ob; ob = ob->next)
	    if (ob->type == FIRSTPIN)
		AddElabInst(nc, ob, FALSE);

	Printf("    %s:  %s\n", vname, ei->signature);

	er = (struct elabrec *)CALLOC(1, sizeof(struct elabrec));
	er->cellname = strsave(tc->name);
	er->variant = strsave(vname);
	er->params = strsave(ei->signature);
	er->next = NULL;
	if (ertail) ertail->next = er;
	else ef->records = er;
	ertail = er;
    }

    StripInstParams(ec, keys, nkeys);

    /* The original cell is no longer instanced.  Resolve it with its	*/
    /* default values, so that it can still be compared on its own.	*/
    ResolveCell(tc, NULL);
    ec->dead = TRUE;
    FREE(keys);
}

/*----------------------------------------------------------------------*/
/* Elaborate all cells in a file (netlist).  Does nothing if the file	*/
/* has already been elaborated since the last netlist was read into	*/
/* it.  Returns the number of subcircuit copies made.			*/
/*----------------------------------------------------------------------*/

int ElaborateFile(int file)
{
    struct elabfile *ef;
    struct elabcell *ec, *queue, *qtail, *cc;
    struct elabinst *ei;
    struct objlist *ob;
    struct elabrec *er;
    int ncopies = 0;

    if (file < 0) return 0;
    ef = GetElabFile(file, TRUE);
    if (ef->done) return 0;
    ef->done = TRUE;
    FreeElabRecords(ef);

    elabfile = file;
    SetParamFile(file);
    InitializeHashTable(&elabdict, OBJHASHSIZE);

    /* Make a record for each cell with contents, then find the	*/
    /* instances of each cell.					*/

    RecurseCellFileHashTable(makeelabcell, file);
    RecurseCellFileHashTable(findelabinsts, file);

    /* Process the cells in topological order (each cell after all	*/
    /* of the cells that instance it), starting with the cells that	*/
    /* are not instanced anywhere.					*/

    ElabRoots = NULL;
    RecurseHashTable(&elabdict, findelabroots);
    queue = ElabRoots;
    for (qtail = queue; qtail && qtail->next; qtail = qtail->next);

    while (queue != NULL) {
	ec = queue;
	queue = queue->next;
	if (queue == NULL) qtail = NULL;
	ec->next = NULL;

	ElaborateCell(ec, ef);

	/* Release the cells that this cell (or its copies) instances */
	for (ob = ec->cell->cell; ob; ob = ob->next) {
	    if (ob->type != FIRSTPIN) continue;
	    cc = LookupElabCell(ob->model.class);
	    if (cc == NULL) continue;
	    if (cc->cell == ec->cell) continue;	/* Shouldn't happen */
	    if (--cc->indegree == 0) {
		if (qtail) qtail->next = cc;
		else queue = cc;
		qtail = cc;
	    }
	}
    }

    /* Any cell not processed is part of a recursive loop of	*/
    /* instances, which is an error in the netlist.  Just resolve	*/
    /* it with its default values.				*/

    for (ec = (struct elabcell *)HashFirst(&elabdict); ec;
		ec = (struct elabcell *)HashNext(&elabdict)) {
	if (ec->indegree > 0) {
	    Fprintf(stderr, "Warning:  Cell %s is part of a recursive loop of "
			"instances.\n", ec->cell->name);
	    ec->indegree = 0;
	    ResolveCell(ec->cell, NULL);
	}
    }

    RecurseHashTable(&elabdict, freeelabcell);
    HashKill(&elabdict);
    elabfile = -1;
    SetParamFile(-1);

    for (er = ef->records; er; er = er->next) ncopies++;
    return ncopies;
}
