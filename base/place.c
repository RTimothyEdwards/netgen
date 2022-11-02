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

/* place.c -- more embedding routines for PROTOCHIP */


/* define the following to enable checking of minimum leaf usage */
#define CHECK_MINUSEDLEAVES

/* define the following to enable checking of minimum common nodes */
#undef CHECK_MINCOMMONNODES

/* define to avoid merging elements that share only global nodes */
#define DISCOUNT_GLOBAL_NODES

/* define to get reams of extra output */
#undef PLACE_DEBUG

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <ctype.h>

#include "netgen.h"
#include "hash.h"
#include "objlist.h"
#include "query.h"
#include "netfile.h"
#include "embed.h"
#include "dbug.h"
#include "print.h"

#ifdef IBMPC
#include <process.h> /* system */
#include <stdlib.h> /* atoi */
#include <bios.h>   /* bioskey */
#include <mem.h>    /* memset */
#endif /* IBMPC */

/* count invokations of different test procedures */
int CountAnyCommonNodes;


/* abridged ownership and connectivity matrices */
/* elements, nodes, leaves are indexed from 1 to N, nodes, leaves */
unsigned short M[MAX_ELEMENTS][7];  
/* height, L, R, SWALLOWED, PINS, LEAVES, USED */

unsigned long MSTAR[MAX_ELEMENTS][(MAX_LEAVES / BITS_PER_LONG)  + 1];

unsigned char C[MAX_ELEMENTS][MAX_NODES + 1];
unsigned char CSTAR[MAX_ELEMENTS][MAX_NODES + 1];

/* elements at level i must have TreeFanout[i] or fewer ports */
int TreeFanout[MAX_TREE_DEPTH + 1];       /* tree fanout at each level */

/* elements at level i must share MinCommonNodes[i] nodes between their kids */
/* or swallow a child entirely */
int MinCommonNodes[MAX_TREE_DEPTH + 1];   

/* elements at level i must contain at least MinUsedLeaves[i] leaves */
int MinUsedLeaves[MAX_TREE_DEPTH + 1];   

int Nodes;   /* number of nodes in the cell */
int Leaves;  /* number of leaves in the cell */
int PackedLeaves; /* == Leaves / BITS_PER_LONG, just to save computation */
int Elements; /* number of elements */
int NewN, NewElements;
int SumPINS, SumCommonNodes, SumUsedLeaves;
int NewSwallowed;
int Pass;
int logging = 0; /* generate output file LOG_FILE_EXT */
int selectivelogging = 0; 
int LogLevel1 = -1; /* automatically log if Level1 == LogLevel1 */
int LogLevel2 = -1;
int FatalError = 0; /* internal error */
int Exhaustive = 0; /* slow, methodical */
int PlaceDebug = 0; /* interactive debug */

FILE *outfile;  /* output file */
FILE *logfile;  /* debugging log file */

static int LeafPins = LEAFPINS; /* was 10.0 */
static float RentExp = RENTEXP;

/* Initialize TreeFanout array to contain the actual fanouts of the chip */
void InitializeFanout(void)
{
  int i;

  for (i = 1; i <= MAX_TREE_DEPTH; i++) 
    TreeFanout[i] = (int)(LeafPins * pow(2.0, i*RentExp));
}

void InitializeMinCommonNodes(void)
{
  int i;
  for (i = 1; i <= MAX_TREE_DEPTH; i++) 
    MinCommonNodes[i] = (int)((TreeFanout[i] - TreeFanout[1] + 2) / 2);
/*    MinCommonNodes[i] = (TreeFanout[i] - TreeFanout[1] + 2) / 2; */
}

void InitializeMinUsedLeaves(void)
{
  int i;
  MinUsedLeaves[1] = 2;
  MinUsedLeaves[2] = 2;
  for (i = 3; i <= MAX_TREE_DEPTH; i++) 
    MinUsedLeaves[i] = 2*MinUsedLeaves[i-1];
}

int RenumberNodes(char *cellname)
/* returns number of nodes in 'cellname', numbered from 1 */
{
  struct nlist *tp;
  struct objlist *ob;
  int maxnode, newnode, oldnode;

  tp = LookupCell(cellname);
  if (tp == NULL) return(0);
  if (tp->class != CLASS_SUBCKT) return(0);
  ob = tp->cell;
  maxnode = -1;
  while (ob != NULL) {
    if (ob->node >  maxnode) 
      maxnode = ob->node;
    ob = ob->next;
  }
  /* renumber all the nodes contiguously from 1 */
  newnode = 1;
  for (oldnode = 1; oldnode <= maxnode; oldnode++) {
    int found;
    found = 0;
    for (ob = tp->cell; ob != NULL; ob = ob->next) 
      if (ob->node == oldnode) {
	found = 1;
	ob->node = newnode;
      }
    if (found) newnode++;
  }
  return(newnode - 1);
}

void EraseMatrices(void)
{
  memzero(C, sizeof(C));
  memzero(CSTAR, sizeof(CSTAR));
  memzero(M, sizeof(M));
  memzero(MSTAR, sizeof(MSTAR));
}

