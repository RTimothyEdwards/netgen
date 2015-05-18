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

/* random.c -- random graph partitioning for PROTOCHIP */

#include <stdio.h>

#include "config.h"
#include "hash.h"
#include "objlist.h"
#include "embed.h"
#include "print.h"
#include "dbug.h"


void GeneratePermutation(int left, int right)
/* permute the elements between (and including) 'left' and 'right' */
{
  int range;
  int element;
  int offset;

  range = right - left + 1;
  for (element = right; element > left; element--) {
    int tmp;
    offset = Random(range);
    range--;
    /* swap 'offset' and 'element' if they are not the same */
    if (left+offset != element) {
      tmp = permutation[left+offset];
      permutation[left+offset] = permutation[element];
      permutation[element] = tmp;
    }
  }
}


#if 0
#define GeneratePartition(left, right, level) ((left+right) / 2)
#else
int GeneratePartition(int left, int right, int level)
/* tries to find a balanced partition, as far as leaf cell usage */
{
  int leftsum, rightsum;
#if 1
  int i, greatest, greatestpos;

  /* find highest element, make it first */
  greatest = 0;
  greatestpos = left;
  for (i = left; i <= right; i++) {
    if (LEVEL(permutation[i]) > greatest) {
      greatest = LEVEL(permutation[i]);
      greatestpos = i;
    }
  }
  if (left != greatestpos) {
    int tmp;

    tmp = permutation[left];
    permutation[left] = permutation[greatestpos];
    permutation[greatestpos] = tmp;
  }
#endif

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

int RandomPartition(int left, int right, int level)
/* return index of new element, if successful partition has bee achieved */
{
  int partition;
  int iterations;
  int found;
  int OriginalNewN;
  int leftelement, rightelement;

#define MAX_PARTITION_ITERATIONS 10

  DBUG_ENTER("Partition");
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

  /* generate a permutation, then split it at 
     (left .. partition), (partition + 1 .. right)
     and check and see if it is valid 
     */

/*  DBUG_PRINT("place",("trying to partition %d, %d",left,right)); */
  iterations = 0;
  do {
    int i;
    int leftfanout, rightfanout;

    iterations++;
    GeneratePermutation(left, right);
    partition = GeneratePartition(left, right, level);
    if (partition == 0) DBUG_RETURN(0); /* no valid partition */

    found = 0;
#ifdef BAD
    /* this is WRONG, as we need to set up {left,right}nodes */
    rightfanout = -1;
    leftfanout = PartitionFanout(left,partition,LEFT);
    if (leftfanout <= TreeFanout[level]) {
      rightfanout = PartitionFanout(partition+1, right, RIGHT);
      if (rightfanout <= TreeFanout[level]) found = 1;
    }
#else
    leftfanout = PartitionFanout(left,partition,LEFT);
    rightfanout = PartitionFanout(partition+1, right, RIGHT);
    if (leftfanout <= TreeFanout[level] && rightfanout <= TreeFanout[level])
	    found = 1;
#endif    

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
#ifdef BAD
      rightfanout = -1; 
      leftfanout =   PartitionFanout(left,partition,LEFT); 
      if (leftfanout <= TreeFanout[level]) { 
	rightfanout = PartitionFanout(partition+1, right, RIGHT); 
	if (rightfanout <= TreeFanout[level]) found = 1;
      }
#else
      leftfanout =   PartitionFanout(left,partition,LEFT); 
      rightfanout = PartitionFanout(partition+1, right, RIGHT); 
      if (leftfanout <= TreeFanout[level] && rightfanout <= TreeFanout[level])
	      found = 1;
#endif
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
#undef BIGLOOP
#ifdef BIGLOOP
    if ((leftelement = RandomPartition(left, partition, level-1)) == 0 ||
      (rightelement = RandomPartition(partition+1, right, level-1)) == 0) 
      found = 0;
#endif
  } while (iterations < MAX_PARTITION_ITERATIONS && !found);
  if (!found) {
    Fprintf(stdout,"Failed embedding at level %d; no partition\n",level);
    NewN = OriginalNewN;
    DBUG_RETURN(0);
  }
#ifndef BIGLOOP
  leftelement = RandomPartition(left, partition, level-1);
  if (leftelement == 0) goto fail;
  rightelement = RandomPartition(partition+1, right, level-1);
  if (rightelement == 0) goto fail;
#endif
  /* add it to the list */
  AddNewElement(leftelement, rightelement);
  DBUG_RETURN(NewN);    
#ifndef BIGLOOP
 fail:
  NewN = OriginalNewN;
  DBUG_RETURN(0);
#endif
}

