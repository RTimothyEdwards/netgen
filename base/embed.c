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

/* embed.c  -- efficient exhaustive embedding algorithm for PROTOCHIP */

/* define to get reams of extra output */
#undef EXTREE_DEBUG

/* define this to use ex_tree to test Exist() */
#undef EX_TREE_FOR_EXIST

#include "config.h"

#include <stdio.h>
#ifdef IBMPC
#include <stdlib.h>
#endif

#ifdef TCL_NETGEN
#include <tcl.h>
#endif

#include "netgen.h"
#include "hash.h"
#include "objlist.h"
#include "netfile.h"
#include "print.h"
#include "embed.h"

int CountExists; /* count number of times test is performed */

#ifdef EX_TREE_FOR_EXIST
/* safe, but excessive */
/* #define EX_SIZE (MAX_ELEMENTS * (MAX_LEAVES+1)) */
#ifdef VMUNIX
#define EX_SIZE 250000
#else
#define EX_SIZE 60000
#endif
#endif /* EX_TREE_FOR_EXIST */



#ifdef EX_TREE_FOR_EXIST

#if EX_SIZE > 64000
#define EX_TREE_WITH_POINTERS
#else
#undef EX_TREE_WITH_POINTERS
#endif

#ifdef EX_TREE_WITH_POINTERS
struct ex {
  struct ex *zero;
  struct ex *one;
};
typedef struct ex *EX_LIST_PTR;
long free_list; /* index into ex_array */

#else

typedef unsigned short EX_LIST_PTR;
struct ex {
  EX_LIST_PTR zero;
  EX_LIST_PTR one;
};
unsigned short free_list; /* index into ex_array */
#endif /* EX_TREE_WITH_POINTERS */

struct ex ex_array[EX_SIZE];
EX_LIST_PTR tree_root;
#endif /* EX_TREE_FOR_EXIST */



#ifdef EX_TREE_FOR_EXIST

#ifdef EX_TREE_WITH_POINTERS
int InitializeExistTest(void)
{
#ifdef EXTREE_DEBUG
  Printf("called InitializeExistTest\n");
#endif
  tree_root = NULL;
  free_list = 0;
  memzero(ex_array, sizeof(ex_array));
  return (1);
}

EX_LIST_PTR GetExListElement(void)
/* return index of next free element */
{
  if (free_list >= EX_SIZE) {
    Fprintf(stderr,"Too many ExListElements; garbage list exhausted\n");
    FatalError = 1;
    return(NULL);
  }
  return(&(ex_array[free_list++]));
}

void AddToExistSet(int E1, int E2)
{
  char ownedleaves[MAX_LEAVES+1];
  int i;
  EX_LIST_PTR ptr;

#if 1
  for (i = 1; i <= Leaves; i++)
    ownedleaves[i] = TestPackedArrayBit(MSTAR[E1],i) || 
      TestPackedArrayBit(MSTAR[E2],i);
#else
  memzero(ownedleaves, sizeof(ownedleaves));
  for (i = 1; i <= Leaves; i++)
    if (TestPackedArrayBit(MSTAR[E1],i) || 
	TestPackedArrayBit(MSTAR[E2],i)) ownedleaves[i] = 1;
#endif

  if (tree_root == NULL) 
    if ((tree_root = GetExListElement()) == NULL) return;

  ptr = tree_root;
  for (i = 1; i <= Leaves; i++) {
    if (ownedleaves[i] == 1) {
      if (ptr->one == NULL) 
	if ((ptr->one = GetExListElement()) == NULL) return;
      ptr = ptr->one;
    }
    else {
      if (ptr->zero == NULL) 
	if ((ptr->zero = GetExListElement()) == NULL) return;
      ptr = ptr->zero;
    }
  }
}
    