int InitializeMatrices(char *cellname)
/* return 1 if OK; upon exit: 'Leaves', 'Nodes', 'Elements' are initialized */
{
  struct nlist *tp;
  struct objlist *ob;
  int i, j;

  tp = LookupCell(cellname);
  if (tp == NULL) return(0);
  if (tp->class != CLASS_SUBCKT) return(0);

  if ((Nodes = RenumberNodes(cellname)) > MAX_NODES) {
    Fprintf(stderr,"Too many nodes in cell: %s (%d > MAX_NODES(%d))\n",
	    cellname, Nodes, MAX_NODES);
    return(0);
  }
  EraseMatrices();  /* all elements are set to 0 */

  /* create connectivity matrix C */
  Leaves = 0;
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    if (ob->type == FIRSTPIN) {
      struct nlist *tp;

      Leaves++;
      if (Leaves > MAX_LEAVES) continue; /* keep going within the for loop */

      tp = LookupCell(ob->model.class);
      if (tp == NULL || (tp->class != CLASS_SUBCKT) || tp->embedding == NULL)
	LEVEL(Leaves) = 0;
      else LEVEL(Leaves) = ((struct embed *)(tp->embedding))->level;
      /* remember, L(Leaves) = R(Leaves) = 0 by EraseMatrices above */
    }
    if (ob->type >= FIRSTPIN) C[Leaves][ob->node] = 1;
  }
  if (Leaves > MAX_LEAVES) {
    Fprintf(stderr, "Too many leaves in cell: %s (%d > MAX_LEAVES(%d))\n",
	    cellname, Leaves, MAX_LEAVES);
    return(0);
  }
  PackedLeaves = Leaves / BITS_PER_LONG;

  /* row 0 of C is special, containing the port list of the entire cell */
  for (ob = tp->cell; ob != NULL; ob = ob->next) 
    if (IsPortInPortlist(ob, tp)) C[0][ob->node] = 1;

  /* matrix M partially init. by EraseMatrices, and by Leaves loop above; */
  /* column PINS of matrix M is special, containing fanout of each element */
  for (i = 0; i <= Leaves; i++) 
    for (j = 1; j <= Nodes; j++) PINS(i) += C[i][j];

  /* initialize the number of leaves contained by each element */
  LEAVES(0) = Leaves;
  for (i = 1; i <= Leaves; i++) LEAVES(i) = 1;

  /* create transitive closure of M */
  for (i = 1; i <= Leaves; i++) 
    SetPackedArrayBit(MSTAR[i],i); /* each leaf owns itself */
  for (i = 1; i <= Leaves; i++)  /* used to be i = 0 ??? */
    SetPackedArrayBit(MSTAR[0],i); /* portlist owns all leaves */

  /* create nodal connectivity transitive closure matrix CSTAR */
  i = 0;
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    if (ob->type == FIRSTPIN) i++;
    if (ob->type >= FIRSTPIN) CSTAR[i][ob->node]++;
  }
  for (j = 1; j <= Nodes; j++){
    CSTAR[0][j] = 0;
    for (i = 1; i <= Leaves; i++) CSTAR[0][j] += CSTAR[i][j];
    if (C[0][j]) CSTAR[0][j]++;  /* increment usage of ports */
  }

  /* initially, number of elements == number of leaves */
  Elements = Leaves;
  return(1);
}

void PrintC(FILE *outfile)
{
  int i, j;

	if (outfile == NULL) return;
	Fprintf(outfile,"C:\n");
	for (i = 0; i <= Elements; i++) {
		Fprintf(outfile,"%4d: %3d | ",i,PINS(i));
		for (j = 1; j <= Nodes; j++) Fprintf(outfile," %d",C[i][j]);
		Fprintf(outfile,"\n");
	}
	Fprintf(outfile,"\n");
}

void PrintCSTAR(FILE *outfile)
{
  int i, j;

	if (outfile == NULL) return;
	Fprintf(outfile,"C*:\n");
	for (i = 0; i <= Elements; i++) {
		Fprintf(outfile,"%4d: ",i);
		for (j=1; j <= Nodes; j++) Fprintf(outfile,"%3d",CSTAR[i][j]);
		Fprintf(outfile,"\n");
	}
	Fprintf(outfile,"\n");
}

void PrintOwnership(FILE *outfile)
{
  int i,j;

  if (outfile == NULL) return;
  Fprintf(outfile,"Ownership matrices M, MSTAR:\n");
  Fprintf(outfile,"element height L    R  S Pins Leaves Used\n");
  for (i = 0; i <= Elements; i++) {
    Fprintf(outfile,"%4d:  %4d %4d %4d %2d %3d %5d %5d: ",i,
	    LEVEL(i), L(i), R(i), SWALLOWED(i), PINS(i), LEAVES(i), USED(i));

    for (j = 1; j <= Leaves; j++) {
      if (TestPackedArrayBit(MSTAR[i],j)) Fprintf(outfile,"1");
      else Fprintf(outfile,"0");
    }
#if 0
    /* debugging stuff for packed arrays */
    Fprintf(outfile," : ");
    for(j = 0; j <= PackedLeaves; j++) 
      Fprintf(outfile," %ld",MSTAR[i][j]);
#endif
    Fprintf(outfile,"\n");
  }
  Fprintf(outfile,"\n");
}

    
void PrintE(FILE *outfile, int E)
{
  if (IsLeaf(E)) Fprintf(outfile, "%d", E);
  else {
    Fprintf(outfile,"(");
    PrintE(outfile,L(E));
    Fprintf(outfile," ");
    PrintE(outfile,R(E));
    Fprintf(outfile,")");
  }
}


