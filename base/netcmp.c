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

/* netcmp.c  -- graph isomorphism testing */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>  /* for rand(), abs(), etc */
#include <setjmp.h>
#include <signal.h>
#include <time.h>    /* for time() as a seed for random number generator */
#include <limits.h>
#include <math.h>    /* for fabs() */
#include <ctype.h>   /* for toupper() */

#ifdef IBMPC
#include <alloc.h>
#include <process.h>  /* for exit() */
/* #include <stdlib.h>   for rand(), srand() */
#endif

#ifdef TCL_NETGEN
#include <tcl.h>
#endif

#include "netgen.h"
#include "netcmp.h"
#include "hash.h"
#include "objlist.h"
#include "query.h"
#include "netfile.h"
#include "print.h"
#include "dbug.h"
#include "hash.h"

#ifdef TCL_NETGEN
int InterruptPending = 0;
void (*oldinthandler)() = SIG_DFL;
extern Tcl_Interp *netgeninterp;
extern int check_interrupt();
#endif

/* define the following to debug core allocation */
#undef DEBUG_ALLOC

/* define the following to try to expedite initialization of 
   data structures by allocating some extra lookup tables */
#define LOOKUP_INITIALIZATION

/*  The data-structures for NETCOMP are rather complicated:

    1) A global variable ElementClasses points to a
    linked-list of ElementClass.  Similarly, a global variable NodeClasses
    points to a linked-list of NodeClass.

    2) Each ElementClass record points to a linked list of Element.
    Each NodeClass points to a linked list of Node.
    This list represents an equivalence class of Elements/Nodes, and
    incorporates components from both graphs.  A legal class has an
    equal number of components from each graph.
*/


struct NodeList {
	struct NodeList *next;
	struct Node *node;
	struct Element *element; 
        /* pins in the same nodelist with equal 'pin_magic' are permutable */
	unsigned long pin_magic;  
};

struct Element {
	unsigned long hashval;
	short graph;  /* which graph did this element come from ? */
	struct objlist *object;  /* points to the START of the object */
	struct Element *next;
	struct ElementClass *elemclass;
	struct NodeList *nodelist;
};

struct ElementClass {
	unsigned long magic;
	struct Element *elements;
	struct ElementClass *next;
	int count;
	int legalpartition;
};

struct NodeClass {
	unsigned long magic;
	struct Node *nodes;
	struct NodeClass *next;
	int count;
	int legalpartition;
};

struct Node {
	unsigned long hashval;
	short graph;  /* which graph did this node come from ? */
	struct objlist *object;
	struct ElementList *elementlist;
	struct NodeClass *nodeclass;
	struct Node *next;
};

struct ElementList {
	struct NodeList *subelement;
	struct Node *node;
	struct ElementList *next;
};

struct Correspond {
	char *class1;
	int file1;
	char *class2;
	int file2;
	struct Correspond *next;
};

struct ElementClass *ElementClasses = NULL;
struct NodeClass *NodeClasses = NULL;
struct Correspond *ClassCorrespondence = NULL;
struct Correspond *CompareQueue = NULL;
struct IgnoreList *ClassIgnore = NULL;

/* global variables for return of lists from CreateLists */
static struct Element *Elements;
static struct Node *Nodes;

/* keep free lists of Elements and Nodes */
static struct Element *ElementFreeList = NULL;
static struct Node *NodeFreeList = NULL;
static struct ElementClass *ElementClassFreeList = NULL;
static struct NodeClass *NodeClassFreeList = NULL;
static struct ElementList *ElementListFreeList = NULL;
static struct NodeList *NodeListFreeList = NULL;

/* global variables to keep track of circuits */
struct nlist *Circuit1;
struct nlist *Circuit2;

/* global variables to handle the output line width */
int left_col_end = 43;
int right_col_end = 87;

/* if TRUE, always partition ALL classes */
int ExhaustiveSubdivision = 0;

#ifdef TEST
static void PrintElement_List(struct Element *E)
{
  struct NodeList *nl;
	
  for (; E != NULL; E = E->next) {
    struct objlist *ob;
    Fprintf(stdout, "   Element %s, circuit: %hd hashval = %lX\n", 
	   E->object->instance.name, E->graph, E->hashval);
    ob = E->object;
    for (nl = E->nodelist; nl != NULL; nl = nl->next) {
#if 1
      Fprintf(stdout, "      %s:  node: %s name: %d pin magic: %lX nodeclassmagic: %lX\n",
	     ob->name + strlen(ob->instance.name) + 1,
	     nl->node->object->name,  nl->node->object->node, 
	     nl->pin_magic, nl->node->nodeclass->magic);
#else
      Fprintf(stdout, "      %s: %lX  node: %lX num: %d magic: %lX nodeclassmag: %lX\n",
	     ob->name + strlen(ob->instance.name) + 1,
	     (long)nl, (long)(nl->node), nl->node->object->node, 
	     nl->magic, nl->node->nodeclass->magic);
#endif
      ob = ob->next;
    }
  }
}

static char *ElementList_Name(struct ElementList *e)
/* returns a pointer to the name of the pin that a particular 
   ElementList element contacts.  */
{
  struct objlist *ob;
  struct NodeList *nl;

  ob = e->subelement->element->object;
  nl = e->subelement->element->nodelist;
  while (nl != e->subelement) {
    nl = nl->next;
    ob = ob->next;
  }
  return(ob->name);
}



static void PrintNode_List(struct Node *N)
{
  struct ElementList *el;
	
  for (; N != NULL; N = N->next) {
    Fprintf(stdout, "   Node %s circuit: %hd (num = %d) addr = %lX, hashval = %lX\n", 
	   N->object->name, N->graph, N->object->node, (long)N, N->hashval);
    for (el = N->elementlist; el != NULL; el = el->next) {
#if 1
      Fprintf(stdout, "      name: %10s pin magic: %lX element class magic: %lX\n", 
	     ElementList_Name(el), el->subelement->pin_magic,
	     el->subelement->element->elemclass->magic);
#else
      Fprintf(stdout, "      %lX  element: %lX name: %s pin magic: "
		"%lX class magic: %lX\n", 
	     (long)el, (long)(el->subelement), 
/*	     el->subelement->element->object->instance.name, */
	     ElementList_Name(el),
	     el->subelement->magic,
	     el->subelement->element->elemclass->magic);
#endif
    }
  }
}

/* For PrintElementClasses() and PrintNodeClasses(), type is:	*/
/*  0 -> Print legal partitions	 (matching groups)		*/
/*  1 -> Print illegal partitions (nonmatching groups)		*/
/* -1 -> Print all partitions					*/

/* Ignore "dolist" in test version */

void PrintElementClasses(struct ElementClass *EC, int type, int dolist)
{
  while (EC != NULL) {
#ifdef TCL_NETGEN
    if (check_interrupt()) break;
#endif
    if (EC->legalpartition) {
       if (type != 1) {
          Fprintf(stdout, "Device class: count = %d; magic = %lX",
			EC->count, EC->magic);
	  Fprintf(stdout, " -- matching group\n");
          PrintElement_List(EC->elements);
       }
    }
    else {
       if (type != 0) {
          Fprintf(stdout, "Device class: count = %d; magic = %lX",
			EC->count, EC->magic);
	  Fprintf(stdout, " -- nonmatching group\n");
          PrintElement_List(EC->elements);
       }
    }
    Fprintf(stdout, "\n");
    EC = EC->next;
  }
}

void PrintNodeClasses(struct NodeClass *NC, int type, int dolist)
{
  while (NC != NULL) {
#ifdef TCL_NETGEN
    if (check_interrupt()) break;
#endif
    if (NC->legalpartition) {
       if (type != 1) {
          Fprintf(stdout, "Net class: count = %d; magic = %lX",
			NC->count, NC->magic);
          Fprintf(stdout, " -- matching group\n");
          PrintNode_List(NC->nodes);
       }
    }
    else {
       if (type != 0) {
          Fprintf(stdout, "Net class: count = %d; magic = %lX",
			NC->count, NC->magic);
          Fprintf(stdout, " -- nonmatching group\n");
          PrintNode_List(NC->nodes);
       }
    }
    Fprintf(stdout, "\n");
    NC = NC->next;
  }
}

#else	/* NOT TEST */

/* For PrintElementClasses() and PrintNodeClasses(), type is:	*/
/*  0 -> Print legal partitions					*/
/*  1 -> Print illegal partitions				*/
/* -1 -> Print all partitions					*/

void PrintElementClasses(struct ElementClass *EC, int type, int dolist)
{
  struct Element *E;
#ifdef TCL_NETGEN
  Tcl_Obj *plist;

  plist = Tcl_NewListObj(0, NULL);
#endif

  while (EC != NULL) {
#ifdef TCL_NETGEN
    if (check_interrupt()) break;
#endif
    if (EC->legalpartition) {
       if (type != 1) {
	  if (dolist == 0) {
	     Printf("Device class: count = %d; magic = %lX", EC->count, EC->magic);
	     Printf(" -- matching group\n");

	     for (E = EC->elements; E != NULL; E = E->next) 
                Printf("   %-20s (circuit %hd) hash = %lX\n", 
			E->object->instance.name, E->graph, E->hashval);
	  }
#ifdef TCL_NETGEN
	  else {
	     Tcl_Obj *elist, *epart1, *epart2;
	     elist = Tcl_NewListObj(0, NULL);
	     epart1 = Tcl_NewListObj(0, NULL);
	     epart2 = Tcl_NewListObj(0, NULL);
	     for (E = EC->elements; E != NULL; E = E->next)
	        Tcl_ListObjAppendElement(netgeninterp,
			(E->graph == Circuit1->file) ? epart1 : epart2,
			Tcl_NewStringObj(E->object->instance.name, -1));
		
	     Tcl_ListObjAppendElement(netgeninterp, elist, epart1);
	     Tcl_ListObjAppendElement(netgeninterp, elist, epart2);
	     Tcl_ListObjAppendElement(netgeninterp, plist, elist);
	  }
#endif
       }
    }
    else {
       if (type != 0) {
	  if (dolist == 0) {
	     Printf("Device class: count = %d; magic = %lX", EC->count, EC->magic);
             Printf(" -- nonmatching group\n");

	     for (E = EC->elements; E != NULL; E = E->next) 
                Printf("   %-20s (circuit %hd) hash = %lX\n", 
			E->object->instance.name, E->graph, E->hashval);
	  }
#ifdef TCL_NETGEN
	  else {
	     Tcl_Obj *elist, *epart1, *epart2;
	     elist = Tcl_NewListObj(0, NULL);
	     epart1 = Tcl_NewListObj(0, NULL);
	     epart2 = Tcl_NewListObj(0, NULL);
	     for (E = EC->elements; E != NULL; E = E->next)
	        Tcl_ListObjAppendElement(netgeninterp,
				(E->graph == Circuit1->file) ? epart1 : epart2,
				Tcl_NewStringObj(E->object->instance.name, -1));

	     Tcl_ListObjAppendElement(netgeninterp, elist, epart1);
	     Tcl_ListObjAppendElement(netgeninterp, elist, epart2);
	     Tcl_ListObjAppendElement(netgeninterp, plist, elist);
	  }
#endif
       }
    }
    EC = EC->next;
  }
#ifdef TCL_NETGEN
  Tcl_SetObjResult(netgeninterp, plist);
#endif
}

void PrintNodeClasses(struct NodeClass *NC, int type, int dolist)
{
  struct Node *N;
#ifdef TCL_NETGEN
  Tcl_Obj *plist;

  plist = Tcl_NewListObj(0, NULL);
#endif

  while (NC != NULL) {
#ifdef TCL_NETGEN
    if (check_interrupt()) break;
#endif
    if (NC->legalpartition) {
       if (type != 1) {
	  if (dolist == 0) {
	     Printf("Net class: count = %d; magic = %lX", NC->count, NC->magic);
	     Printf(" -- matching group\n");
	     for (N = NC->nodes; N != NULL; N = N->next) 
	        Printf("   %-20s (circuit %hd) hash = %lX\n", 
		     N->object->name, N->graph, N->hashval);
	  }
#ifdef TCL_NETGEN
	  else {
	     Tcl_Obj *nlist, *npart1, *npart2;
	     nlist = Tcl_NewListObj(0, NULL);
	     npart1 = Tcl_NewListObj(0, NULL);
	     npart2 = Tcl_NewListObj(0, NULL);
	     for (N = NC->nodes; N != NULL; N = N->next)
	        Tcl_ListObjAppendElement(netgeninterp,
				(N->graph == Circuit1->file) ? npart1 : npart2,
				Tcl_NewStringObj(N->object->name, -1));

	     Tcl_ListObjAppendElement(netgeninterp, nlist, npart1);
	     Tcl_ListObjAppendElement(netgeninterp, nlist, npart2);
	     Tcl_ListObjAppendElement(netgeninterp, plist, nlist);
	  }
#endif
       }
    }
    else {
       if (type != 0) {
	  if (dolist == 0) {
	     Printf("Net class: count = %d; magic = %lX", NC->count, NC->magic);
	     Printf(" -- nonmatching group\n");
	     for (N = NC->nodes; N != NULL; N = N->next) 
	        Printf("   %-20s (circuit %hd) hash = %lX\n", 
		     N->object->name, N->graph, N->hashval);
	  }
#ifdef TCL_NETGEN
	  else {
	     Tcl_Obj *nlist, *npart1, *npart2;
	     nlist = Tcl_NewListObj(0, NULL);
	     npart1 = Tcl_NewListObj(0, NULL);
	     npart2 = Tcl_NewListObj(0, NULL);
	     for (N = NC->nodes; N != NULL; N = N->next)
	        Tcl_ListObjAppendElement(netgeninterp,
				(N->graph == Circuit1->file) ? npart1 : npart2,
				Tcl_NewStringObj(N->object->name, -1));

	     Tcl_ListObjAppendElement(netgeninterp, nlist, npart1);
	     Tcl_ListObjAppendElement(netgeninterp, nlist, npart2);
	     Tcl_ListObjAppendElement(netgeninterp, plist, nlist);
	  }
#endif
       }
    }
    NC = NC->next;
  }
#ifdef TCL_NETGEN
  Tcl_SetObjResult(netgeninterp, plist);
#endif
}
#endif  /* TEST */

/* 
 *---------------------------------------------------------------------
 *---------------------------------------------------------------------
 */

void SummarizeNodeClasses(struct NodeClass *NC)
{
  while (NC != NULL) {
#ifdef TCL_NETGEN
    if (check_interrupt()) break;
#endif
    Printf("Net class: count = %d; magic = %lX; hash = %ld", 
	   NC->count, NC->magic, NC->nodes->hashval);
    if (NC->legalpartition) Printf(" -- matching group\n");
    else Printf(" -- nonmatching group\n");
    NC = NC->next;
  }
}

/* 
 *---------------------------------------------------------------------
 *---------------------------------------------------------------------
 */

void SummarizeElementClasses(struct ElementClass *EC)
{
  while (EC != NULL) {
#ifdef TCL_NETGEN
    if (check_interrupt()) break;
#endif
    Printf("Device class: count = %d; magic = %lX; hash = %ld", 
	   EC->count, EC->magic, EC->elements->hashval);
    if (EC->legalpartition) Printf(" -- matching group\n");
    else Printf(" -- nonmatching group\n");
    EC = EC->next;
  }
}

/* 
 *---------------------------------------------------------------------
 *---------------------------------------------------------------------
 */

void SummarizeDataStructures(void)
{
  struct ElementClass *EC;
  struct NodeClass *NC;
  struct Element *E;
  struct Node *N;
  int cell1, cell2, orphan1, orphan2;

  cell1 = cell2 = 0;
  for (EC = ElementClasses; EC != NULL; EC = EC->next)
    for (E = EC->elements; E != NULL; E = E->next) {
      if (E->graph == Circuit1->file) cell1++;
      else cell2++;
    }
  Printf("Circuit 1 contains %d devices, Circuit 2 contains %d devices.",
	 cell1, cell2);
  if (cell1 != cell2) Printf(" *** MISMATCH ***");
  Printf("\n");

  cell1 = cell2 = 0;
  orphan1 = orphan2 = 0;
  for (NC = NodeClasses; NC != NULL; NC = NC->next)
    for (N = NC->nodes; N != NULL; N = N->next) {
      if (N->graph == Circuit1->file) {
	cell1++;
	if (N->elementlist == NULL) orphan1++;
      }
      else {
	cell2++;
	if (N->elementlist == NULL) orphan2++;
      }
    }
  Printf("Circuit 1 contains %d nets,    Circuit 2 contains %d nets.",
	 cell1, cell2);
  if (cell1 != cell2) Printf(" *** MISMATCH ***");
  Printf("\n");
  if (orphan1 || orphan2) {
    Printf("Circuit 1 contains %d orphan nets, Circuit 2 contains %d orphans.");
    if (orphan1 != orphan2) Printf(" *** MISMATCH ***");
    Printf("\n");
  }
  Printf("\n");
}
  
/*
 *-----------------------------------------------------------------------
 * Structures used by FormatBadElementFragment and FormatBadNodeFragment
 *-----------------------------------------------------------------------
 */

struct FanoutList {
   char *model;
   char *name;
   char permute;
   int count;
};

struct FormattedList {
   char *name;
   int fanout;
   struct FanoutList *flist;
};

/* Forward declaration */
void FreeFormattedLists(struct FormattedList **nlists, int numlists);

/* 
 *---------------------------------------------------------------------
 * FormatBadElementFragment
 *
 * formats the numbers of nets that a particular bad element pin
 * fragment contacts
 *---------------------------------------------------------------------
 */

struct FormattedList *FormatBadElementFragment(struct Element *E)
{     
  struct NodeList **nodes;
  int fanout;
  struct NodeList *nl;
  struct objlist *ob;
  int count, i, j, k, m;
  struct FormattedList *elemlist;

  elemlist = (struct FormattedList *)MALLOC(sizeof(struct FormattedList));
  if (elemlist == NULL) {
     Fprintf(stdout, "Unable to allocated memory to print element fanout.\n");
     return NULL;
  }

  fanout = 0;
  for (nl = E->nodelist; nl != NULL; nl = nl->next) fanout++;
  nodes = (struct NodeList **)CALLOC(fanout, sizeof(struct NodeList *));
  if (nodes == NULL) {
    Fprintf(stderr, "Unable to allocate memory to print element fanout.\n");
    FREE(elemlist);
    return NULL;
  }

  elemlist->flist = (struct FanoutList *)CALLOC(fanout, sizeof(struct FanoutList));
  elemlist->fanout = fanout;
  elemlist->name = E->object->instance.name;
  
  fanout = 0;
  for (nl = E->nodelist; nl != NULL; nl = nl->next) 
    nodes[fanout++] = nl;

  ob = E->object;
  k = 0;
  for (i = 0; i < fanout; i++) {
    if (nodes[i] == NULL) {
       ob = ob->next;
       continue;
    }

    count = 1;
    for (j = i+1; j < fanout; j++) {
      /* count the number of pins with the same magic number (permutable pins) */
      if (nodes[j] != NULL && nodes[i]->pin_magic == nodes[j]->pin_magic) {
	count++;
      }
    }
    if (count == 1) {			/* pin is unique */
      struct ElementList *elems;

      count = 0;
      if (nodes[i]->node != NULL) {
         for (elems = nodes[i]->node->elementlist; elems != NULL;
			elems = elems->next)
	    count++;
      }

      elemlist->flist[k].count = count;
      if (*ob->name != *ob->instance.name)	// e.g., "port_match_error"
         elemlist->flist[k].name = ob->name;
      else
         elemlist->flist[k].name = ob->name + strlen(ob->instance.name) + 1;
      elemlist->flist[k].permute = (char)1;
      k++;
    }
    else {				/* handle multiple permutable pins */
      struct objlist *ob2;
      int maxindex, maxcount;
      unsigned long oldmagic;

      ob2 = ob;
      m = k;
      for (j = i; j < fanout; j++) {
	if (nodes[j] != NULL && nodes[i]->pin_magic == nodes[j]->pin_magic) {
	  if (*ob2->name != *ob2->instance.name)  // e.g., "port_match_error"
            elemlist->flist[k].name = ob2->name;
	  else
            elemlist->flist[k].name = ob2->name + strlen(ob2->instance.name) + 1;
          elemlist->flist[k].permute = (char)0;
          elemlist->flist[k].count = -1;	// Put total count at end
	  k++;
	}
	ob2 = ob2->next;
      }
      k = m;

      /* sort fanouts in DECREASING order */
      maxindex = i;
      maxcount = -1;
      oldmagic = nodes[i]->pin_magic; /* allows us to nuke pins[i] */

      while (maxindex >= 0) {
	maxcount = -1;
	maxindex = -1;
	  
	for (j = i; j < fanout; j++) {
	  if (nodes[j] != NULL && oldmagic == nodes[j]->pin_magic) {
	    struct ElementList *elems;

	    count = 0;
	    if (nodes[j]->node == NULL)	/* Under what condition is the node NULL? */
	       count++;
	    else {
	       for (elems = nodes[j]->node->elementlist; elems != NULL;
			elems = elems->next) 
	          count++;
	    }
	    if (count >= maxcount) {
	       maxcount = count;
	       maxindex = j;
	    }
	  }
	}
	if (maxindex >= 0) {
          elemlist->flist[k].count = maxcount;
	  nodes[maxindex] = NULL;	/* So we don't double-count */
	  k++;
	}
      }
      if (k > 0)
         elemlist->flist[k - 1].permute = (char)1;	 /* Denotes end of list */

    }
    nodes[i] = NULL;
    ob = ob->next;		/* point to next name in list */
  }
  elemlist->fanout = k;

  FREE(nodes);
  return elemlist;
}

/* 
 *---------------------------------------------------------------------
 *---------------------------------------------------------------------
 */

void PrintBadElementFragment(struct Element *E)
{     
  struct NodeList **nodes;
  int fanout;
  struct NodeList *nl;
  struct objlist *ob;
  int count, i, j;

  Fprintf(stdout, "  (%d): %s",E->graph, E->object->instance.name);
  Ftab(stdout, 20);

  fanout = 0;
  for (nl = E->nodelist; nl != NULL; nl = nl->next) fanout++;
  nodes = (struct NodeList **)CALLOC(fanout, sizeof(struct NodeList *));
  if (nodes == NULL) {
    Fprintf(stderr, "Unable to allocate memory to print element fanout.\n");
    return;
  }
  
  Ftab(stdout, 20);
  Fprintf(stdout, " ==>  ");

  Fwrap(stdout, 80);		/* set wrap-around */
  fanout = 0;
  for (nl = E->nodelist; nl != NULL; nl = nl->next) 
    nodes[fanout++] = nl;


  ob = E->object;
  for (i = 0; i < fanout; i++) {
    if (nodes[i] == NULL) {
       ob = ob->next;
       continue;
    }

    count = 1;
    for (j = i+1; j < fanout; j++) {
      /* count the number of pins with the same magic number */
      if (nodes[j] != NULL && nodes[i]->pin_magic == nodes[j]->pin_magic)
	count++;
    }
    if (count == 1) {
      struct ElementList *elems;

      count = 0;
      if (nodes[i]->node != NULL)
         for (elems = nodes[i]->node->elementlist; elems != NULL;
			elems = elems->next)
	    count++;
      if (i != 0) Fprintf(stdout, "; ");
      Fprintf(stdout, "%s = %d", ob->name + strlen(ob->instance.name) + 1, count);
    }
    else {
      struct objlist *ob2;
      int maxindex, maxcount, someprinted;
      unsigned long oldmagic;

      if (i != 0) Fprintf(stdout, "; ");
      Fprintf(stdout, "(");
      ob2 = ob;
      for (j = i; j < fanout; j++) {
	if (nodes[j] != NULL && nodes[i]->pin_magic == nodes[j]->pin_magic) {
	  if (i != j) Fprintf(stdout, ", ");
	  Fprintf(stdout, "%s", ob2->name + strlen(ob2->instance.name) + 1);
	}
	ob2 = ob2->next;
      }
      Fprintf(stdout, ") = (");

      /* sort fanouts in DECREASING order */
      someprinted = 0;
      maxindex = i;
      maxcount = -1;
      oldmagic = nodes[i]->pin_magic; /* allows us to nuke pins[i] */

      while (maxindex >= 0) {
	maxcount = -1;
	maxindex = -1;
	  
	for (j = i; j < fanout; j++) {
	  if (nodes[j] != NULL && oldmagic == nodes[j]->pin_magic) {
	    struct ElementList *elems;

	    count = 0;
	    for (elems = nodes[j]->node->elementlist; elems != NULL;
		 elems = elems->next) 
	      count++;
	    if (count >= maxcount) {
	      maxcount = count;
	      maxindex = j;
	    }
	  }
	}
	if (maxindex >= 0) {
	  if (someprinted) Fprintf(stdout, ", ");
	  Fprintf(stdout, "%d", maxcount);
	  someprinted = 1;
	  nodes[maxindex] = NULL;
	}
      }
      Fprintf(stdout, ")");
    }
    nodes[i] = NULL;
    ob = ob->next;		/* point to next name in list */
  }

  Fprintf(stdout, "\n");
  FREE(nodes);
}

/* 
 *---------------------------------------------------------------------
 * FormatBadNodeFragment --
 *
 * formats the numbers and types of pins that a particular bad node
 * fragment contacts;  e.g.   NODE2  5 p:gate  10 n:source
 *---------------------------------------------------------------------
 */

struct FormattedList *FormatBadNodeFragment(struct Node *N)
{
  struct ElementList **pins;
  int fanout;
  struct ElementList *e;
  int i, j, k;
  struct FormattedList *nodelist;

  fanout = 0;
  for (e = N->elementlist; e != NULL; e = e->next) fanout++;
  /* there are at most 'fanout' different types of pins contacted */

  pins = (struct ElementList **)CALLOC(fanout, sizeof(struct ElementList *));
  if (pins == NULL) {
    Fprintf(stdout, "Unable to allocate memory to print node fanout.\n");
    return NULL;
  }

  nodelist = (struct FormattedList *)MALLOC(sizeof(struct FormattedList));
  if (nodelist == NULL) {
    Fprintf(stdout, "Unable to allocate memory to print node fanout.\n");
    FREE(pins);
    return NULL;
  }
  nodelist->flist = (struct FanoutList *)CALLOC(fanout, sizeof(struct FanoutList));
  nodelist->fanout = fanout;
  nodelist->name = (N->object == NULL) ? NULL : N->object->name;

  fanout = 0;
  for (e = N->elementlist; e != NULL; e = e->next) 
    pins[fanout++] = e;

  /* process pins in sequence, NULLing out pins as they are processed */
  k = 0;
  for (i = 0; i < fanout; i++)
    if (pins[i] != NULL) {
      int count;
      char *model, *pinname, permute;
      struct NodeList *n;
      struct objlist *ob;

      count = 1; /* remember: pins[i] contacts it */
      permute = (char)0;
      model = pins[i]->subelement->element->object->model.class;
      /* find the first pin on that element with the same magic number */
      pinname = "can't happen";
      ob = pins[i]->subelement->element->object;
      for (n = pins[i]->subelement->element->nodelist; n != NULL; n = n->next){
	if (n->pin_magic == pins[i]->subelement->pin_magic) {
	  if ((permute == 0) && (ob->instance.name != NULL)) {
	     pinname = ob->name + strlen(ob->instance.name) + 1;
	  }
	  else if (ob->instance.name != NULL) {
	     char *pinsave = pinname;
	     pinname = (char *)MALLOC(strlen(pinsave) + strlen(ob->name +
			strlen(ob->instance.name) + 1) + 2);
	     sprintf(pinname, "%s|%s", pinsave, ob->name + strlen(ob->instance.name) + 1);
	     if (permute > 1) FREE(pinsave);
	  }
	  permute++;
	}
	ob = ob->next;
      }

      /* now see if any other pins from elements of the same class,
         WITH THE SAME HASH NUMBER, are on this node */
      for (j = i+1; j < fanout; j++) {
	if (pins[j] != NULL && 
	    (*matchfunc)(model, pins[j]->subelement->element->object->model.class) &&
	    	    pins[i]->subelement->pin_magic == pins[j]->subelement->pin_magic) {
	      count++;
	      nodelist->fanout--;
	      pins[j] = NULL;
	    }
      }

      nodelist->flist[k].model = model;
      nodelist->flist[k].name = pinname;
      nodelist->flist[k].count = count;
      nodelist->flist[k].permute = permute;
      k++;

      pins[i] = NULL; /* not really necessary */
    }
  FREE(pins);
  return nodelist;
}

/* 
 *---------------------------------------------------------------------
 *---------------------------------------------------------------------
 */

void PrintBadNodeFragment(struct Node *N)
/* prints out the numbers and types of pins that a particular bad node
   fragment contacts;  e.g.   NODE2  5 p:gate  10 n:source
*/
{
  struct ElementList **pins;
  int fanout;
  struct ElementList *e;
  int i, j;

  Fprintf(stdout, "  (%d): %s",N->graph,  (N->object == NULL) ?
		"(unknown)" : N->object->name);
  
  fanout = 0;
  for (e = N->elementlist; e != NULL; e = e->next) fanout++;
  /* there are at most 'fanout' different types of pins contacted */

  pins = (struct ElementList **)CALLOC(fanout, sizeof(struct ElementList *));
  if (pins == NULL) {
    Fprintf(stdout, "Unable to allocate memory to print node fanout.\n");
    return;
  }
  Ftab(stdout, 25);
  Fprintf(stdout, " ==>  ");

  Fwrap(stdout, 80);  /* set wrap-around */
  fanout = 0;
  for (e = N->elementlist; e != NULL; e = e->next) 
    pins[fanout++] = e;


  /* process pins in sequence, NULLing out pins as they are processed */
  for (i = 0; i < fanout; i++)
    if (pins[i] != NULL) {
      int count;
      char *model, *pinname;
      struct NodeList *n;
      struct objlist *ob;

      count = 1; /* remember: pins[i] contacts it */
      model = pins[i]->subelement->element->object->model.class;
      /* find the first pin on that element with the same magic number */
      pinname = "can't happen";
      ob = pins[i]->subelement->element->object;
      for (n = pins[i]->subelement->element->nodelist; n != NULL; n = n->next){
	if (n->pin_magic == pins[i]->subelement->pin_magic) {
	  pinname = ob->name + strlen(ob->instance.name) + 1;
	  break;    /* MAS 3/13/91 */
	}
	ob = ob->next;
      }

      /* now see if any other pins from elements of the same class,
         WITH THE SAME HASH NUMBER, are on this node */
      for (j = i+1; j < fanout; j++) {
	if (pins[j] != NULL && 
	    (*matchfunc)(model, pins[j]->subelement->element->object->model.class) &&
	    	    pins[i]->subelement->pin_magic == pins[j]->subelement->pin_magic) {
	   count++;
	   /* pins[j] = NULL; */ /* Done in diagnostic output, below */
	 }
      }

      if (i != 0) {
	 /* Fprintf(stdout, ";"); */
	 Fprintf(stdout, "\n");
	 Ftab(stdout, 32);
      }
      Fprintf(stdout, " %s:%s = %d", model, pinname, count);

      /* Diagnostic */
      Fprintf(stdout, "\n");
      Ftab(stdout, 25);
      Fprintf(stdout, ">>>  %s", pins[i]->subelement->element->object->instance);
      for (j = i+1; j < fanout; j++) {
	if (pins[j] != NULL && 
	    (*matchfunc)(model, pins[j]->subelement->element->object->model.class) &&
	    	    pins[i]->subelement->pin_magic == pins[j]->subelement->pin_magic) {
	   /* Diagnostic */
           Fprintf(stdout, "\n");
           Ftab(stdout, 25);
           Fprintf(stdout, ">>>  %s", pins[j]->subelement->element->object->instance);
	   pins[j] = NULL;
	}
      }

      pins[i] = NULL; /* not really necessary */
    }
  Fprintf(stdout, "\n");
  Fwrap(stdout, 0);  /* unset wrap-around */
  FREE(pins);
}

#ifdef TCL_NETGEN

/*----------------------------------------------------------------------*/
/* Generate list of illegal element partitions as a nested list.	*/
/* The outermost list is a list of groups.  Inside each group is two	*/
/* lists, one for each circuit.  In each circuit list is a list of	*/
/* devices in the group.  Each device is a list of name and number of	*/
/* elements.  								*/
/*----------------------------------------------------------------------*/

