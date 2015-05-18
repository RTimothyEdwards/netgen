/*
     NETGEN  --  Copyright 1989, Massimo A. Sivilotti, Caltech
*/

#include "config.h"

#include <stdio.h>
#include <math.h>

#include "hash.h"
#include "objlist.h"
#include "embed.h"
#include "timing.h"
#include "print.h"
#include "dbug.h"

int MaxCPUTime = 100;  /* max number of seconds to burn */

#define MAX_SIM_ANNEAL_ITER 10
#define MAX_CHANGES_PER_ITER 2

#if 0
/* INLINE */
float RandomUniform(void)
{
  /* DANGER! DANGER! Non-portable code below!! */
  return ((float)(rand()) / ((1<<15) - 1.0));
}
#endif

int GenerateAnnealPartition(int left, int right, int level)
/* tries to find a balanced partition, as far as leaf cell usage */
{
  int i;
  int IncludedElements, partition;
  int ChangesMade, Iterations;
  int leftfanout, rightfanout;
  float T;

  level++;  /* just to keep to compiler from whining about unused parameter */

  IncludedElements = (right - left) / 2;
  partition = left + IncludedElements - 1;

  /* don't actually use {left,right}fanout, but need to 
     initialize leftnodes and rightnodes arrays */

  leftfanout = PartitionFanout(left, partition, LEFT);
  rightfanout = PartitionFanout(partition + 1, right, RIGHT);

#if 0
  /* someday, need an escape clause here */
  if (leftfanout <= TreeFanout[level] && rightfanout <= TreeFanout[level])
	  return(partition);
#endif

Printf("called generateannealpartition with left = %d, right = %d\n",left,right);
  /* actually do the annealing now */
  T = 3.0;
  do {
    int el1, el2;
    int delta;

    Iterations = 0;
    ChangesMade = 0;
    do {
      el1 = Random(partition - left + 1) + left;
      el2 = Random(right - partition) + partition + 1;
      Iterations++;

      delta = 0;
      for (i = 1; i <= Nodes; i++) {
	/* could do the following test with XOR */
	if (CSTAR[permutation[el1]][i] == 0 && CSTAR[permutation[el2]][i] == 0)
	  continue;
	if (CSTAR[permutation[el1]][i] != 0 && CSTAR[permutation[el2]][i] != 0)
	  continue;

	/* only one of these is non-zero */
	if (CSTAR[permutation[el1]][i] != 0) {
	  if (rightnodes[i] == 0) {
	    /* things are not good unless all the fanout is captured in el1 */
	    if (CSTAR[permutation[el1]][i] != leftnodes[i]) delta++;
	  }
	  else {
	    /* things are good if all fanout is captured in el1 */
	    if (CSTAR[permutation[el1]][i] == leftnodes[i]) delta--;
	  }
	}
	else {
	  if (leftnodes[i] == 0) {
	    /* things are not good unless all the fanout is captured in el2 */
	    if (CSTAR[permutation[el2]][i] != rightnodes[i]) delta++;
	  }
	  else {
	    /* things are good if all fanout is captured in el2 */
	    if (CSTAR[permutation[el2]][i] == rightnodes[i]) delta--;
	  }
	}
      }
DBUG_EXECUTE("place",
Printf("\n");
Printf("considering swapping %d and %d\n",permutation[el1],permutation[el2]);
Printf("E1: "); 
for (i = 1; i <= Nodes; i++) Printf("%2d ",CSTAR[permutation[el1]][i]);
Printf("\nL:  ");
for (i = 1; i <= Nodes; i++) Printf("%2d ",leftnodes[i]);
Printf("\nE2: ");
for (i = 1; i <= Nodes; i++) Printf("%2d ",CSTAR[permutation[el2]][i]);
Printf("\nR:  ");
for (i = 1; i <= Nodes; i++) Printf("%2d ",rightnodes[i]);
Printf("\nC0: ");
for (i = 1; i <= Nodes; i++) Printf("%2d ",CSTAR[0][i]);
Printf("\ndelta = %d\n", delta);
);

      if (delta < 0 || exp(-delta / T) > RandomUniform()) {
	int tmp;

	if (delta < 0) ChangesMade++;
	/* update the {left, right}nodes arrays */
	for (i = 1; i <= Nodes; i++) {
	   leftnodes[i] +=
		 CSTAR[permutation[el2]][i] - CSTAR[permutation[el1]][i];
	   rightnodes[i] -=
		 CSTAR[permutation[el2]][i] - CSTAR[permutation[el1]][i];
        }
	/* now swap the elements */
DBUG_EXECUTE("place",
        Printf("swapping elements %d and %d\n",
	       permutation[el1],permutation[el2]);
	);
	tmp = permutation[el1];
	permutation[el1] = permutation[el2];
	permutation[el2] = tmp;
      }
      
    } while (ChangesMade <= MAX_CHANGES_PER_ITER && 
	    Iterations < MAX_SIM_ANNEAL_ITER);
    T = 0.90 * T;
Printf("decreasing T to %.2f after %d iterations.\n",T,Iterations);
  } while (ChangesMade > 0);

  return (partition);
}

int AnnealPartition(int left, int right, int level)
/* return index of new element, if successful partition has been found */
{
  int partition;
  int iterations;
  int found;
  int OriginalNewN;
  int leftelement, rightelement;

#define MAX_PARTITION_ITERATIONS 10

  DBUG_ENTER("AnnealPartition");
  OriginalNewN = NewN;
  if (level < LEVEL(permutation[left])) {
    Fprintf(stdout,"Failed at level %d; subtree too deep\n",level);
    DBUG_RETURN(0);
  }

  if (left == right) DBUG_RETURN(permutation[left]);

  if (right - left == 1) {
	  /* don't bother annealing, just add it to the list */
	  AddNewElement(permutation[left], permutation[right]);
	  DBUG_RETURN(NewN);    
  }
  /* use simulated annealing to gather about 1/2 of the elements,
     Then check to see if it is valid.
     */

  DBUG_PRINT("place",("trying to partition %d, %d",left,right)); 
  iterations = 0;
  do {
    int i;
    int leftfanout, rightfanout;

    iterations++;
    partition = GenerateAnnealPartition(left, right, level);
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

#if 0
    if (!found) { 
      int IterationLimit;

      for (IterationLimit = 0; IterationLimit < 20 &&
	   GradientDescent(left, right, partition); IterationLimit++);

      found = 0; 
      leftfanout =  PartitionFanout(left,partition,LEFT); 
      rightfanout = PartitionFanout(partition+1, right, RIGHT); 
      if (leftfanout <= TreeFanout[level] && rightfanout <= TreeFanout[level])
	      found = 1;

      for (i = MAX_TREE_DEPTH; i > level; i--) Fprintf(stdout, "   ");
      Fprintf(stdout,
	      "       Iteration %2d: L fanout %d; R fanout %d (<= %d) %s\n",
	      iterations, leftfanout, rightfanout, TreeFanout[level],
	      found ? "SUCCESSFUL" : "UNSUCCESSFUL");
    }
#endif

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
  leftelement = AnnealPartition(left, partition, level-1);
  if (leftelement == 0) goto fail;
  rightelement = AnnealPartition(partition+1, right, level-1);
  if (rightelement == 0) goto fail;

  /* add it to the list */
  AddNewElement(leftelement, rightelement);
  DBUG_RETURN(NewN);    

 fail:
  NewN = OriginalNewN;
  DBUG_RETURN(0);
}