#if 0
int UsedLeaves(int E1, int E2)
/* returns the number of leaves in E1 + E2, which are assumed independent */
{
  return(LEAVES(E1) + LEAVES(E2));
}
#else
/* returns the number of leaves in E1 + E2, which are assumed independent */
#define UsedLeaves(A,B) (LEAVES(A) + LEAVES(B))
#endif


int CommonNodes(int E1, int E2, int IncludeGlobals)
/* returns the number of nodes that E1 and E2 share */
/* if IncludeGlobals == 0, do not count large connectivity nodes */
{
  int result, node;
	
  result = 0;
  if (IncludeGlobals) {
    for (node = 1; node <= Nodes; node++) 
      if (C[E1][node] && C[E2][node]) result++;
  }
  else {
    for (node = 1; node <= Nodes; node++) 
      if (C[E1][node] && C[E2][node] && !C[0][node]) result++;
  }
#ifdef PLACE_DEBUG
  Printf("CommonNodes(%d,%d) (%s globals) gives %d\n",
	 E1,E2, IncludeGlobals?"including":"excluding", result);
#endif	
  return(result);
}

int GlobalNodes(int E)
/* return the number of global nodes that E contacts */
/* for now, global nodes are just cell ports */
{
  int node;
  int count;

  count = 0;
  for (node = 1; node <= Nodes; node++) 
    if (C[E][node] && C[0][node]) count++;
  return(count);
}

#ifdef DISCOUNT_GLOBAL_NODES
int AnyCommonNodes(int E1, int E2)
/* returns 1 if E1 and E2 share a node */
/* if DISCOUNT_GLOBAL_NODES, do not count large connectivity nodes,
   unless these are the only connections */
{
  int node;
  int nodesincommon;

  CountAnyCommonNodes++;
  nodesincommon = 0;
  for (node = 1; node <= Nodes; node++) {
    /* do not count it if it is a port for the entire cell */
    if (C[E1][node] && C[E2][node]) {
      if (!(C[0][node])) return(1);
      nodesincommon = 1;
    }
  }

#if 1
  if (!nodesincommon) return(0);
  /* if ANY nodes exist that are not ports, return NO_COMMON_NODES */
  for (node = 1; node <= Nodes; node++) 
    if ((C[E1][node] || C[E2][node]) && !(C[0][node])) return (0);

  /* all nodes are global, and some are shared,  so return 1 */
  return(1);

#else
  /* if E1 is contained in E2, or E2 is contained in E1, all is OK */
  /* this is extremely inadequate for merging cells whose pins are all global*/
  return(Swallowed(E2,E1) || Swallowed(E1,E2));
#endif
}

#else
int AnyCommonNodes(int E1, int E2)
/* returns 1 if E1 and E2 share a node */
/* any node, including a global node, is OK */
{
  int node;
  int nodesincommon;

  CountAnyCommonNodes++;
  for (node = 1; node <= Nodes; node++) 
    if (C[E1][node] && C[E2][node]) return(1);
  return(0);
}
#endif /* DISCOUNT_GLOBAL_NODES */


void IncrementUsedCount(int E)
{
#if 0
  if (E == 0) return;
  USED(E)++;
#else
  USED(E)++;
  if (IsLeaf(E)) return;
#endif
  IncrementUsedCount(L(E));
  IncrementUsedCount(R(E));
}

