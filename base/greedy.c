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

/* greedy.c  -- a greedy graph-partitioning algorithm for the PROTOCHIP */

#include "config.h"

#include <stdio.h>
#ifdef IBMPC
#include <mem.h>  /* memset */
#endif

#include "timing.h"
#include "hash.h"
#include "objlist.h"
#include "netfile.h"
#include "embed.h"
#include "print.h"
#include "dbug.h"



/**************************************************************************/
/*****                                                                *****/
/*****              Recursive top-down placement                      *****/
/*****                                                                *****/
/**************************************************************************/

static struct nlist *curcell;
int permutation[MAX_LEAVES+1];
int TopDownStartLevel;

int leftnodes[MAX_NODES + 1];
int rightnodes[MAX_NODES + 1];


int PartitionFanout(int left, int right, int side)
/* returns number of pins for partition (left,right) */
/* has side-effect of setting up the leftnodes and rightnodes arrays;
   these represent the integrated node usages for the left and right 
   partitions */
{
  int i, E;
  int ports;
  int sum;

  ports = 0;
  for (i = 1; i <= Nodes; i++) {
    sum = 0;
    for (E = left; E <= right; E++) sum += CSTAR[permutation[E]][i];
    /* save total node usage in 'leftnodes' and 'rightnodes' */
    if (side == LEFT) leftnodes[i] = sum;
    else rightnodes[i] = sum;

    if (sum && (sum < CSTAR[0][i] || C[0][i])) ports ++;
  }
  return(ports);
}


#ifndef DBUG_OFF
void Dbug_print_cells(int left, int right)
{
  int i;
  Fprintf(DBUG_FILE,"(");
  for (i = left; i <= right; i++) {
    Fprintf(DBUG_FILE,"%s", 
	    (InstanceNumber(curcell, permutation[i]))->instance);
    if (i != right) Fprintf(DBUG_FILE," ");
  }
  Fprintf(DBUG_FILE,") ");
}
#endif


int FindOptimum(int left, int right, int *mynodes, int *othernodes)
{
  int E, max, choice, i;
  int gain[MAX_LEAVES + 1];

  /* find the left optimum */
  for (E = left; E <= right; E++) {
    gain[E] = 0;
    for (i = 1; i <= Nodes; i++) {
#ifdef BAD
      if (C[permutation[E]][i] && mynodes[i] == 1) gain[E]++;
#else
      /* remember: left,rightnodes built up from CSTAR */
      if (C[permutation[E]][i] && mynodes[i] == CSTAR[permutation[E]][i])
	      gain[E]++;
#endif
      else if (C[permutation[E]][i] && othernodes[i] == 0) gain[E]--;
    }
  }
  
  max = 0;
  choice = 0;

  for (E = left; E <= right; E++) {
    DBUG_PRINT("place",("gain for %d is %d", E, gain[E]));
    if (gain[E] >= max) {
      max = gain[E];
      choice = E;
    }
  }
  DBUG_EXECUTE("place", {Fprintf(DBUG_FILE, "\n");}   );
  return(choice);
}


int GradientDescent(int left, int right, int partition)
/* try to exchange pairs in the (left..partition) (partition+1..right) sets
   in order to reduce the number of cut nodes */