int Exists(int E1, int E2)
{
  char ownedleaves[MAX_LEAVES+1];
  int i;
  EX_LIST_PTR ptr;

  CountExists++;
  if (tree_root == NULL) {
#ifdef EXTREE_DEBUG
    Printf("(%d,%d) does not exist (empty list)\n",E1,E2);
#endif
    return(0);
  }

  memzero(ownedleaves, sizeof(ownedleaves));
  for (i = 1; i <= Leaves; i++)
    if (TestPackedArrayBit(MSTAR[E1],i) || 
	TestPackedArrayBit(MSTAR[E2],i)) ownedleaves[i] = 1;

#ifdef EXTREE_DEBUG
  Printf("checking existence of :");
  for (i = 1; i <= Leaves; i++) Printf(" %d",ownedleaves[i]);
  Printf("  ");
#endif

  ptr = tree_root;
  for (i = 1; i <= Leaves; i++) {
    if (ownedleaves[i] == 1) {
      if (ptr->one == NULL) {
#ifdef EXTREE_DEBUG
	Printf("(%d,%d) does not exist (i = %d)\n",E1,E2,i);
#endif
	return(0);
      }
      ptr = ptr->one;
    }
    else {
      if (ptr->zero == NULL) {
#ifdef EXTREE_DEBUG
	Printf("(%d,%d) does not exist (i = %d)\n",E1,E2,i);
#endif
	return(0);
      }
      ptr = ptr->zero;
    }
  }
#ifdef EXTREE_DEBUG
  Printf("(%d,%d) already exists (i = %d)\n",E1,E2,i);
#endif
  return (1);
}

#else

int InitializeExistTest(void)
{
#ifdef EXTREE_DEBUG
  Printf("called InitializeExistTest\n");
#endif
  tree_root = 0;
  free_list = 1;
  memzero(ex_array, sizeof(ex_array));
  return (1);
}

EX_LIST_PTR GetExListElement(void)
/* return index of next free element */
{
  if (free_list >= EX_SIZE) {
    Pprintf(stderr,"Too many ExListElements; garbage list exhausted\n");
    FatalError = 1;
    return(NULL);
  }
  return(free_list++);
}

void AddToExistSet(int E1, int E2)
{
  char ownedleaves[MAX_LEAVES+1];
  int i;
  EX_LIST_PTR ptr;

#if 1
  for (i = 1; i <= Leaves; i++)
    ownedleaves[i] = TestPackedArrayBit(MSTAR[E1],i) || 
      TestPackedArrayBit(MSTAR[E2],i);
#else
  memzero(ownedleaves, sizeof(ownedleaves));
  for (i = 1; i <= Leaves; i++)
    if (TestPackedArrayBit(MSTAR[E1],i) || 
	TestPackedArrayBit(MSTAR[E2],i)) ownedleaves[i] = 1;
#endif

  if (tree_root == 0) 
    if ((tree_root = GetExListElement()) == 0) return;

  ptr = tree_root;
  for (i = 1; i <= Leaves; i++) {
    if (ownedleaves[i] == 1) {
      if (ex_array[ptr].one == 0) {
	/* add it to list */
	if (i == Leaves) ex_array[ptr].one = ptr;
	else if ((ex_array[ptr].one = GetExListElement()) == 0) return;
      }
      ptr = ex_array[ptr].one;
    }
    else {
      if (ex_array[ptr].zero == 0) {
	/* add it to list */
	if (i == Leaves) ex_array[ptr].zero = ptr;
	else if ((ex_array[ptr].zero = GetExListElement()) == 0) return;
      }
      ptr = ex_array[ptr].zero;
    }
  }
}
    