void AddNewElement(int E1, int E2)
{
  int i;
	
  NewN++;
  if (NewN >= MAX_ELEMENTS) {
    Fprintf(stderr,"Too many elements (%d)\n",NewN);
    if (outfile != NULL)
      Fprintf(outfile,"Too many elements (%d)\n",NewN);
    return;
  }
  NewElements++;
	
  /* update ownership matrix */
  LEVEL(NewN) = MAX(LEVEL(E1), LEVEL(E2)) + 1; 
  L(NewN) = E1; R(NewN) = E2;
	
  /* update leaf ownership matrix */
  for (i = 0; i <= PackedLeaves; i++)
    MSTAR[NewN][i] = MSTAR[E1][i] | MSTAR[E2][i];
	
  /* update connectivity matrix with actual portlist */
  for (i = 1; i <= Nodes; i++) 
    if ((C[E1][i] || C[E2][i]) &&
	(CSTAR[E1][i] + CSTAR[E2][i] < CSTAR[0][i]))
      C[NewN][i] = 1;

  /* update number of leaves contained by new element */
  /* for (i = 1; i <= Leaves; i++) 
    if (TestPackedArrayBit(MSTAR[NewN],i)) LEAVES(NewN)++; */
  LEAVES(NewN) = LEAVES(E1) + LEAVES(E2);

  /* increment the instance count for tree rooted by NewN */
  IncrementUsedCount(E1);
  IncrementUsedCount(E2); 

  for (i = 1; i <= Nodes; i++) if (C[NewN][i]) PINS(NewN)++;
  SumPINS += PINS(NewN);
  SumCommonNodes += PINS(E1) + PINS(E2) - PINS(NewN);
  SumUsedLeaves += LEAVES(NewN);

  /* update node usage matrix */
  for (i = 1; i <= Nodes; i++)
    CSTAR[NewN][i] = CSTAR[E1][i] + CSTAR[E2][i];
	
  /* add to exist-checking data structure */
  AddToExistSet(E1, E2);

  if (PlaceDebug) {
    if (NewN == Elements + 1) Printf("\n");
    Printf("Adding new element: ");
    PrintE(stdout,NewN);
    Printf(" pins = %d, commonnodes = %d",
	   PINS(NewN), PINS(E1)+PINS(E2)-PINS(NewN));
    Printf("\n");
  }
}


int CountInLevel(int i, int upto)
/* if upto == 1, count elements of level <= i */
{
  int elem;
  int count;

  count = 0;
  if (upto) {
    for (elem = 1; elem <= Elements; elem++)
      if (LEVEL(elem) <= i) count++;
  }
  else {
    for (elem = 1; elem <= Elements; elem++)
      if (LEVEL(elem) == i) count++;
  }
  return(count);
}


/* file extensions of embedding file and log file */
#define OUT_FILE_EXT ".out"
#define LOG_FILE_EXT ".log"

int OpenEmbeddingFile(char *cellname, char *filename)
/* returns 1 if OK */
{
  struct nlist *tp;
  char outfilename[MAX_STR_LEN];
  char logfilename[MAX_STR_LEN];

  tp = LookupCell(cellname);
  if (tp == NULL) {
    Fprintf(stderr, "No cell: '%s'\n",cellname);
    return(0);
  }
  if (tp->class != CLASS_SUBCKT) {
    Fprintf(stderr, "Cell: '%s' is primitive, and cannot be embedded.\n");
    return(0);
  }
  tp->dumped = 1;

  if (filename == NULL || strlen(filename) == 0) 
    strcpy(outfilename, cellname);
  else
    strcpy(outfilename, filename);
  if (strstr(outfilename, OUT_FILE_EXT) == NULL)
    strcat(outfilename, OUT_FILE_EXT);
	
  outfile = fopen(outfilename,"w");
  if (outfile == NULL) {
    Fprintf(stderr,"Unable to open embedding file %s\n", outfilename);
    return(0);
  }

  logfile = NULL;
  if (logging) {
    if (filename == NULL || strlen(filename) == 0)
      strcpy(logfilename, cellname);
    else
      strcpy(logfilename, cellname);
    if (strstr(logfilename, LOG_FILE_EXT) == NULL)
      strcat(logfilename, LOG_FILE_EXT);
    logfile = fopen(logfilename,"w");
    if (logfile == NULL) {
      Fprintf(stderr,"Unable to open log file %s\n", logfilename);
      logging = 0;
    }
  }
  return(1);
}

void CloseEmbeddingFile(void)
{
  fclose(outfile);
  outfile = NULL;
  if (logfile != NULL) fclose(logfile);
  logfile = NULL;
}






void EmbedCells(char *cellname, enum EmbeddingStrategy strategy)
{
  struct nlist *tp;
  struct objlist *ob;

  tp = LookupCell(cellname);
  if (tp == NULL) {
    Fprintf(stderr, "No cell: '%s'\n",cellname);
    return;
  }
  if (tp->class != CLASS_SUBCKT) {
    Fprintf(stderr, "Cell: '%s' is primitive, and cannot be embedded.\n");
    return;
  }
  for (ob = tp->cell; ob != NULL; ob = ob->next)
    if (ob->type == FIRSTPIN) {
      struct nlist *tp2;
      tp2 = LookupCell(ob->model.class);
      if (!(tp2->dumped) && (tp2->class == CLASS_SUBCKT))
	EmbedCells(ob->model.class,strategy);
    }
  switch (strategy) {
  case bottomup: EmbedCell(cellname, NULL);
    break;
  default:
    TopDownEmbedCell(cellname, NULL, strategy);
    break;
  }
}  


void Embed(char *cellname)
/* use the "best" strategy */
{
  ClearDumpedList();
  EmbedCells(cellname, greedy);
}  

void DoEmbed(char *cellname, enum EmbeddingStrategy strategy)
{
Printf("embedding using strategy %d\n",strategy);
  ClearDumpedList();
  EmbedCells(cellname, strategy);
}  