Tcl_Obj *ListElementClasses(int legal)
{
  struct FormattedList **elist1, **elist2;
  struct ElementClass *escan;
  int numlists1, numlists2, n1, n2, n, f1, f2, i, maxf;
  char *estr;

  Tcl_Obj *lobj, *c1obj, *c2obj, *e1obj, *e2obj, *sobj;
  Tcl_Obj *g1obj, *g2obj, *dobj;

  dobj = Tcl_NewListObj(0, NULL);
  for (escan = ElementClasses; escan != NULL; escan = escan->next) {
    if (legal == escan->legalpartition) {
      struct Element *E;

      lobj = Tcl_NewListObj(0, NULL);
      g1obj = Tcl_NewListObj(0, NULL);
      g2obj = Tcl_NewListObj(0, NULL);

      numlists1 = numlists2 = 0;
      for (E = escan->elements; E != NULL; E = E->next)
      {
	 if (E->graph == Circuit1->file)
	    numlists1++;
	 else
	    numlists2++;
      }
      elist1 = (struct FormattedList **)CALLOC(numlists1,
		sizeof(struct FormattedList *));
      elist2 = (struct FormattedList **)CALLOC(numlists2,
		sizeof(struct FormattedList *));

      n1 = n2 = 0;

      for (E = escan->elements; E != NULL; E = E->next) {
	if (E->graph == Circuit1->file) {
	   elist1[n1] = FormatBadElementFragment(E);
	   n1++;
	}
	else {
	   elist2[n2] = FormatBadElementFragment(E);
	   n2++;
	}
      }

      for (n = 0; n < ((n1 > n2) ? n1 : n2); n++) {
         c1obj = Tcl_NewListObj(0, NULL);
         c2obj = Tcl_NewListObj(0, NULL);

         e1obj = Tcl_NewListObj(0, NULL);
         e2obj = Tcl_NewListObj(0, NULL);

	 if (n < n1) {
	    estr = elist1[n]->name;
	    if (*estr == '/') estr++;	// Remove leading slash, if any
	    Tcl_ListObjAppendElement(netgeninterp, c1obj, Tcl_NewStringObj(estr, -1));
	 }
	 else
	    Tcl_ListObjAppendElement(netgeninterp, c1obj,
			Tcl_NewStringObj("(no matching instance)", -1));
	 Tcl_ListObjAppendElement(netgeninterp, c1obj, e1obj);
	 if (n < n2) {
	    estr = elist2[n]->name;
	    if (*estr == '/') estr++;	// Remove leading slash, if any
	    Tcl_ListObjAppendElement(netgeninterp, c2obj, Tcl_NewStringObj(estr, -1));
	 }
	 else
	    Tcl_ListObjAppendElement(netgeninterp, c2obj,
			Tcl_NewStringObj("(no matching instance)", -1));
	 Tcl_ListObjAppendElement(netgeninterp, c2obj, e2obj);

	 if (n >= n1)
	    maxf = elist2[n]->fanout;
	 else if (n >= n2)
	    maxf = elist1[n]->fanout;
	 else
	    maxf = (elist1[n]->fanout > elist2[n]->fanout) ?
			elist1[n]->fanout : elist2[n]->fanout;

	 f1 = f2 = 0;
	 while ((f1 < maxf) || (f2 < maxf)) {
	    if (n < n1) {
	       if (f1 < elist1[n]->fanout) {
		  if (elist1[n]->flist[f1].permute == (char)1) {
		     sobj = Tcl_NewListObj(0, NULL);
		     Tcl_ListObjAppendElement(netgeninterp, sobj,
				Tcl_NewStringObj(elist1[n]->flist[f1].name, -1));
		     Tcl_ListObjAppendElement(netgeninterp, sobj,
				Tcl_NewIntObj(elist1[n]->flist[f1].count));
		  }
		  else {
		     sobj = Tcl_NewListObj(0, NULL);
		     while (1) {
			Tcl_ListObjAppendElement(netgeninterp, sobj,
				Tcl_NewStringObj(elist1[n]->flist[f1].name, -1));
			Tcl_ListObjAppendElement(netgeninterp, sobj,
				Tcl_NewIntObj(elist1[n]->flist[f1].count));
		        if (elist1[n]->flist[f1].permute != (char)0) break;
			f1++;
		     }
		  }
		  Tcl_ListObjAppendElement(netgeninterp, e1obj, sobj);
	       }
	    }
	    f1++;
	    if (n < n2) {
	       if (f2 < elist2[n]->fanout) {
		  if (elist2[n]->flist[f2].permute == (char)1) {
		     sobj = Tcl_NewListObj(0, NULL);
		     Tcl_ListObjAppendElement(netgeninterp, sobj,
				Tcl_NewStringObj(elist2[n]->flist[f2].name, -1));
		     Tcl_ListObjAppendElement(netgeninterp, sobj,
				Tcl_NewIntObj(elist2[n]->flist[f2].count));
		  }
		  else {
		     sobj = Tcl_NewListObj(0, NULL);
		     while (1) {
			Tcl_ListObjAppendElement(netgeninterp, sobj,
				Tcl_NewStringObj(elist2[n]->flist[f2].name, -1));
			Tcl_ListObjAppendElement(netgeninterp, sobj,
				Tcl_NewIntObj(elist2[n]->flist[f2].count));
		        if (elist2[n]->flist[f2].permute != (char)0) break;
			f2++;
		     }
		  }
		  Tcl_ListObjAppendElement(netgeninterp, e2obj, sobj);
	       }
	    }
	    f2++;
	 }
         Tcl_ListObjAppendElement(netgeninterp, g1obj, c1obj);
         Tcl_ListObjAppendElement(netgeninterp, g2obj, c2obj);
      }
      Tcl_ListObjAppendElement(netgeninterp, lobj, g1obj);
      Tcl_ListObjAppendElement(netgeninterp, lobj, g2obj);
      Tcl_ListObjAppendElement(netgeninterp, dobj, lobj);

      FreeFormattedLists(elist1, numlists1);
      FreeFormattedLists(elist2, numlists2);
    }
  }
  return dobj;
}

#endif

/* 
 *---------------------------------------------------------------------
 *
 * Sort the fanout lists of two formatted list entries so that they
 * are as well aligned as they can be made, practically.  The matching
 * is ad hoc as it does not affect LVS results but only how the results
 * are organized and presented in the output.
 *
 *---------------------------------------------------------------------
 */

void
SortFanoutLists(nlist1, nlist2)
    struct FormattedList *nlist1, *nlist2;
{
    struct hashdict f1hash, f2hash;
    int f1, f2, total;
    struct FanoutList temp;
    int *matched;
    char pinname[1024], pinnameA[1024], pinnameB[1024];

    InitializeHashTable(&f1hash, OBJHASHSIZE);
    InitializeHashTable(&f2hash, OBJHASHSIZE);

    if (nlist1->fanout < nlist2->fanout) {
    	matched = (int *)CALLOC(nlist2->fanout, sizeof(int));
	total = 0;

    	for (f2 = 0; f2 < nlist2->fanout; f2++) {
	    sprintf(pinname, "%s/%s", nlist2->flist[f2].model,
			nlist2->flist[f2].name);
	    HashPtrInstall(pinname, (void *)((long)f2 + 1), &f2hash);
	}

	for (f1 = 0; f1 < nlist1->fanout; f1++) {
	    sprintf(pinname, "%s/%s", nlist1->flist[f1].model,
			nlist1->flist[f1].name);
	    f2 = (int)(long)HashLookup(pinname, &f2hash);
	    if (f2 != 0) {
	    	f2 -= 1;
		matched[f1] = -1;
		total++;
	    	if (f2 != f1) {
		    temp = nlist2->flist[f2];
		    nlist2->flist[f2] = nlist2->flist[f1];
		    nlist2->flist[f1] = temp;
	    	    sprintf(pinnameA, "%s/%s", nlist2->flist[f1].model,
				nlist2->flist[f1].name);
	    	    sprintf(pinnameB, "%s/%s", nlist2->flist[f2].model,
				nlist2->flist[f2].name);
		    HashPtrInstall(pinnameA, (void *)((long)f1 + 1), &f2hash);
		    HashPtrInstall(pinnameB, (void *)((long)f2 + 1), &f2hash);
		}
	    }
	}

	/* To do: If full pin names don't match, match by model name only */
    }
    else {
    	matched = (int *)CALLOC(nlist1->fanout, sizeof(int));
	total = 0;

    	for (f1 = 0; f1 < nlist1->fanout; f1++) {
	    sprintf(pinname, "%s/%s", nlist1->flist[f1].model,
			nlist1->flist[f1].name);
	    HashPtrInstall(pinname, (void *)((long)f1 + 1), &f1hash);
	}

	for (f2 = 0; f2 < nlist2->fanout; f2++) {
	    sprintf(pinname, "%s/%s", nlist2->flist[f2].model,
			nlist2->flist[f2].name);
	    f1 = (int)(long)HashLookup(pinname, &f1hash);
	    if (f1 != 0) {
	    	f1 -= 1;
		matched[f2] = -1;
		total++;
	    	if (f1 != f2) {
		    temp = nlist1->flist[f1];
		    nlist1->flist[f1] = nlist1->flist[f2];
		    nlist1->flist[f2] = temp;
	    	    sprintf(pinnameA, "%s/%s", nlist1->flist[f1].model,
				nlist1->flist[f1].name);
	    	    sprintf(pinnameB, "%s/%s", nlist1->flist[f2].model,
				nlist1->flist[f2].name);
		    HashPtrInstall(pinnameA, (void *)((long)f1 + 1), &f1hash);
		    HashPtrInstall(pinnameB, (void *)((long)f2 + 1), &f1hash);
		}
	    }
	}

	/* To do:  If full pin names don't match, match by model name only */
    }

    FREE(matched);
    HashKill(&f1hash);
    HashKill(&f2hash);
}

/* 
 *---------------------------------------------------------------------
 *
 * Determine the match between two entries in a bad node fragment
 * according to an ad hoc metric of how many fanout entries are
 * the same between the two.  This is not used to match circuits
 * for LVS, but is used to sort the dump of unmatched nets generated
 * for an unmatched subcell, so that the end-user is not presented
 * with a list in a confusingly arbitrary order.
 *
 * Score is normalized to 100.
 * If a model/pin has an equivalent on the other size, add 1
 * If a model/pin equivalent has the same count, add 1
 * Total values and normalize to a 100 score for an exact match.
 *
 *---------------------------------------------------------------------
 */

int
NodeMatchScore(nlist1, nlist2)
    struct FormattedList *nlist1, *nlist2;
{
    struct hashdict f1hash, f2hash;
    char pinname[1024];
    int f1, f2, maxfanout;
    int score = 0;

    InitializeHashTable(&f1hash, OBJHASHSIZE);
    InitializeHashTable(&f2hash, OBJHASHSIZE);

    if (nlist1->fanout < nlist2->fanout) {
    	for (f2 = 0; f2 < nlist2->fanout; f2++) {
	    sprintf(pinname, "%s/%s", nlist2->flist[f2].model,
			nlist2->flist[f2].name);
	    HashPtrInstall(pinname, (void *)((long)f2 + 1), &f2hash);
	}

	for (f1 = 0; f1 < nlist1->fanout; f1++) {
	    sprintf(pinname, "%s/%s", nlist1->flist[f1].model,
			nlist1->flist[f1].name);
	    f2 = (int)(long)HashLookup(pinname, &f2hash);
	    if (f2 != 0) {
	    	f2 -= 1;
		score++;
		if (nlist1->flist[f1].count == nlist2->flist[f2].count)
		    score++;
	    }
	}
    }
    else {
    	for (f1 = 0; f1 < nlist1->fanout; f1++) {
	    sprintf(pinname, "%s/%s", nlist1->flist[f1].model,
			nlist1->flist[f1].name);
	    HashPtrInstall(pinname, (void *)((long)f1 + 1), &f1hash);
	}

	for (f2 = 0; f2 < nlist2->fanout; f2++) {
	    sprintf(pinname, "%s/%s", nlist2->flist[f2].model,
			nlist2->flist[f2].name);
	    f1 = (int)(long)HashLookup(pinname, &f1hash);
	    if (f1 != 0) {
	    	f1 -= 1;
		score++;
		if (nlist2->flist[f2].count == nlist1->flist[f1].count)
		    score++;
	    }
	}
    }

    HashKill(&f1hash);
    HashKill(&f2hash);

    maxfanout = (nlist1->fanout < nlist2->fanout) ? nlist2->fanout : nlist1->fanout;
    score = (50 * score) / maxfanout;

    return score;
}

/* 
 *---------------------------------------------------------------------
 *
 * Sort node list 2 to match the entries in list 1, to the extent
 * possible.  Exact name matching is preferred, followed by matching
 * of the largest percentage of components.  
 *
 *---------------------------------------------------------------------
 */

void SortUnmatchedLists(nlists1, nlists2, n1max, n2max)
    struct FormattedList **nlists1, **nlists2;
    int n1max, n2max;
{
    struct FormattedList *temp;
    int n1, n2;
    int *matched, total, best, ibest;

    struct hashdict n1hash, n2hash;

    InitializeHashTable(&n1hash, OBJHASHSIZE);
    InitializeHashTable(&n2hash, OBJHASHSIZE);

    if (n1max < n2max) {
    	matched = (int *)CALLOC(n2max, sizeof(int));
	total = 0;

    	for (n2 = 0; n2 < n2max; n2++)
	    HashPtrInstall(nlists2[n2]->name, (void *)((long)n2 + 1), &n2hash);

	/* Match by name */
    	for (n1 = 0; n1 < n1max; n1++) {
	    n2 = (int)(long)HashLookup(nlists1[n1]->name, &n2hash);
	    if (n2 != 0) {
	    	n2 -= 1;
		matched[n1] = -1;
		total++;
	    	if (n2 != n1) {
		    temp = nlists2[n2];
		    nlists2[n2] = nlists2[n1];
		    nlists2[n1] = temp;
		    HashPtrInstall(nlists2[n1]->name, (void *)((long)n1 + 1), &n2hash);
		    HashPtrInstall(nlists2[n2]->name, (void *)((long)n2 + 1), &n2hash);
		}
		SortFanoutLists(nlists1[n1], nlists2[n1]);
	    }
	}

	/* For all nets that didn't match by name, match by content */
#if 0
	/* This is ifdef'd out because the improvement in the presentation
	 * of the output is minimal, but the amount of computation is huge.
	 * There are numerous ways to optimize this.
	 */
	if (total < n1max) {
    	    for (n1 = 0; n1 < n1max; n1++) {
		if (matched[n1] != -1) {
		    best = 0;
		    ibest = -1;
		    for (n2 = 0; n2 < n2max; n2++) {
			if (matched[n2] != -1) {
			    matched[n2] = NodeMatchScore(nlists1[n1], nlists2[n2]);
			    if (matched[n2] > best) {
				best = matched[n2];
				ibest = n2;
			    }
		 	}
		    }
		    if (ibest >= 0) {
		       matched[n1] = -1;
		       temp = nlists2[ibest];
		       nlists2[ibest] = nlists2[n1];
		       nlists2[n1] = temp;
		       SortFanoutLists(nlists1[n1], nlists2[n1]);
		    }
		}
	    }
	}
#endif
    }
    else {
    	matched = (int *)CALLOC(n1max, sizeof(int));
	total = 0;

        for (n1 = 0; n1 < n1max; n1++)
	    HashPtrInstall(nlists1[n1]->name, (void *)((long)n1 + 1), &n1hash);

        for (n2 = 0; n2 < n2max; n2++) {
	    n1 = (int)(long)HashLookup(nlists2[n2]->name, &n1hash);
	    if (n1 != 0) {
	    	n1 -= 1;
		matched[n2] = -1;
		total++;
	    	if (n1 != n2) {
		    temp = nlists1[n1];
		    nlists1[n1] = nlists1[n2];
		    nlists1[n2] = temp;
		    HashPtrInstall(nlists1[n1]->name, (void *)((long)n1 + 1), &n1hash);
		    HashPtrInstall(nlists1[n2]->name, (void *)((long)n2 + 1), &n1hash);
		}
		SortFanoutLists(nlists2[n2], nlists1[n2]);
	    }
	    else if ((n1max == 1) && (n2max == 1)) {
		/* Names didn't match but there's only one entry on each side,	*/
		/* so do a sort anyway.						*/
		SortFanoutLists(nlists2[n2], nlists1[n2]);
	    }
	}
	/* For all nets that didn't match by name, match by content */
#if 0
	/* This is ifdef'd out because the improvement in the presentation
	 * of the output is minimal, but the amount of computation is huge.
	 * There are numerous ways to optimize this.
	 */
	if (total < n2max) {
    	    for (n2 = 0; n2 < n2max; n2++) {
		if (matched[n2] != -1) {
		    best = 0;
		    ibest = -1;
		    for (n1 = 0; n1 < n1max; n1++) {
			if (matched[n1] != -1) {
			    matched[n1] = NodeMatchScore(nlists2[n2], nlists1[n1]);
			    if (matched[n1] > best) {
				best = matched[n1];
				ibest = n1;
			    }
		 	}
		    }
		    if (ibest >= 0) {
		       matched[n2] = -1;
		       temp = nlists1[ibest];
		       nlists1[ibest] = nlists1[n2];
		       nlists1[n2] = temp;
		       SortFanoutLists(nlists2[n2], nlists1[n2]);
		    }
		}
	    }
	}
#endif
    }

    FREE(matched);
    HashKill(&n1hash);
    HashKill(&n2hash);
}

/* 
 *---------------------------------------------------------------------
 *---------------------------------------------------------------------
 */

void FormatIllegalElementClasses()
{
  struct FormattedList **elist1, **elist2;
  struct ElementClass *escan;
  int found, numlists1, numlists2, n1, n2, n, f1, f2, i, maxf;
  char *ostr;
  char *estr;
  char *permname;
  char *permcount;
  int bytesleft;

  ostr = CALLOC(right_col_end + 2, sizeof(char));
  permname = CALLOC(right_col_end + 2, sizeof(char));
  permcount = CALLOC(right_col_end + 2, sizeof(char));

  found = 0;
  for (escan = ElementClasses; escan != NULL; escan = escan->next)
    if (!(escan->legalpartition)) {
      struct Element *E;

      if (!found) {
	Fprintf(stdout, "DEVICE mismatches: ");
	Fprintf(stdout, "Class fragments follow (with node fanout counts):\n");

	/* Print in side-by-side format */

	*(ostr + left_col_end) = '|';
	*(ostr + right_col_end) = '\n';
	*(ostr + right_col_end + 1) = '\0';
	for (i = 0; i < left_col_end; i++) *(ostr + i) = ' ';
	for (i = left_col_end + 1; i < right_col_end; i++) *(ostr + i) = ' ';
	snprintf(ostr, left_col_end, "Circuit 1: %s", Circuit1->name);
	snprintf(ostr + left_col_end + 1, left_col_end, "Circuit 2: %s", Circuit2->name);
	for (i = 0; i < right_col_end + 1; i++) if (*(ostr + i) == '\0') *(ostr + i) = ' ';
	Fprintf(stdout, ostr);
      }
      found = 1;

      numlists1 = numlists2 = 0;
      for (E = escan->elements; E != NULL; E = E->next)
      {
	 if (E->graph == Circuit1->file)
	    numlists1++;
	 else
	    numlists2++;
      }
      elist1 = (struct FormattedList **)CALLOC(numlists1,
		sizeof(struct FormattedList *));
      elist2 = (struct FormattedList **)CALLOC(numlists2,
		sizeof(struct FormattedList *));

      n1 = n2 = 0;

      for (E = escan->elements; E != NULL; E = E->next) {
#ifdef TCL_NETGEN
        if (check_interrupt()) {
	   FreeFormattedLists(elist1, n1);
	   FreeFormattedLists(elist2, n2);
	   FREE(ostr);
	   FREE(permname);
	   FREE(permcount);
	   return;
	} 
#endif
	if (E->graph == Circuit1->file) {
	   elist1[n1] = FormatBadElementFragment(E);
	   n1++;
	}
	else {
	   elist2[n2] = FormatBadElementFragment(E);
	   n2++;
	}
      }
      SortUnmatchedLists(elist1, elist2, n1, n2);

      Fprintf(stdout, "\n");
      for (n = 0; n < ((n1 > n2) ? n1 : n2); n++) {
	 if (n != 0) {
	    for (i = 0; i < left_col_end; i++) *(ostr + i) = ' ';
	    for (i = left_col_end + 1; i < right_col_end; i++) *(ostr + i) = ' ';
	    Fprintf(stdout, ostr);
	 } else {
	    for (i = 0; i < right_col_end; i++) *(ostr + i) = '-';
	    Fprintf(stdout, ostr);
	    *(ostr + left_col_end) = '|';
	 }
	 for (i = 0; i < left_col_end; i++) *(ostr + i) = ' ';
	 for (i = left_col_end + 1; i < right_col_end; i++) *(ostr + i) = ' ';
	 if (n < n1) {
	    estr = elist1[n]->name;
	    if (*estr == '/') estr++;	// Remove leading slash, if any
	    snprintf(ostr, left_col_end, "Instance: %s", estr);
	 }
	 else
	    snprintf(ostr, left_col_end, "(no matching instance)");
	 if (n < n2) {
	    estr = elist2[n]->name;
	    if (*estr == '/') estr++;	// Remove leading slash, if any
	    snprintf(ostr + left_col_end + 1, left_col_end, "Instance: %s", estr);
	 }
	 else
	    snprintf(ostr + left_col_end + 1, left_col_end, "(no matching instance)");
	 for (i = 0; i < right_col_end + 1; i++) if (*(ostr + i) == '\0') *(ostr + i) = ' ';
	 Fprintf(stdout, ostr);

	 if (n >= n1)
	    maxf = elist2[n]->fanout;
	 else if (n >= n2)
	    maxf = elist1[n]->fanout;
	 else
	    maxf = (elist1[n]->fanout > elist2[n]->fanout) ?
			elist1[n]->fanout : elist2[n]->fanout;

	 f1 = f2 = 0;
	 while ((f1 < maxf) || (f2 < maxf)) {
	    for (i = 0; i < left_col_end; i++) *(ostr + i) = ' ';
	    for (i = left_col_end + 1; i < right_col_end; i++) *(ostr + i) = ' ';
	    if (n < n1) {
	       if (f1 < elist1[n]->fanout) {
		  if (elist1[n]->flist[f1].permute == (char)1) {
		     snprintf(ostr, left_col_end, "  %s = %d", elist1[n]->flist[f1].name,
				elist1[n]->flist[f1].count);
		  }
		  else {
		     bytesleft = 76;
		     char value[10];
		     sprintf(permname, "(");
		     sprintf(permcount, "(");
		     while (elist1[n]->flist[f1].permute == (char)0) {
			strncat(permname, elist1[n]->flist[f1].name, bytesleft);
			bytesleft -= strlen(elist1[n]->flist[f1].name);
			strcat(permname, ",");
			sprintf(value, "%d", elist1[n]->flist[f1].count);
			strcat(permcount, value);
			strcat(permcount, ",");
			f1++;
		     }
		     strncat(permname, elist1[n]->flist[f1].name, bytesleft);
		     strcat(permname, ")");
		     sprintf(value, "%d", elist1[n]->flist[f1].count);
		     strcat(permcount, value);
		     strcat(permcount, ")");
		     snprintf(ostr, left_col_end, "  %s = %s", permname, permcount);
		  }
	       }
	    }
	    f1++;
	    if (n < n2) {
	       if (f2 < elist2[n]->fanout) {
		  if (elist2[n]->flist[f2].permute == (char)1) {
		     snprintf(ostr + left_col_end + 1, left_col_end, "  %s = %d", elist2[n]->flist[f2].name,
				elist2[n]->flist[f2].count);
		  }
		  else {
		     bytesleft = 76;
		     char value[10];
		     sprintf(permname, "(");
		     sprintf(permcount, "(");
		     while (elist2[n]->flist[f2].permute == (char)0) {
			strncat(permname, elist2[n]->flist[f2].name, bytesleft);
			bytesleft -= strlen(elist2[n]->flist[f2].name);
			strcat(permname, ",");
			sprintf(value, "%d", elist2[n]->flist[f2].count);
			strcat(permcount, value);
			strcat(permcount, ",");
			f2++;
		     }
		     strncat(permname, elist2[n]->flist[f2].name, bytesleft);
		     strcat(permname, ")");
		     sprintf(value, "%d", elist2[n]->flist[f2].count);
		     strcat(permcount, value);
		     strcat(permcount, ")");
		     snprintf(ostr + left_col_end + 1, left_col_end, "  %s = %s", permname, permcount);
		  }
	       }
	    }
	    f2++;
	    for (i = 0; i < right_col_end + 1; i++) if (*(ostr + i) == '\0') *(ostr + i) = ' ';
	    Fprintf(stdout, ostr);
	 }
      }

      FreeFormattedLists(elist1, numlists1);
      FreeFormattedLists(elist2, numlists2);
      for (i = 0; i < right_col_end; i++) *(ostr + i) = '-';
      Fprintf(stdout, ostr);
      *(ostr + left_col_end) = '|';
    }

    FREE(ostr);
    FREE(permname);
    FREE(permcount);
}

/* 
 *---------------------------------------------------------------------
 *---------------------------------------------------------------------
 */

void PrintIllegalElementClasses(void)
{
  struct ElementClass *escan;
  int found;

  found = 0;
  for (escan = ElementClasses; escan != NULL; escan = escan->next)
    if (!(escan->legalpartition)) {
      struct Element *E;

      if (!found) {
	Fprintf(stdout, "DEVICE mismatches: ");
	Fprintf(stdout, "Class fragments follow (with node fanout counts):\n");
      }
      found = 1;
      for (E = escan->elements; E != NULL; E = E->next)
      {
#ifdef TCL_NETGEN
        if (check_interrupt()) return;
#endif
	PrintBadElementFragment(E);
      }
      Fprintf(stdout, "---------------------------\n");
    }
}

/* 
 *---------------------------------------------------------------------
 *---------------------------------------------------------------------
 */

void FreeFormattedLists(struct FormattedList **nlists, int numlists)
{
   int n;
   for (n = 0; n < numlists; n++) {
      FREE(nlists[n]->flist);
      FREE(nlists[n]);
   }
   FREE(nlists);
}

#ifdef TCL_NETGEN

Tcl_Obj *ListNodeClasses(int legal)
{
  struct FormattedList **nlists1, **nlists2;
  struct NodeClass *nscan;
  int numlists1, numlists2, n1, n2, n, f, i, maxf;

  Tcl_Obj *lobj, *c1obj, *c2obj, *n1obj, *n2obj, *sobj;
  Tcl_Obj *dobj, *g1obj, *g2obj;

  dobj = Tcl_NewListObj(0, NULL);
  for (nscan = NodeClasses; nscan != NULL; nscan = nscan->next) {
    if (legal == nscan->legalpartition) {
      struct Node *N;

      lobj = Tcl_NewListObj(0, NULL);
      g1obj = Tcl_NewListObj(0, NULL);
      g2obj = Tcl_NewListObj(0, NULL);

      numlists1 = numlists2 = 0;
      for (N = nscan->nodes; N != NULL; N = N->next) {
	 if (N->graph == Circuit1->file)
	    numlists1++;
	 else
	    numlists2++;
      }
      nlists1 = (struct FormattedList **)CALLOC(numlists1,
		sizeof(struct FormattedNodeList *));
      nlists2 = (struct FormattedList **)CALLOC(numlists2,
		sizeof(struct FormattedList *));

      n1 = n2 = 0; 
      for (N = nscan->nodes; N != NULL; N = N->next) {
	if (N->graph == Circuit1->file) {
	   nlists1[n1] = FormatBadNodeFragment(N);
	   n1++;
	}
	else {
	   nlists2[n2] = FormatBadNodeFragment(N);
	   n2++;
	}
      }

      for (n = 0; n < ((n1 > n2) ? n1 : n2); n++) {
         c1obj = Tcl_NewListObj(0, NULL);
         c2obj = Tcl_NewListObj(0, NULL);

         n1obj = Tcl_NewListObj(0, NULL);
         n2obj = Tcl_NewListObj(0, NULL);

	 if (n < n1)
	    Tcl_ListObjAppendElement(netgeninterp, c1obj,
			Tcl_NewStringObj(nlists1[n]->name, -1));
	 else
	    Tcl_ListObjAppendElement(netgeninterp, c1obj,
			Tcl_NewStringObj("(no matching net)", -1));
	 Tcl_ListObjAppendElement(netgeninterp, c1obj, n1obj);

	 if (n < n2)
	    Tcl_ListObjAppendElement(netgeninterp, c2obj,
			Tcl_NewStringObj(nlists2[n]->name, -1));
	 else
	    Tcl_ListObjAppendElement(netgeninterp, c2obj,
			Tcl_NewStringObj("(no matching net)", -1));
	 Tcl_ListObjAppendElement(netgeninterp, c2obj, n2obj);

	 if (n >= n1)
	    maxf = nlists2[n]->fanout;
	 else if (n >= n2)
	    maxf = nlists1[n]->fanout;
	 else
	    maxf = (nlists1[n]->fanout > nlists2[n]->fanout) ?
			nlists1[n]->fanout : nlists2[n]->fanout;

	 for (f = 0; f < maxf; f++) {
	    if (n < n1)
	       if (f < nlists1[n]->fanout) {
		  sobj = Tcl_NewListObj(0, NULL);
		  Tcl_ListObjAppendElement(netgeninterp, sobj,
			Tcl_NewStringObj(nlists1[n]->flist[f].model, -1));
		  Tcl_ListObjAppendElement(netgeninterp, sobj,
			Tcl_NewStringObj(nlists1[n]->flist[f].name, -1));
		  Tcl_ListObjAppendElement(netgeninterp, sobj,
			Tcl_NewIntObj(nlists1[n]->flist[f].count));

		  if (nlists1[n]->flist[f].permute > 1)
		     FREE(nlists1[n]->flist[f].name);

		  Tcl_ListObjAppendElement(netgeninterp, n1obj, sobj);
	       }
	    if (n < n2)
	       if (f < nlists2[n]->fanout) {
		  sobj = Tcl_NewListObj(0, NULL);
		  Tcl_ListObjAppendElement(netgeninterp, sobj,
			Tcl_NewStringObj(nlists2[n]->flist[f].model, -1));
		  Tcl_ListObjAppendElement(netgeninterp, sobj,
			Tcl_NewStringObj(nlists2[n]->flist[f].name, -1));
		  Tcl_ListObjAppendElement(netgeninterp, sobj,
			Tcl_NewIntObj(nlists2[n]->flist[f].count));

		  if (nlists2[n]->flist[f].permute > 1)
		     FREE(nlists2[n]->flist[f].name);
		  Tcl_ListObjAppendElement(netgeninterp, n2obj, sobj);
	       }
	 }
         Tcl_ListObjAppendElement(netgeninterp, g1obj, c1obj);
         Tcl_ListObjAppendElement(netgeninterp, g2obj, c2obj);
      }
      Tcl_ListObjAppendElement(netgeninterp, lobj, g1obj);
      Tcl_ListObjAppendElement(netgeninterp, lobj, g2obj);
      Tcl_ListObjAppendElement(netgeninterp, dobj, lobj);

      FreeFormattedLists(nlists1, numlists1);
      FreeFormattedLists(nlists2, numlists2);
    }
  }
  return dobj;
}

#endif

/* 
 *---------------------------------------------------------------------
 *---------------------------------------------------------------------
 */

void FormatIllegalNodeClasses()
{
  struct FormattedList **nlists1, **nlists2;
  struct NodeClass *nscan;
  int found, numlists1, numlists2, n1, n2, n, f, i, maxf;
  char *ostr;

  ostr = CALLOC(right_col_end + 2, sizeof(char));
  found = 0;

  /* 
   * To do: match net names across partitions, to make it much clearer how
   * two nets are mismatched, when they have been dropped into different
   * partitions.
   */

  for (nscan = NodeClasses; nscan != NULL; nscan = nscan->next)
    if (!(nscan->legalpartition)) {
      struct Node *N;

      if (!found) {
	Fprintf(stdout, "NET mismatches: ");
	Fprintf(stdout, "Class fragments follow (with fanout counts):\n");

	/* Print in side-by-side format */
	*(ostr + left_col_end) =  '|';
	*(ostr + right_col_end) = '\n';
	*(ostr + right_col_end + 1) = '\0';
	for (i = 0; i < left_col_end; i++) *(ostr + i) = ' ';
	for (i = left_col_end + 1; i < right_col_end; i++) *(ostr + i) = ' ';
	snprintf(ostr, left_col_end, "Circuit 1: %s", Circuit1->name);
	snprintf(ostr + left_col_end + 1, left_col_end, "Circuit 2: %s", Circuit2->name);
	for (i = 0; i < right_col_end + 1; i++) if (*(ostr + i) == '\0') *(ostr + i) = ' ';
	Fprintf(stdout, ostr);
      }
      found = 1;

      numlists1 = numlists2 = 0;
      for (N = nscan->nodes; N != NULL; N = N->next) {
	 if (N->graph == Circuit1->file)
	    numlists1++;
	 else
	    numlists2++;
      }
      nlists1 = (struct FormattedList **)CALLOC(numlists1,
		sizeof(struct FormattedNodeList *));
      nlists2 = (struct FormattedList **)CALLOC(numlists2,
		sizeof(struct FormattedList *));

      n1 = n2 = 0; 
      for (N = nscan->nodes; N != NULL; N = N->next) {
#ifdef TCL_NETGEN
        if (check_interrupt()) {
	   FreeFormattedLists(nlists1, n1);
	   FreeFormattedLists(nlists2, n2);
	   FREE(ostr);
	   return;
	}
#endif
	if (N->graph == Circuit1->file) {
	   nlists1[n1] = FormatBadNodeFragment(N);
	   n1++;
	}
	else {
	   nlists2[n2] = FormatBadNodeFragment(N);
	   n2++;
	}
      }
      SortUnmatchedLists(nlists1, nlists2, n1, n2);

      Fprintf(stdout, "\n");
      for (n = 0; n < ((n1 > n2) ? n1 : n2); n++) {
	 if (n != 0) {
            for (i = 0; i < left_col_end; i++) *(ostr + i) = ' ';
            for (i = left_col_end + 1; i < right_col_end; i++) *(ostr + i) = ' ';
	    Fprintf(stdout, ostr);
	 } else {
            for (i = 0; i < right_col_end; i++) *(ostr + i) = '-';
	    Fprintf(stdout, ostr);
	    *(ostr + left_col_end) = '|';
	 }
         for (i = 0; i < left_col_end; i++) *(ostr + i) = ' ';
         for (i = left_col_end + 1; i < right_col_end; i++) *(ostr + i) = ' ';
	 if (n < n1)
	    snprintf(ostr, left_col_end, "Net: %s", nlists1[n]->name);
	 else
	    snprintf(ostr, left_col_end, "(no matching net)");
	 if (n < n2)
	    snprintf(ostr + left_col_end + 1, left_col_end, "Net: %s", nlists2[n]->name);
	 else
	    snprintf(ostr + left_col_end + 1, left_col_end, "(no matching net)");
         for (i = 0; i < right_col_end + 1; i++) if (*(ostr + i) == '\0') *(ostr + i) = ' ';
         Fprintf(stdout, ostr);

	 if (n >= n1)
	    maxf = nlists2[n]->fanout;
	 else if (n >= n2)
	    maxf = nlists1[n]->fanout;
	 else
	    maxf = (nlists1[n]->fanout > nlists2[n]->fanout) ?
			nlists1[n]->fanout : nlists2[n]->fanout;

	 for (f = 0; f < maxf; f++) {
            for (i = 0; i < left_col_end; i++) *(ostr + i) = ' ';
            for (i = left_col_end + 1; i < right_col_end; i++) *(ostr + i) = ' ';
	    if (n < n1)
	       if (f < nlists1[n]->fanout) {
		  if (nlists1[n]->flist[f].permute <= 1)
	             snprintf(ostr, left_col_end, "  %s/%s = %d",
				nlists1[n]->flist[f].model,
				nlists1[n]->flist[f].name,
				nlists1[n]->flist[f].count);
		  else {
	             snprintf(ostr, left_col_end, "  %s/(%s) = %d",
				nlists1[n]->flist[f].model,
				nlists1[n]->flist[f].name,
				nlists1[n]->flist[f].count);
		     FREE(nlists1[n]->flist[f].name);
		  }
	       }
	    if (n < n2)
	       if (f < nlists2[n]->fanout) {
		  if (nlists2[n]->flist[f].permute <= 1)
	             snprintf(ostr + left_col_end + 1, left_col_end, "  %s/%s = %d",
				nlists2[n]->flist[f].model,
				nlists2[n]->flist[f].name,
				nlists2[n]->flist[f].count);
		  else {
	             snprintf(ostr + left_col_end + 1, left_col_end, "  %s/(%s) = %d",
				nlists2[n]->flist[f].model,
				nlists2[n]->flist[f].name,
				nlists2[n]->flist[f].count);
		     FREE(nlists2[n]->flist[f].name);
		  }
	       }
            for (i = 0; i < right_col_end + 1; i++) if (*(ostr + i) == '\0') *(ostr + i) = ' ';
            Fprintf(stdout, ostr);
	 }
      }

      FreeFormattedLists(nlists1, numlists1);
      FreeFormattedLists(nlists2, numlists2);
      for (i = 0; i < right_col_end; i++) *(ostr + i) = '-';
      Fprintf(stdout, ostr);
      *(ostr + left_col_end) =  '|';
    }
    FREE(ostr);
}

/* 
 *---------------------------------------------------------------------
 *---------------------------------------------------------------------
 */

void PrintIllegalNodeClasses(void)
{
  struct NodeClass *nscan;
  int found;

  found = 0;
  for (nscan = NodeClasses; nscan != NULL; nscan = nscan->next)
    if (!(nscan->legalpartition)) {
      struct Node *N;

      if (!found) {
	Fprintf(stdout, "\n");
	Fprintf(stdout, "NET mismatches: ");
	Fprintf(stdout, "Class fragments follow (with fanouts):\n");
      }
      found = 1;
      for (N = nscan->nodes; N != NULL; N = N->next) {
#ifdef TCL_NETGEN
        if (check_interrupt()) return;
#endif
	PrintBadNodeFragment(N);
      }
      Fprintf(stdout, "---------------------------\n");
    }
}