/* returns 1 if a swap was possible */
{
  int leftchoice, rightchoice;
  int tmp;
#if 1
  int E, leftmax, rightmax, i;
  int gain[MAX_LEAVES + 1];

  /* find the left optimum */
  for (E = left; E <= partition; E++) {
    gain[E] = 0;
    for (i = 1; i <= Nodes; i++) {
#ifdef BAD
      if (C[permutation[E]][i] && leftnodes[i] == 1) gain[E]++;
#else
      if (C[permutation[E]][i] && leftnodes[i] == CSTAR[permutation[E]][i])
	      gain[E]++;
#endif
      else if (C[permutation[E]][i] && rightnodes[i] == 0) gain[E]--;
    }
  }
  
  leftmax = 0;
  leftchoice = 0;
  for (E = left; E <= partition; E++) {
    DBUG_PRINT("place",("gain for %d is %d", E, gain[E]));
    if (gain[E] >= leftmax) {
      leftmax = gain[E];
      leftchoice = E;
    }
  }
  DBUG_EXECUTE("place", Fprintf(DBUG_FILE, "\n");   );

  /* find the right optimum */
  for (E = partition+1; E <= right; E++) {
    gain[E] = 0;
    for (i = 1; i <= Nodes; i++) {
#ifdef BAD
      if (C[permutation[E]][i] && rightnodes[i] == 1) gain[E]++;
#else
      if (C[permutation[E]][i] && rightnodes[i] == CSTAR[permutation[E]][i])
	      gain[E]++;
#endif
      else if (C[permutation[E]][i] && leftnodes[i] == 0) gain[E]--;
    }
  }
  
  rightmax = 0;
  rightchoice = 0;
  for (E = partition+1; E <= right; E++) {
    DBUG_PRINT("place",("gain for %d is %d", E, gain[E]));
    if (gain[E] >= rightmax) {
      rightmax = gain[E];
      rightchoice = E;
    }
  }
  DBUG_EXECUTE("place", Fprintf(DBUG_FILE, "\n");   );

  if (leftmax == 0 && rightmax == 0) return(0);


#else
  leftchoice = FindOptimum(left, partition, leftnodes, rightnodes);
  if (leftchoice == 0) return(0);
  rightchoice = FindOptimum(partition+1, right, rightnodes, leftnodes);
  if (rightchoice == 0) return(0);
#endif

  DBUG_PRINT("place",("Swapping %s and %s", 
	     (InstanceNumber(curcell,permutation[leftchoice]))->instance,
	     (InstanceNumber(curcell,permutation[rightchoice]))->instance));
#ifdef BAD
  tmp = permutation[leftchoice];
  permutation[leftchoice] = permutation[rightchoice];
  permutation[rightchoice] = tmp;
  /* update node usage lists */
  for (tmp = 1; tmp <= Nodes; tmp++) {
    if (C[permutation[leftchoice]][tmp]) {
      leftnodes[tmp] -= C[permutation[leftchoice]][tmp];
      rightnodes[tmp] += C[permutation[leftchoice]][tmp];
    }
    if (C[permutation[rightchoice]][tmp]) {
      leftnodes[tmp] += C[permutation[rightchoice]][tmp];
      rightnodes[tmp] -= C[permutation[rightchoice]][tmp];
    }
  }
#else
  /* update node usage lists, remembering that CSTAR goes into leftnodes */
  for (tmp = 1; tmp <= Nodes; tmp++) {
    if (CSTAR[permutation[leftchoice]][tmp]) {
      leftnodes[tmp] -= CSTAR[permutation[leftchoice]][tmp];
      rightnodes[tmp] += CSTAR[permutation[leftchoice]][tmp];
    }
    if (CSTAR[permutation[rightchoice]][tmp]) {
      leftnodes[tmp] += CSTAR[permutation[rightchoice]][tmp];
      rightnodes[tmp] -= CSTAR[permutation[rightchoice]][tmp];
    }
  }
  /* THEN swap the elements */
  tmp = permutation[leftchoice];
  permutation[leftchoice] = permutation[rightchoice];
  permutation[rightchoice] = tmp;
#endif
  return(1);
}