int CountSubGraphs(char *cellname)
{
  struct nlist *tp;
  int i;
  int groups[MAX_LEAVES+1];
  int contact[MAX_LEAVES+1];
	
  tp = LookupCell(cellname);
  if (tp == NULL) {
    Fprintf(stderr, "No cell: '%s'\n",cellname);
    return(0);
  }
  if (tp->class != CLASS_SUBCKT) {
    Fprintf(stderr, "Cell: '%s' is primitive, and cannot be embedded.\n");
    return(0);
  }

  if (!InitializeMatrices(cellname)) return(0);
  memzero(groups, sizeof(groups));

  for (i = 1; i <= Leaves; i++) groups[i] = i;

  for (i = 1; i <= Leaves; i++) {
    int node;
    int j;
    int mingroup;

    memzero(contact, sizeof(contact));
    contact[i] = 1;
    for (j = i + 1; j <= Leaves; j++) {
      /* else, find all elements that contact this node */
      for (node = 1; node <= Nodes; node++) {
    	if ((C[i][node] && C[j][node]) && !(C[0][node])) {
	  contact[j] = 1;
	  break;
	}
      }
    }
    mingroup = MAX_LEAVES+2;
    for (j = 1; j <= Leaves; j++) {
      if (contact[j] && groups[j] < mingroup) mingroup = groups[j];
    }
    for (j = 1; j <= Leaves; j++)
      if (contact[j]) groups[j] = mingroup;
  }

  Printf("ownership groups: ");
  for (i = 1; i <= Leaves; i++) Printf(" %d",groups[i]);
  Printf("\n");
  return(0);
}









int NumberOfInstances(char *cellname)
{
  struct nlist *tp;
  struct objlist *ob;
  int instances;

  tp = LookupCell(cellname);
  if (tp == NULL) return(0);
  if (tp->class != CLASS_SUBCKT) return(0);
  instances = 0;
  for (ob = tp->cell; ob != NULL; ob = ob->next)
    if (ob->type == FIRSTPIN) instances++;
  return(instances);
}
  
int UniquePorts(struct objlist *pt)
/* assuming that pt points to the beginning of an instance,
   return the number of output ports of the instance, counting
   ports that are connected to the same node as 1
*/
{
  int outputs, found;
  struct objlist *scan;

  outputs = 0;
  do {
    scan = pt;
    found = 0;
    do {
      if (pt != scan && pt->node == scan->node) found = 1;
      scan = scan->next;
    } while (!found && scan->type > FIRSTPIN);
    if (!found) outputs++;
    pt = pt->next;
  } while (pt->type > FIRSTPIN);
  return (outputs);
}

int NodesInCommon(struct objlist *pt1, struct objlist *pt2)
/* assuming that pt1 and pt2 point to the beginning of instances,
   return the number of nodes the two instances share
*/
{
  int common, found;
  struct objlist *scan;

  common = 0;
  do {
    /* make sure there are no other pins connected to this node */
    found = 0;
    scan = pt1;
    do {
      if (scan != pt1 && scan->node == pt1->node) found = 1;
      scan = scan->next;
    } while (!found && scan->type > FIRSTPIN);
    if (!found) {
      scan = pt2;
      found = 0;
      do {
	if (scan->node == pt1->node) found = 1;
	scan = scan->next;
      } while (!found && scan->type > FIRSTPIN);
      if (found) common++;
    }
    pt1 = pt1->next;
  } while (pt1->type > FIRSTPIN);
  return(common);
}

void OldEmbed(char *cellname, char *filename)
{
  struct nlist *tp;
  struct objlist *ob1, *ob2;

  tp = LookupCell(cellname);
  if (tp == NULL) return;
  if (tp->class != CLASS_SUBCKT) return;

  Printf("OldEmbed of element: %s into file %s\n",cellname,filename);
  /* print out element list */
  for (ob1 = tp->cell; ob1 != NULL; ob1 = ob1->next) 
    if (ob1->type == FIRSTPIN) 
      Printf("element: %s, Unique ports = %d\n",ob1->instance,
	     UniquePorts(ob1));

  /* print out shared node matrix */
  for (ob1 = tp->cell; ob1 != NULL; ob1 = ob1->next) {
    if (ob1->type == FIRSTPIN) {
      for (ob2 = tp->cell; ob2 != NULL; ob2 = ob2->next)
	if (ob2->type == FIRSTPIN) Printf("%d ",NodesInCommon(ob1, ob2));
      Printf("\n");
    }
  }
}

static jmp_buf jmpenv;

/* static void handler(void) */
static void handler(int sig)
{
  Fprintf(stderr,"\nInterrupt (%d)!!\n", sig);
  fflush(stderr);
  longjmp(jmpenv,1);
}