/* 
 *---------------------------------------------------------------------
 *---------------------------------------------------------------------
 */

void PrintIllegalClasses(void)
{
  PrintIllegalElementClasses();
  PrintIllegalNodeClasses();
}

/**************************** Free lists ***************************/

#ifdef DEBUG_ALLOC
int ElementAllocated;
int NodeAllocated;
int NodeClassAllocated;
int ElementClassAllocated;
int ElementListAllocated;
int NodeListAllocated;
#endif

struct Element *GetElement(void)
{
	struct Element *new_element;	
	if (ElementFreeList != NULL) {
		new_element = ElementFreeList;
		ElementFreeList = ElementFreeList->next;
		memzero(new_element, sizeof(struct Element));
	}
	else {
	  new_element = (struct Element *)CALLOC(1,sizeof(struct Element));
#ifdef DEBUG_ALLOC
	  ElementAllocated++;
#endif
	}
	return(new_element);
}

void FreeElement(struct Element *old)
{
	old->next = ElementFreeList;
	ElementFreeList = old;
}

struct Node *GetNode(void)
{
	struct Node *new_node;	
	if (NodeFreeList != NULL) {
		new_node = NodeFreeList;
		NodeFreeList = NodeFreeList->next;
		memzero(new_node, sizeof(struct Node));
	}
	else {
	  new_node = (struct Node *)CALLOC(1,sizeof(struct Node));
#ifdef DEBUG_ALLOC
	  NodeAllocated++;
#endif
	}
	return(new_node);
}

void FreeNode(struct Node *old)
{
	old->next = NodeFreeList;
	NodeFreeList = old;
}

struct ElementClass *GetElementClass(void)
{
	struct ElementClass *new_elementclass;	
	if (ElementClassFreeList != NULL) {
		new_elementclass = ElementClassFreeList;
		ElementClassFreeList = ElementClassFreeList->next;
		memzero(new_elementclass, sizeof(struct ElementClass));
	}
	else {
	  new_elementclass =
	    (struct ElementClass *)CALLOC(1,sizeof(struct ElementClass));
#ifdef DEBUG_ALLOC
	  ElementClassAllocated++;
#endif
	}
	new_elementclass->legalpartition = 1;
	return(new_elementclass);
}

void FreeElementClass(struct ElementClass *old)
{
	old->next = ElementClassFreeList;
	ElementClassFreeList = old;
}

struct NodeClass *GetNodeClass(void)
{
	struct NodeClass *new_nodeclass;	
	if (NodeClassFreeList != NULL) {
		new_nodeclass = NodeClassFreeList;
		NodeClassFreeList = NodeClassFreeList->next;
		memzero(new_nodeclass, sizeof(struct NodeClass));
	}
	else {
	  new_nodeclass =
	    (struct NodeClass *)CALLOC(1,sizeof(struct NodeClass));
#ifdef DEBUG_ALLOC
	  NodeClassAllocated++;
#endif
	}
	new_nodeclass->legalpartition = 1;
	return(new_nodeclass);
}

void FreeNodeClass(struct NodeClass *old)
{
	old->next = NodeClassFreeList;
	NodeClassFreeList = old;
}

struct ElementList *GetElementList(void)
{
	struct ElementList *new_elementlist;	
	if (ElementListFreeList != NULL) {
		new_elementlist = ElementListFreeList;
		ElementListFreeList = ElementListFreeList->next;
		memzero(new_elementlist, sizeof(struct ElementList));
	}
	else {
	  new_elementlist =
	    (struct ElementList *)CALLOC(1,sizeof(struct ElementList));
#ifdef DEBUG_ALLOC
	  ElementListAllocated++;
#endif
	}
	return(new_elementlist);
}

void FreeElementList(struct ElementList *old)
{
	old->next = ElementListFreeList;
	ElementListFreeList = old;
}

struct NodeList *GetNodeList(void)
{
	struct NodeList *new_nodelist;	
	if (NodeListFreeList != NULL) {
		new_nodelist = NodeListFreeList;
		NodeListFreeList = NodeListFreeList->next;
		memzero(new_nodelist, sizeof(struct NodeList));
	}
	else {
	  new_nodelist = (struct NodeList *)CALLOC(1,sizeof(struct NodeList));
#ifdef DEBUG_ALLOC
	  NodeListAllocated++;
#endif
	}
	return(new_nodelist);
}

void FreeNodeList(struct NodeList *old)
{
	old->next = NodeListFreeList;
	NodeListFreeList = old;
}

#ifdef DEBUG_ALLOC
void PrintCoreStats(void)
{
  Fprintf(stdout, "DeviceClass records allocated = %d, size = %d\n",
	  ElementClassAllocated, sizeof(struct ElementClass));
  Fprintf(stdout, "Device records allocated = %d, size = %d\n",
	  ElementAllocated, sizeof(struct Element));
  Fprintf(stdout, "NetList records allocated = %d, size = %d\n",
	  NodeListAllocated, sizeof(struct NodeList));
  Fprintf(stdout, "NetClass records allocated = %d, size = %d\n",
	  NodeClassAllocated, sizeof(struct NodeClass));
  Fprintf(stdout, "Net records allocated = %d, size = %d\n",
	  NodeAllocated, sizeof(struct Node));
  Fprintf(stdout, "DeviceList records allocated = %d, size = %d\n",
	  ElementListAllocated, sizeof(struct ElementList));
  Fprintf(stdout, "Total accounted-for memory: %d\n",
	  ElementClassAllocated * sizeof(struct ElementClass) +
	  ElementAllocated * sizeof(struct Element) +
	  NodeListAllocated * sizeof(struct NodeList) +
	  NodeClassAllocated * sizeof(struct NodeClass) +
	  NodeAllocated * sizeof(struct Node) +
	  ElementListAllocated * sizeof(struct ElementList));
}
#endif


static int OldNumberOfEclasses;
static int OldNumberOfNclasses;
static int NewNumberOfEclasses;
static int NewNumberOfNclasses;

static int Iterations;

void FreeEntireElementClass(struct ElementClass *ElementClasses)
{
  struct ElementClass *next;
  struct Element *E, *Enext;
  struct NodeList *n, *nnext;

  while (ElementClasses != NULL) {
    next = ElementClasses->next;
    E = ElementClasses->elements;
    while (E != NULL) {
      Enext = E->next;
      n = E->nodelist;
      while (n != NULL) {
	nnext = n->next;
	FreeNodeList(n);
	n = nnext;
      }
      FreeElement(E);
      E = Enext;
    }
    FreeElementClass(ElementClasses);
    ElementClasses = next;
  }
}


void FreeEntireNodeClass(struct NodeClass *NodeClasses)
{
  struct NodeClass *next;
  struct Node *N, *Nnext;
  struct ElementList *e, *enext;

  while (NodeClasses != NULL) {
    next = NodeClasses->next;
    N = NodeClasses->nodes;
    while (N != NULL) {
      Nnext = N->next;
      e = N->elementlist;
      while (e != NULL) {
	enext = e->next;
	FreeElementList(e);
	e = enext;
      }
      FreeNode(N);
      N = Nnext;
    }
    FreeNodeClass(NodeClasses);
    NodeClasses = next;
  }
}

int BadMatchDetected;
int PropertyErrorDetected;
int NewFracturesMade;


void ResetState(void)
{
  if (NodeClasses != NULL)
    FreeEntireNodeClass(NodeClasses);
  if (ElementClasses != NULL)
    FreeEntireElementClass(ElementClasses);
  NodeClasses = NULL;
  ElementClasses = NULL;
  Circuit1 = NULL;
  Circuit2 = NULL;
  Elements = NULL;
  Nodes = NULL;
  NewNumberOfEclasses = OldNumberOfEclasses = 0;
  NewNumberOfNclasses = OldNumberOfNclasses = 0;
  Iterations = 0;
  BadMatchDetected = 0;
  PropertyErrorDetected = 0;
  NewFracturesMade = 0;
  ExhaustiveSubdivision = 0;	/* why not ?? */
  /* maybe should free up free lists ??? */
}



struct Element *CreateElementList(char *name, short graph)
/* create a list of the correct 'shape' for Elements, but with empty records*/
{
  struct objlist *ob;
  struct nlist *tp;
  struct Element *head, *tail;
	
  /* get a pointer to the cell */	
  tp = LookupCellFile(name, graph);
  if (tp == NULL) {
    Fprintf(stderr, "No cell '%s' found.\n", name);
    return(NULL);
  }

  head = tail = NULL;
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    if (ob->type == FIRSTPIN) {
      struct Element *new_element;

      new_element = GetElement();
      if (new_element == NULL) {
	Fprintf(stderr,"Memory allocation error\n");
#ifdef DEBUG_ALLOC
	PrintCoreStats();
#endif
	ResetState();
	return NULL;
      }
      new_element->object = ob;
      new_element->graph = graph;
      /* append to list */
      if (head == NULL) head = new_element;
      else tail->next = new_element;
      tail = new_element;
    }
    if (ob->type >= FIRSTPIN) {
      struct NodeList *tmp;
      tmp = GetNodeList();
      tmp->element = tail;
      tmp->next = tail->nodelist;
      tail->nodelist = tmp;
    }
  }
  return(head);
}

#ifdef LOOKUP_INITIALIZATION

struct ElementList **LookupElementList;

struct Node *CreateNodeList(char *name, short graph)
/* create a list of the correct 'shape' of the Node list */
/* for now, all the records are blank */
{
  struct objlist *ob, *newobj;
  struct nlist *tp;
  struct Node *head, *tail, *new_node;
  int maxnode, i;
  struct ElementList *tmp;

  /* get a pointer to the cell */	
  tp = LookupCellFile(name, graph);
  if (tp == NULL) {
    Fprintf(stderr, "No cell '%s' found.\n", name);
    return(NULL);
  }

  /* find the max. node number */
  maxnode = 0;
  for (ob = tp->cell; ob != NULL; ob = ob->next) 
    if (ob->type >= FIRSTPIN && ob->node > maxnode) maxnode = ob->node;
	
  /* now allocate the lookup table */
  LookupElementList = 
    (struct ElementList **)CALLOC(maxnode + 1, sizeof(struct ElementList *));
  if (LookupElementList == NULL) {
    Fprintf(stderr, "Unable to allocate space for lookup table\n");
    return(NULL);
  }

  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    // Requirement that ob->node be greater than zero eliminates
    // unconnected nodes (value -1) and dummy nodes (value 0)
    if (ob->type >= FIRSTPIN && (ob->node > 0)) {
      tmp = GetElementList();
      if (tmp == NULL) {
	Fprintf(stderr,"Memory allocation error\n");
#ifdef DEBUG_ALLOC
	PrintCoreStats();
#endif
	ResetState();
	return NULL;
      }
      tmp->next = LookupElementList[ob->node];
      LookupElementList[ob->node] = tmp;
    }
  }

  /* now generate a list of Nodes */
  head = tail = NULL;
  for (i = 0; i <= maxnode; i++) {
    if (LookupElementList[i] != NULL) {
      newobj = LookupObject(NodeName(tp, i), tp);
      if (newobj != NULL) {	/* NULL objects may be element property records */
        new_node = GetNode();
        if (new_node == NULL) {
	  Fprintf(stderr,"Memory allocation error\n");
#ifdef DEBUG_ALLOC
	  PrintCoreStats();
#endif
	  ResetState();
	  return NULL;
        }
        new_node->object = newobj;
        new_node->graph = graph;
        new_node->elementlist = LookupElementList[i];
        for (tmp = new_node->elementlist; tmp != NULL; tmp = tmp->next)
	  tmp->node = new_node;
        if (head == NULL) head = new_node;
        else tail->next = new_node;
        tail = new_node;
      }
    }
  }
  return (head);
}

/* creates two lists of the correct 'shape', then traverses nodes
 * in sequence to link up 'subelement' field of ElementList,
 *	then 'node' field of NodeList structures.
 *
 * Return the number of devices combined by series/parallel merging
 */		

int CreateLists(char *name, short graph)
{
  struct Element *ElementScan;
  struct ElementList *EListScan;
  struct NodeList *NListScan;
  struct objlist *ob;
  struct nlist *tp;
  int ppass, spass, pcnt, scnt, total;
	
  /* get a pointer to the cell */	
  tp = LookupCellFile(name, graph);
  if (tp == NULL) {
    Fprintf(stderr, "No cell '%s' found.\n", name);
    return 0;
  }

  if (Circuit1 == NULL) Circuit1 = tp;
  else if (Circuit2 == NULL) Circuit2 = tp;
  else {
    Fprintf(stderr, "Error: CreateLists() called more than twice without a reset.\n");
    return 0;
  }

  /* Parallel and series combinations.  Run until networks of	*/
  /* devices are resolved into a single device with the network	*/
  /* represented by a number of property records.		*/

  total = 0;
  for (ppass = 0; ; ppass++) {
     pcnt = CombineParallel(name, graph);
     total += pcnt;
     if (ppass > 0 && pcnt == 0) break;
     for (spass = 0; ; spass++) {
        scnt = CombineSeries(name, graph);
        total += scnt;
        if (scnt == 0) break;
     }
     if (spass == 0) break;
  }
  /* Uncomment this for series/parallel network diagnostics */
  /* DumpNetworkAll(name, graph); */

  Elements = CreateElementList(name, graph);
  Nodes = CreateNodeList(name, graph);
  if (LookupElementList == NULL) return total;

  ElementScan = NULL;
  NListScan = NULL;
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    if (ob->type == FIRSTPIN) {
      if (ElementScan == NULL) ElementScan = Elements;
      else ElementScan = ElementScan->next;
      NListScan = ElementScan->nodelist;
    }
    /* now hook up node */
    if (ob->type >= FIRSTPIN && (ob->node > 0)) {
      EListScan = LookupElementList[ob->node];
      
      /* write it into the EListScan slot */
      EListScan->subelement = NListScan;
      NListScan->node = EListScan->node;

      /* point to next available ElementList unit */
      LookupElementList[ob->node] = EListScan->next;
      NListScan = NListScan->next;
    }
  }

  FREE(LookupElementList);
  LookupElementList = NULL;
  return total;
}

#else

struct Node *CreateNodeList(char *name, short graph)
/* create a list of the correct 'shape' of the Node list */
/* for now, all the records are blank */
{
  struct objlist *ob;
  struct nlist *tp;
  struct Node *head, *tail, *new_node;
  int maxnode, i;

  /* get a pointer to the cell */	
  tp = LookupCellFile(name, graph);
  if (tp == NULL) {
    Fprintf(stderr, "No cell '%s' found.\n", name);
    return(NULL);
  }

  /* find the max. node number */
  maxnode = 0;
  for (ob = tp->cell; ob != NULL; ob = ob->next) 
    if (ob->type >= FIRSTPIN && ob->node > maxnode) maxnode = ob->node;
	
  /* now sequence through these node numbers, looking for existance only */
  head = tail = NULL;
  for (i = 1; i <= maxnode; i++) {
    new_node = NULL;
    for (ob = tp->cell; ob != NULL; ob = ob->next) {
      if (ob->type >= FIRSTPIN && ob->node == i) {
	struct ElementList *tmp;
	if (new_node == NULL) {
	  /* it is the first */
	  new_node = GetNode();
	  if (new_node == NULL) {
	    Fprintf(stderr,"Memory allocation error\n");
#ifdef DEBUG_ALLOC
	    PrintCoreStats();
#endif
	    ResetState();
	    return NULL;
	  }
	  /* probably want something like LookupObject(NodeNumber(i)) */
	  new_node->object = ob;
	  new_node->graph = graph;
	  if (head == NULL) head = new_node;
	  else tail->next = new_node;
	  tail = new_node;
	}
	/* prepend this element to the front of the sublist */
	tmp = GetElementList();
	if (new_node == NULL) {
	  Fprintf(stderr,"Memory allocation error\n");
#ifdef DEBUG_ALLOC
	  PrintCoreStats();
#endif
	  ResetState();
	  return NULL;
	}
	tmp->node = new_node;
	tmp->next = new_node->elementlist;
	new_node->elementlist = tmp;
      }
    }
  }
  return (head);
}

/* creates two lists of the correct 'shape', then traverses nodes
 * in sequence to link up 'subelement' field of ElementList,
 * then 'node' field of NodeList structures.
 *
 * Return the number of devices combined by series/parallel merging
 */		

int CreateLists(char *name, short graph)
{
  struct Element *E, *ElementScan;
  struct Node *N, *NodeScan;
  struct ElementList *EListScan;
  struct NodeList *NListScan;
  struct objlist *ob, *obscan;
  struct nlist *tp;
  int node, ppass, spass, pcnt, scnt, total;
	
  /* get a pointer to the cell */	
  tp = LookupCellFile(name, graph);
  if (tp == NULL) {
    Fprintf(stderr, "No cell '%s' found.\n", name);
    return 0;
  }

  if (Circuit1 == NULL) Circuit1 = tp;
  else if (Circuit2 == NULL) Circuit2 = tp;
  else {
    Fprintf(stderr, "Error: CreateLists() called more than twice without a reset.\n");
    return 0;
  }

  ConnectAllNodes(name, graph);

  /* Parallel and series combinations.  Run until networks of	*/
  /* devices are resolved into a single device with the network	*/
  /* represented by a number of property records.		*/

  total = 0;
  for (ppass = 0; ; ppass++) {
     pcnt = CombineParallel(name, graph);
     total += pcnt;
     if (ppass > 0 && pcnt == 0) break;
     for (spass = 0; ; spass++) {
        scnt = CombineSeries(name, graph);
        total += scnt;
        if (scnt == 0) break;
     }
     if (spass == 0) break;
  }
  /* Uncomment this for series/parallel network diagnostics */
  /* DumpNetworkAll(name, graph); */

  E = CreateElementList(name, graph);
  N = CreateNodeList(name, graph);
  NodeScan = N;
  node = 1;
  while (NodeScan != NULL) {
    int foundone;
		
    foundone = 0;
    /* look for all instances of 'node' */
    EListScan = NodeScan->elementlist;
    obscan = NULL;
    for (ob = tp->cell; ob != NULL; ob = ob->next) {
      if (ob->type == FIRSTPIN) obscan = ob;

      if (ob->node == node && ob->type >= FIRSTPIN) {
	struct objlist *tmp;
	foundone = 1;

	/* find object in Element list */
	for (ElementScan = E; ElementScan->object != obscan;
	     ElementScan = ElementScan->next) ;
	NListScan = ElementScan->nodelist;
	for (tmp = obscan; tmp != ob; tmp = tmp->next)
	  NListScan = NListScan->next;

	/* write it into the EListScan slot */
	EListScan->subelement = NListScan;
	NListScan->node = EListScan->node;
				
	EListScan = EListScan->next;
      }
    }
    if (foundone) NodeScan = NodeScan->next; /* for non-contiguous nodes */
    node++;
  }
  Elements = E;
  Nodes = N;
  return total;
}

#endif /* LOOKUP_INITIALIZATION */

int
CheckLegalElementPartition(struct ElementClass *head)
{
  int found;
  struct ElementClass *scan;
  struct Element *E;
  int C1, C2;

  /* now check for bad element classes */
  found = 0;
  for (scan = head; scan != NULL; scan = scan->next) {
    
    if (scan->count == 2) continue;
    C1 = C2 = 0;
    for (E = scan->elements; E != NULL; E = E->next) {
      if (E->graph == Circuit1->file) C1++;
      else C2++;
    }
    scan->count = C1 + C2;
    if (C1 != C2) {
      found = 1;
      BadMatchDetected = 1;
      scan->legalpartition = 0;
    }
  }
  return found;
}
    

struct ElementClass *MakeElist(struct Element *E)
/* traverses a list of elements.  Puts all elements having the
   same hashval into the same class.  Returns a pointer to a list
   of element classes, each of which contains a list of elements.
*/
{
  struct ElementClass *head, *new_elementclass, *scan,
                      *bad_elementclass, *tail;
  struct Element *enext;
  int found;

  head = NULL;
  while (E != NULL) {
    found = 0;
    enext = E->next;
    for (scan = head; scan != NULL && !found; scan = scan->next)
      if (scan->magic == E->hashval) {
	      found = 1;
	      break; /* get out of for loop without changing scan */
      }
    if (!found) {
      /* need to create a new one, and prepend to list */
      new_elementclass = GetElementClass();
      if (new_elementclass == NULL) {
	Fprintf(stderr,"Memory allocation error\n");
#ifdef DEBUG_ALLOC
	PrintCoreStats();
#endif
	ResetState();
	return NULL;
      }
      new_elementclass->magic = E->hashval;
      new_elementclass->next = head;
      head = new_elementclass;
      scan = head;
    }
    /* prepend to list already present */
    E->next = scan->elements;
    E->elemclass = scan;
    scan->elements = E;
    scan->count++;  /* just added by MAS */
      
    E = enext;
  }

  if (!CheckLegalElementPartition(head))
     return(head);  /* we are done */

  /* now regroup all the illegal partitions into a single class */
  bad_elementclass = GetElementClass();
  bad_elementclass->legalpartition = 0;
  for (scan = head; scan != NULL; scan = scan->next) {
    struct Element *E, *enext;

    if (!(scan->legalpartition)) {
      for (E = scan->elements; E != NULL; ) {
	enext = E->next;
	/* prepend to list already present */
	E->next = bad_elementclass->elements;
	E->elemclass = bad_elementclass;
	bad_elementclass->elements = E;
	bad_elementclass->count++;  /* just added by MAS */

	E = enext;
      }
    }    
  }
  /* eat all empty element classes */
  tail = bad_elementclass;
  for (scan = head; scan != NULL; ) {
    struct ElementClass *badclass;

    if (!(scan->legalpartition)) {
      badclass = scan;
      scan = scan->next;
      FreeElementClass(badclass);
    }
    else {
      tail->next = scan;
      scan = scan->next;
      tail->next->next = NULL;
      tail = tail->next;
    }
  }
  head = bad_elementclass;

  if (head->next != NULL)
    NewFracturesMade = 1;  /* list did fracture into more than one class */

  
  return (head);
}

int
CheckLegalNodePartition(struct NodeClass *head)
{
  struct NodeClass *scan;
  int found;
  struct Node *N;
  int C1, C2;

  /* now check for bad node classes */
  found = 0;
  for (scan = head; scan != NULL; scan = scan->next) {
    
    if (scan->count == 2) continue;
    C1 = C2 = 0;
    for (N = scan->nodes; N != NULL; N = N->next) {
      if (N->graph == Circuit1->file) C1++;
      else C2++;
    }
    scan->count = C1 + C2;
    if (C1 != C2) {
      /* we have an illegal partition */
      found = 1;
      BadMatchDetected = 1;
      scan->legalpartition = 0;
    }
  }
  return found;
}

struct NodeClass *MakeNlist(struct Node *N)
{
  struct NodeClass *head, *new_nodeclass, *scan, *bad_nodeclass, *tail;
  struct Node *nnext;
  int found;

  head = NULL;
  while (N != NULL) {
    found = 0;
    nnext = N->next;
    for (scan = head; scan != NULL && !found; scan = scan->next)
      if (scan->magic == N->hashval) {
	      found = 1;
	      break; /* get out of for loop without changing scan */
      }
    if (!found) {
      /* need to create a new one, and prepend to list */
      new_nodeclass = GetNodeClass();
      if (new_nodeclass == NULL) {
	Fprintf(stderr,"Memory allocation error\n");
#ifdef DEBUG_ALLOC
	PrintCoreStats();
#endif
	ResetState();
	return NULL;
      }
      new_nodeclass->magic = N->hashval;
      new_nodeclass->next = head;
      head = new_nodeclass;
      scan = head;
    }
    /* prepend to list already present */
    N->next = scan->nodes;
    N->nodeclass = scan;
    scan->nodes = N;
    scan->count++;
      
    N = nnext;
  }

  if (!CheckLegalNodePartition(head))
     return(head);  /* we are done */
    
  /* now regroup all the illegal partitions into a single class */
  bad_nodeclass = GetNodeClass();
  bad_nodeclass->legalpartition = 0;
  for (scan = head; scan != NULL; scan = scan->next) {
    struct Node *N, *nnext;

    if (!(scan->legalpartition)) {
      for (N = scan->nodes; N != NULL; ) {
	nnext = N->next;
	/* prepend to list already present */
	N->next = bad_nodeclass->nodes;
	N->nodeclass = bad_nodeclass;
	bad_nodeclass->nodes = N;
	bad_nodeclass->count++;  /* just added by MAS */

	N = nnext;
      }
    }    
  }
  /* eat all empty element classes */
  tail = bad_nodeclass;
  for (scan = head; scan != NULL; ) {
    struct NodeClass *badclass;

    if (!(scan->legalpartition)) {
      badclass = scan;
      scan = scan->next;
      FreeNodeClass(badclass);
    }
    else {
      tail->next = scan;
      scan = scan->next;
      tail->next->next = NULL;
      tail = tail->next;
    }
  }
  head = bad_nodeclass;

  if (head->next != NULL)
	NewFracturesMade = 1;  /* list did fracture into more than one class */
  return (head);
}


/* need to choose MAX_RANDOM to fit inside an 'int' field */
#define MAX_RANDOM (INT_MAX)
/* #define MAX_RANDOM ((1L << 19) - 1) */

#define Magic(a) (a = Random(MAX_RANDOM))
#define MagicSeed(a) RandomSeed(a)

int FractureElementClass(struct ElementClass **Elist)
/* returns the number of new classes that were created */
{
  struct ElementClass *Eclass, *Ehead, *Etail, *Enew, *Enext;

  Ehead = Etail = NULL;
  /* traverse the list, fracturing as required, and freeing EC to recycle */
  Eclass = *Elist;
  while (Eclass != NULL) {
    Enext = Eclass->next;
    if (Eclass->count != 2 || ExhaustiveSubdivision) {
       Enew = MakeElist(Eclass->elements);
       FreeElementClass(Eclass);
       if (Ehead == NULL) {
	  Ehead = Etail = Enew;
	  Magic(Etail->magic);
       }
       else Etail->next = Enew;
       while (Etail->next != NULL) {
	  Etail = Etail->next;
	  /* don't forget to assign new magic numbers to the new elements */
          Magic(Etail->magic);
       }
    }
    else {
       Enew = Eclass;
       Enew->next = NULL;
       if (Ehead == NULL) Ehead = Etail = Enew;
       else Etail->next = Enew;
       Etail = Enew;
    }
    Eclass = Enext;
  }
  *Elist = Ehead;
  NewNumberOfEclasses = 0;
  for (Eclass = *Elist; Eclass != NULL; Eclass = Eclass->next) 
	  NewNumberOfEclasses++;

  if (Debug == TRUE) {
     if (Iterations == 0) Fprintf(stdout, "\n");
     Fprintf(stdout, "Iteration: %3d: Element classes = %4d (+%d);", Iterations,
	  	NewNumberOfEclasses, NewNumberOfEclasses - OldNumberOfEclasses);
     Ftab(stdout, 50);
  }

  /* make the New* things deltas */
  NewNumberOfEclasses -= OldNumberOfEclasses;
  OldNumberOfEclasses += NewNumberOfEclasses;
  return(NewNumberOfEclasses); 
}

int FractureNodeClass(struct NodeClass **Nlist)
/* returns the number of new classes that were created */
{  struct NodeClass *Nclass, *Nhead, *Ntail, *Nnew, *Nnext;
  Nhead = Ntail = NULL;
  /* traverse the list, fracturing as required, and freeing NC to recycle */
  Nclass = *Nlist;
  while (Nclass != NULL) {
    Nnext = Nclass->next;	  
    if (Nclass->count != 2 || ExhaustiveSubdivision) {
       Nnew = MakeNlist(Nclass->nodes);
       FreeNodeClass(Nclass);
       if (Nhead == NULL) {
	  Nhead = Ntail = Nnew;
	  Magic(Ntail->magic);
       }
       else Ntail->next = Nnew;
       while (Ntail->next != NULL) {
          Ntail = Ntail->next;
	  /* don't forget to assign new magic numbers to the new elements */
          Magic(Ntail->magic);
	}
    }
    else {
       Nnew = Nclass;
       Nnew->next = NULL;
       if (Nhead == NULL) Nhead = Ntail = Nnew;
       else Ntail->next = Nnew;
       Ntail = Nnew;
    }
    Nclass = Nnext;
  }
  *Nlist = Nhead;
  NewNumberOfNclasses = 0;
  for (Nclass = *Nlist; Nclass != NULL; Nclass = Nclass->next)
	  NewNumberOfNclasses++;

  if (Debug == TRUE) {
    Fprintf(stdout, "Net groups = %4d (+%d)\n",
	  NewNumberOfNclasses, NewNumberOfNclasses - OldNumberOfNclasses);
  }

  /* make the New* things deltas */
  NewNumberOfNclasses -= OldNumberOfNclasses;
  OldNumberOfNclasses += NewNumberOfNclasses;
  return(NewNumberOfNclasses);
}

/* Structure used by lookupclass */

typedef struct _chd {
   int file;
   unsigned long classhash;
} chdata;

/*----------------------------------------------------------------------*/
/* Callback function used by LookupClassEquivalent			*/
/*----------------------------------------------------------------------*/

struct nlist *lookupclass(struct hashlist *p, void *clientdata)
{
    struct nlist *ptr;
    chdata *chd = (chdata *)clientdata;

    ptr = (struct nlist *)(p->ptr);

    if (ptr->file == chd->file)
	if (ptr->classhash == chd->classhash)
	    return ptr;

    return NULL;
}

/*----------------------------------------------------------------------*/
/* Given the class (cellname) "model" in file "file1", find the		*/
/* equivalent class in "file2".  This is done by exhaustively searching	*/
/* the cell database for a matching classhash number.  It is therefore	*/
/* not intended to be run often;  it should be only run when choosing	*/
/* two cells to compare.						*/
/*----------------------------------------------------------------------*/

struct nlist *LookupClassEquivalent(char *model, int file1, int file2)
{
   struct nlist *tp, *tp2;
   chdata chd;

   tp = LookupCellFile(model, file1);
   if (tp == NULL) return NULL;

   chd.file = file2;
   chd.classhash = tp->classhash;

   tp2 = RecurseCellHashTable2(lookupclass, (void *)(&chd));
   return tp2;
}

/*----------------------------------------------------------------------*/
/* Look up the cell in file2 which has been specified as a match for	*/
/* cell tc1 using the "equate classes" command.  If there is no match,	*/
/* then return NULL.  If there is a match, then set the	classhash	*/
/* values to be equal to each other.					*/
/*----------------------------------------------------------------------*/

struct nlist *LookupPrematchedClass(struct nlist *tc1, int file2)
{
   struct Correspond *crec;
   struct nlist *tc2 = NULL;

   for (crec = ClassCorrespondence; crec != NULL; crec = crec->next) {
      if (crec->file1 == tc1->file) {
	 if ((*matchfunc)(tc1->name, crec->class1))
	    if (crec->file2 == file2 || crec->file2 == -1)
	       tc2 = LookupCellFile(crec->class2, file2);
      }
      else if (crec->file1 == file2) {
	 if ((*matchfunc)(tc1->name, crec->class2))
	    if (crec->file2 == tc1->file || crec->file2 == -1)
	       tc2 = LookupCellFile(crec->class1, file2);
      }
      else if (crec->file2 == tc1->file) {
	 if ((*matchfunc)(tc1->name, crec->class2))
	    if (crec->file1 == -1)
	       tc2 = LookupCellFile(crec->class1, file2);
      }
      else if (crec->file2 == file2) {
	 if ((*matchfunc)(tc1->name, crec->class1))
	    if (crec->file1 == -1)
	       tc2 = LookupCellFile(crec->class2, file2);
      }
      else if (crec->file1 == -1 && crec->file2 == -1) {
	 if ((*matchfunc)(tc1->name, crec->class1))
	    tc2 = LookupCell(crec->class2);
	 else if ((*matchfunc)(tc1->name, crec->class2))
	    tc2 = LookupCell(crec->class1);
      }
   }
   if (tc2 != NULL) tc2->classhash = tc1->classhash;
   return tc2;
}

/*----------------------------------------------------------------------*/
/* Scan the property list of a device to find the number of devices	*/
/* implied by the total of M records.  If the device does not have a	*/
/* property list, then return 1.  If any property list does not have an	*/
/* "M" record, treat it as 1.						*/
/*----------------------------------------------------------------------*/

int GetNumDevices(struct objlist *ob)
{
    int p, found, M = 0;
    struct objlist *obs;
    struct valuelist *vl;
    
    obs = ob;
    if (obs->type != PROPERTY)
       for (obs = ob->next; obs && (obs->type != FIRSTPIN) &&
		(obs->type != PROPERTY); obs = obs->next);

    if ((obs == NULL) || (obs->type != PROPERTY)) return 1;

    while (obs && (obs->type == PROPERTY)) {
	found = FALSE;
	for (p = 0; ; p++) {
	    vl = &obs->instance.props[p];
	    if (vl->type == PROP_ENDLIST) break;
	    if (vl->key == NULL) continue;
            if ((*matchfunc)(vl->key, "M")) {
                if (vl->type == PROP_DOUBLE)
	            M += (int)vl->value.dval;
		else
	            M += vl->value.ival;
		found = TRUE;
		break;
	    }
	}
	if (found == FALSE) M++;
	obs = obs->next;
    }
    return M;
}

/*----------------------------------------------------------------------*/
/* Attempt to define FirstElementPass that will generate element	*/
/* classes by names of pins, which will allow elements with different	*/
/* cell names in different circuits to be grouped.  Ultimately we want	*/
/* a solution that allows classes to be forced to be equated.		*/
/*									*/
/* During the pass, element lists are checked for subcircuits that have	*/
/* no equivalent in the other circuit.  If so, they are marked for	*/
/* flattening by setting the hashval entry to -1 and the routine	*/
/* returns -1.  If every cell had at least one match, or all non-	*/
/* matching cells were fundamental devices or black-box subcircuits,	*/
/* then the setup for comparison continues, and the routine returns 0.	*/
/* However, if "noflat" is set to 1, then the circuits are compared	*/
/* as-is, even if one or more non-matching elements could be flattened.	*/
/*----------------------------------------------------------------------*/