int GenerateGreedyPartition(int left, int right, int level)
/* tries to find a balanced partition, as far as leaf cell usage */
{
  int i;
  int head, tail;
  int queue[MAX_LEAVES + 1];
  char status[MAX_LEAVES + 1];
  int IncludedElements;

#define QUEUED  1
#define INSIDE  2
#define OUTSIDE 3

#if 0
Printf("before GreedyPermutation\n");
for (i = left; i <= right; i++) Printf(" %d",permutation[i]);
Printf("\n");
#endif

  memzero(status, sizeof(status));
  for (i = left; i <= right; i++) status[permutation[i]] = OUTSIDE;
  head = 0;
  tail = 0;
  IncludedElements = 0;

  while (IncludedElements <= (right - left) / 2) {
    int element;

    element = level;  /* keep the compiler from bitching */
    if (head != tail) element = queue[head++];
    else {
      /* start from some random element */
      for (i = left; i <= right; i++) {
	if (status[permutation[i]] == OUTSIDE) {
	  element = permutation[i];
	  break;
	}
      }
    }
    status[element] = INSIDE;
    IncludedElements++;

    for (i = left; i <= right; i++) {
      /* check to see if an element should be added to the queue */
      if (status[permutation[i]] == QUEUED) continue;
      if (status[permutation[i]] == INSIDE) continue;
      /* otherwise, add it to the queue, if it has common nodes with element */
      if (AnyCommonNodes(element, permutation[i])) {
	status[permutation[i]] = QUEUED;
	queue[tail++] = permutation[i];
      }
    }
  }
  /* at this point, status contains the list of elements, classified
     as either INSIDE, OUTSIDE, or QUEUED.  It is now easy to generate
     a permutation.
     */
  head = left;
  tail = right;
  for (i = 1; i <= Leaves; i++) {
    if (status[i] == INSIDE) permutation[head++] = i;
    else if (status[i] != 0) permutation[tail--] = i;
  }

  return (left + IncludedElements - 1);
#if 0
  {
    int leftsum, rightsum;
    leftsum = rightsum = 0;
    while (left < right) {
      if (leftsum < rightsum) leftsum += POW2(LEVEL(permutation[left++]));
      else rightsum += POW2(LEVEL(permutation[right--]));
    }
    if (leftsum > POW2(level) || rightsum > POW2(level)) {
      Fprintf(stdout,"No valid partition found at level %d\n",level);
      return(0); 
    }
    return(left);
  }
#endif
}


int GreedyPartition(int left, int right, int level)
/* return index of new element, if successful partition has been found */
{
  int partition;
  int iterations;
  int found;
  int OriginalNewN;
  int leftelement, rightelement;

#define MAX_PARTITION_ITERATIONS 10

  DBUG_ENTER("GreedyPartition");
  OriginalNewN = NewN;

#if 0
  if (level < 0) DBUG_RETURN(0);
#else
  if (level < LEVEL(permutation[left])) {
    Fprintf(stdout,"Failed at level %d; subtree too deep\n",level);
    DBUG_RETURN(0);
  }
#endif

  if (left == right) DBUG_RETURN(permutation[left]);

  /* use a greedy algorithm to gather elements, starting at 'left',
     until about 1/2 have been collected.  Then check to see if
     it is valid
     */

/*  DBUG_PRINT("place",("trying to partition %d, %d",left,right)); */
  iterations = 0;
  do {
    int i;
    int leftfanout, rightfanout;

    iterations++;
    partition = GenerateGreedyPartition(left, right, level);
    if (partition == 0) DBUG_RETURN(0); /* no valid partition */

    found = 0;
    leftfanout = PartitionFanout(left,partition,LEFT);
    rightfanout = PartitionFanout(partition+1, right, RIGHT);
    if (leftfanout <= TreeFanout[level] && rightfanout <= TreeFanout[level])
	    found = 1;

    if (!found || level > TopDownStartLevel - 2) {
      for (i = MAX_TREE_DEPTH; i > level; i--) Fprintf(stdout, "   ");
      Fprintf(stdout,
    "Level: %d; L (%d leaves) fanout %d; R (%d leaves) fanout %d (<= %d) %s\n",
	      level, (partition - left + 1), leftfanout, 
	      (right - partition), rightfanout, TreeFanout[level],
	      found ? "SUCCESSFUL" : "UNSUCCESSFUL");
    }

    if (!found) { 
      int IterationLimit;

      for (IterationLimit = 0; IterationLimit < 20 &&
	   GradientDescent(left, right, partition); IterationLimit++);

      found = 0; 
      leftfanout =   PartitionFanout(left,partition,LEFT); 
      rightfanout = PartitionFanout(partition+1, right, RIGHT); 
      if (leftfanout <= TreeFanout[level] && rightfanout <= TreeFanout[level])
	      found = 1;

      for (i = MAX_TREE_DEPTH; i > level; i--) Fprintf(stdout, "   ");
      Fprintf(stdout,
	      "       Iteration %2d: L fanout %d; R fanout %d (<= %d) %s\n",
	      iterations, leftfanout, rightfanout, TreeFanout[level],
	      found ? "SUCCESSFUL" : "UNSUCCESSFUL");
    }

    DBUG_EXECUTE("place", 
		 Fprintf(DBUG_FILE,"Level %d: ",level);
		 Dbug_print_cells(left,partition);
		 Dbug_print_cells(partition+1,right);
		 Fprintf(DBUG_FILE,"\n");
		 {
		   int i;
		   Fprintf(DBUG_FILE,"L ");
		   for (i = 1; i <= Nodes; i++) 
		     Fprintf(DBUG_FILE,"%2d ",leftnodes[i]);
		   Fprintf(DBUG_FILE,"\nR ");
		   for (i = 1; i <= Nodes; i++) 
		     Fprintf(DBUG_FILE,"%2d ",rightnodes[i]);
		   Fprintf(DBUG_FILE,"\n");
		 }
		 Fprintf(DBUG_FILE, "%s\n", found?"SUCCESSFUL":"UNSUCCESSFUL");
		 );
  } while (iterations < MAX_PARTITION_ITERATIONS && !found);
  if (!found) {
    Fprintf(stdout,"Failed embedding at level %d; no partition\n",level);
    goto fail;
  }


  leftelement = GreedyPartition(left, partition, level-1);
  if (leftelement == 0) goto fail;
  rightelement = GreedyPartition(partition+1, right, level-1);
  if (rightelement == 0) goto fail;

  /* add it to the list */
  AddNewElement(leftelement, rightelement);
  DBUG_RETURN(NewN);    

 fail:
  NewN = OriginalNewN;
  DBUG_RETURN(0);
}