void SetupArray(char *prompt1, char *prompt2, char *prompt3, int *data, 
                void (*proc)(void))
{
  int i, oldfanout;
  char name[MAX_STR_LEN];

  Printf(prompt1);
  for (i = 1; i <= MAX_TREE_DEPTH; i++) 
    Printf(" %d", data[i]);
  Printf("\n");

  oldfanout = 1;
  for (i = 1; i <= MAX_TREE_DEPTH; i++) {
    char prompt[MAX_STR_LEN];
    int newfanout;
    sprintf(prompt, prompt2, i);
    promptstring(prompt, name);
    newfanout = atoi(name);
    if (i == 1 && newfanout == 0) {
      proc();			/* reset to original */
      i = MAX_TREE_DEPTH;
    }
    else {
      if (newfanout == 0)	/* fill out MinCommonNodes */
	for (; i <= MAX_TREE_DEPTH; i++) 
	  data[i] = oldfanout;
      else {
	data[i] = newfanout;
	oldfanout = newfanout;
      }
    }
  }
  Printf(prompt3);
  for (i = 1; i <= MAX_TREE_DEPTH; i++) 
    Printf(" %d", data[i]);
  Printf("\n");
  return;
}

void SetupArrayFromString(char *prompt1, char *prompt3, int *data, 
                void (*proc)(void), char *text)
{
  int i, oldfanout, newfanout;
  char string[MAX_STR_LEN];
  char *ch;
  char *endch;

  strcpy(string, text);
  Printf(prompt1);
  for (i = 1; i <= MAX_TREE_DEPTH; i++) 
    Printf(" %d", data[i]);
  Printf("\n");

  ch = strtok(string, " ");
  if (ch == NULL) return;
  oldfanout = (int)strtol(ch, &endch, 10);
  if (endch == ch) return;
  if (oldfanout == 0) {
    proc();
    return;
  }
  /* it is a valid non-zero number */
  for (i = 1; i <= MAX_TREE_DEPTH; i++) {
    data[i] = oldfanout;
    /* look at next element in string */
    if (ch != NULL) {
      ch = strtok(NULL, " ");
      if (ch == NULL) newfanout = 0;
      else newfanout = (int)strtol(ch, NULL, 10);
      if (newfanout == 0) ch = NULL;
      else oldfanout = newfanout;
    }
  }
  Printf(prompt3);
  for (i = 1; i <= MAX_TREE_DEPTH; i++) 
    Printf(" %d", data[i]);
  Printf("\n");
  return;
}

void SetupTreeFanout(char *string)
{
  SetupArrayFromString("Old fanout:", "New fanout: ", 
		       TreeFanout, InitializeFanout, string);
}

void SetupMinCommonNodes(char *string)
{
  SetupArrayFromString("Old common node requrements:",
	     "New common node requrements:", MinCommonNodes,
	     InitializeMinCommonNodes, string);
}

void SetupMinUsedLeaves(char *string)
{
  SetupArrayFromString("Old leaf containment requirements:",
	     "New leaf usage requrements:", MinUsedLeaves,
	     InitializeMinUsedLeaves, string);
}

void SetupLeafPinout(char *string)
{
  int i;

  LeafPins = atoi(string);
  if (LeafPins == 0) LeafPins = LEAFPINS;
  InitializeFanout();
  Printf("New Fanout:\n");
  for (i = 1; i <= MAX_TREE_DEPTH; i++) 
    Printf(" %d", TreeFanout[i]);
  Printf("\n");
}

void SetupRentExp(char *string)
{
  int i;

  RentExp = atof(string);
  InitializeFanout();
  Printf("New Fanout:\n");
  for (i = 1; i <= MAX_TREE_DEPTH; i++) 
    Printf(" %d", TreeFanout[i]);
  Printf("\n");
}


void ToggleLogging(void)
{
  logging = !logging;
  if (logging) Printf("Log file (%s) will be generated\n",LOG_FILE_EXT);
  else Printf("No log file will be written.\n");
}

void ToggleExhaustive(void)
{
  Exhaustive = !Exhaustive;
  if (Exhaustive)  Printf("Exhaustive element consideration enabled.\n");
  else Printf("Accelerating heuristics enabled.\n");
}

void ToggleDebug(void)
{
  PlaceDebug = !PlaceDebug;
  if (PlaceDebug) Printf("Verbose output will be generated.\n");
  else Printf("Silent output.\n");
}

void DescribeCell(char *name, int detail)
{
  Printf("Cell: %s contains %d instances, %d nodes and %d ports\n", name, 
	 NumberOfInstances(name),  RenumberNodes(name), NumberOfPorts(name, -1));
  PrintEmbeddingTree(stdout,name,detail);
}

void ProtoEmbed(char *name, char ch)
{
  enum EmbeddingStrategy strategy;

  strategy = greedy;
  if (toupper(ch) == 'A') strategy = anneal;
  if (toupper(ch) == 'G') strategy = greedy;
  if (toupper(ch) == 'O') strategy = bottomup;
  if (toupper(ch) == 'R') strategy = random_embedding;
  if (LookupCell(name) == NULL)
    Fprintf(stderr,"No cell '%s' found.\n",name);
  else {
    if (islower(ch)) DoEmbed(name, strategy);
    else TopDownEmbedCell(name,NULL,strategy);
  }
}