int FirstElementPass(struct Element *E, int noflat, int dolist)
{
  struct Element *Esrch, *Ecorr;
  struct NodeList *n;
  struct nlist *tp1, *tp2, *tp;
  int C1, C2, M1, M2, i;
  char *ostr;
  int needflat = 0;
#ifdef TCL_NETGEN
  Tcl_Obj *clist1, *clist2;
#endif

  ostr = CALLOC(right_col_end + 2, sizeof(char));

  if (Debug == 0) {
     Fprintf(stdout, "Subcircuit summary:\n");
     *(ostr + left_col_end) =  '|';
     *(ostr + right_col_end) = '\n';
     *(ostr + right_col_end + 1) = '\0';
     for (i = 0; i < left_col_end; i++) *(ostr + i) = ' ';
     for (i = left_col_end + 1; i < right_col_end; i++) *(ostr + i) = ' ';

     snprintf(ostr, left_col_end, "Circuit 1: %s", Circuit1->name);
     snprintf(ostr + left_col_end + 1, left_col_end, "Circuit 2: %s", Circuit2->name);
     for (i = 0; i < right_col_end + 1; i++) if (*(ostr + i) == '\0') *(ostr + i) = ' ';
     Fprintf(stdout, ostr);
     for (i = 0; i < left_col_end; i++) *(ostr + i) = '-';
     for (i = left_col_end + 1; i < right_col_end; i++) *(ostr + i) = '-';
     Fprintf(stdout, ostr);
  }

#ifdef TCL_NETGEN
  if (dolist) {
     clist1 = Tcl_NewListObj(0, NULL);
     clist2 = Tcl_NewListObj(0, NULL);
  }
#endif

  // Print side-by-side comparison of elements based on class correspondence
  // Use the hashval record to mark what entries have been processed already.

  for (Esrch = E; Esrch != NULL; Esrch = Esrch->next) {
     if (Esrch->graph == Circuit1->file && Esrch->hashval == 0) {
	Esrch->hashval = 1;
	C1 = 1;
	C2 = 0;
	M2 = 0;
	M1 = GetNumDevices(Esrch->object);
	tp1 = LookupCellFile(Esrch->object->model.class, Circuit1->file);
	tp2 = LookupClassEquivalent(Esrch->object->model.class, Circuit1->file,
			Circuit2->file);
	for (Ecorr = E; Ecorr != NULL; Ecorr = Ecorr->next) {
	   if (Ecorr->hashval == 0) {
	      if (Ecorr->graph == Circuit2->file) {
		 tp = LookupCellFile(Ecorr->object->model.class, Circuit2->file);
		 // if (tp == tp2) {
		 if (tp && tp2 && (tp->classhash == tp2->classhash)) {
		    Ecorr->hashval = 1;
		    C2++;
		    M2 += GetNumDevices(Ecorr->object);
	         }
	      }
	      else if (Ecorr->graph == Circuit1->file) {
		 tp = LookupCellFile(Ecorr->object->model.class, Circuit1->file);
		 // if (tp == tp1) {
		 if (tp && tp1 && (tp->classhash == tp1->classhash)) {
		    Ecorr->hashval = 1;
		    C1++;
		    M1 += GetNumDevices(Ecorr->object);
		 }
	      }
	   }
	}

	if (C2 == 0)
	    if (tp1->class == CLASS_SUBCKT)
		if (!noflat) {
		    Esrch->hashval = -1;	/* Mark for flattening */
		    needflat = 1;
		}

	if (Debug == 0) {

	   for (i = 0; i < left_col_end; i++) *(ostr + i) = ' ';
	   for (i = left_col_end + 1; i < right_col_end; i++) *(ostr + i) = ' ';
	   if (M1 == C1)
           	snprintf(ostr, left_col_end, "%s (%d)", Esrch->object->model.class, C1);
	   else
           	snprintf(ostr, left_col_end, "%s (%d->%d)", Esrch->object->model.class,
				M1, C1);
	   if (C2 > 0) {
	      if (M2 == C2)
              	 snprintf(ostr + left_col_end + 1, left_col_end, "%s (%d)%s", tp2->name,
			C2, (C2 == C1) ? "" : " **Mismatch**");
	      else
              	 snprintf(ostr + left_col_end + 1, left_col_end, "%s (%d->%d)%s",
			tp2->name, M2, C2, (C2 == C1) ? "" : " **Mismatch**");
	   }
	   else {
              snprintf(ostr + left_col_end + 1, left_col_end, "(no matching element)");
	   }
           for (i = 0; i < right_col_end + 1; i++) if (*(ostr + i) == '\0') *(ostr + i) = ' ';
           Fprintf(stdout, ostr);
	}
#ifdef TCL_NETGEN
	if (dolist) {
	   Tcl_Obj *elist;
	   elist = Tcl_NewListObj(0, NULL);
	   Tcl_ListObjAppendElement(netgeninterp, elist,
			Tcl_NewStringObj(Esrch->object->model.class, -1));
	   Tcl_ListObjAppendElement(netgeninterp, elist,
			Tcl_NewIntObj(C1));
	   Tcl_ListObjAppendElement(netgeninterp, clist1, elist);

	   elist = Tcl_NewListObj(0, NULL);
	   if (C2 > 0) {
	      Tcl_ListObjAppendElement(netgeninterp, elist,
			Tcl_NewStringObj(tp2->name, -1));
	      Tcl_ListObjAppendElement(netgeninterp, elist,
			Tcl_NewIntObj(C2));
	   }
	   else {
	      Tcl_ListObjAppendElement(netgeninterp, elist,
			Tcl_NewStringObj("(no matching element)", -1));
	      Tcl_ListObjAppendElement(netgeninterp, elist,
			Tcl_NewIntObj(0));
	   }
	   Tcl_ListObjAppendElement(netgeninterp, clist2, elist);
	}
#endif
     }
  }
  for (Esrch = E; Esrch != NULL; Esrch = Esrch->next) {
     if (Esrch->graph == Circuit2->file && Esrch->hashval == 0) {
	Esrch->hashval = 1;
	C2 = 1;
	M2 = GetNumDevices(Esrch->object);
	tp2 = LookupCellFile(Esrch->object->model.class, Circuit2->file);
	tp1 = LookupClassEquivalent(Esrch->object->model.class, Circuit2->file,
			Circuit1->file);
	for (Ecorr = E; Ecorr != NULL; Ecorr = Ecorr->next) {
	   if (Ecorr->hashval == 0) {
	      if (Ecorr->graph == Circuit2->file) {
		 tp = LookupCellFile(Ecorr->object->model.class, Circuit2->file);
		 // if (tp == tp2) {
		 if (tp->classhash == tp2->classhash) {
		    Ecorr->hashval = 1;
		    C2++;
		    M2 += GetNumDevices(Ecorr->object);
	         }
	      }
	   }
	}

	if (tp2->class == CLASS_SUBCKT)
	    if (!noflat) {
		Esrch->hashval = -1;	/* Mark for flattening */
		needflat = 1;
	    }

	if (Debug == 0) {
	   for (i = 0; i < left_col_end; i++) *(ostr + i) = ' ';
	   for (i = left_col_end + 1; i < right_col_end; i++) *(ostr + i) = ' ';
           snprintf(ostr, left_col_end, "(no matching element)");
	   if (C2 == M2)
              snprintf(ostr + left_col_end + 1, left_col_end, "%s (%d)",
			Esrch->object->model.class, C2);
	   else
              snprintf(ostr + left_col_end + 1, left_col_end, "%s (%d->%d)",
			Esrch->object->model.class, M2, C2);
           for (i = 0; i < right_col_end + 1; i++) if (*(ostr + i) == '\0') *(ostr + i) = ' ';
           Fprintf(stdout, ostr);
	}
#ifdef TCL_NETGEN
	if (dolist) {
	   Tcl_Obj *elist;
	   elist = Tcl_NewListObj(0, NULL);
	   Tcl_ListObjAppendElement(netgeninterp, elist,
			Tcl_NewStringObj("(no matching element)", -1));
	   Tcl_ListObjAppendElement(netgeninterp, elist,
			Tcl_NewIntObj(0));
	   Tcl_ListObjAppendElement(netgeninterp, clist1, elist);

	   elist = Tcl_NewListObj(0, NULL);
	   Tcl_ListObjAppendElement(netgeninterp, elist,
			Tcl_NewStringObj(Esrch->object->model.class, -1));
	   Tcl_ListObjAppendElement(netgeninterp, elist,
			Tcl_NewIntObj(C2));
	   Tcl_ListObjAppendElement(netgeninterp, clist2, elist);
	}
#endif
     }
  }

  C1 = C2 = 0;
  while (E != NULL) {

    /* initialize random no. gen. to model-specific number */
    tp = LookupCellFile(E->object->model.class, E->graph);
    MagicSeed(tp->classhash);

    for (n = E->nodelist; n != NULL; n = n->next)
       Magic(n->pin_magic);

    E->hashval = tp->classhash;
    if (E->graph == Circuit1->file) C1++;
    else C2++;
    E = E->next;
  }
  if (Debug == TRUE) {
     if (C1 != C2)
        Fprintf(stderr, "Device Mismatch: Circuit 1 has %d, Circuit 2 has %d.\n",
			C1, C2);
  }
  else {
     for (i = 0; i < left_col_end; i++) *(ostr + i) = ' ';
     for (i = left_col_end + 1; i < right_col_end; i++) *(ostr + i) = ' ';
     snprintf(ostr, left_col_end, "Number of devices: %d%s", C1, (C1 == C2) ? "" :
		" **Mismatch**");
     snprintf(ostr + left_col_end + 1, left_col_end, "Number of devices: %d%s", C2, (C1 == C2) ? "" :
		" **Mismatch**");
     for (i = 0; i < right_col_end + 1; i++) if (*(ostr + i) == '\0') *(ostr + i) = ' ';
     Fprintf(stdout, ostr);
  }

#ifdef TCL_NETGEN
  if (dolist) {
     Tcl_Obj *mlist;

     mlist = Tcl_NewListObj(0, NULL);
     Tcl_ListObjAppendElement(netgeninterp, mlist, clist1);
     Tcl_ListObjAppendElement(netgeninterp, mlist, clist2);

     Tcl_SetVar2Ex(netgeninterp, "lvs_out", NULL,
		Tcl_NewStringObj("devices", -1),
		TCL_APPEND_VALUE | TCL_LIST_ELEMENT);
     Tcl_SetVar2Ex(netgeninterp, "lvs_out", NULL, mlist,
		TCL_APPEND_VALUE | TCL_LIST_ELEMENT);
  }
#endif

  FREE(ostr);
  return 0;
}

void FirstNodePass(struct Node *N, int dolist)
{
  struct ElementList *E;
  int fanout;
  int C1, C2;
  
  C1 = C2 = 0;
  while (N != NULL) {
    fanout = 0;
    for (E = N->elementlist; E != NULL; E = E->next)  fanout++;
    N->hashval = fanout;
    if (N->graph == Circuit1->file) C1++;
    else C2++;
    N = N->next;
  }
  if (Debug == TRUE) {
     if (C1 != C2)
        Fprintf(stderr, "Net Mismatch: Circuit 1 has %d, Circuit 2 has %d.\n",C1,C2);
  }
  else {
     char *ostr;
     int i;

     ostr = CALLOC(right_col_end + 2, sizeof(char));

     *(ostr + left_col_end) =  '|';
     *(ostr + right_col_end) = '\n';
     *(ostr + right_col_end + 1) = '\0';

     for (i = 0; i < left_col_end; i++) *(ostr + i) = ' ';
     for (i = left_col_end + 1; i < right_col_end; i++) *(ostr + i) = ' ';
     snprintf(ostr, left_col_end, "Number of nets: %d%s", C1, (C1 == C2) ? "" :
		" **Mismatch**");
     snprintf(ostr + left_col_end + 1, left_col_end, "Number of nets: %d%s", C2, (C1 == C2) ? "" :
		" **Mismatch**");
     for (i = 0; i < right_col_end + 1; i++) if (*(ostr + i) == '\0') *(ostr + i) = ' ';
     Fprintf(stdout, ostr);

     for (i = 0; i < right_col_end; i++) *(ostr + i) = '-';
     Fprintf(stdout, ostr);

     FREE(ostr);
  }

#ifdef TCL_NETGEN
  if (dolist) {
     Tcl_Obj *nlist;

     nlist = Tcl_NewListObj(0, NULL);
     Tcl_ListObjAppendElement(netgeninterp, nlist, Tcl_NewIntObj(C1));
     Tcl_ListObjAppendElement(netgeninterp, nlist, Tcl_NewIntObj(C2));
     Tcl_SetVar2Ex(netgeninterp, "lvs_out", NULL,
		Tcl_NewStringObj("nets", -1),
		TCL_APPEND_VALUE | TCL_LIST_ELEMENT);
     Tcl_SetVar2Ex(netgeninterp, "lvs_out", NULL, nlist,
		TCL_APPEND_VALUE | TCL_LIST_ELEMENT);
  }
#endif
}

/*--------------------------------------------------------------*/
/* Declare cells to be non-matching by clearing the		*/
/* CELL_MATCHED	flag in both cells.  This will force cell	*/
/* flattening during hierarchical LVS.				*/
/*--------------------------------------------------------------*/

void MatchFail(char *name1, char *name2)
{
   struct nlist *tc1, *tc2;

   tc1 = LookupCell(name1);
   tc2 = LookupCell(name2);

   if (!(tc1->flags & CELL_DUPLICATE) && !(tc2->flags & CELL_DUPLICATE)) {
      tc1->flags &= ~CELL_MATCHED;
      tc2->flags &= ~CELL_MATCHED;
   }
   else if (tc1->flags & CELL_DUPLICATE)
      tc1->flags &= ~CELL_MATCHED;
   else if (tc2->flags & CELL_DUPLICATE)
      tc2->flags &= ~CELL_MATCHED;
}

/*--------------------------------------------------------------*/

int FlattenUnmatched(struct nlist *tc, char *parent, int stoplevel, int loclevel)
{
   struct nlist *tcsub;
   struct objlist *ob;
   int changed = 0;

   if (loclevel == stoplevel && !(tc->flags & CELL_MATCHED)) {
      ClearDumpedList();
      if (Debug == TRUE) Fprintf(stdout, "Level %d ", loclevel);
      Fprintf(stdout, "Flattening unmatched subcell %s in circuit %s (%d)",
				tc->name, parent, tc->file);
      changed = flattenInstancesOf(parent, tc->file, tc->name);
      Fprintf(stdout, "(%d instance%s)\n", changed, ((changed == 1) ? "" : "s"));
      return 1;
   }

   if (tc->cell == NULL) return 0;

   changed = 1;
   while (changed) {
      changed = 0;
      for (ob = tc->cell; ob != NULL; ob = ob->next) {
         tcsub = NULL;
         if (ob->type == FIRSTPIN) {
	    /* First check if there is a class equivalent */
	    tcsub = LookupCellFile(ob->model.class, tc->file);
	    if (!tcsub || (tcsub->class != CLASS_SUBCKT)) continue;
	    else if (tcsub == tc) continue;
	    if (FlattenUnmatched(tcsub, tc->name, stoplevel, loclevel + 1)) {
	       changed = 1;
	       break;
	    }
	 }
      }
   }

   return 0;
}

/*--------------------------------------------------------------*/

void DescendCountQueue(struct nlist *tc, int *level, int loclevel)
{
   struct nlist *tcsub;
   struct objlist *ob;

   if (loclevel > *level) *level = loclevel;

   for (ob = tc->cell; ob != NULL; ob = ob->next) {
      tcsub = NULL;
      if (ob->type == FIRSTPIN) {
	 /* First check if there is a class equivalent */
	 tcsub = LookupCellFile(ob->model.class, tc->file);
	 /* Module (black-box) class needs pin checking */
	 if (!tcsub || ((tcsub->class != CLASS_SUBCKT) &&
		    (tcsub->class != CLASS_MODULE))) continue;
	 else if (tcsub == tc) continue;
	 DescendCountQueue(tcsub, level, loclevel + 1);
      }
   }
}

/*--------------------------------------------------------------*/

void DescendCompareQueue(struct nlist *tc, struct nlist *tctop, int stoplevel,
		int loclevel, int flip)
{
   struct nlist *tcsub, *tc2, *tctest;
   struct objlist *ob;
   struct Correspond *scomp, *newcomp;
   char *sdup = NULL;

   if (loclevel == stoplevel && !(tc->flags & CELL_MATCHED)) {

      // Any duplicate cell should be name-matched against a non-duplicate
      if (tc->flags & CELL_DUPLICATE) {
	 sdup = strstr(tc->name, "[[");
	 if (sdup) *sdup = '\0';
      }

      // Find exact-name equivalents or cells that have been specified
      // as equivalent using the "equate class" command.

      // Check if cell names were forced to be matched using the
      // "equate classes" command.  This takes precedence over any
      // name matching.

      tc2 = LookupPrematchedClass(tc, tctop->file);
      if (tc2 == NULL) {
	 tc2 = LookupClassEquivalent(tc->name, tc->file, tctop->file);

	 // If there is a name equivalent, then make sure that
	 // the matching entry does not exist in the prematched
	 // class list with a match to something else.

         if (tc2 != NULL) {
	    tctest = LookupPrematchedClass(tc2, tc->file);
	    if (tctest != NULL && tctest != tc) {
	       if (sdup) *sdup = '[';
	       return;
	    }
         }
      }

      if (sdup) *sdup = '[';

      if (tc2 != NULL) {
	 newcomp = (struct Correspond *)CALLOC(1, sizeof(struct Correspond));
	 newcomp->next = NULL;
	 if (flip) {
	    newcomp->class1 = tc2->name;
	    newcomp->file1 = tc2->file;
	    newcomp->class2 = tc->name;
	    newcomp->file2 = tc->file;
	 }
	 else {
	    newcomp->class1 = tc->name;
	    newcomp->file1 = tc->file;
	    newcomp->class2 = tc2->name;
	    newcomp->file2 = tc2->file;
	 }
	
         if (Debug == TRUE)
	    Fprintf(stdout, "Level %d Appending %s %s to compare queue\n",
			loclevel, tc->name, tc2->name);

	 /* Add this pair to the end of the list of cells to compare */
	 if (CompareQueue == NULL)
	    CompareQueue = newcomp;
	 else {
	    for (scomp = CompareQueue; scomp->next; scomp = scomp->next);
	    scomp->next = newcomp;
	 }
	 tc->flags |= CELL_MATCHED;
	 tc2->flags |= CELL_MATCHED;
      }
      else if (Debug == TRUE)
	 Fprintf(stdout, "Level %d Class %s is unmatched and will be flattened\n",
			loclevel, tc->name);
      return;
   }

   /* Now work through the subcircuits of tc */

   for (ob = tc->cell; ob != NULL; ob = ob->next) {
      tcsub = NULL;
      if (ob->type == FIRSTPIN) {
	 tcsub = LookupCellFile(ob->model.class, tc->file);
	 if (!tcsub || ((tcsub->class != CLASS_SUBCKT) &&
		    (tcsub->class != CLASS_MODULE))) continue;
	 else if (tcsub == tc) continue;
	 DescendCompareQueue(tcsub, tctop, stoplevel, loclevel + 1, flip);
      }
   }
}

/*--------------------------------------------------------------*/
/* Routine that assigns Circuit1 and Circuit2.  This will be 	*/
/* done by CreateLists, but doing it earlier allows one to use	*/
/* the names "-circuit1" and "-circuit2" in the setup file.	*/
/*--------------------------------------------------------------*/

void AssignCircuits(char *name1, int file1, char *name2, int file2)
{
   struct nlist *tc1, *tc2;

   tc1 = LookupCellFile(name1, file1);
   tc2 = LookupCellFile(name2, file2);

   if (tc1 != NULL) Circuit1 = tc1;
   if (tc2 != NULL) Circuit2 = tc2;
}

/*--------------------------------------------------------------*/
/* Parse the top-level cells for hierarchical structure, and	*/
/* generate a stack of cells to compare.  This is done		*/
/* carefully from bottom up so as to raise the likelihood that	*/
/* we are comparing two circuits that really are supposed to	*/
/* be equivalent.  Comparisons are then done from bottom up so	*/
/* that anything that isn't matched can be flattened as we go;	*/
/* in the worst case, if no subcells can be definitively	*/
/* matched, then we will end up running a comparison on two	*/
/* completely flattened netlists.				*/
/*								*/
/* Return 0 on success, 1 on failure to look up cell name1,	*/
/* and 2 on failure to look up cell name2.			*/
/*--------------------------------------------------------------*/


int CreateCompareQueue(char *name1, int file1, char *name2, int file2)
{
   struct nlist *tc1, *tc2, *tcsub1, *tcsub2;
   struct Correspond *newcomp, *scomp;
   int level;

   tc1 = LookupCellFile(name1, file1);
   tc2 = LookupCellFile(name2, file2);

   if (tc1 == NULL) return 1;
   if (tc2 == NULL) return 2;

   level = 0;

   /* Recurse the hierarchies of tc1 and tc2 and find the deepest level */
   DescendCountQueue(tc1, &level, 0);
   DescendCountQueue(tc2, &level, 0);

   /* Starting at the deepest level, compare each circuit to the other	*/
   /* When each level has put as many cells as it can find on the	*/
   /* compare queue, then flatten all the unmatched cells in that level	*/

   while (level > 0) {
      if (Debug == TRUE)
         Fprintf(stdout, "Descend level %d circuit 1\n", level);
      DescendCompareQueue(tc1, tc2, level, 0, 0);
      if (Debug == TRUE)
         Fprintf(stdout, "Descend level %d circuit 2\n", level);
      DescendCompareQueue(tc2, tc1, level, 0, 1);

      /* NOTE:  Preemptive flattening is inefficient and can cause
       * unnecessary flattening of cells that will never be compared.
       * let the prematch stage take care of this.
       */
      /*--------------------------------------
      if (Debug == TRUE)
         Fprintf(stdout, "Flatten level %d circuit 1\n", level);
      FlattenUnmatched(tc1, name1, level, 0);
      if (Debug == TRUE)
         Fprintf(stdout, "Flatten level %d circuit 2\n", level);
      FlattenUnmatched(tc2, name2, level, 0);
      ---------------------------------------*/

      level--;
   }

   /* Add the topmost cells to the end of the compare queue */

   newcomp = (struct Correspond *)CALLOC(1, sizeof(struct Correspond));
   newcomp->next = NULL;
   newcomp->class1 = tc1->name;
   newcomp->file1 = tc1->file;
   newcomp->class2 = tc2->name;
   newcomp->file2 = tc2->file;

   if (CompareQueue == NULL)
      CompareQueue = newcomp;
   else {
      for (scomp = CompareQueue; scomp->next; scomp = scomp->next);
      scomp->next = newcomp;
   }

   tc1->flags |= CELL_MATCHED;
   tc2->flags |= CELL_MATCHED;

   return 0;
}

/*----------------------------------------------*/
/* Read the top of the compare queue, but do	*/
/* not alter the stack.				*/
/*----------------------------------------------*/

int PeekCompareQueueTop(char **name1, int *file1, char **name2, int *file2)
{
   if (CompareQueue == NULL)
      return -1;

   *name1 = CompareQueue->class1;
   *file1 = CompareQueue->file1;
   *name2 = CompareQueue->class2;
   *file2 = CompareQueue->file2;

   return 0;
}

/*----------------------------------------------*/
/* Return the top of the compare queue stack,	*/
/* and pop the topmost entry.  Return 0 on	*/
/* success, -1 if the compare queue is empty.	*/
/*----------------------------------------------*/

int GetCompareQueueTop(char **name1, int *file1, char **name2, int *file2)
{
   struct Correspond *nextcomp;

   if (PeekCompareQueueTop(name1, file1, name2, file2) < 0)
      return -1;

   nextcomp = CompareQueue->next; 
   FREE(CompareQueue);
   CompareQueue = nextcomp;
   return 0;
}

/*--------------------------------------*/
/* Delete the comparison queue		*/
/*--------------------------------------*/

void RemoveCompareQueue()
{
   struct Correspond *comp, *nextcomp;

   for (comp = CompareQueue; comp != NULL;) {
      nextcomp = comp->next;
      FREE(comp);
      comp = nextcomp;
   }
   CompareQueue = NULL;
}

/*----------------------------------------------------------------------*/
/* Output a summary of the contents of the two circuits being compared	*/
/*----------------------------------------------------------------------*/

void DescribeContents(char *name1, int file1, char *name2, int file2)
{
    Fprintf(stdout, "\n");  // blank line before new circuit diagnostics in log file
    /* print preliminary statistics */
    Printf("\nContents of circuit 1:  ");
    DescribeInstance(name1, file1);
    Printf("Contents of circuit 2:  ");
    DescribeInstance(name2, file2);
    Printf("\n");
}

/*----------------------------------*/
/* Create an initial data structure */
/*----------------------------------*/

void CreateTwoLists(char *name1, int file1, char *name2, int file2, int dolist)
{
    struct Element *El1;
    struct Node *N1;
    struct nlist *tc1, *tc2, *tcf;
    int modified;

    ResetState();

    if (file1 == -1)
        tc1 = LookupCell(name1);
    else
        tc1 = LookupCellFile(name1, file1);

    if (file2 == -1)
        tc2 = LookupCell(name2);
    else
        tc2 = LookupCellFile(name2, file2);

    /* determine if matching will be case sensitive or case insensitive */
    matchfunc = match;
    matchintfunc = matchfile;
    hashfunc = hashcase;
    if (tc1 != NULL && tc2 != NULL) {
        if ((tc1->flags & CELL_NOCASE) && (tc2->flags & CELL_NOCASE)) {
	   matchfunc = matchnocase;
	   matchintfunc = matchfilenocase;
	   hashfunc = hashnocase;
        }
    }

    modified = CreateLists(name1, file1);
    if (Elements == NULL) {
       Printf("Circuit %s contains no devices.\n", name1);
       return;
    }
    if (Nodes == NULL) {
       Printf("Circuit %s contains no nets.\n", name1);
       return;
    }

    ElementClasses = GetElementClass();
    if (ElementClasses == NULL) {
       Fprintf(stderr, "Memory allocation error\n");
#ifdef DEBUG_ALLOC
       PrintCoreStats();
#endif
       ResetState();
       return;
    }
    ElementClasses->elements = Elements;
    Magic(ElementClasses->magic);

    for (El1 = Elements; El1->next != NULL; El1 = El1->next) {
       El1->elemclass = ElementClasses;
    }
    /* El1 now points to last element of list */

    NodeClasses = GetNodeClass();
    if (NodeClasses == NULL) {
       Fprintf(stderr,"Memory allocation error\n");
#ifdef DEBUG_ALLOC
       PrintCoreStats();
#endif
       ResetState();
       return;
    }
    NodeClasses->nodes = Nodes;
    Magic(NodeClasses->magic);

    for (N1 = Nodes; N1->next != NULL; N1 = N1->next) {
       N1->nodeclass = NodeClasses;
    }
    /* N1 now points to last element of list */


    modified += CreateLists(name2, file2);
    if (Elements == NULL) {
       Printf("Circuit %s contains no devices.\n", name2);
       ResetState();
       return;
    }

    if (Nodes == NULL) {
       Printf("Circuit %s contains no nets.\n", name2);
       ResetState();
       return;
    }

    if (modified > 0) {
       Printf("Circuit was modified by parallel/series device merging.\n");
       Printf("New circuit summary:\n\n");
       /* print preliminary statistics */
       Printf("Contents of circuit 1:  ");
       DescribeInstance(name1, file1);
       Printf("Contents of circuit 2:  ");
       DescribeInstance(name2, file2);
       Printf("\n");
    }

    /* splice new lists into existing lists */
    El1->next = Elements;
    for (El1 = Elements; El1->next != NULL; El1 = El1->next) {
       El1->elemclass = ElementClasses;
    }

    N1->next = Nodes;
    for (N1 = Nodes; N1->next != NULL; N1 = N1->next) {
       N1->nodeclass = NodeClasses;
    }

    /* print preliminary statistics */
    SummarizeDataStructures();
  
#ifdef TCL_NETGEN
    if (dolist) {
       Tcl_Obj *nlist;

       nlist = Tcl_NewListObj(0, NULL);
       Tcl_ListObjAppendElement(netgeninterp, nlist, Tcl_NewStringObj(name1, -1));
       Tcl_ListObjAppendElement(netgeninterp, nlist, Tcl_NewStringObj(name2, -1));

       Tcl_SetVar2Ex(netgeninterp, "lvs_out", NULL,
		Tcl_NewStringObj("name", -1),
		TCL_APPEND_VALUE | TCL_LIST_ELEMENT);
       Tcl_SetVar2Ex(netgeninterp, "lvs_out", NULL, nlist,
		TCL_APPEND_VALUE | TCL_LIST_ELEMENT);
    }
#endif

    /* perform first set of fractures */
    FirstElementPass(ElementClasses->elements, FALSE, dolist);

    FirstNodePass(NodeClasses->nodes, dolist);
    FractureElementClass(&ElementClasses);
    FractureNodeClass(&NodeClasses);
}

void RegroupDataStructures(void)
{
  struct ElementClass *EC;
  struct Element *El1, *Etail;
  struct NodeClass *NC;
  struct Node *N1, *Ntail;
  

  if (ElementClasses == NULL || NodeClasses == NULL) {
    Fprintf(stderr, "Need to initialize data structures first!\n");
    return;
  }

  Elements = Etail = NULL;
  for (EC = ElementClasses; EC != NULL; ) {
    struct ElementClass *ECnext;
    ECnext = EC->next;
    if (Elements == NULL) Elements = EC->elements;
    else Etail->next = EC->elements;
    for (El1= EC->elements; El1 != NULL && El1->next != NULL; El1 = El1->next);
    Etail = El1;
    FreeElementClass(EC);
    EC = ECnext;
  }

  ElementClasses = GetElementClass();
  if (ElementClasses == NULL) {
    Fprintf(stderr,"Memory allocation error\n");
#ifdef DEBUG_ALLOC
    PrintCoreStats();
#endif
    ResetState();
    return;
  }
  ElementClasses->elements = Elements;

  for (El1 = Elements; El1->next != NULL; El1 = El1->next) {
    El1->elemclass = ElementClasses;
  }
  /* El1 now points to last element of list */


  Nodes = Ntail = NULL;
  for (NC = NodeClasses; NC != NULL; ) {
    struct NodeClass *NCnext;
    NCnext = NC->next;
    if (Nodes == NULL) Nodes = NC->nodes;
    else Ntail->next = NC->nodes;
    for (N1 = NC->nodes; N1 != NULL && N1->next != NULL; N1 = N1->next);
    Ntail = N1;
    FreeNodeClass(NC);
    NC = NCnext;
  }


  NodeClasses = GetNodeClass();
  if (NodeClasses == NULL) {
    Fprintf(stderr,"Memory allocation error\n");
#ifdef DEBUG_ALLOC
    PrintCoreStats();
#endif
    ResetState();
    return;
  }
  NodeClasses->nodes = Nodes;

  for (N1 = Nodes; N1->next != NULL; N1 = N1->next) {
    N1->nodeclass = NodeClasses;
  }

  /* reset magic numbers */
  NewNumberOfEclasses = OldNumberOfEclasses = 0;
  NewNumberOfNclasses = OldNumberOfNclasses = 0;
  Iterations = 0;

  /* perform first set of fractures */
  FirstElementPass(ElementClasses->elements, TRUE, 0);
  FirstNodePass(NodeClasses->nodes, 0);
  FractureElementClass(&ElementClasses);
  FractureNodeClass(&NodeClasses);
}

unsigned long ElementHash(struct Element *E)
{
  struct NodeList *N;
  unsigned long hashval = 0;
  
  for (N = E->nodelist; N != NULL; N = N->next)
     if (N->node != NULL)
        hashval += (N->pin_magic ^ N->node->nodeclass->magic);

  // Added by Tim 10/10/2012.  Cannot ignore own hashval, or else
  // two instances of different cells can be swapped and although
  // the netlist will report as not matching, the illegal partition
  // will be recast as legal, and no information will be generated
  // about the mismatched elements.
  hashval ^= E->hashval;

  return(hashval);
}

unsigned long NodeHash(struct Node *N)
{
  struct ElementList *E;
  unsigned long hashval = 0;
  
  for (E = N->elementlist; E != NULL; E = E->next)
     hashval += (E->subelement->pin_magic ^
		E->subelement->element->hashval ^	// Added by Tim
		E->subelement->element->elemclass->magic);

  return(hashval);
}

int Iterate(void)
/* does one iteration, and returns TRUE if we are done */
{
  int notdone;
  struct ElementClass *EC;
  struct NodeClass *NC;

  if (ElementClasses == NULL || NodeClasses == NULL) {
    Fprintf(stderr, "Need to initialize data structures first!\n");
    return(1);
  }

  for (EC = ElementClasses; EC != NULL; EC = EC->next) 
    Magic(EC->magic);
  for (NC = NodeClasses; NC != NULL; NC = NC->next) 
    Magic(NC->magic);

  Iterations++;
  NewFracturesMade = 0;
  
  for (EC = ElementClasses; EC != NULL; EC = EC->next) {
    struct Element *E;
    for (E = EC->elements; E != NULL; E = E->next)
      E->hashval = ElementHash(E);

    // Check for partitions of two elements, not balanced
    if (EC->count == 2 && EC->elements->graph ==
		EC->elements->next->graph)
       EC->legalpartition = 0;
  }

  notdone = FractureElementClass(&ElementClasses);

  for (NC = NodeClasses; NC != NULL; NC = NC->next) {
    struct Node *N;
    for (N = NC->nodes; N != NULL; N = N->next)
      N->hashval = NodeHash(N);

    // Check for partitions of two nodes, not balanced
    if (NC->count == 2 && NC->nodes->graph ==
		NC->nodes->next->graph)
       NC->legalpartition = 0;
  }
  notdone = notdone | FractureNodeClass(&NodeClasses);


#if 0
  if (NewFracturesMade) Printf("New fractures made;   ");
  else Printf("No new fractures made; ");
  if (BadMatchDetected) Printf("Matching error has been detected.");
  if (PropertyErrorDetected) Printf("Property error has been detected.");
  Printf("\n");
#endif

  return(!notdone);
}

/*--------------------------------------------------------------*/
/* Combine properties of ob1 starting at property idx1 up to	*/
/* property (idx1 + run1), where devices match critical series	*/
/* values and can be combined by summing over the "S" record.	*/
/*--------------------------------------------------------------*/

int series_optimize(struct objlist *ob1, struct nlist *tp1, int idx1,
	int run1, int comb)
{
   struct objlist *obn;
   int i;

   obn = ob1;
   for (i = 0; i < idx1; i++) obn = obn->next;
   return PropertyOptimize(obn, tp1, run1, TRUE, comb);
}

typedef struct _propsort {
    double value;   /* Primary sorting value */
    double avalue;  /* Secondary sorting value */
    double slop;    /* Delta for accepting equality */
    int idx;
    unsigned char flags;
    struct objlist *ob;
} propsort;

/*--------------------------------------------------------------*/
/* Property sorting routine used by qsort().  Sorts on "value"	*/
/* unless the "value" for the two entries differs by less than	*/
/* "slop", in which case the entries are sorted on "avalue".	*/
/*--------------------------------------------------------------*/

static int compsort(const void *p1, const void *p2)
{
    propsort *s1, *s2;
    double smax;

    s1 = (propsort *)p1;
    s2 = (propsort *)p2;

    smax = fmax(s1->slop, s2->slop);
    if (fabs(s1->value - s2->value) <= smax)
       return (s1->avalue > s2->avalue) ? 1 : 0;
    else
       return (s1->value > s2->value) ? 1 : 0;
}

/*--------------------------------------------------------------*/
/* Sort properties of ob1 starting at property idx1 up to	*/
/* property (idx1 + run).  Use series critical property for	*/
/* sorting.  Combine properties by S before sort.  Note that	*/
/* use of "S" implies that the devices are the same in all	*/
/* properties.							*/
/* ob1 is the record before the first property.			*/
/*--------------------------------------------------------------*/

