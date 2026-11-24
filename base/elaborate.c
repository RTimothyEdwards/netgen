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
/*									*/
/* Verilog modules that netgen's verilog reader cannot read (e.g.,	*/
/* because they have generate blocks or expressions depending on	*/
/* parameters) are elaborated by yosys (through the Tcl procedure	*/
/* netgen::yosys_elaborate), which writes a simple netlist that netgen	*/
/* reads in place of the module.  This is done first with the		*/
/* module's default parameter values, replacing the contents of the	*/
/* module, and then again for each other set of parameter values	*/
/* passed to it, in place of copying the module and resolving its	*/
/* parameters.								*/

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

#ifdef TCL_NETGEN
extern Tcl_Interp *netgeninterp;
#endif

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
    unsigned char done;		/* Cell has been elaborated */
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

    /* A copy of a cell elaborated by yosys may instance a cell that	*/
    /* the original did not (e.g., in a conditional generate block),	*/
    /* and which may then have already been elaborated.		*/
    if (ec->done && (ec->cell != parent))
	Printf("Warning:  Cell %s is instanced by %s (elaborated by yosys) "
		"after %s was elaborated;  parameters passed to it by %s "
		"are not resolved.\n", ec->cell->name, parent->name,
		ec->cell->name, parent->name);

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
/* The contents of cell "parent" have been replaced (by yosys):  remove	*/
/* the records of the instances in its old contents, and add records	*/
/* for the instances in its new contents.  The new instances are not	*/
/* counted in "indegree" (the old ones were).				*/
/*----------------------------------------------------------------------*/