int Exists(int E1, int E2)
{
  char ownedleaves[MAX_LEAVES+1];
  int i;
  EX_LIST_PTR ptr;

  CountExists++;
  if (tree_root == 0) {
#ifdef EXTREE_DEBUG
    Printf("(%d,%d) does not exist (empty list)\n",E1,E2);
#endif
    return(0);
  }

  memzero(ownedleaves, sizeof(ownedleaves));
  for (i = 1; i <= Leaves; i++)
    if (TestPackedArrayBit(MSTAR[E1],i) || 
	TestPackedArrayBit(MSTAR[E2],i)) ownedleaves[i] = 1;

#ifdef EXTREE_DEBUG
  Printf("checking existence of :");
  for (i = 1; i <= Leaves; i++) Printf(" %d",ownedleaves[i]);
  Printf("  ");
#endif

  ptr = tree_root;
  for (i = 1; i <= Leaves; i++) {
    if (ownedleaves[i] == 1) {
      if (ex_array[ptr].one == 0) {
#ifdef EXTREE_DEBUG
	Printf("(%d,%d) does not exist (i = %d)\n",E1,E2,i);
#endif
	return(0);
      }
      ptr = ex_array[ptr].one;
    }
    else {
      if (ex_array[ptr].zero == 0) {
#ifdef EXTREE_DEBUG
	Printf("(%d,%d) does not exist (i = %d)\n",E1,E2,i);
#endif
	return(0);
      }
      ptr = ex_array[ptr].zero;
    }
  }
#ifdef EXTREE_DEBUG
  Printf("(%d,%d) already exists (i = %d)\n",E1,E2,i);
#endif
  return (1);
}

void PrintExistSetStats(FILE *f)
{
  Fprintf(f,"Free list = %d of %d entries; ",free_list,EX_SIZE);
}


#endif /* EX_TREE_WITH_POINTERS */
#else /* EX_TREE_FOR_EXIST */

struct ex_entry {
  unsigned long mstar[(MAX_LEAVES / BITS_PER_LONG)  + 1];
  struct ex_entry *next;
};

#if 1
struct ex_entry *ex_tab[MAX_ELEMENTS];
#else
#ifdef IBMPC
struct ex_entry *ex_tab[4100];
#else
struct ex_entry *ex_tab[66000];
#endif
#endif


void PRINTPACKED(unsigned long *mstar)
{
  int i;
  for (i = 0; i <= PackedLeaves; i++) Printf("%lX ",mstar[i]);
}


#ifdef IBMPC
static unsigned int lochash(unsigned long *mstar)
#else
static unsigned long lochash(unsigned long *mstar)
#endif
{
  int i;
  unsigned long hashval;
  
  hashval = *mstar;
  for (i = 1; i <= PackedLeaves; i++) hashval ^= mstar[i];
  /* we now have a 32 bit field that we want to collapse to 16 / 12 bits */
#ifdef EXTREE_DEBUG
  Printf("hashval = %ld; ",(long)hashval);
#endif

#if 1
  hashval = hashval % (MAX_ELEMENTS - 1);
#else
  hashval = (hashval >> 16) ^ (hashval & 0x0000FFFFL);
#ifdef IBMPC
  hasval = hashval >> 4;
#endif
#endif

#ifdef EXTREE_DEBUG
  Printf("element hashed to %ld\n",(long)hashval);
#endif
#ifdef IBMPC
  return((int)hashval);
#else
  return(hashval);
#endif
}

struct ex_entry *hashlookup(unsigned long *mstar)
{
  struct ex_entry *np;
  int i;

  for (np = ex_tab[lochash(mstar)]; np != NULL; np = np->next) 
    for (i = 0; i <= PackedLeaves && mstar[i] == (np->mstar)[i]; i++) 
      if (i == PackedLeaves) {
#ifdef EXTREE_DEBUG
	Printf("element found in hash table\n");
#endif
	return(np);
      }
#ifdef EXTREE_DEBUG
  Printf("element not in hash table\n");
#endif
  return(NULL);
}

static struct ex_entry *hashinstall(unsigned long *mstar)
{
  struct ex_entry *np;
#ifdef IBMPC
  unsigned int hashval;
#else
  unsigned long hashval;
#endif
  int i;