void series_sort(struct objlist *ob1, struct nlist *tp1, int idx1, int run)
{
   struct objlist *obn, *obp;
   propsort *proplist;
   struct property *kl;
   struct valuelist *vl, *sl;
   int i, p, sval, merge_type;
   double cval, slop;

   obn = ob1->next;
   for (i = 0; i < idx1; i++) obn = obn->next;

   // Create a structure of length (run) to hold property value
   // and index.  Then sort that list, then use the sorted
   // indexes to sort the actual property linked list.

   proplist = (propsort *)MALLOC(run * sizeof(propsort));

   obp = obn;
   sval = 1;
   cval = slop = 0.0;
   for (i = 0; i < run; i++) {
      sl = NULL;
      merge_type = MERGE_NONE;
      for (p = 0;; p++) {
	 vl = &(obp->instance.props[p]);
	 if (vl->type == PROP_ENDLIST) break;
	 if (vl->key == NULL) continue;
         if ((*matchfunc)(vl->key, "S")) {
	    sval = vl->value.ival;
	    sl = vl;
	 }
	 else {
	    kl = (struct property *)HashLookup(vl->key, &(tp1->propdict));
	    if (kl && (kl->merge & (MERGE_S_ADD | MERGE_S_PAR))) {
		if (vl->type == PROP_INTEGER) {
		   cval = (double)vl->value.ival;
		   slop = (double)kl->slop.ival;
		}
		else {
		   cval = vl->value.dval;
		   slop = kl->slop.dval;
		}
	 	merge_type = kl->merge & (MERGE_S_ADD | MERGE_S_PAR);
	    }
	 }
      }
      if (merge_type == MERGE_S_ADD) {
	 proplist[i].value = cval * (double)sval;
         proplist[i].slop = slop;
         proplist[i].avalue = 0;
	 if (sl) sl->value.ival = 1;
      }
      else if (merge_type == MERGE_S_PAR) {
	 proplist[i].value = cval / (double)sval;
         proplist[i].slop = slop;
         proplist[i].avalue = 0;
	 if (sl) sl->value.ival = 1;
      }
      else {
	 /* Components which declare no series addition method stay unsorted */
	 proplist[i].value = (double)0;
	 proplist[i].avalue = (double)0;
         proplist[i].slop = (double)1E-6;
      }
      proplist[i].idx = i;
      proplist[i].ob = obp;
      obp = obp->next;
   }
   obn = obp;	/* Link from last property */

   qsort(&proplist[0], run, sizeof(propsort), compsort);

   // Re-sort list
   obp = ob1;
   for (i = 0; i < run; i++) {
      obp->next = proplist[i].ob;
      obp = obp->next;
   }
   obp->next = obn;	/* Restore last link */

   // In series runs, all records after the first start with tag "+".
   // If the tag got moved to the first record, then move it down to
   // the record that is missing the tag.

   obn = ob1->next;
   if (!strcmp(obn->instance.props[0].key, "_tag")) {
      char *tmpkey;
      struct valuelist *kv2;
      int l;

      if (!strcmp(obn->instance.props[0].value.string, "+")) {
	 /* Remove this property tag "+" */
	 FREE(obn->instance.props[0].key);
	 FREE(obn->instance.props[0].value.string);
	 for (p = 0;; p++) {
	    obn->instance.props[p].key = obn->instance.props[p + 1].key;
	    obn->instance.props[p].type = obn->instance.props[p + 1].type;
	    obn->instance.props[p].value = obn->instance.props[p + 1].value;
	    if (obn->instance.props[p].type == PROP_ENDLIST) break;
	 }
	 for (i = 1; i < run; i++) {
	    obn = obn->next;
	    if (strcmp(obn->instance.props[0].key, "_tag")) {
	       for (p = 1;; p++) 
		  if (obn->instance.props[p].type == PROP_ENDLIST) break;

	       /* Create a new property record to hold all the existing	*/
	       /* properties plus the tag at the beginning.		*/

	       kv2 = (struct valuelist *)MALLOC((p + 2) * sizeof(struct valuelist));
	       kv2->key = strsave("_tag");
	       kv2->type = PROP_STRING;
	       kv2->value.string = (char *)MALLOC(2);
	       kv2->value.string[0] = '+';
	       kv2->value.string[1] = '\0';
	       for (l = 0; l <= p; l++)
		  kv2[l + 1] = obn->instance.props[l];
	       FREE(obn->instance.props);
	       obn->instance.props = kv2;
	       break;
	    }
	 }
      }
   }

   FREE(proplist);
}

/*--------------------------------------------------------------*/
/* Combine properties of ob1 starting at property idx1 up to	*/
/* property (idx1 + run1), where devices match critical 	*/
/* parallel values and can be combined by summing over the "M"	*/
/* record.							*/
/*--------------------------------------------------------------*/

int parallel_optimize(struct objlist *ob1, struct nlist *tp1, int idx1,
	int run1, int comb)
{
   struct objlist *obn;
   int i;

   obn = ob1;
   for (i = 0; i < idx1; i++) obn = obn->next;
   return PropertyOptimize(obn, tp1, run1, FALSE, comb);
}

/*--------------------------------------------------------------*/
/* Sort properties of ob1 starting at property idx1 up to	*/
/* property (idx1 + run).  Use parallel critical property for	*/
/* sorting.  Multiply critical property by M before sort.	*/
/* ob1 is the record before the first property.			*/
/*--------------------------------------------------------------*/

void parallel_sort(struct objlist *ob1, struct nlist *tp1, int idx1, int run)
{
   struct objlist *obn, *obp;
   propsort *proplist;
   struct property *kl;
   struct valuelist *vl;
   int i, p, mval, merge_type;
   int has_crit;
   char ca, co;
   double tval, tslop;
   double aval, pval, oval, aslop, pslop;

   obn = ob1->next;
   for (i = 0; i < idx1; i++) obn = obn->next;

   /* Create a structure of length (run) to hold critical property
    * value and index.  Then sort that list, then use the sorted
    * indexes to sort the actual property linked list.
    *
    * Collect properties of interest that we can potentially sort on.
    * That includes the M value, critical parameter, additive value(s),
    * and finally any parameter.  Keep these values and their slop.
    * If there is a critical value, then sort on additive values * M.
    * If not, then sort on M and additive values.
    */

   proplist = (propsort *)MALLOC(run * sizeof(propsort));

   obp = obn;
   mval = 1;
   pval = aval = oval = 0.0;
   for (i = 0; i < run; i++) {
      has_crit = FALSE;
      merge_type = MERGE_NONE;
      ca = co = (char)0;

      for (p = 0;; p++) {
	 vl = &(obp->instance.props[p]);
	 if (vl->type == PROP_ENDLIST) break;
	 if (vl->key == NULL) continue;
         if ((*matchfunc)(vl->key, "M")) {
	    mval = vl->value.ival;
	    continue;
	 }
         kl = (struct property *)HashLookup(vl->key, &(tp1->propdict));
	 if (kl == NULL) continue;		/* Ignored property */

	 /* Get the property value and slop.  Promote if needed.  Save	*/
	 /* property and slop as type double so they can be sorted.	*/

	 if ((vl->type == PROP_STRING || vl->type == PROP_EXPRESSION) &&
		  	(kl->type != vl->type))
	    PromoteProperty(kl, vl, obp, tp1);
	 if (vl->type == PROP_INTEGER) {
	    tval = (double)vl->value.ival;
	    tslop = (double)kl->slop.ival;
	 }
	 else if (vl->type == PROP_STRING) {
	    /* This is unlikely---no method to merge string properties! */
	    tval = (double)vl->value.string[0]
			+ (double)vl->value.string[1] / 10.0;
	    tslop = (double)0;
	 }
	 else {
	    tval = vl->value.dval;
	    tslop = kl->slop.dval;
	 }

	 if (kl->merge & MERGE_P_CRIT) {
	    has_crit = TRUE;
	    pval = tval;
	    pslop = tslop;
	 }
	 else if (kl->merge & (MERGE_P_ADD | MERGE_P_PAR)) {
	    if ((ca == (char)0) || (toupper(vl->key[0]) > ca)) {
	       merge_type = kl->merge & (MERGE_P_ADD | MERGE_P_PAR);
	       aval = tval;
	       aslop = tslop;
	       ca = toupper(vl->key[0]);
	    }
	 }
	 else if ((co == (char)0) || (toupper(vl->key[0]) > co)) {
	    oval = tval;
	    co = toupper(vl->key[0]);
	 }
      }
      if (has_crit == TRUE) {
	 /* If there is a critical value, then sort first   */
	 /* by critical value				    */
	 proplist[i].value = pval;
	 proplist[i].slop = pslop;

	 /* then sort on additive value times M	*/
	 /* or on non-additive value.		*/
         if (merge_type == MERGE_P_ADD)
	    proplist[i].avalue = aval * (double)mval;
         else if (merge_type == MERGE_P_PAR)
	    proplist[i].avalue = aval / (double)mval;
	 else
	    proplist[i].avalue = (double)mval;
      }
      else {
         if (merge_type != MERGE_NONE) {
	    proplist[i].value = aval;
	    proplist[i].slop = aslop;
	    proplist[i].avalue = (double)mval;
	 }
	 else {
	    proplist[i].value = (double)mval;
	    proplist[i].slop = (double)0;
	    proplist[i].avalue = oval;
	 }
      }
      proplist[i].idx = i;
      proplist[i].ob = obp;
      obp = obp->next;
   }

   obn = obp;	/* Link from last property */

   qsort(&proplist[0], run, sizeof(propsort), compsort);

   // Re-sort list
   obp = ob1;
   for (i = 0; i < run; i++) {
      obp->next = proplist[i].ob;
      obp = obp->next;
   }
   obp->next = obn;	/* Restore last link */

   FREE(proplist);
}

/*--------------------------------------------------------------*/
/* Attempt to match two property lists representing series/	*/
/* parallel combinations of devices.  Where the number of 	*/
/* devices is not equal, try to reduce the one with more	*/
/* devices to match.  If there are the same number of parallel	*/
/* or series devices, check if they match better by swapping.	*/
/* The goal is to get two property lists that can be checked by	*/
/* 1-to-1 matching in PropertyMatch().				*/
/*--------------------------------------------------------------*/

void PropertySortAndCombine(struct objlist *pre1, struct nlist *tp1,
		struct objlist *pre2, struct nlist *tp2)
{
   struct objlist *obn, *obp;
   struct objlist *ob1, *ob2;

   int p, n;
   int run, cnt, idx1, idx2, max1, max2;
   int icount1, icount2, changed, result;
   char *netwk1, *netwk2;
   char *c1, *c2;
   struct valuelist *vl;
   int iterations = 0;

   ob1 = pre1->next;
   ob2 = pre2->next;

   changed = 1;
   while (changed) {
      iterations++;
      changed = 0;

      /* Remove group tags if they no longer contain series devices */
      while (remove_group_tags(pre1));
      while (remove_group_tags(pre2));

      // How many property records are there?
      // If there is only one property record in each instance then
      // there is nothing to be sorted.
      icount1 = 0;
      for (obn = ob1; obn && obn->type == PROPERTY; obn = obn->next) icount1++;
      icount2 = 0;
      for (obn = ob2; obn && obn->type == PROPERTY; obn = obn->next) icount2++;
      if (icount1 < 2 && icount2 < 2) {
	 /* Printf("Networks have been reduced to 1 device each; "
			"optimization done.\n"); */
	 return;
      }

      // Construct a string of characters representing the networks
      // of the two devices.  First determine the size of each, then
      // allocate and fill.

      n = 1;
      for (obn = ob1; obn && obn->type == PROPERTY; obn = obn->next) {
         n++;
         for (p = 0;; p++) {
	    vl = &(obn->instance.props[p]);
	    if (vl->type == PROP_ENDLIST) break;
	    if (vl->key == NULL) continue;
            if (!strcmp(vl->key, "_tag")) n += strlen(vl->value.string);
         }
      }
      netwk1 = (char *)MALLOC(n);
      netwk1[0] = '\0';
      for (obn = ob1; obn && obn->type == PROPERTY; obn = obn->next) {
         for (p = 0;; p++) {
	    vl = &(obn->instance.props[p]);
	    if (vl->type == PROP_ENDLIST) break;
	    if (vl->key == NULL) continue;
            if (!strcmp(vl->key, "_tag")) strcat(netwk1, vl->value.string);
         }
         strcat(netwk1, "D");
      }
      n = 1;
      for (obn = ob2; obn && obn->type == PROPERTY; obn = obn->next) {
         n++;
         for (p = 0;; p++) {
	    vl = &(obn->instance.props[p]);
	    if (vl->type == PROP_ENDLIST) break;
	    if (vl->key == NULL) continue;
            if (!strcmp(vl->key, "_tag")) n += strlen(vl->value.string);
         }
      }
      netwk2 = (char *)MALLOC(n);
      netwk2[0] = '\0';
      for (obn = ob2; obn && obn->type == PROPERTY; obn = obn->next) {
         for (p = 0;; p++) {
	    vl = &(obn->instance.props[p]);
	    if (vl->type == PROP_ENDLIST) break;
	    if (vl->key == NULL) continue;
            if (!strcmp(vl->key, "_tag")) strcat(netwk2, vl->value.string);
         }
         strcat(netwk2, "D");
      }

      /* Printf("Diagnostic: network1 is \"%s\"  "
      		"network2 is \"%s\"\n", netwk1, netwk2); */

      /* Method to resolve any network to the largest solution that	   */
      /* matches both sides.  Use the netwk1, netwk2 strings to determine  */
      /* and manage the topology.  Note that at this point devices have	   */
      /* already been combined if all critical properties match, so any	   */
      /* parallel devices remaining are not considered mergeable unless as */
      /* a last resort.  Parallel devices need to be checked for swapping, */
      /* however.  So the steps are:					   */
      /* 1) Find parallel devices with more elements in one circuit than   */
      /*    in the other.  If non-summing parameters of interest match,	   */
      /*    then merge all devices that can be merged until both sides	   */
      /*    have the same number of devices.				   */
      /* 2) Find series devices that have more elements in one circuit	   */
      /*    than in the other.  If non-summing parameters of interest	   */
      /*    match, then merge all devices that can be merged until	   */
      /*    both sides have the same number of devices.			   */
      /* 3) Find parallel devices that have the same number in both 	   */
      /*    circuits.  Check if critical parameters match between the 	   */
      /*    circuits.  If not, check if swapping devices in circuit1	   */
      /*    makes a better match to circuit2.			  	   */

      /* Case 1:  Parallel devices with more elements in one circuit	*/

      /* Find the largest group of parallel devices in circuit1 */
      run = 0;
      cnt = 0;
      idx1 = 0;
      max1 = 0;
      for (c1 = netwk1; ; c1++) {
         if (*c1 == 'D') {
	    run++;
	    cnt++;
         }
         else {
            if (run > max1) {
               max1 = run;
	       idx1 = cnt - run;
            }
            run = 0;
         }
         if (*c1 == '\0') break;
      }

      /* Find the largest group of parallel devices in circuit2 */
      run = 0;
      cnt = 0;
      idx2 = 0;
      max2 = 0;
      for (c2 = netwk2; ; c2++) {
         if (*c2 == 'D') {
 	    run++;
	    cnt++;
         }
         else {
            if (run > max2) {
               max2 = run;
	       idx2 = cnt - run;
            }
            run = 0;
         }
         if (*c2 == '\0') break;
      }

      // if (max1 != max2)
      //    Printf("Circuit 1 has %d devices in parallel while circuit 2 has %d\n",
      //		(max1 == 1) ? 0 : max1, (max2 == 1) ? 0 : max2);

      if (max1 > 1) {
	 result = parallel_optimize(ob1, tp1, idx1, max1, FALSE);
	 if (result > 0) changed += result;
	 else if ((result < 0) && (max1 > max2))
	    changed += series_optimize(ob1, tp1, idx1, max1, TRUE);
      }
    
      if (max2 > 1) {
	 result = parallel_optimize(ob2, tp2, idx2, max2, FALSE);
	 if (result > 0) changed += result;
	 else if ((result < 0) && (max2 > max1))
	    changed += series_optimize(ob2, tp2, idx2, max2, TRUE);
      }

      if (changed > 0) {
         FREE(netwk1);
         FREE(netwk2);
         continue;
      }

      if (max1 > 1) {
	 parallel_sort(pre1, tp1, idx1, max1);
         /* Re-link first property, because it may have been moved */
         ob1 = pre1->next;
      }
      if (max2 > 1) {
	 parallel_sort(pre2, tp2, idx2, max2);
         /* Re-link first property, because it may have been moved */
	 ob2 = pre2->next;
      }

      /* Case 2:  Series devices with more elements in one circuit */

      /* Find the largest group of series devices in circuit1 */
      run = 0;
      cnt = 0;
      idx1 = 0;
      max1 = 0;
      for (c1 = netwk1; ; c1++) {
         if (*c1 == 'D') {
            if (run == 0) run++;
	    cnt++;
	    if (*(c1 + 1) == '+' && *(c1 + 2) == 'D') {
	       run++;
               c1++;
	    }
         }
         else {
            if (run > max1) {
               max1 = run;
	       idx1 = cnt - run;
            }
            run = 0;
         }
         if (*c1 == '\0') break;
      }

      /* Find the largest group of series devices in circuit2 */
      run = 0;
      cnt = 0;
      idx2 = 0;
      max2 = 0;
      for (c2 = netwk2; ; c2++) {
         if (*c2 == 'D') {
            if (run == 0) run++;
	    cnt++;
	    if (*(c2 + 1) == '+' && *(c2 + 2) == 'D') {
	       run++;
               c2++;
	    }
         }
         else {
            if (run > max2) {
               max2 = run;
	       idx2 = cnt - run;
            }
            run = 0;
         }
         if (*c2 == '\0') break;
      }

      // if (max1 != max2)
      //    Printf("Circuit 1 has %d devices in series while circuit 2 has %d\n",
      //		(max1 == 1) ? 0 : max1, (max2 == 1) ? 0 : max2);

      if (max1 > 1) {
	 result = series_optimize(ob1, tp1, idx1, max1, FALSE);
	 if (result > 0) changed += result;
	 else if ((result < 0) && (max1 > max2))
	    changed += parallel_optimize(ob1, tp1, idx1, max1, TRUE);
      }
      if (max2 > 1) {
	 result = series_optimize(ob2, tp2, idx2, max2, FALSE);
	 if (result > 0) changed += result;
	 else if ((result < 0) && (max2 > max1))
	    changed += parallel_optimize(ob2, tp2, idx2, max2, TRUE);
      }

      if (changed > 0) {

         FREE(netwk1);
         FREE(netwk2);
         continue;
      }

      if (max1 > 1) {
         /* Re-link first property, because it may have been moved */
	 series_sort(pre1, tp1, idx1, max1);
         ob1 = pre1->next;
      }
      if (max2 > 1) {
         /* Re-link first property, because it may have been moved */
         series_sort(pre2, tp2, idx2, max2);
         ob2 = pre2->next;
      }

      FREE(netwk1);
      FREE(netwk2);

      /* Continue looping until there are no further changes to be made */
   }
   if (iterations > 1)
      Printf("No more changes can be made to series/parallel networks.\n");
}

/*--------------------------------------------------------------*/
/* "ob" points to the first property record of an object	*/
/* instance.  Check if there are multiple property records.  If	*/
/* so, group them by same properties of interest, order them by	*/
/* critical property (if defined), and merge devices with the	*/
/* same properties (by summing property "M" for devices)	*/
/*								*/
/* For final optimization, if comb == 1 and M > 1, then merge	*/
/* the critical property over M and set M to 1.			*/
/*								*/
/* Return the number of devices modified.			*/
/*--------------------------------------------------------------*/

typedef struct _proplink *proplinkptr;
typedef struct _proplink {
   struct property *prop;
   proplinkptr next;
} proplink;

int PropertyOptimize(struct objlist *ob, struct nlist *tp, int run, int series,
	int comb)
{
   struct objlist *ob2, *obt;
   struct property *kl, *m_rec, **plist;
   unsigned char **clist;
   struct valuelist ***vlist, *vl, *vl2, *newvlist, critval;
   proplinkptr plink, ptop;
   int pcount, p, i, j, k, pmatch, ival, ctype;
   double dval;
   static struct valuelist nullvl, dfltvl;
   char multiple[2], other[2];
   int changed = 0, fail = 0;

   multiple[1] = '\0';
   multiple[0] = (series == TRUE) ? 'S' : 'M';
   other[1] = '\0';
   other[0] = (series == TRUE) ? 'M' : 'S';

   nullvl.type = PROP_INTEGER;
   nullvl.value.ival = 0;

   if (run == 0) return 0;	// Sanity check.

   // Look through master cell property list and create
   // an array of properties of interest to fill in order.

   m_rec = NULL;
   ptop = NULL;
   pcount = 1;
   kl = (struct property *)HashFirst(&(tp->propdict));
   while (kl != NULL) {
      // Make a linked list so we don't have to iterate through the hash again 
      plink = (proplinkptr)MALLOC(sizeof(proplink));
      plink->prop = kl;
      plink->next = ptop;
      ptop = plink;
      if ((*matchfunc)(kl->key, multiple)) {
	 kl->idx = 0;
	 m_rec = kl;
      }
      else
	 kl->idx = pcount++;

      kl = (struct property *)HashNext(&(tp->propdict));
   }
   // Recast the linked list as an array
   plist = (struct property **)CALLOC(pcount, sizeof(struct property *));
   vlist = (struct valuelist ***)CALLOC(pcount, sizeof(struct valuelist **));
   clist = (unsigned char **)CALLOC(pcount, sizeof(unsigned char *));
   if (m_rec == NULL) {
      vlist[0] = (struct valuelist **)CALLOC(run, sizeof(struct valuelist *));
      clist[0] = (unsigned char *)CALLOC(run, sizeof(unsigned char));
   }

   while (ptop != NULL) {
      plist[ptop->prop->idx] = ptop->prop;
      vlist[ptop->prop->idx] = (struct valuelist **)CALLOC(run,
		sizeof(struct valuelist *));
      clist[ptop->prop->idx] = (unsigned char *)CALLOC(run, sizeof(unsigned char));
      plink = ptop;
      FREE(ptop);
      ptop = plink->next;
   }
   
   // Now, for each property record, sort the properties of interest
   // so that they are all in order.  Property "M" ("S") goes in position
   // zero.

   i = 0;
   for (ob2 = ob; ob2 && ob2->type == PROPERTY; ob2 = ob2->next) {
      for (p = 0;; p++) {
	 vl = &(ob2->instance.props[p]);
	 if (vl->type == PROP_ENDLIST) break;
	 if (vl->key == NULL) continue;
	 kl = (struct property *)HashLookup(vl->key, &(tp->propdict));
	 if (kl == NULL && m_rec == NULL) {
	    if ((*matchfunc)(vl->key, multiple)) {
	       vlist[0][i] = vl;
	    }
	 }
	 if (kl == NULL) {
	    /* Prevent setting both M > 1 and S > 1 in any one	*/
	    /* device, as it is ambiguous.			*/

	    if ((*matchfunc)(vl->key, other)) {
	       if (vl->type == PROP_INTEGER)
		  if (vl->value.ival > 1)
		     fail = 1;
	    }
	 }
 	 else if (kl != NULL) {
	    vlist[kl->idx][i] = vl;
	    if (series == FALSE) {
		if (kl->merge & (MERGE_P_ADD | MERGE_P_PAR | MERGE_P_CRIT))
		    clist[kl->idx][i] = kl->merge &
			    (MERGE_P_ADD | MERGE_P_PAR | MERGE_P_CRIT);
	    }
	    else if (series == TRUE) {
		if (kl->merge & (MERGE_S_ADD | MERGE_S_PAR | MERGE_S_CRIT))
		    clist[kl->idx][i] = kl->merge &
			    (MERGE_S_ADD | MERGE_S_PAR | MERGE_S_CRIT);
	    }
	 }
      }
      if (++i == run) break;
   }

   // Check for "M" ("S") records with type double and promote them to integer
   for (i = 0; i < run; i++) {
      vl = vlist[0][i];
      if (vl != NULL) {
         if (vl->type == PROP_DOUBLE) {
            vl->type = PROP_INTEGER;
	    vl->value.ival = (int)(vl->value.dval + 0.5);
         }
      }
   }

   // Now combine records with same properties by summing M (S).
   if (comb == FALSE) {
      for (i = 0; i < run - 1; i++) {
	 int nr_empty = 0;
	 for (j = i + 1; j < run; j++) {
	    pmatch = 0;
	    for (p = 1; p < pcount; p++) {
	       kl = plist[p];
	       vl = vlist[p][i];
	       vl2 = vlist[p][j];
	       if (vl == NULL && vl2 == NULL) {
		  pmatch++;
		  continue;
	       }

	       // If either value is missing, it takes kl->pdefault
	       // and must apply promotions if necessary.

	       else if (vl == NULL || vl2 == NULL) {
		  if (vl == NULL) {
		     if (kl->type != vlist[p][j]->type)
			PromoteProperty(kl, vl2, ob2, tp);
		     vl = &dfltvl;
		  }
		  else {
		     if (kl->type != vlist[p][i]->type)
			PromoteProperty(kl, vl, ob2, tp);
		     vl2 = &dfltvl;
		  }
		  dfltvl.type = kl->type;
		  switch (kl->type) {
		     case PROP_STRING:
			dfltvl.value.string = kl->pdefault.string;
			break;
		     case PROP_INTEGER:
			dfltvl.value.ival = kl->pdefault.ival;
			break;
		     case PROP_DOUBLE:
		     case PROP_VALUE:
			dfltvl.value.ival = kl->pdefault.ival;
			break;
		     case PROP_EXPRESSION:
			dfltvl.value.stack = kl->pdefault.stack;
			break;
		  }
	       }

	       switch(vl->type) {
		  case PROP_DOUBLE:
		  case PROP_VALUE:
		     dval = 2 * fabs(vl->value.dval - vl2->value.dval)
				/ (vl->value.dval + vl2->value.dval);
		     if (dval <= kl->slop.dval) pmatch++;
		     break;
		  case PROP_INTEGER:
		     ival = abs(vl->value.ival - vl2->value.ival);
		     if (ival <= kl->slop.ival) pmatch++; 
		     break;
		  case PROP_STRING:
		     if ((*matchfunc)(vl->value.string, vl2->value.string)) pmatch++;
		     break;

		  /* will not attempt to match expressions, but it could
		   * be done with some minor effort by matching each
		   * stack token and comparing those that are strings.
		   */
	       }
	    }
	    if (fail == 1) {
	       /* If failure due to need to prevent M > 1 and S > 1 on 	*/
	       /* the same device, then do not do optimization.  If	*/
	       /* optimization could have been done, return -1.		*/

	       if (pmatch == (pcount - 1))
		  changed = -1;
	    }
	    else if (pmatch == (pcount - 1)) {
	       // Sum M (S) (p == 0) records and remove one record
	       if (vlist[0][i] == NULL) {
		  // Add this to the end of the property record
		  // find ith record in ob
		  p = 0;
		  for (ob2 = ob; p != i; ob2 = ob2->next, p++);
		  // Count entries, add one, reallocate
		  for (p = 0;; p++) {
		     vl = &ob2->instance.props[p];
		     if (vl->type == PROP_ENDLIST) break;
		  }
		  p++;
		  newvlist = (struct valuelist *)CALLOC(p + 1,
				sizeof(struct valuelist));
		  // Move end record forward
	  	  vl = &newvlist[p];
	  	  vl->key = NULL;
	  	  vl->type = PROP_ENDLIST;
	  	  vl->value.ival = 0;

	  	  // Add "M" ("S") record behind it
	  	  vl = &newvlist[--p];
	  	  vl->key = strsave(multiple);
	  	  vl->type = PROP_INTEGER;
	  	  vl->value.ival = 1;
	  	  vlist[0][i] = vl;
	  	  // Copy the rest of the records and regenerate vlist
	  	  for (--p; p >= 0; p--) {
		     vl = &newvlist[p];
		     vl->key = ob2->instance.props[p].key;
		     vl->type = ob2->instance.props[p].type;
		     vl->value = ob2->instance.props[p].value;

		     kl = (struct property *)HashLookup(vl->key, &(tp->propdict));
		     if (kl != NULL) vlist[kl->idx][i] = vl;
		  }

		  // Replace instance properties with the new list
		  FREE(ob2->instance.props);
		  ob2->instance.props = newvlist;
	       }

	       if (vlist[0][j] == NULL) {
		  vlist[0][j] = &nullvl;	// Mark this position
		  vlist[0][i]->value.ival++;
	       }
	       else if (vlist[0][j]->value.ival > 0) {
		  vlist[0][i]->value.ival += vlist[0][j]->value.ival;
		  vlist[0][j]->value.ival = 0;
	       }
	       else
		  nr_empty++;
	    }
	 }
	 // If everything from i to the end of the run has been matched
	 // and zeroed out, then nothing more can be merged.
	 if (nr_empty == (run - (i + 1)))
	    break;
      }
   }

   // If comb == TRUE, reduce M (or S) to 1 by merging additive properties
   // (if any)

   if (comb == TRUE) {
      int mult, cidx = -1;
      struct valuelist *avl, *cvl = NULL;
      critval.type = PROP_ENDLIST;
      critval.value.dval = 0.0;
      for (i = 0; i < run; i++) {
         avl = NULL;
	 if (vlist[0][i] == NULL) continue;
	 mult = vlist[0][i]->value.ival;
	 changed = 0;

	 /* For all properties that are not M, S, or crit,		*/
	 /* combine as specified by the merge type of the property.	*/

	 for (p = 1; p < pcount; p++) {
	    vl = vlist[p][i];
	    ctype = clist[p][i];

	    /* critical properties never combine, but track them */
	    if ((series == TRUE) && (ctype & MERGE_S_CRIT)) {
		if ((vl->type != critval.type) || (vl->value.dval != critval.value.dval))
		{
		    critval.type = vl->type;
		    critval.value = vl->value;
		    cidx = i;
		}
		continue;
	    }
	    if ((series == FALSE) && (ctype & MERGE_P_CRIT)) {
		if ((vl->type != critval.type) || (vl->value.dval != critval.value.dval))
		{
		    critval.type = vl->type;
		    critval.value = vl->value;
		    cidx = i;
		}
		continue;
	    }

	    if (mult > 1) {
		if (ctype & (MERGE_S_ADD | MERGE_P_ADD)) {
		    if (vl->type == PROP_INTEGER)
			vl->value.ival *= mult;
		    else if (vl->type == PROP_DOUBLE)
			vl->value.dval *= (double)mult;
		    vlist[0][i]->value.ival = 1;
		    changed += mult;
		}
		else if (ctype & (MERGE_S_PAR | MERGE_P_PAR)) {
		    /* Technically one should check if divide-by-mult */	
		    /* reduces the value to < 1 and promote to double */
		    /* if so, but that's a very unlikely case.	*/
		    if (vl->type == PROP_INTEGER)
			vl->value.ival /= mult;
		    else if (vl->type == PROP_DOUBLE)
			vl->value.dval /= (double)mult;
		    vlist[0][i]->value.ival = 1;
		    changed += mult;
		}
	    }
	    if (ctype & (MERGE_S_ADD | MERGE_P_ADD | MERGE_S_PAR | MERGE_P_PAR))
		avl = vl;
         }
	 if (cidx == i) cvl = avl;

	 /* Sorting should have put all records with the same critical	*/
	 /* value together sequentially.  So if there are still		*/
	 /* multiple property records, then merge them into the first	*/
	 /* record with the same critical property value.		*/

	 if ((i > 0) && (cidx >= 0) && (cidx < i)) {
	    for (p = 1; p < pcount; p++) {
		vl = vlist[p][i];
		ctype = clist[p][i];

		if (ctype & (MERGE_S_ADD | MERGE_P_ADD)) {
		    vlist[0][i]->value.ival = 0;	/* set M to 0 */
		    if (cvl && (cvl->type == PROP_INTEGER))
		    {
			if (vl->type == PROP_INTEGER)
			    cvl->value.ival += vl->value.ival;
			else {
			    cvl->type = PROP_DOUBLE;
			    cvl->value.dval = (double)cvl->value.ival + vl->value.dval;
			}
		    }
		    else if ((cvl && vl->type == PROP_DOUBLE))
		    {
			if (vl->type == PROP_INTEGER)
			    cvl->value.dval += (double)vl->value.ival;
			else
			    cvl->value.dval += vl->value.dval;
		    }
		}
		else if (ctype & (MERGE_S_PAR | MERGE_P_PAR)) {
		    vlist[0][i]->value.ival = 0;    /* set M to 0 */
		    /* To do parallel combination, both types need to
		     * be double, so recast them if they are integer.
		     */
		    if (vl->type == PROP_INTEGER) {
			vl->type = PROP_DOUBLE;
			vl->value.dval = (double)(vl->value.ival);
		    }
		    if (cvl && (cvl->type == PROP_INTEGER)) {
			cvl->type = PROP_DOUBLE;
			cvl->value.dval = (double)cvl->value.ival;
		    }
		    if ((cvl && (vl->type == PROP_DOUBLE))) {
		        cvl->value.dval =
				sqrt(cvl->value.dval * cvl->value.dval
				+ vl->value.dval * vl->value.dval);
		    }
		}
	    }
	 }
	 if (changed > 0) {
	    if (series)
		Printf("Combined %d series devices.\n", changed);
	    else
		Printf("Combined %d parallel devices.\n", changed);
	 }
      }
   }

   // Remove entries with M (S) = 0
   ob2 = ob;
   for (i = 1; i < run; i++) {
      vl = vlist[0][i];
      if (vl != NULL && vl->value.ival == 0) {
	 obt = ob2->next;
	 ob2->next = ob2->next->next;
	 FreeObjectAndHash(obt, tp);
         changed++;
      }
      else
	 ob2 = ob2->next;
   }

cleanup:

   // Cleanup memory allocation
   for (p = 0; p < pcount; p++) {
      kl = (struct property *)plist[p];
      if (kl) kl->idx = 0;
      FREE(vlist[p]);
      FREE(clist[p]);
   }
   FREE(plist);
   FREE(vlist);
   FREE(clist);

   return changed;
}

#ifdef TCL_NETGEN

/*--------------------------------------------------------------*/
/* Property list starts with a pair of instance names, followed	*/
/* by a list of corresponding but mismatched properties.	*/
/*--------------------------------------------------------------*/

Tcl_Obj *NewPropertyList(char *inst1, char *inst2)
{
   Tcl_Obj *proplist;
   Tcl_Obj *mpair, *instobj;

   mpair = Tcl_NewListObj(0, NULL);
   instobj = Tcl_NewStringObj(inst1, -1);
   Tcl_ListObjAppendElement(netgeninterp, mpair, instobj);
   instobj = Tcl_NewStringObj(inst2, -1);
   Tcl_ListObjAppendElement(netgeninterp, mpair, instobj);

   proplist = Tcl_NewListObj(0, NULL);
   Tcl_ListObjAppendElement(netgeninterp, proplist, mpair);
   return proplist;
}