void ProtoPrintParameters(void)
{
  Printf("PROTOCHIP embedder compiled with:\n");
  Printf("MAX_LEAVES = %d; (MAX_TREE_DEPTH = %d)\n",
	 MAX_LEAVES, MAX_TREE_DEPTH);
  Printf("MAX_ELEMENTS = %d, MAX_NODES = %d\n",
	 MAX_ELEMENTS, MAX_NODES);
}

void PROTOCHIP(void)
/* a simple command interpreter to manage embedding/routing */
{
  char name[MAX_STR_LEN];
  char ch;
  
  InitializeFanout();
  InitializeMinCommonNodes();
  InitializeMinUsedLeaves();
  setjmp(jmpenv);
  signal(SIGINT,handler);
  do {
    promptstring("PROTOCHIP command: ", name);
    ch = name[0];

    switch (ch) {
    case 'p':
      ProtoPrintParameters();
      break;
    case 'l':
      ToggleLogging();
      break;
    case 'L':
      promptstring("Log if level1 == ",name);
      LogLevel1 = atoi(name);
      promptstring("Log if level2 == ",name);
      LogLevel2 = atoi(name);
      selectivelogging = ((LogLevel1 != -1) || (LogLevel2 != -1));
      if (selectivelogging) logging = 1;
      break;
    case 'x':
      ToggleExhaustive();
      break;
    case 'V':
      ToggleDebug();
      break;
    case '#':
      if (strlen(name) > 1) {
	char *command;
	command = name+1;
	DBUG_PUSH(command);
      }
      else {
	promptstring("Dbug command? ",name);
	DBUG_PUSH(name);
      }
      break;
    case 'd':
    case 'D':
      promptstring("Describe cell: ", name);
      if (LookupCell(name) == NULL) 
	Fprintf(stderr,"No cell '%s' found.\n",name);
      else DescribeCell(name,(ch == 'd'));
      break;

      /* different embedding strategies -- see enum EmbeddingStrategy above */
    case 'e':
    case 'E':
      promptstring("Cell to embed: ", name);
      if (LookupCell(name) == NULL) 
	Fprintf(stderr,"No cell '%s' found.\n",name);
      else
	Embed(name);
      break;
#if 0
    case 'r':
    case 'R':
      promptstring("Cell to embed: ", name);
      if (LookupCell(name) == NULL)
	Fprintf(stderr,"No cell '%s' found.\n",name);
      else {
	if (ch == 'e') DoEmbed(name, random_embedding);
	else TopDownEmbedCell(name,NULL, random_embedding);
      }
      break;
    case 'o':
    case 'O':
      promptstring("Cell to embed: ", name);
      if (LookupCell(name) == NULL)
	Fprintf(stderr,"No cell '%s' found.\n",name);
      else {
	if (ch == 'o') DoEmbed(name, bottomup);
	else EmbedCell(name,NULL);
      }
      break;
    case 'G':
    case 'g':
      promptstring("Cell to embed: ", name);
      if (LookupCell(name) == NULL)
	Fprintf(stderr,"No cell '%s' found.\n",name);
      else {
	if (ch == 'g') DoEmbed(name, greedy);
	else TopDownEmbedCell(name,NULL, greedy);
      }
      break;
    case 'A':
    case 'a':
      promptstring("Cell to embed: ", name);
      if (LookupCell(name) == NULL)
	Fprintf(stderr,"No cell '%s' found.\n",name);
      else {
	if (ch == 'a') DoEmbed(name, anneal);
	else TopDownEmbedCell(name,NULL, anneal);
      }
      break;
#else
    case 'r':
    case 'R':
    case 'o':
    case 'O':
    case 'G':
    case 'g':
    case 'A':
    case 'a':
      promptstring("Cell to embed: ", name);
      ProtoEmbed(name, ch);
      break;
#endif

    case 's':
      promptstring("Cell to count sub-graphs: ",name);
      CountSubGraphs(name);
      break;
    case 'h': PrintCellHashTable(0, -1); break;
    case 'H': PrintCellHashTable(1, -1); break;
    case 'F':
      promptstring("Enter leaf pinout: ",name);
      LeafPins = atoi(name);
      if (LeafPins == 0) LeafPins = 10;
      Printf("New Fanout:\n");
      {
	int i;
	for (i = 1; i <= MAX_TREE_DEPTH; i++) 
	  Printf(" %d", TreeFanout[i]);
      }
      Printf("\n");
      break;
    case 'X':
      promptstring("Enter Rent's Rule exponent: ",name);
      RentExp = atof(name);
      InitializeFanout();
      Printf("New Fanout:\n");
      {
	int i;
	for (i = 1; i <= MAX_TREE_DEPTH; i++) 
	  Printf(" %d", TreeFanout[i]);
      }
      Printf("\n");
      break;
#if 1
    case 'f':
      SetupArray("Fanout is currently:",
		 "Fanout for level %d (0 to quit): ",
		 "New fanout: ", TreeFanout, InitializeFanout);
      break;
    case 'c':
      SetupArray("Common node requrements are currently:",
		 "Common nodes for level %d (0 to quit): ",
		 "New common node requrements:", MinCommonNodes,
		 InitializeMinCommonNodes);
      break;
    case 'C':
      SetupArray("Leaf containment requirements are currently:",
		 "Used leaves for level %d (0 to quit): ",
		 "New leaf usage requrements:", MinUsedLeaves,
		 InitializeMinUsedLeaves);
      break;

#else
    case 'f':
      Printf("Fanout is currently:");
      for (i = 1; i <= MAX_TREE_DEPTH; i++) 
	Printf(" %d", TreeFanout[i]);
      Printf("\n");

      oldfanout = 1;
      for (i = 1; i <= MAX_TREE_DEPTH; i++) {
	char prompt[MAX_STR_LEN];
	int newfanout;
	sprintf(prompt,"Fanout for level %d (0 to quit): ",i);
	promptstring(prompt, name);
	newfanout = atoi(name);
	if (i == 1 && newfanout == 0) {
	  InitializeFanout();	/* reset to original */
	  i = MAX_TREE_DEPTH;
	}
	else {
	  if (newfanout == 0)	/* fill out TreeFanout */
	    for (; i <= MAX_TREE_DEPTH; i++) 
	      TreeFanout[i] = oldfanout;
	  else {
	    TreeFanout[i] = newfanout;
	    oldfanout = newfanout;
	  }
	}
      }
      Printf("New fanout:");
      for (i = 1; i <= MAX_TREE_DEPTH; i++) 
	Printf(" %d", TreeFanout[i]);
      Printf("\n");
      break;
    case 'c':
      Printf("Common node requrements are currently:");
      for (i = 1; i <= MAX_TREE_DEPTH; i++) 
	Printf(" %d", MinCommonNodes[i]);
      Printf("\n");

      oldfanout = 1;
      for (i = 1; i <= MAX_TREE_DEPTH; i++) {
	char prompt[MAX_STR_LEN];
	int newfanout;
	sprintf(prompt,"Common nodes for level %d (0 to quit): ",i);
	promptstring(prompt, name);
	newfanout = atoi(name);
	if (i == 1 && newfanout == 0) {
	  InitializeMinCommonNodes(); /* reset to original */
	  i = MAX_TREE_DEPTH;
	}
	else {
	  if (newfanout == 0)	/* fill out MinCommonNodes */
	    for (; i <= MAX_TREE_DEPTH; i++) 
	      MinCommonNodes[i] = oldfanout;
	  else {
	    MinCommonNodes[i] = newfanout;
	    oldfanout = newfanout;
	  }
	}
      }
      Printf("New common node requrements:");
      for (i = 1; i <= MAX_TREE_DEPTH; i++) 
	Printf(" %d", MinCommonNodes[i]);
      Printf("\n");
      break;
    case 'C':
      Printf("Leaf containment requirements are currently:");
      for (i = 1; i <= MAX_TREE_DEPTH; i++) 
	Printf(" %d", MinUsedLeaves[i]);
      Printf("\n");

      oldfanout = 1;
      for (i = 1; i <= MAX_TREE_DEPTH; i++) {
	char prompt[MAX_STR_LEN];
	int newfanout;
	sprintf(prompt,"Used leaves for level %d (0 to quit): ",i);
	promptstring(prompt, name);
	newfanout = atoi(name);
	if (i == 1 && newfanout == 0) {
	  InitializeMinUsedLeaves(); /* reset to original */
	  i = MAX_TREE_DEPTH;
	}
	else {
	  if (newfanout == 0)	/* fill out MinCommonNodes */
	    for (; i <= MAX_TREE_DEPTH; i++) 
	      MinUsedLeaves[i] = oldfanout;
	  else {
	    MinUsedLeaves[i] = newfanout;
	    oldfanout = newfanout;
	  }
	}
      }
      Printf("New leaf usage requrements:");
      for (i = 1; i <= MAX_TREE_DEPTH; i++) 
	Printf(" %d", MinUsedLeaves[i]);
      Printf("\n");
      break;
#endif
    case '!': 
#ifdef IBMPC
      system("");
#else
      system("/bin/csh");
#endif
      break;
    case 'q': break;
    case 'Q' : exit(0);
    default:
      Printf("Embed: (e)mbed (E); (o)ld embed (O); e(x)haustive old embed\n");
      Printf("       (r)andom cut embedding algorithm\n");
      Printf("       (g)reedy embedding algorithm, simulated (a)nnealing\n");
      Printf("Embed parameters: (f)anout, (c)ommon nodes, leaf (C)ontainment.\n");
      Printf("                  Leaf (F)anout, Rent's rule e(X)ponent\n");
      Printf("(d)escribe cell; print (h)ash table (H); toggle primiti(v)e bit\n");
      Printf("count (s)ub-graphs, (p)rint embedding constants\n");
      Printf("toggle (l)ogging (L = single level); toggle (V)erbose output\n");
      Printf("(q)uit; (Q)uit immediately; (!) push shell, (#) set dbug\n");
      break;
    }
  } while (ch != 'q');
  signal(SIGINT,SIG_DFL);
}