static void RenewElabInsts(struct nlist *parent)
{
    struct elabcell *ec;
    struct elabinst *ei, *eprev, *enext;
    struct objlist *ob;

    for (ec = (struct elabcell *)HashFirst(&elabdict); ec;
		ec = (struct elabcell *)HashNext(&elabdict)) {
	eprev = NULL;
	for (ei = ec->insts; ei; ei = enext) {
	    enext = ei->next;
	    if (ei->parent == parent) {
		if (eprev) eprev->next = enext;
		else ec->insts = enext;
		if (ei->signature) FREE(ei->signature);
		FREE(ei);
	    }
	    else
		eprev = ei;
	}
	ec->lastinst = eprev;
    }

    for (ob = parent->cell; ob; ob = ob->next)
	if (ob->type == FIRSTPIN)
	    AddElabInst(parent, ob, FALSE);
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

static char *MakeSignature(struct nlist *tc, struct objlist *prop,
		char **keys, int nkeys)
{
    char *sig, valstr[64];
    int i, len;
    double dval;

    len = 1;
    for (i = 0; i < nkeys; i++) len += strlen(keys[i]) + 2 + 32;
    sig = (char *)MALLOC(len);
    *sig = '\0';
    for (i = 0; i < nkeys; i++) {
	if (GetInstParam(tc, prop, keys[i], &dval) == 1)
	    snprintf(valstr, 63, "%.10g", dval);
	else
	    strcpy(valstr, "?");
	if (i > 0) strcat(sig, " ");
	strcat(sig, keys[i]);
	strcat(sig, "=");
	strcat(sig, valstr);
    }
    return sig;
}

static void SetSignatures(struct elabcell *ec, char **keys, int nkeys)
{
    struct elabinst *ei;
    struct elabcell *pc;

    for (ei = ec->insts; ei; ei = ei->next) {
	pc = LookupElabCell(ei->parent->name);
	if ((pc != NULL) && pc->dead) continue;
	if (ei->signature) FREE(ei->signature);
	ei->signature = MakeSignature(ec->cell, ei->prop, keys, nkeys);
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
/* Remove a cell from netlist "file".  Port records must be freed	*/
/* separately (CellDelete() would treat "model.port" as a string).	*/
/*----------------------------------------------------------------------*/

static void DeleteCell(char *name, int file)
{
    struct nlist *tp;
    struct objlist *ob, *obnext, *oblast;
    char *cname;

    tp = LookupCellFile(name, file);
    if (tp == NULL) return;

    oblast = NULL;
    for (ob = tp->cell; ob; ob = obnext) {
	obnext = ob->next;
	if (ob->type == PORT) {
	    if (oblast) oblast->next = obnext;
	    else tp->cell = obnext;
	    if (ob->name != NULL) FreeString(ob->name);
	    if (ob->instance.name != NULL) FreeString(ob->instance.name);
	    FREE(ob);
	}
	else
	    oblast = ob;
    }
    cname = strsave(name);	/* "name" may be the cell's own name */
    CellDelete(cname, file);
    FREE(cname);
}

/*----------------------------------------------------------------------*/
/* Put the ports of cell "nc" (read from the output of yosys) in the	*/
/* same order as the ports of cell "tc" (the module as read by netgen),	*/
/* which instances of the module depend on.  Returns TRUE on success,	*/
/* or FALSE if the two do not have the same ports.			*/
/*----------------------------------------------------------------------*/

static int ReorderPorts(struct nlist *nc, struct nlist *tc)
{
    struct objlist *ob, *nob, *prev, *head, *tail, *rest;
    int nports = 0, tports = 0;

    for (ob = tc->cell; ob; ob = ob->next)
	if (ob->type == PORT) tports++;

    /* Move each port of nc, in the order of the ports of tc, from	*/
    /* nc's list to a new list.						*/
    head = tail = NULL;
    for (ob = tc->cell; ob; ob = ob->next) {
	if (ob->type != PORT) continue;
	prev = NULL;
	for (nob = nc->cell; nob; nob = nob->next) {
	    if ((nob->type == PORT) && (*matchfunc)(nob->name, ob->name)) break;
	    prev = nob;
	}
	if (nob == NULL) {
	    Printf("Module %s from yosys has no port %s.\n", tc->name, ob->name);
	    break;
	}
	if (prev) prev->next = nob->next;
	else nc->cell = nob->next;
	nob->next = NULL;
	if (tail) tail->next = nob;
	else head = nob;
	tail = nob;
	nports++;
    }

    /* Ports that were not moved */
    rest = nc->cell;
    for (nob = rest; nob; nob = nob->next)
	if (nob->type == PORT) {
	    if (nports == tports)
		Printf("Module %s from yosys has port %s, which the module "
			"read by netgen does not.\n", tc->name, nob->name);
	    nports = -1;
	}

    /* Put the list back together (ports first) */
    if (tail) {
	tail->next = rest;
	nc->cell = head;
    }
    if (nports != tports) return FALSE;

    FreeNodeNames(nc);
    CacheNodeNames(nc);
    return TRUE;
}

/*----------------------------------------------------------------------*/
/* Replace the contents of cell "tc" with the contents of cell "nc",	*/
/* and remove cell "nc".  The ports must already be in the same order	*/
/* (see ReorderPorts()).  The cell "tc" itself is kept, as other	*/
/* records point to it.  Its parameters and flags are not changed.	*/
/*----------------------------------------------------------------------*/

static void ReplaceContents(struct nlist *tc, struct nlist *nc)
{
    struct objlist *ob;
    struct hashdict dict;
    struct objlist **cache;
    long maxnum;

    ob = tc->cell;
    tc->cell = nc->cell;
    nc->cell = ob;

    dict = tc->objdict;
    tc->objdict = nc->objdict;
    nc->objdict = dict;

    dict = tc->instdict;
    tc->instdict = nc->instdict;
    nc->instdict = dict;

    cache = tc->nodename_cache;
    tc->nodename_cache = nc->nodename_cache;
    nc->nodename_cache = cache;

    maxnum = tc->nodename_cache_maxnodenum;
    tc->nodename_cache_maxnodenum = nc->nodename_cache_maxnodenum;
    nc->nodename_cache_maxnodenum = maxnum;

    tc->class = nc->class;

    /* The cells instanced by the original contents (now in nc) are no	*/
    /* longer instanced there.						*/
    for (ob = nc->cell; ob; ob = ob->next) {
	struct nlist *cc;
	if (ob->type != FIRSTPIN) continue;
	cc = LookupCellFile(ob->model.class, tc->file);
	if ((cc != NULL) && (cc->number > 0)) cc->number--;
    }

    /* This removes the original contents of tc, now in nc */
    DeleteCell(nc->name, nc->file);
}

#ifdef TCL_NETGEN

/*----------------------------------------------------------------------*/
/* Callback to make a list of the cells in a netlist, other than	*/
/* "ExtTarget", for netgen::yosys_elaborate:  each is a list of the	*/
/* cell name, its pin names, and its parameter names.			*/
/*----------------------------------------------------------------------*/

static struct nlist *ExtTarget;
static Tcl_Obj *ExtCellList;

static int addextcell(struct hashlist *p, int file)
{
    struct nlist *tc = (struct nlist *)(p->ptr);
    struct objlist *ob;
    struct property *kl;
    Tcl_Obj *elem, *pins, *params;

    if (tc->file != file) return 0;
    if (tc == ExtTarget) return 0;
    if (tc->flags & CELL_TOP) return 0;		/* Cell for the file itself */

    pins = Tcl_NewListObj(0, NULL);
    for (ob = tc->cell; ob; ob = ob->next)
	if (ob->type == PORT)
	    Tcl_ListObjAppendElement(netgeninterp, pins,
			Tcl_NewStringObj(ob->name, -1));

    params = Tcl_NewListObj(0, NULL);
    for (kl = (struct property *)HashFirst(&(tc->propdict)); kl;
		kl = (struct property *)HashNext(&(tc->propdict)))
	Tcl_ListObjAppendElement(netgeninterp, params,
			Tcl_NewStringObj(kl->key, -1));

    elem = Tcl_NewListObj(0, NULL);
    Tcl_ListObjAppendElement(netgeninterp, elem, Tcl_NewStringObj(tc->name, -1));
    Tcl_ListObjAppendElement(netgeninterp, elem, pins);
    Tcl_ListObjAppendElement(netgeninterp, elem, params);
    Tcl_ListObjAppendElement(netgeninterp, ExtCellList, elem);
    return 0;
}

/*----------------------------------------------------------------------*/
/* Run yosys (through the Tcl procedure netgen::yosys_elaborate) on the	*/
/* verilog module "tc" (recorded in "em"), with the parameter values in	*/
/* "params" (a Tcl list of names and values, or NULL for the module's	*/
/* defaults), and read the result into the netlist as cell "newname",	*/
/* with its ports in the same order as those of "tc".  Returns the new	*/
/* cell, or NULL if yosys failed or its output could not be used.	*/
/*----------------------------------------------------------------------*/

static struct nlist *YosysElaborate(struct nlist *tc, struct extmodule *em,
		Tcl_Obj *params, char *newname)
{
    Tcl_Obj *cmd;
    struct nlist *nc;
    char *outname;
    int result;

    if (em->source == NULL) return NULL;

    ExtTarget = tc;
    ExtCellList = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(ExtCellList);
    RecurseCellFileHashTable(addextcell, tc->file);

    cmd = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(cmd);
    Tcl_ListObjAppendElement(netgeninterp, cmd,
		Tcl_NewStringObj("netgen::yosys_elaborate", -1));
    Tcl_ListObjAppendElement(netgeninterp, cmd, Tcl_NewStringObj(em->source, -1));
    Tcl_ListObjAppendElement(netgeninterp, cmd, Tcl_NewStringObj(tc->name, -1));
    Tcl_ListObjAppendElement(netgeninterp, cmd, Tcl_NewStringObj("-cells", -1));
    Tcl_ListObjAppendElement(netgeninterp, cmd, ExtCellList);
    if (params != NULL) {
	Tcl_ListObjAppendElement(netgeninterp, cmd, Tcl_NewStringObj("-param", -1));
	Tcl_ListObjAppendElement(netgeninterp, cmd, params);
    }
    if (em->defines != NULL) {
	Tcl_ListObjAppendElement(netgeninterp, cmd, Tcl_NewStringObj("-define", -1));
	Tcl_ListObjAppendElement(netgeninterp, cmd,
		Tcl_NewStringObj(em->defines, -1));
    }
    result = Tcl_EvalObjEx(netgeninterp, cmd, TCL_EVAL_GLOBAL);
    Tcl_DecrRefCount(cmd);
    Tcl_DecrRefCount(ExtCellList);
    ExtTarget = NULL;

    if (result != TCL_OK) {
	Fprintf(stderr, "Error running yosys:  %s\n",
		Tcl_GetStringResult(netgeninterp));
	Tcl_ResetResult(netgeninterp);
	return NULL;
    }
    outname = strsave((char *)Tcl_GetStringResult(netgeninterp));
    Tcl_ResetResult(netgeninterp);
    if (*outname == '\0') {
	FREE(outname);
	return NULL;
    }

    nc = ReadVerilogModuleAs(outname, tc->file, tc->name, newname);
    if (nc == NULL)
	Printf("The output of yosys (%s) could not be read.\n", outname);
    else if (nc->class == CLASS_MODULE) {
	Printf("The output of yosys for module %s (%s) is not a netlist.\n",
		tc->name, outname);
	DeleteCell(newname, tc->file);
	nc = NULL;
    }
    else if (ReorderPorts(nc, tc) == FALSE) {
	Printf("The ports of module %s from yosys (%s) do not match the "
		"module as read by netgen.\n", tc->name, outname);
	DeleteCell(newname, tc->file);
	nc = NULL;
    }
    else
	remove(outname);	/* Keep the file if it could not be used */
    FREE(outname);
    return nc;
}

/*----------------------------------------------------------------------*/
/* Make a Tcl list of parameter names and values for an instance with	*/
/* property record "prop" of cell "tc", for YosysElaborate().		*/
/* Parameters whose values are not numbers are left out (so that yosys	*/
/* uses the module's default).						*/
/*----------------------------------------------------------------------*/

static Tcl_Obj *YosysParams(struct nlist *tc, struct objlist *prop,
		char **keys, int nkeys)
{
    Tcl_Obj *params;
    char valstr[64];
    double dval;
    int i;

    params = Tcl_NewListObj(0, NULL);
    for (i = 0; i < nkeys; i++) {
	if (GetInstParam(tc, prop, keys[i], &dval) != 1) {
	    Printf("Warning:  Value of parameter %s of module %s is not a "
		"number;  using the default value.\n", keys[i], tc->name);
	    continue;
	}
	if ((dval == (double)(long long)dval) && (dval < 1e18) && (dval > -1e18))
	    snprintf(valstr, 63, "%lld", (long long)dval);
	else
	    snprintf(valstr, 63, "%.17g", dval);
	Tcl_ListObjAppendElement(netgeninterp, params,
		Tcl_NewStringObj(keys[i], -1));
	Tcl_ListObjAppendElement(netgeninterp, params,
		Tcl_NewStringObj(valstr, -1));
    }
    return params;
}

#endif	/* TCL_NETGEN */

/*----------------------------------------------------------------------*/
/* Return the record of a module to be elaborated by yosys, if cell	*/
/* "tc" is one (and yosys has replaced its contents), or NULL.		*/
/*----------------------------------------------------------------------*/

static struct extmodule *ExtModuleOf(struct nlist *tc)
{
    struct extmodule *em;

    if (!(tc->flags & CELL_EXTERNAL)) return NULL;
    em = ExtModuleLookup(tc->name, tc->file);
    if ((em == NULL) || (em->status != EXT_DONE)) return NULL;
    return em;
}

/*----------------------------------------------------------------------*/
/* Make a copy of cell "tc" named "vname" for the instances having the	*/
/* parameters of instance "ei", with the parameters resolved.  For a	*/
/* module elaborated by yosys, yosys makes the copy (unless the		*/
/* parameters are those of the module's current contents).		*/
/*----------------------------------------------------------------------*/

static struct nlist *MakeVariant(struct elabcell *ec, struct elabinst *ei,
		char **keys, int nkeys, char *vname)
{
    struct nlist *tc = ec->cell, *nc = NULL;
    struct extmodule *em;
    struct objlist *ctx;
    struct property *kl;

    em = ExtModuleOf(tc);
#ifdef TCL_NETGEN
    if ((em != NULL) && ((em->params == NULL) || strcmp(em->params, ei->signature))) {
	Tcl_Obj *params = YosysParams(tc, ei->prop, keys, nkeys);
	Tcl_IncrRefCount(params);
	nc = YosysElaborate(tc, em, params, vname);
	Tcl_DecrRefCount(params);
	if (nc != NULL) {
	    nc->flags |= tc->flags & ~(CELL_TOP | CELL_MATCHED |
			CELL_PROPSMATCHED | CELL_DUPLICATE | CELL_EXTERNAL);
	    for (kl = (struct property *)HashFirst(&(tc->propdict)); kl;
			kl = (struct property *)HashNext(&(tc->propdict)))
		HashPtrInstall(kl->key, CopyProperty(kl), &(nc->propdict));
	    return nc;
	}
	Printf("Warning:  Module %s could not be elaborated by yosys with "
		"parameters %s;  using its contents for %s.\n", tc->name,
		ei->signature, (em->params) ? em->params : "default parameters");
    }
#endif
    nc = CloneCell(tc, vname);
    if (em != NULL) nc->flags &= ~CELL_EXTERNAL;
    ctx = MakeContext(ec, ei, keys, nkeys);
    ResolveCell(nc, ctx);
    FreeObject(ctx);
    return nc;
}

/*----------------------------------------------------------------------*/
/* Have yosys elaborate the module "tc" (recorded in "em") in place,	*/
/* with the parameters of instance "ei", or with its defaults if "ei"	*/
/* is NULL.  Returns TRUE on success.					*/
/*----------------------------------------------------------------------*/

static int YosysInPlace(struct nlist *tc, struct extmodule *em,
		struct elabinst *ei, char **keys, int nkeys, char *signature)
{
#ifdef TCL_NETGEN
    struct nlist *nc;
    Tcl_Obj *params = NULL;
    char tmpname[MAX_STR_LEN];

    snprintf(tmpname, MAX_STR_LEN - 1, "%s@yosys", tc->name);
    if (ei != NULL) {
	params = YosysParams(tc, ei->prop, keys, nkeys);
	Tcl_IncrRefCount(params);
    }
    nc = YosysElaborate(tc, em, params, tmpname);
    if (params != NULL) Tcl_DecrRefCount(params);
    if (nc == NULL) return FALSE;

    ReplaceContents(tc, nc);
    if (em->params != NULL) FREE(em->params);
    em->params = (signature != NULL) ? strsave(signature) : NULL;
    return TRUE;
#else
    return FALSE;
#endif
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
    struct extmodule *em;
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
	em = ExtModuleOf(tc);
	if (em != NULL) {
	    /* Module elaborated by yosys:  do it again, unless the	*/
	    /* contents already have these parameters.			*/
	    if ((em->params == NULL) || strcmp(em->params, ei->signature)) {
		if (YosysInPlace(tc, em, ei, keys, nkeys, ei->signature)) {
		    Printf("Module %s elaborated by yosys with parameters %s\n",
				tc->name, ei->signature);
		    RenewElabInsts(tc);
		}
		else
		    Printf("Warning:  Module %s could not be elaborated by yosys "
				"with parameters %s;  using %s.\n", tc->name,
				ei->signature, (em->params) ? em->params :
				"default parameters");
	    }
	}
	else {
	    ctx = MakeContext(ec, ei, keys, nkeys);
	    ResolveCell(tc, ctx);
	    FreeObject(ctx);
	}
	StripInstParams(ec, keys, nkeys);
	FREE(keys);
	return;
    }

    /* Make a copy of the cell for each unique set of parameters */

    Printf("Subcircuit %s has %d unique sets of parameters%s:\n", tc->name, nsets,
		(ExtModuleOf(tc) != NULL) ? " (copies made by yosys)" : "");
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

	nc = MakeVariant(ec, ei, keys, nkeys, vname);

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
/* Callback to collect the verilog modules in a file that are waiting	*/
/* to be elaborated by yosys.						*/
/*----------------------------------------------------------------------*/

struct extlist {
    struct nlist *cell;
    struct extmodule *em;
    struct extlist *next;
};

static struct extlist *ExtPending;

static int findextpending(struct hashlist *p, int file)
{
    struct nlist *tc = (struct nlist *)(p->ptr);
    struct extmodule *em;
    struct extlist *el;

    if (tc->file != file) return 0;
    if (!(tc->flags & CELL_EXTERNAL)) return 0;
    em = ExtModuleLookup(tc->name, tc->file);
    if ((em == NULL) || (em->status != EXT_PENDING)) return 0;

    el = (struct extlist *)MALLOC(sizeof(struct extlist));
    el->cell = tc;
    el->em = em;
    el->next = ExtPending;
    ExtPending = el;
    return 0;
}

/*----------------------------------------------------------------------*/
/* Have yosys elaborate each verilog module in the file that netgen's	*/
/* verilog reader could not read, with its default parameter values,	*/
/* replacing the module's contents.  This is done before the rest of	*/
/* elaboration so that the cells instanced by the module are known.	*/
/*----------------------------------------------------------------------*/

static void ElaborateExternal(int file)
{
    struct extlist *el;
    struct nlist *tc;
    char **keys, *sig;
    int nkeys;

    ExtPending = NULL;
    RecurseCellFileHashTable(findextpending, file);

    while (ExtPending != NULL) {
	el = ExtPending;
	ExtPending = el->next;
	tc = el->cell;

	nkeys = GetCellParams(tc, &keys);
	sig = MakeSignature(tc, NULL, keys, nkeys);
	if (YosysInPlace(tc, el->em, NULL, keys, nkeys, sig)) {
	    el->em->status = EXT_DONE;
	    Printf("Module %s elaborated by yosys.\n", tc->name);
	}
	else if (el->em->forced) {
	    /* Netgen read the module, so use that */
	    el->em->status = EXT_FAILED;
	    tc->class = el->em->class;
	    tc->flags &= ~CELL_EXTERNAL;
	    Printf("Module %s could not be elaborated by yosys;  netgen's "
			"reading of it is used.\n", tc->name);
	}
	else {
	    el->em->status = EXT_FAILED;
	    Printf("Module %s could not be elaborated by yosys, and is "
			"treated as a black box.\n", tc->name);
	}
	FREE(sig);
	FREE(keys);
	FREE(el);
    }
}

/*----------------------------------------------------------------------*/
/* Elaborate all cells in a file (netlist).  Does nothing if the file	*/
/* has already been elaborated since the last netlist was read into	*/
/* it.  Returns the number of subcircuit copies made.			*/
/*----------------------------------------------------------------------*/

int ElaborateFile(int file)
{
    struct elabfile *ef;
    struct elabcell *ec, *queue, *qtail, *cc, **children;
    struct elabinst *ei;
    struct objlist *ob;
    struct elabrec *er;
    int ncopies = 0, nchild, i;

    if (file < 0) return 0;
    ef = GetElabFile(file, TRUE);
    if (ef->done) return 0;
    ef->done = TRUE;
    FreeElabRecords(ef);

    elabfile = file;
    SetParamFile(file);

    ElaborateExternal(file);

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

	/* Find the cells that this cell instances (as counted in their	*/
	/* "indegree") before elaborating it, as yosys may replace its	*/
	/* contents.							*/
	nchild = 0;
	for (ob = ec->cell->cell; ob; ob = ob->next)
	    if (ob->type == FIRSTPIN) nchild++;
	children = (struct elabcell **)MALLOC((nchild + 1) *
			sizeof(struct elabcell *));
	nchild = 0;
	for (ob = ec->cell->cell; ob; ob = ob->next) {
	    if (ob->type != FIRSTPIN) continue;
	    cc = LookupElabCell(ob->model.class);
	    if (cc == NULL) continue;
	    if (cc->cell == ec->cell) continue;	/* Shouldn't happen */
	    children[nchild++] = cc;
	}

	ElaborateCell(ec, ef);
	ec->done = TRUE;

	/* Release the cells that this cell (or its copies) instances */
	for (i = 0; i < nchild; i++) {
	    cc = children[i];
	    if (--cc->indegree == 0) {
		if (qtail) qtail->next = cc;
		else queue = cc;
		qtail = cc;
	    }
	}
	FREE(children);
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