/*--------------------------------------------------------------*/
/* Generate a Tcl list entry for a property mismatching pair	*/
/*--------------------------------------------------------------*/

Tcl_Obj *PropertyList(struct valuelist *vl1, struct valuelist *vl2)
{
   Tcl_Obj *mobj, *mpair, *propobj;

   mpair = Tcl_NewListObj(0, NULL);

   mobj = Tcl_NewListObj(0, NULL);
   if (vl1 == NULL)
      propobj = Tcl_NewStringObj("(no matching parameter)", -1);
   else
      propobj = Tcl_NewStringObj(vl1->key, -1);
   Tcl_ListObjAppendElement(netgeninterp, mobj, propobj);

   if (vl1 == NULL)
      propobj = Tcl_NewStringObj("(no value)", -1);
   else if (vl1->type == PROP_INTEGER)
      propobj = Tcl_NewIntObj(vl1->value.ival);
   else if (vl1->type == PROP_DOUBLE)
      propobj = Tcl_NewDoubleObj(vl1->value.dval);
   else if (vl1->type == PROP_STRING)
      propobj = Tcl_NewStringObj(vl1->value.string, -1);
   Tcl_ListObjAppendElement(netgeninterp, mobj, propobj);

   Tcl_ListObjAppendElement(netgeninterp, mpair, mobj);

   mobj = Tcl_NewListObj(0, NULL);
   if (vl2 == NULL)
      propobj = Tcl_NewStringObj("(no matching parameter)", -1);
   else
      propobj = Tcl_NewStringObj(vl2->key, -1);
   Tcl_ListObjAppendElement(netgeninterp, mobj, propobj);

   if (vl2 == NULL)
      propobj = Tcl_NewStringObj("(no value)", -1);
   else if (vl2->type == PROP_INTEGER)
      propobj = Tcl_NewIntObj(vl2->value.ival);
   else if (vl2->type == PROP_DOUBLE)
      propobj = Tcl_NewDoubleObj(vl2->value.dval);
   else if (vl2->type == PROP_STRING)
      propobj = Tcl_NewStringObj(vl2->value.string, -1);
   else if (vl2->type == PROP_EXPRESSION)
      propobj = Tcl_NewStringObj("(unresolved expression)", -1);
   Tcl_ListObjAppendElement(netgeninterp, mobj, propobj);

   Tcl_ListObjAppendElement(netgeninterp, mpair, mobj);
   return mpair;
}
#endif

/*--------------------------------------------------------------*/
/* The core of PropertyMatch(), check if a single property	*/
/* record matches between instance tp1 and tp2.  If do_print	*/
/* is 1, then print the mismatch results.			*/
/*								*/
/* If rval != NULL, then return the count of mismatches		*/
/* and return -1 in *rval if there is a mismatch in the		*/
/* number of property records following the one checked.  If	*/
/* rval == NULL, then return 1 if there is a mismatch in M or	*/
/* 2 if there is a mismatch in S; otherwise return 0, meaning	*/
/* that no final optimization is possible.			*/
/*--------------------------------------------------------------*/

#ifdef TCL_NETGEN
Tcl_Obj *
#else
void
#endif
PropertyCheckMismatch(struct objlist *tp1, struct nlist *tc1,
	char *inst1, struct objlist *tp2, struct nlist *tc2,
	char *inst2, int do_print, int do_list, int *count,
	int *rval)
{
   int mismatches = 0;
   int len2, *check2;
   struct property *kl1, *kl2, *klt;
   struct valuelist *vl1, *vl2;
   int i, j;
   int islop;
   int ival1, ival2;
   double pd, dslop, dval1, dval2;
   static struct valuelist mvl, svl;
   static struct property klm, kls;
   static char mkey[2], skey[2];

#ifdef TCL_NETGEN
   Tcl_Obj *proplist = NULL;
#endif

   // Set up static records representing property M = 1 and S = 1
   mkey[0] = 'M';
   mkey[1] = '\0';
   skey[0] = 'S';
   skey[1] = '\0';
   mvl.type = PROP_INTEGER;
   mvl.value.ival = 1;
   mvl.key = mkey;
   svl.type = PROP_INTEGER;
   svl.value.ival = 1;
   svl.key = skey;
   klm.key = mkey;
   klm.type = PROP_INTEGER;
   klm.merge = 0;
   klm.pdefault.ival = 1;
   klm.slop.ival = 0;
   kls.key = skey;
   kls.type = PROP_INTEGER;
   kls.merge = 0;
   kls.pdefault.ival = 1;
   kls.slop.ival = 0;

   // Find length of second property list.  Create checklist to check
   // off each property that has been compared.
   for (j = 0;; j++) {
      vl2 = &(tp2->instance.props[j]);
      if (vl2->type == PROP_ENDLIST) break;
   }
   len2 = j;
   if (len2 > 0)
      check2 = (int *)CALLOC(len2, sizeof(int));
   else
      check2 = NULL;

   for (i = 0;;) {
      vl1 = &(tp1->instance.props[i]);
      if (vl1->type == PROP_ENDLIST) {
	 /* Any zeros in the check2 list are properties that	*/
	 /* are missing in the 1st circuit.  First check for M	*/
	 /* or S records that can be compared against an 	*/
	 /* implicit record of M = 1 or S = 1.  Then, any	*/
	 /* remaining entries are errors.			*/

	 for (j = 0; j < len2; j++) {
	    if (check2[j] == 0) {
               vl2 = &(tp2->instance.props[j]);
	       vl1 = &mvl;
	       if ((*matchfunc)(vl1->key, vl2->key)) break;
	       vl1 = &svl;
	       if ((*matchfunc)(vl1->key, vl2->key)) break;

	       /* Check if property is "of interest".	*/
	       kl2 = (struct property *)HashLookup(vl2->key, &(tc2->propdict));
	       if (kl2 != NULL) {

	          /* No match */
	          if (do_print) {
	             Fprintf(stdout, "%s vs. %s:\n", inst1, inst2);
	             Fprintf(stdout, "Property %s in circuit2 has no matching "
				"property in circuit1\n",  vl2->key);
	          }
#ifdef TCL_NETGEN
	          if (do_list) {
	              Tcl_Obj *mpair = PropertyList(NULL, vl2);
		      if (!proplist) proplist = Tcl_NewListObj(0, NULL);
		      Tcl_ListObjAppendElement(netgeninterp, proplist, mpair);
	          }
#endif
	          if (rval != NULL) mismatches++;
	       }
	    }
	 }
	 if (j == len2) break;
      }
      else i++;
      if (vl1 == NULL) continue;
      if (vl1->key == NULL) continue;

      /* Check if this is a "property of interest". */
      kl1 = (struct property *)HashLookup(vl1->key, &(tc1->propdict));
      if (kl1 == NULL) {
	 if ((*matchfunc)(vl1->key, mvl.key))
	    kl1 = &klm;
	 else if ((*matchfunc)(vl1->key, svl.key))
	    kl1 = &kls;
	 else {
            if (j < len2) check2[j] = 1;	/* Mark as checked */
	    continue;
	 }
      }

      /* Find the matching property in vl2. */

      for (j = 0;; j++) {
         vl2 = &(tp2->instance.props[j]);
	 if (vl2->type == PROP_ENDLIST) break;
	 if (check2[j] == 0)
	    if ((*matchfunc)(vl1->key, vl2->key)) break;
      }
      if (vl2->type == PROP_ENDLIST) {
	 /* Check against M and S records;  a missing M or S	*/
	 /* record is equivalent to M = 1 or S = 1.		*/

	 if (vl1 != &mvl)
	    if ((*matchfunc)(vl1->key, mvl.key)) vl2 = &mvl;
	 if (vl1 != &svl)
	    if ((*matchfunc)(vl1->key, svl.key)) vl2 = &svl;
      }
      if (vl2->type == PROP_ENDLIST) {
	 /* vl1 had a property of interest that was not found	*/
         /* in vl2, so mark this as a missing property error.	*/

         if (do_print) {
	    Fprintf(stdout, "%s vs. %s:\n", inst1, inst2);
	    Fprintf(stdout, "Property %s in circuit1 has no matching "
			"property in circuit2\n",  vl1->key);
	 }
#ifdef TCL_NETGEN
         if (do_list) {
             Tcl_Obj *mpair = PropertyList(vl1, NULL);
	     if (!proplist) proplist = Tcl_NewListObj(0, NULL);
	     Tcl_ListObjAppendElement(netgeninterp, proplist, mpair);
         }
#endif
	 if (rval != NULL) mismatches++;
      }
      else if (j < len2)
	 check2[j] = 1;		/* Mark property as checked */

      if (vl2 == NULL) continue;
      if (vl2->key == NULL) continue;

      /* Both device classes must agree on the properties to compare */
      kl2 = (struct property *)HashLookup(vl2->key, &(tc2->propdict));
      if (kl2 == NULL) {
         if (vl2 == &mvl)
	    kl2 = &klm;
	 else if (vl2 == &svl)
	    kl2 = &kls;
	 else if ((*matchfunc)(vl2->key, mvl.key))
	    kl2 = &klm;
	 else if ((*matchfunc)(vl2->key, svl.key))
	    kl2 = &kls;
	 else
	    continue;
      }

      /* Watch out for uninitialized entries in cell def */
      if (vl1->type == vl2->type) {
	 if (kl1->type == PROP_STRING && kl1->pdefault.string == NULL)
	    SetPropertyDefault(kl1, vl1);
	 if (kl2->type == PROP_STRING && kl2->pdefault.string == NULL)
	    SetPropertyDefault(kl2, vl2);
      }

      /* Promote properties as necessary to make sure they all match */
      if (kl1->type != vl1->type) PromoteProperty(kl1, vl1, tp1, tc1);
      if (kl2->type != vl2->type) PromoteProperty(kl2, vl2, tp2, tc2);

      /* If kl1 and kl2 types differ, choose one type to target. Prefer	*/
      /* double if either type is double, otherwise string.		*/

      if (kl1->type == PROP_DOUBLE)
	 klt = kl1;
      else if (kl2->type == PROP_DOUBLE)
	 klt = kl2;
      else if (kl1->type == PROP_INTEGER)
	 klt = kl1;
      else if (kl2->type == PROP_INTEGER)
	 klt = kl2;
      else if (kl1->type == PROP_STRING)
	 klt = kl1;
      else if (kl2->type == PROP_STRING)
         klt = kl2;
      else
	 klt = kl1;

      if (vl2->type != klt->type) PromoteProperty(klt, vl2, tp2, tc2);
      if (vl1->type != klt->type) PromoteProperty(klt, vl1, tp1, tc1);

      if (vl1->type != vl2->type) {
	 if (do_print && (vl1->type != vl2->type)) {
	    if (mismatches == 0)
		Fprintf(stdout, "%s vs. %s:\n", inst1, inst2);

	    Fprintf(stdout, " %s circuit1: ", kl1->key);
	    switch (vl1->type) {
		case PROP_DOUBLE:
		case PROP_VALUE:
		    Fprintf(stdout, "%g", vl1->value.dval);
		    break;
		case PROP_INTEGER:
		    Fprintf(stdout, "%d", vl1->value.ival);
		    break;
		case PROP_STRING:
		    Fprintf(stdout, "\"%s\"", vl1->value.string);
		    break;
		case PROP_EXPRESSION:
		    Fprintf(stdout, "(unresolved expression)");
		    break;
	    }
	    Fprintf(stdout, "   ");
	    switch (vl2->type) {
		case PROP_DOUBLE:
		case PROP_VALUE:
		    Fprintf(stdout, "%g", vl2->value.dval);
		    break;
		case PROP_INTEGER:
		    Fprintf(stdout, "%d", vl2->value.ival);
		    break;
		case PROP_STRING:
		    Fprintf(stdout, "\"%s\"", vl2->value.string);
		    break;
		case PROP_EXPRESSION:
		    Fprintf(stdout, "(unresolved expression)");
		    break;
	    }
	    Fprintf(stdout, "  (property type mismatch)\n");
	 }
#ifdef TCL_NETGEN
         if (do_list) {
             Tcl_Obj *mpair = PropertyList(vl1, vl2);
	     if (!proplist) proplist = Tcl_NewListObj(0, NULL);
	     Tcl_ListObjAppendElement(netgeninterp, proplist, mpair);
         }
#endif
	 if (rval != NULL) mismatches++;
      }

      else switch (klt->type) {
	 case PROP_DOUBLE:
	 case PROP_VALUE:
	    dval1 = vl1->value.dval;
	    dval2 = vl2->value.dval;

	    dslop = klt->slop.dval;
	    pd = 2 * fabs(dval1 - dval2) / (dval1 + dval2);
	    if (pd > dslop) {
	       if (do_print) {
		  if (mismatches == 0)
		     Fprintf(stdout, "%s vs. %s:\n", inst1, inst2);
		  Fprintf(stdout, " %s circuit1: %g   circuit2: %g   ",
				kl1->key, dval1, dval2);
		  if (vl1->value.dval > 0.0 && vl2->value.dval > 0.0)
		     Fprintf(stdout, "(delta=%.3g%%, cutoff=%.3g%%)\n",
				100 * pd, 100 * dslop);
		  else
		     Fprintf(stdout, "\n");
	       }
#ifdef TCL_NETGEN
	       if (do_list) {
                  Tcl_Obj *mpair = PropertyList(vl1, vl2);
		  if (!proplist) proplist = Tcl_NewListObj(0, NULL);
	          Tcl_ListObjAppendElement(netgeninterp, proplist, mpair);
               }
#endif
	       if (rval != NULL) mismatches++;
	    }
	    break;
	 case PROP_INTEGER:
	    ival1 = vl1->value.ival;
	    ival2 = vl2->value.ival;

	    islop = klt->slop.ival;
	    if (abs(ival1 - ival2) > islop) {
	       if (do_print) {
		  if (mismatches == 0)
		     Fprintf(stdout, "%s vs. %s:\n", inst1, inst2);
		  Fprintf(stdout, " %s circuit1: %d   circuit2: %d   ",
				kl1->key, ival1, ival2);
		  if (ival1 > 0 && ival2 > 0)
		     Fprintf(stdout, "(delta=%d, cutoff=%d)\n",
				abs(ival1 - ival2), islop);
		  else
		     Fprintf(stdout, "\n");
	       }
#ifdef TCL_NETGEN
	       if (do_list) {
                  Tcl_Obj *mpair = PropertyList(vl1, vl2);
		  if (!proplist) proplist = Tcl_NewListObj(0, NULL);
	          Tcl_ListObjAppendElement(netgeninterp, proplist, mpair);
               }
#endif
	       if (rval != NULL) mismatches++;
	       else if (*(kl1->key + 1) == '\0') {
		  if (toupper(*kl1->key) == 'M')
		     mismatches = 1;
		  else if (toupper(*kl1->key) == 'S')
		     mismatches = 2;
	       }
	    }
	    break;
	 case PROP_STRING:
	    islop = (int)(klt->slop.dval + 0.5);
	    if (islop == 0) {
	       if (!(*matchfunc)(vl1->value.string, vl2->value.string)) {
		  if (do_print) {
		     if (mismatches == 0)
		        Fprintf(stdout, "%s vs. %s:\n", inst1, inst2);
		     Fprintf(stdout, " %s circuit1: \"%s\"   "
				"circuit2: \"%s\"   (exact match req'd)\n",
				kl1->key, vl1->value.string,
				vl2->value.string);
		  }
#ifdef TCL_NETGEN
	          if (do_list) {
                     Tcl_Obj *mpair = PropertyList(vl1, vl2);
		     if (!proplist) proplist = Tcl_NewListObj(0, NULL);
	             Tcl_ListObjAppendElement(netgeninterp, proplist, mpair);
                  }
#endif
	          if (rval != NULL) mismatches++;
	       }
	    }
	    else {
	       if (strncasecmp(vl1->value.string, vl2->value.string, islop)) {
		  if (do_print) {
		     if (mismatches == 0)
		        Fprintf(stdout, "%s vs. %s:\n", inst1, inst2);
		     Fprintf(stdout,  " %s circuit1: \"%s\"   "
				"circuit2: \"%s\"   (check to %d chars.)\n",
		      		kl1->key, vl1->value.string,
				vl2->value.string, islop);
		  }
#ifdef TCL_NETGEN
	          if (do_list) {
                     Tcl_Obj *mpair = PropertyList(vl1, vl2);
		     if (!proplist) proplist = Tcl_NewListObj(0, NULL);
	             Tcl_ListObjAppendElement(netgeninterp, proplist, mpair);
                  }
#endif
	          if (rval != NULL) mismatches++;
	       }
	    }
	    break;
	 case PROP_EXPRESSION:
	    /* Expressions could potentially be compared. . . */
	    if (do_print)
	       Fprintf(stdout,  " %s (unresolved expressions.)\n", kl1->key);
#ifdef TCL_NETGEN
	    if (do_list) {
               Tcl_Obj *mpair = PropertyList(vl1, vl2);
	       if (!proplist) proplist = Tcl_NewListObj(0, NULL);
	       Tcl_ListObjAppendElement(netgeninterp, proplist, mpair);
            }
#endif
	    if (rval != NULL) mismatches++;
	    break;
      }
      if ((vl1 == NULL && vl2 != NULL) || (vl1 != NULL && vl2 == NULL))
	 /* Different number of properties of interest */
	 if (rval) *rval = -1;
   }

   if (len2 > 0) FREE(check2);
   *count = mismatches;

#ifdef TCL_NETGEN
   return proplist;
#endif

}

/*--------------------------------------------------------------*/
/* Dump a description of a device's series/parallel network	*/
/*--------------------------------------------------------------*/

void DumpNetwork(struct objlist *ob, int cidx)
{
    struct valuelist *vl;
    struct objlist *tp;
    int i;

    for (tp = ob; tp && (tp->type != PROPERTY); tp = tp->next)
	if ((tp > ob) && (tp->type == FIRSTPIN))
	    return;

    if (tp == NULL) return;

    Fprintf(stdout, "Circuit %d instance %s network:\n", cidx, ob->instance.name);

    for (; tp && (tp->type == PROPERTY); tp = tp->next) {
	for (i = 0;; i++) {
	    vl = &(tp->instance.props[i]);
	    if (vl->type == PROP_ENDLIST) break;
	    if (!strcmp(vl->key, "_tag")) {
		Fprintf(stdout, "%s\n", vl->value.string);
		continue;
	    }
	    Fprintf(stdout, "  %s = ", vl->key); 
	    switch(vl->type) {
		case PROP_STRING:
		    Fprintf(stdout, "%s\n", vl->value.string); 
		    break;
		case PROP_INTEGER:
		    Fprintf(stdout, "%d\n", vl->value.ival); 
		    break;
		case PROP_DOUBLE:
		case PROP_VALUE:
		    Fprintf(stdout, "%g\n", vl->value.dval); 
		    break;
		case PROP_EXPRESSION:
		    Fprintf(stdout, "(expression)\n");
		    break;
	    }
	}
    }
}

/*--------------------------------------------------------------*/
/* Call DumpNetwork() on each device in the object list		*/
/*--------------------------------------------------------------*/

void DumpNetworkAll(char *model, int file)
{
    struct nlist *tp;
    struct objlist *ob;

    if ((tp = LookupCellFile(model, file)) == NULL) {
	Printf("Cell: %s does not exist.\n", model);
    }
    for (ob = tp->cell; ob; ob = ob->next)
	if (ob->type == FIRSTPIN)
	    DumpNetwork(ob, file);
}

/*--------------------------------------------------------------*/
/* Compare the properties of two objects.  The passed values	*/
/* ob1 and ob2 are pointers to the first entry (firstpin) of	*/
/* each object.							*/
/*								*/
/* Return -1 on complete failure (should not happen), or	*/
/* return the number of mismatched properties (0 = perfect	*/
/* match of all properties, or there were no properties to	*/
/* compare).							*/
/*								*/
/* NOTE:  ob1 must belong to Circuit1, and ob2 must belong to	*/
/* Circuit2.  The calling procedure is responsble for ensuring	*/
/* that this is true.						*/
/*								*/
/* NOTE:  This routine assumes that if file1 == file2 or if	*/
/* file2 != Circuit1->graph, then "do_print" is FALSE.  It 	*/
/* hard-codes "Circuit 1" and "Circuit 2" in most print		*/
/* statements.  file1 == file2 only when checking properties	*/
/* for symmetry breaking, where nothing is printed.		*/
/*--------------------------------------------------------------*/

#ifdef TCL_NETGEN
Tcl_Obj *
#else
void
#endif
PropertyMatch(struct objlist *ob1, int file1,
		struct objlist *ob2, int file2,
		int do_print, int do_list, int *retval)
{
   struct nlist *tc1, *tc2;
   struct objlist *tp1, *tp2, *obn1, *obn2, *tpc;
   struct property *kl1, *kl2;
   struct valuelist *vl1, *vl2;
   int t1type, t2type, run1, run2;
   int i, mismatches = 0, checked_one;
   int rval = 1;
   char *inst1, *inst2;
#ifdef TCL_NETGEN
   Tcl_Obj *proplist = NULL, *mpair, *mlist;
#endif

   tc1 = LookupCellFile(ob1->model.class, file1);
   tc2 = LookupCellFile(ob2->model.class, file2);

   if (tc1 == NULL || tc2 == NULL) {
      if (tc1 == NULL)
	 Fprintf(stdout, "Error:  Circuit %d device \"%s\" not found!\n",
		    file1, ob1->model.class);
      else
	 Fprintf(stdout, "Error:  Circuit %d device \"%s\" not found!\n",
		    file2, ob2->model.class);
#ifdef TCL_NETGEN
      return NULL;
#else
      return;
#endif
   }

   if (tc1->classhash != tc2->classhash) {
      *retval = -1;
#ifdef TCL_NETGEN
      return NULL;
#else
      return;
#endif
   }

   /* Find the first property record of each circuit.  obn1, obn2 are	*/
   /* the last device record before the properties for each device.	*/
   for (tp1 = ob1->next; (tp1 != NULL) && tp1->type > FIRSTPIN; tp1 = tp1->next)
      obn1 = tp1;
   for (tp2 = ob2->next; (tp2 != NULL) && tp2->type > FIRSTPIN; tp2 = tp2->next)
      obn2 = tp2;
   if (tp1 && ((tp1->type == FIRSTPIN) || (tp1->type == NODE)))
      tp1 = NULL;	/* tp1 had no properties */
   if (tp2 && ((tp2->type == FIRSTPIN) || (tp2->type == NODE)))
      tp2 = NULL;	/* tp2 had no properties */

   /* Check if there are any properties to match */

   if ((tp1 == NULL) && (tp2 == NULL)) {
      *retval = 0;
#ifdef TCL_NETGEN
      return NULL;
#else
      return;
#endif
   }
   t1type = (tp1 != NULL) ? tp1->type : 0;
   t2type = (tp2 != NULL) ? tp2->type : 0;

   if (tp1 == NULL)
      if (t2type != PROPERTY) {
         *retval = 0;
#ifdef TCL_NETGEN
         return NULL;
#else
         return;
#endif
      }
	
   if (tp2 == NULL)
      if (t1type != PROPERTY) {
         *retval = 0;
#ifdef TCL_NETGEN
         return NULL;
#else
         return;
#endif
      }

   // Sanity check---shouldn't happen
   if (tp1 && (t1type == PROPERTY) && (tp1->instance.props == NULL))
	t1type = UNKNOWN;
   if (tp2 && (t2type == PROPERTY) && (tp2->instance.props == NULL))
	t2type = UNKNOWN;

   if ((t1type != PROPERTY) && (t2type != PROPERTY)) {
      *retval = 0;
#ifdef TCL_NETGEN
      return NULL;
#else
      return;
#endif
   }

   /* Check for no-connect pins in merged devices on both sides.	*/
   /* Both sides should either have no-connects marked, or neither.	*/
   /* (Permutable pins may need to be handled correctly. . .		*/

   for (tp1 = ob1, tp2 = ob2; (tp1 != NULL) && tp1->type >= FIRSTPIN &&
	    (tp2 != NULL) && tp2->type >= FIRSTPIN; tp1 = tp1->next, tp2 = tp2->next) {
      struct objlist *node1, *node2;

      if (file1 == Circuit1->file)
	 node1 = Circuit1->nodename_cache[tp1->node];
      else
	 node1 = Circuit2->nodename_cache[tp1->node];

      if (file2 == Circuit1->file)
         node2 = Circuit1->nodename_cache[tp2->node];
      else
         node2 = Circuit2->nodename_cache[tp2->node];

      /* NOTE: A "no-connect" node (multiple no-connects represented by a
       * single node) has non-zero flags.  A non-node entry in the cache
       * implies a node with zero flags.
       */
      if (node1->flags != node1->flags) {
	 Fprintf(stdout, "  Parallelized instances disagree on pin connections.\n");
	 Fprintf(stdout, "    Circuit1 instance %s pin %s connections are %s (%d)\n",
		    tp1->instance.name, node1->name,
		    (node1->flags == 0) ? "tied together" : "no connects",
		    node1->flags);
	 Fprintf(stdout, "    Circuit2 instance %s pin %s connections are %s (%d)\n",
		    tp2->instance.name, node2->name,
		    (node2->flags == 0) ? "tied together" : "no connects",
		    node2->flags);
	 mismatches++;
      }
   }

   // Attempt to organize devices by series and parallel combination
   if (t1type == PROPERTY && t2type == PROPERTY)
      PropertySortAndCombine(obn1, tc1, obn2, tc2);

   // PropertySortAndCombine can move the first property, so recompute it
   // for each circuit.

   for (tp1 = ob1; (tp1 != NULL) && tp1->type >= FIRSTPIN; tp1 = tp1->next);
   for (tp2 = ob2; (tp2 != NULL) && tp2->type >= FIRSTPIN; tp2 = tp2->next);

   // Find name for printing, removing leading slash if needed.
   inst1 = ob1->instance.name;
   if (*inst1 == '/') inst1++;
   inst2 = ob2->instance.name;
   if (*inst2 == '/') inst2++;

   checked_one = FALSE;
   while(1) {
      if ((t1type != PROPERTY) && (checked_one == TRUE)) {
	 // t2 has more property records than t1, and they did not get
	 // merged equally by PropertySortAndCombine().
	 if (do_print) {
	    Fprintf(stdout, "Circuit 1 parallel/series network does not match"
			" Circuit 2\n");
	    DumpNetwork(ob1, 1);
	    DumpNetwork(ob2, 2);
	 }
	 mismatches++;
      }
      else if (t1type != PROPERTY) {
	 // t1 has no properties.  See if t2's properties are required
	 // to be checked.  If so, flag t2 instance as unmatched

	 for (i = 0;; i++) {
	    vl2 = &(tp2->instance.props[i]);
	    if (vl2->type == PROP_ENDLIST) break;
	    if (vl2 == NULL) continue;
	    if (vl2->key == NULL) continue;
	    kl2 = (struct property *)HashLookup(vl2->key, &(tc2->propdict));
	    if (kl2 != NULL) {
		// Allowed for one instance to be missing "M" or "S".
		if (!(*matchfunc)(vl2->key, "M") && !(*matchfunc)(vl2->key, "S"))
		    break;	// Property is required
	    }
	 }
	 if (vl2->type != PROP_ENDLIST) {
	    mismatches++;
	    if (do_print) {
		if (vl2 && vl2->key)
		    Fprintf(stdout, "Circuit 2 %s instance %s property"
				" \"%s\" has no match in circuit 1.\n",
				Circuit2->name, inst2, vl2->key);
		else
		    Fprintf(stdout, "Circuit 2 %s instance %s has no"
				" property match in circuit 1.\n",
				Circuit2->name, inst2);
	    }
#ifdef TCL_NETGEN
	    if (do_list) {
	        mpair = PropertyList(NULL, vl2);
		if (mpair) {
		   if (!proplist) proplist = NewPropertyList(inst1, inst2);
		   Tcl_ListObjAppendElement(netgeninterp, proplist, mpair);
		}
	    }
#endif
	    rval = -1;
	 }
	 else
	    rval = 0;
      }
      else if ((t2type != PROPERTY) && (checked_one == TRUE)) {
	 // t1 has more property records than t2, and they did not get
	 // merged equally by PropertySortAndCombine().
	 Fprintf(stdout, "Circuit 2 parallel/series network does not match"
			" Circuit 1\n");
	 DumpNetwork(ob1, 1);
	 DumpNetwork(ob2, 2);
	 mismatches++;
      }
      else if (t2type != PROPERTY) {
	 // t2 has no properties.  See if t1's properties are required
	 // to be checked.  If so, flag t1 instance as unmatched

	 for (i = 0;; i++) {
	    vl1 = &(tp1->instance.props[i]);
	    if (vl1->type == PROP_ENDLIST) break;
	    if (vl1 == NULL) continue;
	    if (vl1->key == NULL) continue;
	    kl1 = (struct property *)HashLookup(vl1->key, &(tc1->propdict));
	    if (kl1 != NULL) {
		// Allowed for one instance to be missing "M" or "S".
		if (!(*matchfunc)(vl1->key, "M") && !(*matchfunc)(vl1->key, "S"))
		    break;	// Property is required
	    }
	 }
	 if (vl1->type != PROP_ENDLIST) {
	    mismatches++;
	    if (do_print) {
		if (vl1 && vl1->key)
		    Fprintf(stdout, "Circuit 1 %s instance %s property"
				" \"%s\" has no match in circuit 2.\n",
				Circuit1->name, inst1, vl1->key);
		else
		    Fprintf(stdout, "Circuit 1 %s instance %s has no"
				" property match in circuit 2.\n",
				Circuit1->name, inst1);
	    }
#ifdef TCL_NETGEN
	    if (do_list) {
	        mpair = PropertyList(vl1, NULL);
		if (mpair) {
		   if (!proplist) proplist = NewPropertyList(inst1, inst2);
		   Tcl_ListObjAppendElement(netgeninterp, proplist, mpair);
		}
	    }
#endif
	    rval = -1;
	 }
	 else
	    rval = 0;
      }
      else {
	 int multmatch, count;
	 PropertyCheckMismatch(tp1, tc1, inst1, tp2, tc2,
			inst2, FALSE, FALSE, &multmatch, NULL);
	 if (multmatch == 1) {
	    /* Final attempt:  Reduce M to 1 on both devices */
	    run1 = run2 = 0;
	    for (tpc = tp1; tpc && (tpc->type == PROPERTY); tpc = tpc->next) run1++;
	    for (tpc = tp2; tpc && (tpc->type == PROPERTY); tpc = tpc->next) run2++;
	    PropertyOptimize(tp1, tc1, run1, FALSE, TRUE);
	    PropertyOptimize(tp2, tc2, run2, FALSE, TRUE);
	 }
	 else if (multmatch == 2) {
	    /* Final attempt:  Reduce S to 1 on both devices */
	    run1 = run2 = 0;
	    for (tpc = tp1; tpc && (tpc->type == PROPERTY); tpc = tpc->next) run1++;
	    for (tpc = tp2; tpc && (tpc->type == PROPERTY); tpc = tpc->next) run2++;
	    PropertyOptimize(tp1, tc1, run1, TRUE, TRUE);
	    PropertyOptimize(tp2, tc2, run2, TRUE, TRUE);
	 }
#ifdef TCL_NETGEN
	 mlist =
#endif
	 PropertyCheckMismatch(tp1, tc1, inst1, tp2, tc2,
			inst2, do_print, do_list, &count, &rval);
	 mismatches += count;
#ifdef TCL_NETGEN
	 if (do_list && (mlist != NULL)) {
	    if (!proplist) proplist = NewPropertyList(inst1, inst2);
	    Tcl_ListObjAppendList(netgeninterp, proplist, mlist);
	 }
#endif
      }

      /* Move to the next property record */

      if (tp1) tp1 = tp1->next;
      if (tp2) tp2 = tp2->next;
      t1type = (tp1) ? tp1->type : 0;
      t2type = (tp2) ? tp2->type : 0;
      if ((t1type != PROPERTY) && (t2type != PROPERTY)) break;
      checked_one = TRUE;
   }
 
   *retval = (rval < 0) ? rval : mismatches;

#ifdef TCL_NETGEN
   return proplist;
#endif
}

/*--------------------------------------------------------------*/
/* Check device properties of one element class against the	*/
/* other.  Use graph1 for the reference property names.		*/
/* EC is assumed to have only one element from each class.	*/
/* Automorphisms (element classes with multiple entries) are	*/
/* handled separately by re-partitioning the element class	*/
/* based on the property values.				*/
/*								*/
/* Return -1 on complete failure (bad element class), or else	*/
/* return the mismatch number determined by PropertyMatch()	*/
/*--------------------------------------------------------------*/

#ifdef TCL_NETGEN
Tcl_Obj *
#else
void
#endif
PropertyCheck(struct ElementClass *EC, int do_print, int do_list, int *rval)
{
   struct Element *E1, *E2, *Etmp;
#ifdef TCL_NETGEN
   Tcl_Obj *mpair;
#endif

   /* This element class should contain exactly two entries,	*/
   /* one belonging to each graph.				*/

   if (((E1 = EC->elements) == NULL) ||
	((E2 = EC->elements->next) == NULL) ||
	(E2->next != NULL) ||
	(E1->graph == E2->graph))
   {
      *rval = -1;
#ifdef TCL_NETGEN
      return NULL;
#else
      return;
#endif
   }

   if (E1->graph != Circuit1->file) {	/* Ensure that E1 is Circuit1 */
      Etmp = E1;
      E1 = E2;
      E2 = Etmp;
   }
#ifdef TCL_NETGEN
   return PropertyMatch(E1->object, E1->graph, E2->object, E2->graph,
		do_print, do_list, rval);
#else
   PropertyMatch(E1->object, E1->graph, E2->object, E2->graph,
		do_print, do_list, rval);
#endif
}

/*--------------------------------------------------------------*/
/* Print results of property checks				*/
/*--------------------------------------------------------------*/

void PrintPropertyResults(int do_list)
{
    int rval;
    struct ElementClass *EC;
#ifdef TCL_NETGEN

    if (do_list) {
       Tcl_Obj *proplist, *eprop;

       proplist = Tcl_NewListObj(0, NULL);
       for (EC = ElementClasses; EC != NULL; EC = EC->next) {
 	   eprop = PropertyCheck(EC, 1, 1, &rval);
	   if (eprop != NULL)
	      Tcl_ListObjAppendElement(netgeninterp, proplist, eprop);
       }
       Tcl_SetVar2Ex(netgeninterp, "lvs_out", NULL,
			Tcl_NewStringObj("properties", -1),
			TCL_APPEND_VALUE | TCL_LIST_ELEMENT);
       Tcl_SetVar2Ex(netgeninterp, "lvs_out", NULL, proplist,
			TCL_APPEND_VALUE | TCL_LIST_ELEMENT);
    }
    else {
#endif

    for (EC = ElementClasses; EC != NULL; EC = EC->next)
 	PropertyCheck(EC, 1, 0, &rval);

#ifdef TCL_NETGEN
    }
#endif
}

/*----------------------------------------------------------------------*/
/* Return 0 if perfect matching found, else return number of		*/
/* automorphisms, and return -1 if invalid matching found. 		*/
/*----------------------------------------------------------------------*/