  hashval = lochash(mstar);
  for (np = ex_tab[hashval]; np != NULL; np = np->next) 
    for (i = 0; i <= PackedLeaves && mstar[i] == (np->mstar)[i]; i++) 
      if (i == PackedLeaves) {
#ifdef EXTREE_DEBUG
	Printf("element found in hash table\n");
#endif
	return(np);
      }

  /* not found in hash table, so install it */
  if ((np = (struct ex_entry *)CALLOC(1, sizeof(struct ex_entry))) == NULL)
    return(NULL);
  /*    memcpy(np->mstar, mstar, (PackedLeaves + 1)*sizeof(long)); */
  memcpy(np->mstar, mstar, sizeof(np->mstar));
  np->next = ex_tab[hashval];
#ifdef EXTREE_DEBUG
  Printf("Element installed in hash table\n");
#endif
  return(ex_tab[hashval] = np);
}

int Exists(int E1, int E2)
{
  int i;
  unsigned long mstar[(MAX_LEAVES / BITS_PER_LONG)  + 1];

  CountExists++;

  for (i = 0; i <= PackedLeaves; i++)
    mstar[i] = MSTAR[E1][i] | MSTAR[E2][i];

#ifdef EXTREE_DEBUG
  Printf("TESTING Existence of (%d,%d)",E1,E2);
  PRINTPACKED(mstar);
  Printf("\n");
#endif
  return (hashlookup(mstar) != NULL);
}

int InitializeExistTest(void) 
{
#ifdef IBMPC
  int i;
#else
  long i;
#endif
  struct ex_entry *np;
  struct ex_entry *npnext;

  for (i = 0; i < sizeof(ex_tab) / sizeof(ex_tab[0]); i++) 
    for (np = ex_tab[i]; np != NULL; np = npnext) {
      npnext = np->next;
      FREE(np);
    }
  memzero(ex_tab, sizeof(ex_tab));
  return(1);
}

void AddToExistSet(int E1, int E2) 
{
  int i;
  unsigned long mstar[(MAX_LEAVES / BITS_PER_LONG)  + 1];

  for (i = 0; i <= PackedLeaves; i++)
    mstar[i] = MSTAR[E1][i] | MSTAR[E2][i];

#ifdef EXTREE_DEBUG
  Printf("Requesting installation of (%d,%d) in hash table\n",E1,E2);
#endif
  hashinstall(mstar);
}

void PrintExistSetStats(FILE *f)
{
  struct ex_entry *np;
#ifdef IBMPC
  int i;
#else
  long i;
#endif
  long bins;
  long nodes;

  bins = 0;
  nodes = 0;
  for (i = 0; i < sizeof(ex_tab) / sizeof(ex_tab[0]); i ++) 
    if ((np = ex_tab[i]) != NULL) {
      bins++;
      while (np != NULL) {
	nodes++;
	np = np->next;
      }
    }

  Fprintf(f,"Exist hash table stats: %ld of %ld bins used",
	 (long)bins, (long)(sizeof(ex_tab) / sizeof(ex_tab[0])));
  if (bins != 0) Fprintf(f,", %ld nodes (%.2f nodes/bin)",
			 nodes, (float)nodes / (float)bins);
  Fprintf(f,"\n");
  Fprintf(f,"Exist hash table memory usage: %ld bytes\n",
	  (long)(sizeof(ex_tab) + nodes * sizeof(struct ex_entry)));
}


#endif /* EX_TREE_FOR_EXIST */

int linelength = 80;


void FreeEmbeddingTree(struct embed *E)
{
  if (E == NULL) return;
  if (E->left != NULL) FreeEmbeddingTree(E->left);
  if (E->right != NULL) FreeEmbeddingTree(E->right);
  FREE(E);
}