void TopDownEmbedCell(char *cellname, char *filename, 
	enum EmbeddingStrategy strategy)
{
  struct nlist *tp;
  int i;
  int Found;
  float StartTime;
	
  tp = LookupCell(cellname);
  curcell = tp;
  if (!OpenEmbeddingFile(cellname, filename)) return;
	
  StartTime = CPUTime();
  if (!InitializeMatrices(cellname)) return;
  NewN = Elements;
  for (i = 1; i <= Leaves; i++) permutation[i] = i;

  RandomSeed(1);
  Found = 0;
  TopDownStartLevel = MAX_TREE_DEPTH;
  switch (strategy) {
  case random_embedding:
    Found = RandomPartition(1, Leaves, TopDownStartLevel);
    break;
  case greedy:
    Found = GreedyPartition(1, Leaves, TopDownStartLevel);
    break;
  case anneal:
    Found = AnnealPartition(1, Leaves, TopDownStartLevel);
    break;
  case bottomup:
    Fprintf(stderr,"ERROR: called TopDownEmbedCell with bottomup strategy\n");
    break;
  }
  if (Found) {
    Printf ("successful embedding (Element %d) (time = %.2f s):\n", 
	    NewN, ElapsedCPUTime(StartTime));
#if 0
    for (i = 1; i <= Leaves; i++) Printf("%d ",permutation[i]);
    Printf("\n");
#endif
#if 1
    PrintE(stdout,NewN);
    Printf("\n");
#endif

    FreeEmbeddingTree((struct embed *)(tp->embedding));
    tp->embedding = EmbeddingTree(tp, Found);
    PrintEmbeddingTree(stdout,cellname,1);
    PrintEmbeddingTree(outfile,cellname,1);
    if (logging) PrintEmbeddingTree(logfile,cellname,1);
  }
  else {
    Fprintf(stdout,"No embedding found. Sorry.\n");
    Fprintf(outfile,"No embedding found. Sorry.\n");
    if (logging) Fprintf(logfile,"No embedding found. Sorry.\n");
  }

  CloseEmbeddingFile();
}