int VerifyMatching(void)
{
  int ret;
  struct ElementClass *EC;
  struct NodeClass *NC;
  struct Element *E;
  struct Node *N;
  int C1, C2, result;

  if (BadMatchDetected) return(-1);
  
  ret = 0;
  for (EC = ElementClasses; EC != NULL; EC = EC->next) {
    C1 = C2 = 0;
    for (E = EC->elements; E != NULL; E = E->next) 
      (E->graph == Circuit1->file) ? C1++ : C2++;
    if (C1 != C2) return(-1);
    if (C1 != 1)
       ret++;
    else if (PropertyErrorDetected != 1) {
       PropertyCheck(EC, 0, 0, &result);
       if (result > 0)
	  PropertyErrorDetected = 1;
       else if (result < 0)
	  PropertyErrorDetected = -1;
    }
  }

  for (NC = NodeClasses; NC != NULL; NC = NC->next) {
    C1 = C2 = 0;
    for (N = NC->nodes; N != NULL; N = N->next) {
      (N->graph == Circuit1->file) ? C1++ : C2++;
    }
    if (C1 != C2) return(-1);
    if (C1 != 1) ret++;
  }
  return(ret);
}

void PrintAutomorphisms(void)
{
  struct ElementClass *EC;
  struct NodeClass *NC;
  struct Element *E;
  struct Node *N;
  int C1, C2;

  for (EC = ElementClasses; EC != NULL; EC = EC->next) {
    C1 = C2 = 0;
    for (E = EC->elements; E != NULL; E = E->next) 
      (E->graph == Circuit1->file) ? C1++ : C2++;
    if (C1 != C2) continue;
    if (C1 != 1) {
      Printf("Device Automorphism:\n");
      for (E = EC->elements; E != NULL; E = E->next)
	Printf("  Circuit %d: %s\n",E->graph, E->object->instance.name);
      Printf("------------------\n");
    }
  }

  for (NC = NodeClasses; NC != NULL; NC = NC->next) {
    C1 = C2 = 0;
    for (N = NC->nodes; N != NULL; N = N->next) 
      (N->graph == Circuit1->file) ? C1++ : C2++;
    if (C1 != C2) continue;
    if (C1 != 1) {
      Printf("Net Automorphism:\n");
      for (N = NC->nodes; N != NULL; N = N->next)
	Printf("  Circuit %d: %s\n",N->graph, N->object->name);
      Printf("------------------\n");
    }
  }
}

/*
 *-------------------------------------------------------------------------
 * ResolveAutomorphsByPin
 *
 * Equivalence as many device pairs within an automorphic class by
 * comparing pin names of those devices connected to pins, and
 * separating out those devices that are connected to matching pins
 * in each circuit.
 *
 * Return value is the same as VerifyMatching()
 *-------------------------------------------------------------------------
 */

int ResolveAutomorphsByPin()
{
    struct NodeClass *NC;
    struct Node *N;
    int C1, C2;
    unsigned long newhash, orighash;
    int portnum;

    /* Diagnostic */
    Fprintf(stdout, "Resolving automorphisms by pin name.\n");

    for (NC = NodeClasses; NC != NULL; NC = NC->next) {
	struct Node *N1, *N2;
	C1 = C2 = 0;
	N1 = N2 = NULL;
	for (N = NC->nodes; N != NULL; N = N->next) {
	    if (N->graph == Circuit1->file)
 		C1++;
	    else
		C2++;
	}
	if (C1 == C2 && C1 != 1) {

	    /* This is an automorphic class.  For each node in	*/
	    /* the class, determine if it is a pin.  If so,	*/
	    /* give it a new hash value, then find the		*/
	    /* corresponding pin name for the other circuit.	*/

	    orighash = NC->nodes->hashval;
	    for (N1 = NC->nodes; N1 != NULL; N1 = N1->next) {
		if (N1->hashval != orighash) continue;
		for (N2 = N1->next; N2 != NULL; N2 = N2->next) {
		    if ((N2->graph != N1->graph) &&
				(*matchfunc)(N2->object->name, N1->object->name)) {
			Magic(newhash);
			N1->hashval = newhash;
			N2->hashval = newhash;
			break;
		    }
		}
	    }
	}
    }

    FractureElementClass(&ElementClasses); 
    FractureNodeClass(&NodeClasses); 
    ExhaustiveSubdivision = 1;
    while (!Iterate() && VerifyMatching() != -1); 
    return(VerifyMatching());
}

/*
 *-------------------------------------------------------------------------
 * ResolveAutomorphsByProperty
 *
 * Equivalence as many device pairs within an automorphic class by
 * comparing properties and matching devices with matching properties.
 *
 *-------------------------------------------------------------------------
 */

int ResolveAutomorphsByProperty()
{
    struct ElementClass *EC;
    struct Element *E;
    int C1, C2, result, badmatch;
    unsigned long orighash, newhash;

    /* Diagnostic */
    Fprintf(stdout, "Resolving automorphisms by property value.\n");

    for (EC = ElementClasses; EC != NULL; EC = EC->next) {
	struct Element *E1, *E2;
	C1 = C2 = 0;
	E1 = E2 = NULL;
	for (E = EC->elements; E != NULL; E = E->next) {

	    if (E->graph == Circuit1->file)
		C1++;
	    else
		C2++;
	}
	if (C1 == C2 && C1 != 1) {

	    /* This is an automorphic class.  For each		*/
	    /* device, assign a new hash value, then assign	*/
	    /* the same hash value to all devices with		*/
	    /* matching properties in either circuit.		*/
	    /* Continue until all elements have received	*/
	    /* new hash values.					*/

	    orighash = EC->elements->hashval;

	    /* Properties that fail to match for any reason	*/
	    /* will only result in the automorphic group being	*/
	    /* unresolved at this stage.			*/

	    for (E1 = EC->elements; E1 != NULL; E1 = E1->next) {
		if (E1->hashval != orighash) continue;
		Magic(newhash);
		E1->hashval = newhash;
		C1 = 1;
		C2 = 0;

		badmatch = FALSE;
		for (E2 = E1->next; E2 != NULL; E2 = E2->next) {
		    if (E2->hashval != orighash) continue;
		    PropertyMatch(E1->object, E1->graph, E2->object, E2->graph,
			    FALSE, FALSE, &result);
		    if (result == 0) {
			E2->hashval = newhash;
			if (E2->graph == E1->graph)
			    C1++;
			else
			    C2++;
		    }
		}

		// If devices don't match equally in Circuit1 and Circuit2.
		// Since we don't want a property error to force the whole
		// circuit to be declared mismatched, arbitrarily revert
		// some of the components back to the original hashval
		// until the list lengths match.  The non-matching portions
		// will appear in the report of propery errors.

		while (C2 > C1) {
		    for (E2 = EC->elements; E2 != NULL; E2 = E2->next) {
			if ((E2->graph != E1->graph) && (E2->hashval == newhash)) {
			    E2->hashval = orighash;
			    C2--;
			    if (C2 == C1) break;
			}
		    }
		}
		while (C1 > C2) {
		    for (E2 = EC->elements; E2 != NULL; E2 = E2->next) {
			if ((E2->graph == E1->graph) && (E2->hashval == newhash)) {
			    E2->hashval = orighash;
			    C1--;
			    if (C1 == C2) break;
			}
		    }
		}
	    }
	}
    }

    FractureElementClass(&ElementClasses); 
    FractureNodeClass(&NodeClasses); 
    ExhaustiveSubdivision = 1;
    while (!Iterate() && VerifyMatching() != -1); 
    return(VerifyMatching());
}

/*
 *-------------------------------------------------------------------------
 *
 * ResolveAutormorphisms --
 *
 * Do symmetry breaking of automorphisms.
 *
 * Arbitrarily equivalence all pairs of elements and nodes within all
 * automorphic classes, and run to completion.
 *
 * Return value is the same as VerifyMatching()
 *
 *-------------------------------------------------------------------------
 */

int ResolveAutomorphisms()
{
  struct ElementClass *EC;
  struct Element *E;
  struct NodeClass *NC;
  struct Node *N;
  int C1, C2;

  for (EC = ElementClasses; EC != NULL; EC = EC->next) {
    struct Element *E1, *E2;
    C1 = C2 = 0;
    E1 = E2 = NULL;
    for (E = EC->elements; E != NULL; E = E->next) {
      if (E->graph == Circuit1->file) {
	C1++;
	E1 = E;
      }
      else {
	C2++;
	E2 = E;
      }
    }
    if (C1 == C2 && C1 != 1) {
      unsigned long newhash;
      Magic(newhash);
      E1->hashval = newhash;
      E2->hashval = newhash;
      goto converge;
    }
  }

  for (NC = NodeClasses; NC != NULL; NC = NC->next) {
    struct Node *N1, *N2;
    C1 = C2 = 0;
    N1 = N2 = NULL;
    for (N = NC->nodes; N != NULL; N = N->next) {
      if (N->graph == Circuit1->file) {
	C1++;
	N1 = N;
      }
      else {
	C2++;
	N2 = N;
      }
    }
    if (C1 == C2 && C1 != 1) {
      unsigned long newhash;
      Magic(newhash);
      N1->hashval = newhash;
      N2->hashval = newhash;
      goto converge;
    }
  }

 converge:
  FractureElementClass(&ElementClasses);
  FractureNodeClass(&NodeClasses);
  ExhaustiveSubdivision = 1;
  while (!Iterate() && VerifyMatching() != -1);
  return(VerifyMatching());
}

/*------------------------------------------------------*/
/* PermuteSetup --					*/
/* Add an entry to a cell's "permutes" linked list.	*/
/* Return 1 if OK, 0 if error				*/
/*							*/
/* NOTE:  When comparing two netlists, it is only	*/
/* necessary to permute pins on devices of one of them.	*/
/* So if a device has the same name in two circuits,	*/
/* e.g., "pmos", and is defined as a subcircuit so that	*/
/* it is not automatically permuted according to the	*/
/* default rules, then it is valid to use LookupCell,	*/
/* which returns the first one encountered, mark those	*/
/* pins as permuted, and not touch the other "pmos".	*/
/*------------------------------------------------------*/

int PermuteSetup(char *model, int filenum, char *pin1, char *pin2)
{
    struct Permutation *newperm, *perm;
    struct nlist *tp;
    struct objlist *obj1, *obj2;

    // If -1 is passed as filenum, then re-run this routine on
    // each of Circuit1 and Circuit2 models.

    if (filenum == -1) {
	if ((Circuit1 != NULL) && (Circuit1->file != -1))
	    PermuteSetup(model, Circuit1->file, pin1, pin2);
	if ((Circuit2 != NULL) && (Circuit2->file != -1))
	    PermuteSetup(model, Circuit2->file, pin1, pin2);
	return 1;
    }

    tp = LookupCellFile(model, filenum);
    if (tp == NULL) {
        Printf("No such model %s\n", model);
       	return 0;
    }
    obj1 = LookupObject(pin1, tp);
    if (obj1 == NULL) {
        Printf("No such pin %s in model %s\n", pin1, model);
       	return 0;
    }
    obj2 = LookupObject(pin2, tp);
    if (obj2 == NULL) {
        Printf("No such pin %s in model %s\n", pin2, model);
       	return 0;
    }

    /* Now check that this permutation is not already in the list. */

    for (perm = tp->permutes; perm != NULL; perm = perm->next)
       if ((*matchfunc)(perm->pin1, pin1) && (*matchfunc)(perm->pin2, pin2))
	  return 1;

    newperm = (struct Permutation *)CALLOC(1, sizeof(struct Permutation));
    newperm->pin1 = obj1->name;
    newperm->pin2 = obj2->name;
    newperm->next = tp->permutes;
    tp->permutes = newperm;
    return 1;
}

/*------------------------------------------------------*/
/* PermuteForget --					*/
/* Remove an entry from a cell's "permutes" linked	*/
/* list.  This makes it more convenient to use the	*/
/* default permutations and declare individual		*/
/* exceptions.						*/
/*							*/
/* Return 1 if OK, 0 if error				*/
/*							*/
/* If pin1 and pin2 are NULL, then forget all		*/
/* permutations on all pins.				*/
/*------------------------------------------------------*/

int PermuteForget(char *model, int filenum, char *pin1, char *pin2)
{
    struct Permutation *lastperm, *perm, *nextperm;
    struct nlist *tp;
    struct objlist *obj1, *obj2;

    // If -1 is passed as filenum, then re-run this routine on
    // each of Circuit1 and Circuit2 models.

    if (filenum == -1) {
	if ((Circuit1 != NULL) && (Circuit1->file != -1))
	    PermuteForget(model, Circuit1->file, pin1, pin2);
	if ((Circuit2 != NULL) && (Circuit2->file != -1))
	    PermuteForget(model, Circuit2->file, pin1, pin2);
	return 1;
    }

    tp = LookupCellFile(model, filenum);
    if (tp == NULL) {
        Printf("No such model %s\n", model);
       	return 0;
    }

    if ((pin1 != NULL) && (pin2 != NULL)) {
	obj1 = LookupObject(pin1, tp);
	if (obj1 == NULL) {
            Printf("No such pin %s in model %s\n", pin1, model);
       	    return 0;
	}
	obj2 = LookupObject(pin2, tp);
	if (obj2 == NULL) {
            Printf("No such pin %s in model %s\n", pin2, model);
       	    return 0;
	}

	/* Now remove this permutation from the list, if it's there. */

	lastperm = NULL;
	for (perm = tp->permutes; perm != NULL;) {
	    nextperm = perm->next;
	    if (((*matchfunc)(perm->pin1, pin1) &&
			(*matchfunc)(perm->pin2, pin2)) ||
			((*matchfunc)(perm->pin1, pin2) &&
			(*matchfunc)(perm->pin2, pin1))) {
		if (lastperm == NULL)
		    tp->permutes = perm->next;
		else
		    lastperm->next = perm->next;
		FREE(perm);
		break;
	    }	
	    else
		lastperm = perm;
	    perm = nextperm;
	}
    }
    else {

	/* Blanket remove all permutations for this device */

	lastperm = NULL;
	for (perm = tp->permutes; perm != NULL;) {
	    nextperm = perm->next;
	    FREE(perm);
	    perm = nextperm;
	}
    }
    return 1;
}

/*------------------------------------------------------*/
/* Permute --						*/
/* For each entry in a cell's "permutes" linked list,	*/
/* set the magic numbers of pin1 and pin2 to be the	*/
/* same.						*/
/* Return 1 if OK, 0 if error				*/
/*------------------------------------------------------*/

int Permute()
{
   struct Permutation *perm;
   struct ElementClass *EC;
   struct Element *E;
   struct NodeList *NL;
   struct objlist *ob;
   struct nlist *tp;
   unsigned long one, two;

   for (EC = ElementClasses; EC != NULL; EC = EC->next) {
      for (E = EC->elements; E != NULL; E = E->next) {
	 tp = LookupCellFile(E->object->model.class, E->graph);
	 for (perm = tp->permutes; perm != NULL; perm = perm->next) {
	    one = two = 0;
	    ob = E->object;
	    for (NL = E->nodelist; NL != NULL && !one; NL = NL->next) {
	       if ((*matchfunc)(perm->pin1, ob->name
				+ strlen(ob->instance.name) + 1)) 
		  one = NL->pin_magic;
	       ob = ob->next;
	    }
	    ob = E->object;
	    for (NL = E->nodelist; NL != NULL && !two; NL = NL->next) {
	       if ((*matchfunc)(perm->pin2, ob->name
				+ strlen(ob->instance.name) + 1)) 
		  two = NL->pin_magic;
	       ob = ob->next;
	    }
	    if (one == 0) {
	       Fprintf(stderr, "Class %s does not have pin %s.\n",
				tp->name, perm->pin1);
	       if (two == 0)
		  Fprintf(stderr, "Class %s does not have pin %s.\n",
				tp->name, perm->pin2);
	       return (0);
	    }
	    if (two == 0) {
	       Fprintf(stderr, "Class %s does not have pin %s.\n",
				tp->name, perm->pin2);
	       return (0);
 	    }

	    /* update magic numbers */
	    for (NL = E->nodelist; NL != NULL; NL = NL->next) 
	       if (NL->pin_magic == one)
		  NL->pin_magic = two;
	 }
      }
   }
   return(1);
}
	
/*------------------------------------------------------*/
/* Force two elements two be declared matching between	*/
/* the two circuits being compared			*/
/* Return 1 on success, 0 on failure			*/
/*------------------------------------------------------*/

int EquivalenceElements(char *name1, int file1, char *name2, int file2)
{
  struct ElementClass *EC;
  struct Element *E, *E1, *E2;

  if (Circuit1 == NULL || Circuit2 == NULL) {
     Printf("Circuits not being compared!\n");
     return 1;
  }

  for (EC = ElementClasses; EC != NULL; EC = EC->next) {
    E1 = E2 = NULL;
    for (E = EC->elements; E != NULL; E = E->next) {
      if (E->graph==file1 && E1==NULL &&
		(*matchfunc)(E->object->instance.name, name1)) E1=E;
      if (E->graph==file2 && E2==NULL &&
		(*matchfunc)(E->object->instance.name, name2)) E2=E;
    }
    if (E1 != NULL || E2 != NULL) {
      struct ElementClass *NewList, *EndOfNewList;
      if (E1 == NULL || E2 == NULL) 
	return 0;  /* did not find both in same equivalence class */
      /* otherwise, create a new equivalence class */
      for (E = EC->elements; E != NULL; E = E->next) {
	if (E == E1 || E == E2) E->hashval = 1;
	else E->hashval = 0;
      }

      /* now make new equivalence classes, and tear EC out of old list */

      NewList = MakeElist(EC->elements);
      for (EndOfNewList = NewList; EndOfNewList->next != NULL; 
	   EndOfNewList = EndOfNewList->next) ;
      EndOfNewList->next = EC->next;
      if (EC == ElementClasses) {
	FreeElementClass (ElementClasses);
	ElementClasses = NewList;
      }
      else {
	struct ElementClass *EC3;
	for (EC3 = ElementClasses; EC3->next != EC; EC3 = EC3->next) ;
	EC3->next = NewList;
	FreeElementClass(EC);
      }
      return 1;
    }
  }
  return 0;
}

/*------------------------------------------------------*/
/* Force two nodes to be declared matching between the	*/
/* two circuits being compared.				*/
/* Return 1 on success, 0 on failure			*/
/*------------------------------------------------------*/

int EquivalenceNodes(char *name1, int file1, char *name2, int file2)
{
  int node1, node2;
  struct objlist *ob;
  struct NodeClass *NC;
  struct Node *N, *N1, *N2;
  struct nlist *np1, *np2;

  if (Circuit1 == NULL || Circuit2 == NULL) {
     Fprintf(stderr, "Circuits not being compared!\n");
     return 1;
  }

  if (file1 == Circuit1->file) {
     np1 = Circuit1;
     np2 = Circuit2;
  }
  else {
     np1 = Circuit2;
     np2 = Circuit1;
  }

  ob = LookupObject(name1, np1);
  if (ob == NULL) return 0;
  node1 = ob->node;

  ob = LookupObject(name2, np2);
  if (ob == NULL) return 0;
  node2 = ob->node;

  for (NC = NodeClasses; NC != NULL; NC = NC->next) {
    N1 = N2 = NULL;
    for (N = NC->nodes; N != NULL; N = N->next) {
      if (N->graph== file1 && N1==NULL && N->object->node == node1) N1=N;
      if (N->graph== file2 && N2==NULL && N->object->node == node2) N2=N;
    }
    if (N1 != NULL || N2 != NULL) {
      struct NodeClass *NewList, *EndOfNewList;
      if (N1 == NULL || N2 == NULL) 
	return(0);  /* did not find both in same equivalence class */
      /* otherwise, create a new equivalence class */
      for (N = NC->nodes; N != NULL; N = N->next) {
	if (N == N1 || N == N2) N->hashval = 1;
	else N->hashval = 0;
      }
      /* now make new equivalence classes, and tear NC out of old list */
      NewList = MakeNlist(NC->nodes);
      for (EndOfNewList = NewList; EndOfNewList->next != NULL; 
	   EndOfNewList = EndOfNewList->next) ;
      EndOfNewList->next = NC->next;
      if (NC == NodeClasses) {
	FreeNodeClass (NodeClasses);
	NodeClasses = NewList;
      }
      else {
	struct NodeClass *NC3;
	for (NC3 = NodeClasses; NC3->next != NC; NC3 = NC3->next) ;
	NC3->next = NewList;
	FreeNodeClass(NC);
      }
      return 1;
    }
  }
  return 0;
}

/*------------------------------------------------------*/
/* Ignore any class named "name" in the input.  If	*/
/* netlists exist at the time this routine is called,	*/
/* then remove all elements of this class from the	*/
/* database.						*/
/*------------------------------------------------------*/

int IgnoreClass(char *name, int file, unsigned char type)
{
   struct IgnoreList *newIgnore;

   if ((file == -1) && (Circuit1 != NULL) && (Circuit2 != NULL)) {
      IgnoreClass(name, Circuit1->file, type);
      IgnoreClass(name, Circuit2->file, type);
      return 0;
   }

   newIgnore = (struct IgnoreList *)MALLOC(sizeof(struct IgnoreList));
   newIgnore->next = ClassIgnore;
   ClassIgnore = newIgnore;
   newIgnore->class = (char *)MALLOC(1 + strlen(name));
   strcpy(newIgnore->class, name);
   newIgnore->file = file;
   newIgnore->type = type;

   /* Remove existing classes from database */
   if (type == IGNORE_CLASS)
      ClassDelete(name, file);
   else
      RemoveShorted(name, file);

   return 0;
}

/*------------------------------------------------------*/
/* Declare that the device class "name1" in file1	*/
/* is the same as the device class "name2" in file2	*/
/*							*/
/* If file1 and file2 are -1, then these are names to	*/
/* be checked as netcmp works through the hierarchy.  	*/
/* Otherwise, look up the structure for each file and	*/
/* set the classhash of the second to that of the first	*/
/*							*/
/* Return 1 on success, 0 on failure			*/
/*------------------------------------------------------*/

int EquivalenceClasses(char *name1, int file1, char *name2, int file2)
{
   char *class1, *class2;
   struct Correspond *newc;
   struct nlist *tp, *tp2, *tpx;
   unsigned char need_new_seed = 0;
   int reverse = 0;

   if (file1 != -1 && file2 != -1) {

      tp = LookupClassEquivalent(name1, file1, file2);
      if (tp && (*matchfunc)(tp->name, name2))
	 return 1;	/* Already equivalent */

      tp = LookupCellFile(name1, file1);
      tp2 = LookupCellFile(name2, file2);
      if (tp->classhash == tp2->classhash)
	 return 1;	/* Already equivalent */

      /* Where cells with duplicate cell names have been checked and	*/
      /* found to be equivalent, the original keeps the hash value.	*/

      if (tp->flags & CELL_DUPLICATE)
	 reverse = 1;

      /* Do a cross-check for each name in the other netlist.  If 	*/
      /* conflicting names exist, then alter the classhash to make it	*/
      /* unique.  In the case of duplicate cells, don't do this.	*/

      if (!(tp->flags & CELL_DUPLICATE) && !(tp2->flags & CELL_DUPLICATE)) {
	 tpx = LookupCellFile(name1, file2);
	 if (tpx != NULL) need_new_seed = 1;
	 tpx = LookupCellFile(name2, file1);
	 if (tpx != NULL) need_new_seed = 1;
      }

      /* Now make the classhash values the same so that these cells	*/
      /* are indistinguishable by the netlist comparator.		*/

      if (need_new_seed == 1) {
	 char *altname;
	 while (need_new_seed == 1) {
	    altname = (char *)MALLOC(strlen(name1) + 2);
	    sprintf(altname, "%s%c", name1, (char)(65 + Random(26)));
	    tp->classhash = (*hashfunc)(altname, 0);

	    /* Make sure randomly-altered name is not in any netlist */
	    if ((LookupCellFile(altname, file1) == NULL) &&
			(LookupCellFile(altname, file2) == NULL))
		need_new_seed = 0;

	    FREE(altname);
	 }
      }

      if (reverse)
         tp->classhash = tp2->classhash;
      else
         tp2->classhash = tp->classhash;
      return 1;
   }

   /* Create a new class correspondence entry.  Use the names in the	*/
   /* records found by Lookup() so that the don't need to allocate and	*/
   /* free the string records.						*/

   newc = (struct Correspond *)CALLOC(1, sizeof(struct Correspond));
   newc->class1 = strsave(name1);
   newc->file1 = file1;
   newc->class2 = strsave(name2);
   newc->file2 = file2;

   newc->next = ClassCorrespondence;
   ClassCorrespondence = newc;

   return 1;
}

#ifdef TCL_NETGEN

/*----------------------------------------------------------------------*/
/* Callback function used by MatchPins					*/
/*----------------------------------------------------------------------*/

int reorderpins(struct hashlist *p, int file)
{
    struct nlist *ptr;
    struct nlist *tc2 = Circuit2;
    struct objlist *ob, *ob2, *firstpin;
    int i, numports, *nodes, unordered;
    char **names;

    ptr = (struct nlist *)(p->ptr);

    if (ptr->file != file) return 1;	/* Keeps the search going */

    /* Pull the port order from the cell and put it in a list	*/
    /* NOTE:  This method requires the use of "CleanupPins()"	*/
    /* to make sure that there are no unconnected ports, and	*/
    /* that there is a 1:1 match between the port lists of all	*/
    /* instances of both cells.					*/

    numports = 0;
    unordered = 0;
    ob2 = tc2->cell;
    while (ob2 && ob2->type == PORT) {
	if (ob2->model.port < 0) {
	    ob2->model.port = numports;
	    unordered = 1;
	}
	numports++;
	ob2 = ob2->next;
    }
    nodes = (int *)CALLOC(numports, sizeof(int));
    names = (char **)CALLOC(numports, sizeof(char *));

    if (unordered)
	Fprintf(stderr, "Ports of %s are unordered.  "
		"Ordering will be arbitrary.\n", tc2->name);

    for (ob = ptr->cell; ob != NULL; ) {
	if (ob->type == FIRSTPIN) {
	    if ((*matchfunc)(ob->model.class, tc2->name)) {
		char *sptr = ob->instance.name;
		if (*sptr == '/') sptr++;
		if (Debug == TRUE)
		   Fprintf(stdout, "Reordering pins on instance %s\n", sptr);

		firstpin = ob;
		ob2 = tc2->cell;
		for (i = 0; i < numports; i++) {
		    if (ob2->model.port >= numports) {
			Fprintf(stderr, "Port number %d greater than number "
				"of ports %d\n", ob2->model.port + 1, numports);
		    }
		    else {
		        nodes[ob2->model.port] = ob->node;
		        names[ob2->model.port] = ob->name;
		    }

		    ob = ob->next;
		    ob2 = ob2->next;
		    if (i < numports - 1) {
			if (ob == NULL || ob->type <= FIRSTPIN) {
			    Fprintf(stderr, "Instance of %s has only "
				"%d of %d ports\n",
				tc2->name, i + 1, numports);
			    break;
			}
			else if (ob2 == NULL || ob2->type != PORT) {
			    Fprintf(stderr, "Instance of %s has "
				"%d ports, expected %d\n",
				tc2->name, i + 1, numports);
			    break;
			}
		    }
		}
		
		ob = firstpin;
		for (i = 0; i < numports; i++) {
		   if (names[i] == NULL) {
		      ob->name = strsave("port_match_error");
		      ob->node = -1;
		   }
		   else {
		      ob->node = nodes[i];
		      ob->name = names[i];
		   }
		   HashPtrInstall(ob->name, ob, &(ptr->objdict));
		   ob = ob->next;
		   names[i] = NULL;
		   if (ob == NULL) break;	// Error message already output
		}
	    }
	    else
		ob = ob->next;
	}
	else
	    ob = ob->next;
    }

    FREE(nodes);
    FREE(names);
    return 1;		/* Continue the search. . . */
}

/*----------------------------------------------------------------------*/
/* Another callback function used by MatchPins				*/
/* Search through all instances of each cell, find matches for the	*/
/* cell passed through "clientdata", and add extra pins whereever an	*/
/* "UNKNOWN" type is found in the cell's pin list.			*/
/*									*/
/* Always return NULL to keep the search going.				*/
/*----------------------------------------------------------------------*/

struct nlist *addproxies(struct hashlist *p, void *clientdata)
{
    struct nlist *ptr;
    struct nlist *tc = (struct nlist *)clientdata;
    struct objlist *ob, *lob, *tob, *obn, *firstpin;
    int i, numnodes, maxnode;

    ptr = (struct nlist *)(p->ptr);
    if (ptr->file != tc->file) return NULL;	/* Keep going */

    // Count the largest node number used in the cell
    maxnode = -1;
    for (ob = ptr->cell; ob; ob = ob->next)
       if (ob->type >= FIRSTPIN || ob->type == NODE)
	  if (ob->node >= maxnode)
	     maxnode = ob->node + 1;
    numnodes = maxnode;

    lob = NULL;
    ob = ptr->cell;
    while (ob != NULL) {
       while (ob && ob->type != FIRSTPIN) {
	  lob = ob;
	  ob = ob->next;
       }
       if (ob && ob->model.class != NULL) {
	  if (!(*matchfunc)(ob->model.class, tc->name)) {
	     lob = ob;
	     ob = ob->next;
	     continue;
	  }
       }
       if (ob == NULL) break;

       tob = tc->cell;
       i = FIRSTPIN;
       firstpin = ob;
       while (tob && (tob->type == PORT || tob->type == UNKNOWN)) {
	  if (tob->type == UNKNOWN) {
	     /* Do not do anything with (no pins) entries (in the reference cell) */
	     if (strcmp(tob->name, "proxy(no pins)")) {
		/* But if the target cell instance has proxy(no pins), then reuse
		 * the record and modify it.
		 */
	        if (ob && !strcmp(ob->name, "proxy(no pins)")) {
		   obn = ob;
		   FREE(ob->name);
		   obn->name = (char *)MALLOC(strlen(ob->instance.name)
				+ strlen(tob->name) + 2);
		   sprintf(obn->name, "%s/%s", ob->instance.name, tob->name);
		   ob = obn->next;
		}
		else {
		   obn = (struct objlist *)CALLOC(1, sizeof(struct objlist));
		   obn->name = (char *)MALLOC(strlen(firstpin->instance.name)
				+ strlen(tob->name) + 2);
		   sprintf(obn->name, "%s/%s", firstpin->instance.name, tob->name);
		   obn->instance.name = strsave(firstpin->instance.name);
		   obn->model.class = strsave(tc->name);
		   obn->next = ob;	// Splice into object list
		   lob->next = obn;
		}
		obn->type = i++;
		obn->node = numnodes++;
		lob = obn;

		// Hash the new pin record for "LookupObject()"
		HashPtrInstall(obn->name, obn, &(ptr->objdict));

		if (tob == tc->cell) {
		    // Rehash the instance in instdict
		    HashPtrInstall(firstpin->instance.name, firstpin, &(ptr->instdict));
		}
	     }
	     else {
		lob = ob;
		ob->type = i++;
		ob = ob->next;
	     }
	  }
	  else if (ob == NULL) {
	     // This should not happen. . .
	     Fprintf(stdout, "Error:  Premature end of pin list on instance %s.\n",
			firstpin->instance.name);
	     break;
	  }
	  else {
	     lob = ob;
	     ob->type = i++;
	     ob = ob->next;
	  }
	  tob = tob->next;
       }
    }

    /* Insert a record for each new node added to the cell */
    for (i = maxnode; i < numnodes; i++) {
       obn = (struct objlist *)CALLOC(1, sizeof(struct objlist));
       obn->node = i;
       obn->type = NODE;
       obn->model.class = NULL;
       obn->instance.name = NULL;
       obn->name = (char *)MALLOC(12);
       sprintf(obn->name, "dummy_%d", i);
       obn->next = NULL;
       lob->next = obn;
       lob = obn;
       HashPtrInstall(obn->name, obn, &(ptr->objdict));
    }

    // We messed with the node name list, so have to re-cache them
    if (maxnode < numnodes) CacheNodeNames(ptr);

    return NULL;	/* Keep the search going */
}

/*------------------------------------------------------*/
/* Declare that the device class "name1" is equivalent	*/
/* to class "name2".  This is the same as the above	*/
/* routine, except that the cells must be at the top	*/
/* of the compare queue, and must already be proven	*/
/* equivalent by LVS.  Determine a pin correspondence,	*/
/* then modify all instances of "name2" to match all	*/
/* instances of "name1" by pin reordering.  If either	*/
/* cell has disconnected pins, they are shuffled to the	*/
/* end of the pin list.  If two or more pins correspond	*/
/* to net automorphisms, then they are added to the	*/
/* list of permuted pins.				*/
/*							*/
/* NOTE:  This routine must not be called on any	*/
/* circuit pair that has not been matched.  If a 	*/
/* circuit pair has been matched with automorphisms,	*/
/* then some pins may be matched arbitrarily.		*/
/*							*/
/* If "dolist" is 1, append the list representing the	*/
/* output (if any) to variable tcl_out, if it exists.	*/
/*							*/
/* Return codes:					*/
/* 2: Neither cell had pins, so matching is unnecessary	*/
/* 1: Exact match					*/
/* 0: Inexact match resolved by proxy pin insertion	*/
/*------------------------------------------------------*/