struct embed *EmbeddingTree(struct nlist *tp, int E)
/* return element E embedded in a tree of 'struct embed' */
{
  struct embed *node;

  if (E == 0) return(NULL);
  if ((node = (struct embed *)CALLOC(1, sizeof(struct embed))) == NULL)
    return(NULL);

  node->cell = tp;
#if 0
  if (LEVEL(E) == 0) {
#else
  if (L(E) == 0 && R(E) == 0) {
#endif
    /* root element */
    node->instancenumber = E;
    node->level = LEVEL(E);
    return(node);
  }

  /* not a root element */
  node->right = EmbeddingTree(tp, R(E));
  node->left = EmbeddingTree(tp, L(E));
  if (R(E) == 0) node->level = node->left->level + 1;
  else if (L(E) == 0) node->level = node->right->level + 1;
  else node->level = MAX(node->left->level, node->right->level) + 1;
  return(node);
}

struct embed *FlattenEmbeddingTree(struct embed *E)
/* flattens a forest of trees, returning a copy */
{
  struct embed *node;
  int index;

  if (E == NULL) return(NULL);
  if ((node = (struct embed *)CALLOC(1, sizeof(struct embed))) == NULL)
    return(NULL);

  node->cell = E->cell;
  node->level = E->level;

  if (E->left == NULL && E->right == NULL) {
    /* root element */
    struct embed *tmp;
    struct nlist *tp;
    struct objlist *ob;

    ob = InstanceNumber(E->cell,E->instancenumber);
    tp = LookupCell(ob->model.class);

    if (tp->embedding != NULL) {
      tmp = FlattenEmbeddingTree((struct embed *)(tp->embedding));
      node->left = tmp->left;
      node->right = tmp->right;
      node->level = E->level;
      node->instancenumber = 0;
    }
    else memcpy(node, E, sizeof(struct embed));
    return(node);
  }

  /* not a root element */
  node->right = FlattenEmbeddingTree(E->right);
  node->left = FlattenEmbeddingTree(E->left);
  node->level = E->level;

  /* make it a balanced tree */
  for  (index = E->right->level + 1; index < node->level; index++) {
    struct embed *tmp;
    
    if ((tmp = (struct embed *)CALLOC(1, sizeof(struct embed))) == NULL)
      return(NULL);
    tmp->level = index;
    tmp->left = NULL;
    tmp->right = node->right;
    node->right = tmp;
  }
  for  (index = E->right->level + 1; index < node->level; index++) {
    struct embed *tmp;
    
    if ((tmp = (struct embed *)CALLOC(1, sizeof(struct embed))) == NULL)
      return(NULL);
    tmp->level = index;
    tmp->left = NULL;
    tmp->right = node->right;
    node->right = tmp;
  }
    
  return(node);
}

#define PRINT_INDENT 2

int LenEmbed(char *prefix, struct nlist *np, struct embed *E, int flatten)
/* return the number of characters required to print element E */
{
  char longstr[MAX_STR_LEN];

  if (E == NULL) return(0);
  if (E->left == NULL && E->right == NULL) {
    /* this is a root in our cell's embedding heirarchy */
    struct objlist *ob;
    char *instancename;
    char *model;
    struct nlist *np2;

    ob = InstanceNumber(np,E->instancenumber);
    instancename = ob->instance.name;
    model = ob->model.class;
    np2 = LookupCell(model);
    if (np2 == NULL) return(0);
    sprintf(longstr, "%s%s", prefix, instancename);
    if ((np2->class != CLASS_SUBCKT) || np2->embedding == NULL || !flatten) 
      return(strlen(longstr));
    /* else, prepend model */
    strcat(longstr,SEPARATOR);
    return(LenEmbed(longstr, np2, (struct embed *)np2->embedding, flatten));
  }

  /* else it is a compound element (with 2 parentheses) */
  return(PRINT_INDENT + 2 + LenEmbed(prefix, np, E->left, flatten) + 
	 LenEmbed(prefix, np, E->right, flatten));
}

void PrintEmb(FILE *outfile, char *prefix, struct nlist *np, 
	      struct embed *E, int indent, int flatten)
/* just print out the element on a single line */
/* assumes that we have been indented enough to print it directly */
{
  if (E == NULL) return;
  if (E->left == NULL && E->right == NULL) {
    /* this is a root in our cell's embedding heirarchy */
    struct objlist *ob;
    char *instancename;
    struct nlist *np2;
    char name[MAX_STR_LEN];

    ob = InstanceNumber(np,E->instancenumber);
    instancename = ob->instance.name;
    np2 = LookupCell(ob->model.class);
    if (np2 == NULL) return;
     sprintf(name,"%s%s", prefix, instancename);
    if ((np2->class != CLASS_SUBCKT) || np2->embedding == NULL || !flatten) 
      Fprintf(outfile, "%s", name);
    else {    
      strcat(name,SEPARATOR);
      PrintEmb(outfile, name, np2, (struct embed *)np2->embedding, 
	       indent + 2*PRINT_INDENT, flatten);
    }
    return;
  }

  /* it is a compound element */
  Fprintf(outfile,"(");
  PrintEmb(outfile, prefix, np, E->left, indent, flatten);
  Fprintf(outfile," ");
  PrintEmb(outfile, prefix, np, E->right, indent, flatten);
  Fprintf(outfile,")");
}

void PrintEmbed(FILE *outfile, char *prefix, struct nlist *np,
	       struct embed *E, int indent, int flatten)
/* assumes that cursor is on col. 1 of line at entry */
/* upon return, cursor is on col. 1 of next free line */
{
  int i;

  if (E == NULL) return;
  if (E->right == NULL && E->left == NULL) {
    /* it is a root element */
    struct objlist *ob;
    char *instancename;
    struct nlist *np2;
    char name[MAX_STR_LEN];

    ob = InstanceNumber(np,E->instancenumber);
    instancename = ob->instance.name;
    np2 = LookupCell(ob->model.class);
    if (np2 == NULL) return;

    if (np2->embedding != NULL && flatten) {
      sprintf(name,"%s%s%s", prefix, instancename, SEPARATOR);
      PrintEmbed(outfile, name, np2, (struct embed *)np2->embedding, 
		 indent + PRINT_INDENT, flatten);
      return;
    }
    else {
      for (i = 0; i < indent; i++) Fprintf(outfile," ");
      PrintEmb(outfile, prefix, np, E, indent, flatten);
      Fprintf(outfile,"\n");
      return;
    }
  }
    
  if (indent + LenEmbed(prefix, np, E, flatten) >= linelength) {
    for (i = 0; i < indent; i++) Fprintf(outfile," ");
    Fprintf(outfile,"(\n");
    PrintEmbed(outfile, prefix, np, E->left, indent + PRINT_INDENT, flatten);
    PrintEmbed(outfile, prefix, np, E->right,indent + PRINT_INDENT, flatten);
    for (i = 0; i < indent; i++) Fprintf(outfile," ");
    Fprintf(outfile,")\n");
  }
  else {
    for (i = 0; i < indent; i++) Fprintf(outfile," ");
    Fprintf(outfile,"(");
    PrintEmb(outfile, prefix, np, E->left, indent, flatten);
    Fprintf(outfile," ");
    PrintEmb(outfile, prefix, np, E->right, indent, flatten);
    Fprintf(outfile,")\n");
  }
}

void PrintEmbeddingTree(FILE *outfile, char *cellname, int flatten)
{
  struct nlist *np;

  if (outfile == NULL) return;
  np = LookupCell(cellname);
  if (np == NULL) return;
  if (np->embedding == NULL) 
    Fprintf(outfile, "No embedding for '%s' has been determined.\n",cellname);
  else {
    Fprintf(outfile, "Embedding for %s (level %d):\n",cellname,
	    ((struct embed *)(np->embedding))->level);
    PrintEmbed(outfile, "", np, (struct embed *)(np->embedding), 0, flatten);
    Fprintf(outfile, "\n");
  }
}