int MatchPins(struct nlist *tc1, struct nlist *tc2, int dolist)
{
   char *cover, *ctemp;
   char *bangptr1, *bangptr2, *backslashptr1, *backslashptr2;
   struct objlist *ob1, *ob2, *obn, *obp, *ob1s, *ob2s, *obt;
   struct NodeClass *NC;
   struct Node *N1, *N2;
   int i, j, k, m, a, b, swapped, numnodes, numorig;
   int result = 1, haspins = 0, notempty = 0;
   int hasproxy1 = 0, hasproxy2 = 0;
   int needclean1 = 0, needclean2 = 0;
   char *ostr;
#ifdef TCL_NETGEN
   Tcl_Obj *mlist, *plist1, *plist2;
#endif

   if (tc1 == NULL) tc1 = Circuit1;
   if (tc2 == NULL) tc2 = Circuit2;

   for (ob2 = tc2->cell; ob2 != NULL; ob2 = ob2->next) {
      if (ob2->type != PORT) break;
      else haspins = 1;
      ob2->model.port = -1;
   }
   numnodes = 0;
   for (ob1 = tc1->cell; ob1 != NULL; ob1 = ob1->next) {
      if (ob1->type != PORT) break;
      else haspins = 1;
      numnodes++;
   }

   if (haspins == 0) {
      // Neither cell has any ports, so this is probably a top-level
      // cell and there is nothing to do.
      return 2;
   }

   cover = (char *)CALLOC(numnodes, sizeof(char));
   numorig = numnodes;

#ifdef TCL_NETGEN
   if (dolist) {
      mlist = Tcl_NewListObj(0, NULL);
      plist1 = Tcl_NewListObj(0, NULL);
      plist2 = Tcl_NewListObj(0, NULL);
      Tcl_ListObjAppendElement(netgeninterp, mlist, plist1);
      Tcl_ListObjAppendElement(netgeninterp, mlist, plist2);
   }
#endif

   ostr = CALLOC(right_col_end + 2, sizeof(char));

   if (Debug == 0) {
       /* Format side-by-side comparison of pins */
       Fprintf(stdout, "\nSubcircuit pins:\n");
       *(ostr + left_col_end) = '|';
       *(ostr + right_col_end) = '\n';
       *(ostr + right_col_end + 1) = '\0';
       for (i = 0; i < left_col_end; i++) *(ostr + i) = ' ';
       for (i = left_col_end + 1; i < right_col_end; i++) *(ostr + i) = ' ';
       snprintf(ostr, left_col_end, "Circuit 1: %s", tc1->name);
       snprintf(ostr + left_col_end + 1, left_col_end, "Circuit 2: %s", tc2->name);
       for (i = 0; i < right_col_end + 1; i++) if (*(ostr + i) == '\0') *(ostr + i) = ' ';
       Fprintf(stdout, ostr);
       for (i = 0; i < left_col_end; i++) *(ostr + i) = '-';
       for (i = left_col_end + 1; i < right_col_end; i++) *(ostr + i) = '-';
       Fprintf(stdout, ostr);
   }

   for (NC = NodeClasses; NC != NULL; NC = NC->next) {
      a = 0;
      for (N1 = NC->nodes; N1 != NULL; N1 = N1->next) {
         if (N1->graph == Circuit1->file) {
	    obn = N1->object;
	    if (IsPort(obn)) {
	       i = 0;
	       for (ob1 = tc1->cell; ob1 != NULL; ob1 = ob1->next, i++) {
	          if ((IsPort(ob1)) && (ob1->node == obn->node)) {
		     b = 0;
                     for (N2 = NC->nodes; N2 != NULL; N2 = N2->next) {
	                if (N2->graph != Circuit1->file) {
			   if (b == a) break;
			   else b++;
			}
	             }
	             if (N2 == NULL) {
#ifdef TCL_NETGEN
			if (dolist) {
			   Tcl_SetVar2Ex(netgeninterp, "lvs_out", NULL,
					Tcl_NewStringObj("pins", -1),
					TCL_APPEND_VALUE | TCL_LIST_ELEMENT);
			   Tcl_SetVar2Ex(netgeninterp, "lvs_out", NULL, mlist,
					TCL_APPEND_VALUE | TCL_LIST_ELEMENT);
			}
#endif
			FREE(ostr);
			return 1;
		     }

		     obp = N2->object;
		     j = 0;
	             for (ob2 = tc2->cell; ob2 != NULL; ob2 = ob2->next, j++) {
	          	if ((IsPort(ob2)) && (ob2->node == obp->node)) {
			   if (Debug == 0) {
			      for (m = 0; m < left_col_end; m++) *(ostr + m) = ' ';
			      for (m = left_col_end + 1; m < right_col_end; m++) *(ostr + m) = ' ';
			      snprintf(ostr, left_col_end, "%s", ob1->name);
			      if ((*matchfunc)(ob1->name, ob2->name))
			         snprintf(ostr + left_col_end + 1, left_col_end, "%s", ob2->name);
			      else {
				 /* Check remainder of ports to see if there is a name match on the
				  * same net number (multiple ports tied to the same net)
				  */
				 struct objlist *ob3;
				 for (ob3 = ob2->next, ++j; ob3 != NULL; ob3 = ob3->next, j++) {
				     if ((IsPort(ob3)) && (ob3->node == ob2->node)) {
					if ((*matchfunc)(ob3->name, ob1->name)) {
					   ob2 = ob3;
			         	   snprintf(ostr + left_col_end + 1, left_col_end, "%s", ob2->name);
					   break;
					}
				     }
				     else {
					ob3 = NULL;
					break;	/* All pins w/the same node should be together */
				     }
				 }
				 if (ob3 == NULL) {
				    if (ob2->model.port == -1)
				       snprintf(ostr + left_col_end + 1, left_col_end, "%s **Mismatch**", ob2->name);
				    else
				       snprintf(ostr + left_col_end + 1, left_col_end, "(no matching pin)");
				    /* Pins with different names are on different nets,
				     * so this should trigger an error return code.
				     */
				    result = 0;
				 }
			      }
			      for (m = 0; m < right_col_end + 1; m++)
				 if (*(ostr + m) == '\0') *(ostr + m) = ' ';
			      Fprintf(stdout, ostr);
			   }
			   else {
			      Fprintf(stdout, "Circuit %s port %d \"%s\""
					" = cell %s port %d \"%s\"\n",
					tc1->name, i, obn->name,
					tc2->name, j, obp->name);
			   }
#ifdef TCL_NETGEN
			   if (dolist) {
		              Tcl_ListObjAppendElement(netgeninterp, plist1,
					Tcl_NewStringObj(obn->name, -1));
		              Tcl_ListObjAppendElement(netgeninterp, plist2,
					Tcl_NewStringObj(obp->name, -1));
			   }
#endif
			   ob2->model.port = i;		/* save order */
			   *(cover + i) = (char)1;

			   /* If there are multiple pins on the same net, cycle through them;	*/
			   /* otherwise, move to the next entry in the partition.		*/
			   if (ob1->next && (ob1->next->node == ob1->node)) {
			      ob1 = ob1->next;
			      ob2 = tc2->cell;	/* Restart search for matching pin */
			      i++;
			   }
			   else
			      break;
			}
		     }
		     if (ob2 == NULL) {
			if (Debug == 0) {
			   // If first cell has no pins but 2nd cell
			   // does, then "no matching pin" entries will
			   // be generated for all pins in the 2nd cell,
			   // so don't print out the "no pins" entry.

			   if (strcmp(obn->name, "(no pins)")) {
			      for (m = 0; m < left_col_end; m++) *(ostr + m) = ' ';
			      for (m = left_col_end + 1; m < right_col_end; m++) *(ostr + m) = ' ';
			      snprintf(ostr, 32, "%s", obn->name);
			      snprintf(ostr + left_col_end + 1, left_col_end, "(no matching pin)");
			      for (m = 0; m < right_col_end + 1; m++)
				 if (*(ostr + m) == '\0') *(ostr + m) = ' ';
			      Fprintf(stdout, ostr);
			   }
			}
			else {
		           Fprintf(stderr, "No matching pin in cell %s for "
					"cell %s pin %s\n",
					tc2->name, tc1->name, obn->name);
			}
#ifdef TCL_NETGEN
			if (dolist && strcmp(obn->name, "(no pins)")) {
		           Tcl_ListObjAppendElement(netgeninterp, plist1,
				Tcl_NewStringObj(obn->name, -1));
		           Tcl_ListObjAppendElement(netgeninterp, plist2,
				Tcl_NewStringObj("(no matching pin)", -1));
			}
#endif
			result = 0;

			/* Make a pass through circuit 1 to find out if	*/
			/* the pin really is connected to anything, or	*/
			/* has been left orphaned after flattening.  If	*/
			/* disconnected, set its node number to -2.	*/

			notempty = 0;
			for (obt = ob1->next; obt; obt = obt->next) {
			   if (obt->type >= FIRSTPIN) {
			      notempty = 1;
			      if (obt->node == ob1->node)
				 break;
			   }
			}
		        if ((obt == NULL) && (notempty == 1)) {
			   ob1->node = -2;	// Will run this through cleanuppins
			   needclean1 = 1;
			}
		     }
		     break;
		  }
	       }

	       if (ob1 == NULL) {
		  if (Debug == 0) {
		     for (m = 0; m < left_col_end; m++) *(ostr + m) = ' ';
		     for (m = left_col_end + 1; m < right_col_end; m++) *(ostr + m) = ' ';
		     snprintf(ostr, left_col_end, "%s", obn->name);
		     snprintf(ostr + left_col_end + 1, left_col_end, "(no matching pin)");
		     for (m = 0; m < right_col_end + 1; m++)
			if (*(ostr + m) == '\0') *(ostr + m) = ' ';
		     Fprintf(stdout, ostr);
		  }
		  else {
		     Fprintf(stderr, "No netlist match for cell %s pin %s\n",
				tc1->name, obn->name);
		  }
#ifdef TCL_NETGEN
		  if (dolist) {
		     Tcl_ListObjAppendElement(netgeninterp, plist1,
				Tcl_NewStringObj(obn->name, -1));
		     Tcl_ListObjAppendElement(netgeninterp, plist2,
				Tcl_NewStringObj("(no matching pin)", -1));
		  }
#endif
		  result = 0;
	       }
	    }
	    a++;
	 }
      }
   }

   /* Do any unmatched pins have the same name? 		*/
   /* This should not happen if unconnected pins are eliminated	*/
   /* so apply only to black-box (CELL_PLACEHOLDER) entries.	*/ 
   /* (Semi-hack: Allow "!" global flag) */
   /* (Another semi-hack: Ignore the leading backslash in	*/
   /* backslash-escaped verilog names.  Removing the backslash	*/
   /* and ending space character is a common way to convert to	*/
   /* legal SPICE.						*/

   ob1 = tc1->cell;
  
   for (i = 0; i < numorig; i++) {
      bangptr1 = strrchr(ob1->name, '!');
      if (bangptr1 && (*(bangptr1 + 1) == '\0'))
         *bangptr1 = '\0';
      else bangptr1 = NULL;

      backslashptr1 = (*(ob1->name) == '\\') ? ob1->name + 1 : ob1->name;

      if (*(cover + i) == (char)0) {
	 j = 0;
         for (ob2 = tc2->cell; ob2 != NULL; ob2 = ob2->next) {
	    char *name1, *name2;

	    if (!IsPort(ob2)) break;

	    bangptr2 = strrchr(ob2->name, '!');
	    if (bangptr2 && (*(bangptr2 + 1) == '\0'))
	       *bangptr2 = '\0';
	    else bangptr2 = NULL;

   	    backslashptr2 = (*(ob2->name) == '\\') ? ob2->name + 1 : ob2->name;

	    name1 = backslashptr1;
	    name2 = backslashptr2;

	    /* Recognize proxy pins as matching unconnected pins */
	    if (!strncmp(name1, "proxy", 5) && (ob2->node == -1)) name1 +=5;
	    if (!strncmp(name2, "proxy", 5) && (ob1->node == -1)) name2 +=5;

	    if ((*matchfunc)(name1, name2)) {

	       /* If both sides have unconnected nodes, then pins with	*/
	       /* matching names are an automatic match.  Otherwise, if	*/
	       /* matching black-box entries, then pins are always	*/
	       /* matched by name.					*/

	       if (((ob1->node == -1) && (ob2->node == -1)) ||
			(((tc1->flags & CELL_PLACEHOLDER) &&
			(tc2->flags & CELL_PLACEHOLDER)) ||
			(NodeClasses == NULL))) {

	          ob2->model.port = i;		/* save order */
	          *(cover + i) = (char)1;

	          if (Debug == 0) {
		     for (m = 0; m < left_col_end; m++) *(ostr + m) = ' ';
		     for (m = left_col_end + 1; m < right_col_end; m++) *(ostr + m) = ' ';
		     snprintf(ostr, left_col_end, "%s", ob1->name);
		     snprintf(ostr + left_col_end + 1, left_col_end, "%s", ob2->name);
		     for (m = 0; m < right_col_end + 1; m++)
		        if (*(ostr + m) == '\0') *(ostr + m) = ' ';
		     Fprintf(stdout, ostr);
	          }
	          else {
		     Fprintf(stdout, "Circuit %s port %d \"%s\""
				" = cell %s port %d \"%s\"\n",
				tc1->name, i, ob1->name,
				tc2->name, j, ob2->name);
	          }
#ifdef TCL_NETGEN
	          if (dolist) {
		     Tcl_ListObjAppendElement(netgeninterp, plist1,
				Tcl_NewStringObj(ob1->name, -1));
		     Tcl_ListObjAppendElement(netgeninterp, plist2,
				Tcl_NewStringObj(ob2->name, -1));
	          }
#endif
	          if (bangptr2) *bangptr2 = '!';
		  break;
	       }
	    }
	    if (bangptr2) *bangptr2 = '!';
	    j++;
         }
      }
      ob1 = ob1->next;
      if (bangptr1) *bangptr1 = '!';
   }

   /* Find the end of the pin list in tc1, for adding proxy pins */

   for (ob1 = tc1->cell; ob1 != NULL; ob1 = ob1->next) {
      if (ob1 && ((ob1->next && ob1->next->type != PORT) || ob1->next == NULL))
	 break;
   }
   if (ob1 == NULL) ob1 = tc1->cell;	/* No ports */

   /* Assign non-matching pins in tc2 with real node	*/
   /* connections in the cell to the end.  Create pins	*/
   /* in tc1 to match.					*/

   for (ob2 = tc2->cell; ob2 != NULL; ob2 = ob2->next) {
      if (ob2->type != PORT) break;
      if (ob2->model.port == -1) {

	 if (Debug == 0) {
	    // See above for reverse case
	    if (strcmp(ob2->name, "(no pins)")) {
	       for (m = 0; m < left_col_end; m++) *(ostr + m) = ' ';
	       for (m = left_col_end + 1; m < right_col_end; m++) *(ostr + m) = ' ';
	       snprintf(ostr, left_col_end, "(no matching pin)");
	       snprintf(ostr + left_col_end + 1, left_col_end, "%s", ob2->name);
	       for (m = 0; m < right_col_end + 1; m++)
		  if (*(ostr + m) == '\0') *(ostr + m) = ' ';
	       Fprintf(stdout, ostr);
	    }
	 }
	 else {
	    Fprintf(stderr, "No netlist match for cell %s pin %s\n",
				tc2->name, ob2->name);
	 }

	 /* Before making a proxy pin, check to see if	*/
	 /* flattening instances has left a port with a	*/
	 /* net number that doesn't connect to anything	*/

         notempty = 0;
	 for (obt = ob2->next; obt; obt = obt->next) {
	    if (obt->type >= FIRSTPIN) {
               notempty = 1;
	       if (obt->node == ob2->node)
		  break;
	    }
	 }
	 if ((obt == NULL) && (notempty == 1)) {
	    ob2->node = -2;	// Will run this through cleanuppins
	    needclean2 = 1;

	    /* On the top level, missing pins are an error, even if	*/
	    /* they appear to match unconnected pins on the other side. */
	    if (CompareQueue == NULL)
		result = 0;

#ifdef TCL_NETGEN
            if (dolist) {
	       Tcl_ListObjAppendElement(netgeninterp, plist1,
			Tcl_NewStringObj("(no pin)", -1));
	       Tcl_ListObjAppendElement(netgeninterp, plist2,
			Tcl_NewStringObj(ob2->name, -1));
            }
#endif
	    continue;
	 }
	 else if (notempty == 1) {
	    /* Flag this as an error */
	    result = 0;
#ifdef TCL_NETGEN
            if (dolist) {
	       Tcl_ListObjAppendElement(netgeninterp, plist1,
			Tcl_NewStringObj("(no matching pin)", -1));
	       Tcl_ListObjAppendElement(netgeninterp, plist2,
			Tcl_NewStringObj(ob2->name, -1));
            }
#endif
	 }
	 ob2->model.port = numnodes++;	// Assign a port order

	 /* Add a proxy pin to tc1 */
	 /* Technically, this should have a matching net number. */
	 /* But nothing connects to it, so it is only needed to  */
	 /* make sure the "pin magic" numbers are correctly	 */
	 /* assigned to both cells.				 */

         obn = (struct objlist *)CALLOC(1, sizeof(struct objlist));
         obn->name = (char *)MALLOC(6 + strlen(ob2->name));
         sprintf(obn->name, "proxy%s", ob2->name);
         obn->type = UNKNOWN;
         // obn->model.port = -1;
         obn->instance.name = NULL;
         obn->node = -1;
	 if (ob1 == tc1->cell) {
	    obn->next = ob1;
	    tc1->cell = obn;
	 }
	 else {
            obn->next = ob1->next;
            ob1->next = obn;
	 }
         ob1 = obn;
	 hasproxy1 = 1;

	 HashPtrInstall(obn->name, obn, &(tc1->objdict));
      }
   }

   /* Find the end of the pin list in tc2, for adding proxy pins */

   for (ob2 = tc2->cell; ob2 != NULL; ob2 = ob2->next) {
      if (ob2 && ((ob2->next && ob2->next->type != PORT) || ob2->next == NULL))
	 break;
   }
   if (ob2 == NULL) ob2 = tc2->cell;	/* No ports */

   /* If cell 2 has fewer nodes than cell 1, then add dummy (unconnected)  */
   /* pins to cell 2.  If these correspond to numbers missing in the match */
   /* sequence, then fill in the missing numbers.  Otherwise, add the	   */
   /* extra nodes to the end.						   */

   j = 0;
   for (i = 0; i < numorig; i++) {
      if (*(cover + i) == (char)1) continue;

      /* If the equivalent node in tc1 is not disconnected	*/
      /* (node != -1) then we should report a match error,	*/
      /* although this does not necessarily imply an error in	*/
      /* netlist connectivity.					*/ 

      ob1 = tc1->cell;
      for (k = i; k > 0 && ob1 != NULL; k--)
	 ob1 = ob1->next;

      if (ob1 == NULL || ob1->type != PORT || ob1->node >= 0) {
	 /* Check if ob1->node might really be disconnected */
	 for (obn = ob1->next; obn; obn = obn->next) {
	    if (obn->node == ob1->node) break;
	 }
	 if (obn == NULL) ob1->node = -1;	/* Make disconnected */
      }

      if (ob1 == NULL || ob1->type != PORT || ob1->node >= 0
		|| (ob1->node < 0 && tc1->class == CLASS_MODULE)
		|| (ob1->node < 0 && ob1->model.port == -1)) {

	 /* Add a proxy pin to tc2 */
         obn = (struct objlist *)CALLOC(1, sizeof(struct objlist));
	 if (ob1 == NULL) {
	    obn->name = (char *)MALLOC(15);
            sprintf(obn->name, "proxy%d", rand() & 0x3ffffff);
	 }
	 else {
            obn->name = (char *)MALLOC(6 + strlen(ob1->name));
            sprintf(obn->name, "proxy%s", ob1->name);
	 }
         obn->type = UNKNOWN;
         obn->model.port = (i - j);
         obn->instance.name = NULL;
         obn->node = -1;

	 /* Note:  Has this pin already been accounted for? */
	 if (Debug == 0) {
	    if (strcmp(ob1->name, "(no pins)")) {
	       for (m = 0; m < left_col_end; m++) *(ostr + m) = ' ';
	       for (m = left_col_end + 1; m < right_col_end; m++) *(ostr + m) = ' ';
	       snprintf(ostr, left_col_end, "%s", ob1->name);
	       snprintf(ostr + left_col_end + 1, left_col_end, "(no matching pin)");
	       for (m = 0; m < right_col_end + 1; m++)
		  if (*(ostr + m) == '\0') *(ostr + m) = ' ';
	       Fprintf(stdout, ostr);
	    }
	 }
	 else {
	    Fprintf(stderr, "No netlist match for cell %s pin %s\n",
				tc1->name, ob1->name);
	 }

	 if (ob2 == tc2->cell) {
	    obn->next = ob2;
	    tc2->cell = obn;
	 }
	 else {
            obn->next = ob2->next;
            ob2->next = obn;
	 }
         ob2 = obn;
	 hasproxy2 = 1;

	 HashPtrInstall(obn->name, obn, &(tc2->objdict));
      }

      else if (ob1 != NULL && ob1->type == PORT) {
	 /* Disconnected node was not meaningful, has no pin match in	*/
	 /* the compared circuit, and so should be discarded.		*/
	 ob1->node = -2;
	 needclean1 = 1;

	 /* Adjust numbering around removed node */
	 for (ob2s = tc2->cell; ob2s != NULL && ob2s->type == PORT; ob2s = ob2s->next) {
	    if (ob2s->model.port > (i - j)) ob2s->model.port--;
	 }
	 j++;
      }
   }
   FREE(cover);

   if (Debug == 0) {
      for (i = 0; i < right_col_end; i++) *(ostr + i) = '-';
      Fprintf(stdout, ostr);
   }

   /* Run cleanuppins on circuit 1 */
   if (needclean1) {
      CleanupPins(tc1->name, tc1->file);
   }

   /* Add proxy pins to all instances of Circuit1 */

   if (hasproxy1) {
      RecurseCellHashTable2(addproxies, (void *)(tc1));
      CacheNodeNames(tc1);
   }

   /* Clean up "UNKNOWN" records from Circuit1 */

   for (obn = tc1->cell; obn; obn = obn->next) {
      if (obn->type == UNKNOWN) obn->type = PORT;
      else if (obn->type != PORT) break;
   }

   /* Run cleanuppins on circuit 2 */
   if (needclean2) {
      CleanupPins(tc2->name, tc2->file);
   }

   /* Add proxy pins to all instances of Circuit2 */

   if (hasproxy2) {
      RecurseCellHashTable2(addproxies, (void *)(tc2));
      CacheNodeNames(tc2);
   }

   /* Clean up "UNKNOWN" records from Circuit2 */

   for (obn = tc2->cell; obn; obn = obn->next) {
      if (obn->type == UNKNOWN) obn->type = PORT;
      else if (obn->type != PORT) break;
   }

   /* Check for ports that did not get ordered */
   for (obn = tc2->cell; obn && (obn->type == PORT); obn = obn->next) {
      if (obn->model.port == -1) {
	 if (obn->node == -1) {
	    // This only happens when pins have become separated from any net.
	 }
	 else {
	    // This should not happen. . .
	    Fprintf(stderr, "Error:  Connected pin %s (node %d) did not get "
			"ordered!\n", obn->name, obn->node);
	 }
      }
   }

   /* Reorder pins in Circuit2 instances to match Circuit1 */

   RecurseCellFileHashTable(reorderpins, Circuit2->file);

   /* Reorder pins in Circuit2 cell to match Circuit1		*/
   /* Unlike the instance records, the structures are swapped,	*/
   /* so the object hash pointers don't become invalid.		*/

   do {
      swapped = 0;
      obn = NULL;
      for (ob2 = tc2->cell; ob2 != NULL; ob2 = ob2->next) {
	 if (ob2->next != NULL && IsPort(ob2) && IsPort(ob2->next)) {
	    if (ob2->model.port > ob2->next->model.port) {
	       swapped++;
	       if (obn != NULL) {
		  obn->next = ob2->next;
	          ob2->next = ob2->next->next;
	          obn->next->next = ob2;
	       }
	       else {
		  tc2->cell = ob2->next;
	          ob2->next = ob2->next->next;
	          tc2->cell->next = ob2;
	       }
	       break;
	    }
	 }
	 obn = ob2;
      }
   } while (swapped > 0);

   /* Whether or not pins matched, reset ob2's pin indexes to 0 */

   for (ob2 = tc2->cell; ob2 != NULL; ob2 = ob2->next) {
      if (ob2->type != PORT) break;
      ob2->model.port = -1;
   }

#ifdef TCL_NETGEN
   /* Handle list output */

   if (dolist) {
      Tcl_SetVar2Ex(netgeninterp, "lvs_out", NULL,
			Tcl_NewStringObj("pins", -1),
			TCL_APPEND_VALUE | TCL_LIST_ELEMENT);
      Tcl_SetVar2Ex(netgeninterp, "lvs_out", NULL, mlist,
			TCL_APPEND_VALUE | TCL_LIST_ELEMENT);
   }
#endif

   FREE(ostr);
   return result;
}

/*------------------------------------------------------*/
/* Find the equivalent node to object "ob" in the other	*/
/* circuit.  Return a pointer to the object structure	*/
/* of the equivalent node or NULL if there is no	*/
/* equivalent.						*/
/*							*/
/* It is the responsibility of the calling function to	*/
/* decide what to do about automorphisms.		*/
/*							*/
/* Return 1 if a match is found, 0 if a match is not	*/
/* found, and -1 if the node itself is not found.	*/
/*------------------------------------------------------*/

int EquivalentNode(char *name, struct nlist *circuit, struct objlist **retobj)
{
  short ckt1;
  struct NodeClass *NC;
  struct Node *N1, *N2;
  struct objlist *ob;
  int retval = -1;

  if (Circuit1 == NULL || Circuit2 == NULL) return retval;

  if (circuit != NULL) {
     ob = LookupObject(name, circuit);
     if (ob == NULL) return retval;
  }
  else {
     ob = LookupObject(name, Circuit1);
     if (ob == NULL) {
        ob = LookupObject(name, Circuit2);
        if (ob == NULL) return retval;
     }
  }

  for (NC = NodeClasses; NC != NULL; NC = NC->next) {
    for (N1 = NC->nodes; N1 != NULL; N1 = N1->next) {
      if (N1->object == ob) {
	retval = 0;		/* Node exists. . . */
	ckt1 = N1->graph;
        for (N2 = NC->nodes; N2 != NULL; N2 = N2->next)
	  if (N2->graph != ckt1) {
	    *retobj = N2->object;
	    return 1;
	  }
      }
    }
  }
  return retval;
}

/*------------------------------------------------------*/
/* Find the equivalent element to object "ob" in the	*/
/* other circuit.  Return a pointer to the object 	*/
/* structure of the equivalent element or NULL if there	*/
/* is no equivalent.					*/
/*							*/
/* It is the responsibility of the calling function to	*/
/* decide what to do about automorphisms.		*/
/*							*/
/* Return 1 if a match was found, 0 if the element has	*/
/* no match, and -1 if the element was not found.	*/
/*------------------------------------------------------*/

int EquivalentElement(char *name, struct nlist *circuit, struct objlist **retobj)
{
  short ckt1;
  struct ElementClass *EC;
  struct Element *E1, *E2;
  struct objlist *ob;
  int retval = -1;

  if (Circuit1 == NULL || Circuit2 == NULL) return retval;

  if (circuit != NULL) {
     ob = LookupInstance(name, circuit);
     if (ob == NULL) return retval;
  }
  else {
     ob = LookupInstance(name, Circuit1);
     if (ob == NULL) {
        ob = LookupInstance(name, Circuit2);
        if (ob == NULL) return retval;
     }
  }

  for (EC = ElementClasses; EC != NULL; EC = EC->next) {
     for (E1 = EC->elements; E1 != NULL; E1 = E1->next) {
        if (E1->object == ob) {
	   retval = 0;
	   ckt1 = E1->graph;
           for (E2 = EC->elements; E2 != NULL; E2 = E2->next)
	      if (E2->graph != ckt1) {
	         *retobj = E2->object;
	         return 1;
	      }
        }
     }
  }
  return retval;
}

/*------------------------------------------------------*/
/* Flatten the two cells at the top of the compare	*/
/* queue.						*/
/*------------------------------------------------------*/

void FlattenCurrent()
{
   if (Circuit1 != NULL && Circuit2 != NULL) {
      Fprintf(stdout, "Flattening subcell %s\n", Circuit1->name);
      FlattenInstancesOf(Circuit1->name, Circuit1->file);

      Fprintf(stdout, "Flattening subcell %s\n", Circuit2->name);
      FlattenInstancesOf(Circuit2->name, Circuit2->file);
   }
}

/*------------------------------------------------------*/
/* Handler is only used when netgen is run from a	*/
/* terminal, not the Tk console.			*/
/*------------------------------------------------------*/

void handler(int sig)
{
   /* Don't do anything else here! */
   InterruptPending = 1;
}

/*------------------------------------------------------*/
/* Set up the interrupt flag (both methods) and signal	*/
/* handler (terminal-based method only).		*/
/*------------------------------------------------------*/

void enable_interrupt()
{
   InterruptPending = 0;
   oldinthandler = signal(SIGINT, handler);
}

void disable_interrupt()
{
   if (InterruptPending)
      InterruptPending = 0;
   signal(SIGINT, oldinthandler);
}

#else

static jmp_buf jmpenv;

/* static void handler(void) */
static void handler(int sig)
{
  Fprintf(stderr,"\nInterrupt (%d)!!\n", sig);
  Fflush(stderr);
  longjmp(jmpenv,1);
}

/*----------------------------------------------------------------------*/
/* Note that this cover-all routine is not called from the Tcl/Tk	*/
/* version, which replaces it with the script "lvs".			*/
/* return 1 if the two are identical.  Try to resolve automorphisms	*/
/* using the original "full" symmetry breaking algorithm.		*/
/*----------------------------------------------------------------------*/

int Compare(char *cell1, char *cell2)
{
  int automorphisms;

  DescribeContents(cell1, -1, cell2, -1);
  CreateTwoLists(cell1, -1, cell2, -1, 0);
  Permute();
  while (!Iterate()); 
  ExhaustiveSubdivision = 1;
  while (!Iterate());

  automorphisms = VerifyMatching();
  if (automorphisms == -1) {
    MatchFail(cell1, cell2);
    Fprintf(stderr, "Circuits do not match.\n");
    PrintIllegalClasses();
    return(0);
  }
  if (automorphisms == 0) Fprintf(stdout, "Circuits match correctly.\n");
  if (PropertyErrorDetected == 1) {
     Fprintf(stdout, "There were property errors.\n");
     PrintPropertyResults(0);
  }
  else if (PropertyErrorDetected == -1) {
     Fprintf(stdout, "There were missing properties.\n");
     PrintPropertyResults(0);
  }
  if (automorphisms == 0) return(1);

  Fprintf(stdout, "Circuits match with %d automorphisms.\n", automorphisms);
  if (VerboseOutput) PrintAutomorphisms();

  /* arbitrarily resolve automorphisms */
  Fprintf(stdout, "\n");
  Fprintf(stdout, "Resolving automorphisms by arbitrary symmetry breaking:\n");
  while ((automorphisms = ResolveAutomorphisms()) > 0) ;
  if (automorphisms == -1) {
    MatchFail(cell1, cell2);
    Fprintf(stdout, "Circuits do not match.\n");
    return(0);
  }
  Fprintf(stdout, "Circuits match correctly.\n");
  return(1);
}


void NETCOMP(void)
/* a simple command interpreter to manage embedding/routing */
{
  char name[100];
  char name2[100];
  char ch;
  
  setjmp(jmpenv);
  signal(SIGINT,handler);
  MagicSeed(time(NULL));
  
  do {
    promptstring("NETCMP command: ", name);
    ch = name[0];

    switch (ch) {
    case 'c':
      promptstring("Enter cell 1: ",name);
      promptstring("Enter cell 2: ",name2);
      DescribeContents(name, -1, name2, -1);
      CreateTwoLists(name, -1, name2, -1, 0);
#ifdef DEBUG_ALLOC
      PrintCoreStats();
#endif
      break;
    case 'i':
      if (!Iterate()) Printf("Please iterate again.\n");
      else Printf("No fractures made: we're done.\n");
      break;
    case 's':
      SummarizeElementClasses(ElementClasses);
      SummarizeNodeClasses(NodeClasses);
      break;
    case 'P':
      PrintElementClasses(ElementClasses, -1, 0);
      PrintNodeClasses(NodeClasses, -1, 0);
      break;
    case 'r':
      while (!Iterate()) ; 
      /* fall through to 'v' below */
    case 'v':
      if (ElementClasses == NULL || NodeClasses == NULL)
	Printf("Must initialize data structures first.\n");
      else {
	int automorphisms;
	automorphisms = VerifyMatching();
	if (automorphisms == -1) {
	  PrintIllegalClasses();
	  Fprintf(stdout, "Netlists do not match.\n");
	}
	else {
	  if (automorphisms) 
	    Printf("Circuits match with %d automorphisms.\n", automorphisms);
	  else Printf("Circuits match correctly.\n");
	}
      }
      break;
    case 'R':
      if (ElementClasses == NULL || NodeClasses == NULL)
	Printf("Must initialize data structures first.\n");
      else {
	int automorphisms;
	while (!Iterate()) ; 
	automorphisms = VerifyMatching();
	if (automorphisms == -1) Fprintf(stdout, "Netlists do not match.\n");
	else {
	  Printf("Netlists match with %d automorphisms.\n", automorphisms);
	  while ((automorphisms = ResolveAutomorphisms()) > 0)
	    Printf("  automorphisms = %d.\n", automorphisms);
	  if (automorphisms == -1) Fprintf(stdout, "Netlists do not match.\n");
	  else Printf("Circuits match correctly.\n");
	}
      }
      break;
    case 'a':
      PrintAutomorphisms();
      break;
    case 'd':
      /* equivalence two devices */
      Printf("Force matching of two devices.\n");
      promptstring("Enter device in circuit 1: ",name);
      promptstring("Enter device in circuit 2: ",name2);
      if (EquivalenceElements(name, Circuit1->file, name2, Circuit2->file)) 
	Printf("Devices %s and %s are now equivalent.\n", name, name2);
      else Printf("Unable to match devices %s and %s.\n",name, name2);
      break;
    case 'n':
      /* equivalence two nodes */
      Printf("Force matching of two nets.\n");
      promptstring("Enter net in circuit 1: ",name);
      promptstring("Enter net in circuit 2: ",name2);
      if (EquivalenceNodes(name, Circuit1->file, name2, Circuit2->file))
	Printf("Nets %s and %s are now equivalent.\n", name, name2);
      else Printf("Unable to match nets %s and %s.\n",name, name2);
      break;
    case 'p':
      {
	char model[100];
	/* equivalence two pins on a given class of element */
	Printf("Allow permutation of two pins.\n");
	promptstring("Enter cellname: ",model);
	promptstring("Enter pin 1: ",name);
	promptstring("Enter pin 2: ",name2);
	if (PermuteSetup(model, -1, name, name2))
	   Printf("%s == %s\n",name, name2);
	else Printf("Unable to permute pins %s, %s.\n",name, name2);
	break;
      }
    case 't':
      if (PermuteSetup("n", -1, "drain", "source")) 
	Printf("n-channel: source == drain.\n");
      if (PermuteSetup("p", -1, "drain", "source")) 
	Printf("p-channel: source == drain.\n");
      if (PermuteSetup("e", -1, "bottom_a", "bottom_b")) 
	Printf("poly cap: permuting poly1 regions.\n");
      if (PermuteSetup("r", -1, "end_a", "end_b")) 
	Printf("resistor: permuting endpoints.\n");
      if (PermuteSetup("c", -1, "top", "bottom")) 
	Printf("capacitor: permuting sides.\n");
      break;
    case 'x':
      ExhaustiveSubdivision = !ExhaustiveSubdivision;
      Printf("Exhaustive subdivision %s.\n", 
	     ExhaustiveSubdivision ? "ENABLED" : "DISABLED");
      break;
    case 'o':
      RegroupDataStructures();
      break;
    case 'q': break;
    case 'Q' : exit(0);
    default:
      Printf("(c)reate internal data structure\n");
      Printf("do an (i)teration\n");
      Printf("(r)un to completion (convergence)\n");
      Printf("(R)un to completion (resolve automorphisms)\n");
      Printf("(v)erify results\n");
      Printf("print (a)utomorphisms\n");
      Printf("equate two (d)evices\n");
      Printf("equate two (n)ets\n");
      Printf("(p)ermute pins on elements\n");
      Printf("enable (t)ransistor permutations\n");
      Printf("toggle e(x)haustive subdivision\n");
      Printf("(P)rint internal data structure\n");
      Printf("(s)ummarize internal data structure\n");
      Printf("start (o)ver (reset data structures)\n");
      Printf("(q)uit NETCMP, (Q)uit NETGEN\n");
      break;
    }
  } while (ch != 'q');
  signal(SIGINT,SIG_DFL);
}

/* endif TCL_NETGEN */
#endif
	
