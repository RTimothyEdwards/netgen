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

/* bottomup.c -- Bottom-up graph partitioning algorithm for embedding
                 netlists on hierarchical prototyping chip.
*/		 

#include "config.h"
#include <stdio.h>

#include "hash.h"
#include "objlist.h"
#include "timing.h"
#include "embed.h"
#include "dbug.h"
#include "print.h"

/* count invokations of different test procedures */
int CountIndependent;
int CountFanoutOK;
int CountSwallowedElements;


INLINE 
int Independent(int E1, int E2)
/* return 1 if E1 and E2 share no leaves in common */
{
  int leafelement;
  CountIndependent++;
  for (leafelement = 0; leafelement <= PackedLeaves; leafelement++)
    if (MSTAR[E1][leafelement] & MSTAR[E2][leafelement]) return (0);
  return(1);
}

INLINE
int FanoutOK(int E1, int E2)
{
  int approxfanout;
  int node;
	
  CountFanoutOK++;

  /* trivial exclusion based on fanout upper bound */
  /* remember, the current PROTOCHIP requires allocation of 
     an output even for swallowed nodes */

  approxfanout = 0;
  for (node = 1; node <= Nodes; node++)
    if (C[E1][node] || C[E2][node]) approxfanout++;
  if (approxfanout > TreeFanout[MAX(LEVEL(E1),LEVEL(E2)) + 1]) return(0);
  return(1);
}


INLINE
int Swallowed(int Parent, int Child)
/* returns 1 if Child's fanout is contained in Parent's */
{
  int node;

  for (node = 1; node <= Nodes; node++) 
    if (C[Child][node] && !(C[Parent][node])) return(0);

#ifdef PLACE_DEBUG
  Printf("Element %d swallowed by %d\n",Child,Parent);
#endif
  return(1);
}
				
#if 1
#define SmallEnough(a,b) 0
#else
int SmallEnough(int E1, int E2)
/* returns 1 if fanout(E1+E2) is less than MAX(fanout(E1),fanout(E2)) */
{
  int node;
  int fanout;

  return(0); /* for now */

  fanout = 0;
  for (node = 1; node <= Nodes; node++)
    if (C[E1][node] || C[E2][node]) 
      if (CSTAR[E1][node] + CSTAR[E2][node] < CSTAR[0][node]) fanout++;

  return (fanout <= MAX(PINS(E1), PINS(E2)));

#ifdef PLACE_DEBUG
  Printf("Element %d,%d is SmallEnough\n",E1,E2);
#endif
  return(1);
}
#endif

INLINE 
int SuccessfulEmbedding(int E)
/* returns 1 if element E is a successful embedding */
{
  int testleaf;

#if 1
  for (testleaf = 0; testleaf <= PackedLeaves; testleaf++)
    if (MSTAR[E][testleaf] != MSTAR[0][testleaf]) return (0);
#else
  for (testleaf = 1; testleaf <= Leaves; testleaf++)
    if (!TestPackedArrayBit(MSTAR[E],testleaf)) return(0);
#endif
  return(1);
}


void STARTPASS(FILE *f, int level1, int level2)
{
  if (f == NULL) return;
  Fprintf(f,"%2d: ",Pass);
  if (Exhaustive) 
    Fprintf(f,"to level %d: (%d) ",level1,CountInLevel(level1,1));
  else Fprintf(f,"(%d,%d) [%5d,%5d]",
	       level1,level2,CountInLevel(level1,0),CountInLevel(level2,0));
#ifdef PLACE_DEBUG
  Fprintf(f,"\n");
#endif
  Fflush(f);
}

void ENDPASS(FILE *f, int level1, int level2)
{
  if (f == NULL) return;
  Fprintf(f,"%5dA,%3dS,%5dT", 
	  NewElements, NewSwallowed, Elements);
#if 1
  if (NewElements) {
    int level;
    level = MAX(level1, level2) + 1;
    Fprintf(f," (%4.1fP %4.1fC %4.1fL)(%2d %2d %2d)", 
	    (float)SumPINS / NewElements, (float)SumCommonNodes / NewElements,
	    (float)SumUsedLeaves / NewElements,
	    TreeFanout[level], MinCommonNodes[level],  MinUsedLeaves[level] );
  }
#else
  if (NewElements) Fprintf(f," (%4.1fP <= %2d, %4.1fC >= %2d, %4.1fL >= %2d)", 
			   (float)SumPINS / (float)NewElements,
			   TreeFanout[MAX(level1,level2) + 1],
			   (float)SumCommonNodes / (float)NewElements,
			   MinCommonNodes[MAX(level1,level2) + 1],
			   (float)SumUsedLeaves / (float)NewElements,
			   MinUsedLeaves[MAX(level1,level2) + 1] );
#endif
  Fprintf(f,"\n");
  Fflush(f);
}

static float StartTime;

void EPILOG(FILE *f, int element)
{
  if (f == NULL) return;
  Fprintf(f,"Stats: Passes = %d, Elements = %d", Pass, Elements);
  if (element)  Fprintf(f, ", Level = %d",LEVEL(element));
  Fprintf(f, ", Elapsed CPU time = %.2f s\n", ElapsedCPUTime(StartTime));

  Fprintf(f,"Tests: Indep. = %d, Conn. = %d, Fanout = %d, Exists = %d\n",
	  CountIndependent, CountAnyCommonNodes, CountFanoutOK, CountExists);

  PrintExistSetStats(f);
  Fprintf(f,"Swallowed elements = %d", CountSwallowedElements);
  if (Exhaustive) Fprintf(f,", EXHAUSTIVE");
  Fprintf(f,"\n\n");
  Fflush(f);
}

void SwallowSubTree(int E, int level)
{
  if (E != 0 && !(SWALLOWED(E))) {
    SWALLOWED(E) = level;
    CountSwallowedElements++;
    NewSwallowed++;

    SwallowSubTree(L(E),level);
    SwallowSubTree(R(E),level);
  }
}

int Logging(int level1, int level2)
/* return 1 if we should log this level */
{
  if (!logging) return(0);
  if (!selectivelogging) return(1);
  if (level2 == -1) return(level1 == LogLevel1);
  if (LogLevel2 == -1) return(level1 == LogLevel1);
  if (LogLevel1 == -1) return(level2 == LogLevel2);
  return((level1 == LogLevel1) && (level2 == LogLevel2));
}
    

#if 0

int DoAPass(int Level1, int Level2)
/* tries to merge elements of level 'Level1' with elements of level 'Level2' */
/* returns element number if successful embedding found */
{
  int E1, E2;
  int PossiblyDone;
  int MaxLevel, MinLevel, ThisLevel;
  int MinDoneLevel, junk;
  int found;

  Pass++;
  STARTPASS(stdout,Level1,Level2);
  STARTPASS(outfile,Level1,Level2);
  if (logging) STARTPASS(logfile,Level1,Level2);

  /* determine the minimum embedding level */
  MaxLevel = MAX(Level1, Level2);
  MinLevel = MIN(Level1, Level2);
  ThisLevel = MaxLevel + 1;  /* level at which any new element will go */
  junk = Leaves - 1;
  for (MinDoneLevel = 0; junk; MinDoneLevel++) junk = junk >> 1;
  PossiblyDone = (MaxLevel >= MinDoneLevel - 1);

  NewElements = 0;
  NewSwallowed = 0;
  SumPINS = 0;
  SumCommonNodes = 0;
  SumUsedLeaves = 0;

  found = 0;
  /* make first pass to determine what we can swallow */
#if 1
  for (E1 = 1; E1 <= Elements; E1++) {
#else
  for (E1 = Elements; E1 > 0; E1--) {
#endif
    if ((LEVEL(E1) != MaxLevel && LEVEL(E1) != MinLevel)) continue;
/* might exclude SWALLOWED elements as well */
    if (SWALLOWED(E1) && SWALLOWED(E1) != Pass) continue;
    for (E2 = E1 - 1; E2 > 0; E2--) {
      if ((LEVEL(E2) != MaxLevel && LEVEL(E1) == MinLevel) ||
	  (LEVEL(E2) != MinLevel && LEVEL(E1) == MaxLevel)) continue;
/* might exclude SWALLOWED elements as well */
      if (SWALLOWED(E2) && SWALLOWED(E2) != Pass) continue;

      if (Independent(E1,E2) 
#ifdef CHECK_MINUSEDLEAVES
	  && UsedLeaves(E1,E2) >= MinUsedLeaves[MaxLevel + 1]
#endif
	  && AnyCommonNodes(E1,E2) 
	  && FanoutOK(E1,E2)
	  && !Exists(E1,E2)
	  && (Swallowed(E1, E2) || Swallowed(E2, E1) || SmallEnough(E1,E2))) {
	SwallowSubTree(E1, Pass);
	SwallowSubTree(E2, Pass);
	AddNewElement(E1,E2);
#ifdef PLACE_DEBUG
	Printf("Swallowing (%d,%d) (%d common nodes) into element %d\n",
	       E1,E2,CommonNodes(E1,E2,1),NewN);
#endif
	if (PossiblyDone && SuccessfulEmbedding(NewN)) {
	  found = NewN;
	  goto done;
	}
	if (NewN >= MAX_ELEMENTS) return(MAX_ELEMENTS);
	if (FatalError) goto done;
	/* break; do not consider E1 or E2 any further */
      }
    }
  }

  /* make second pass to consider merging everything else */
#if 1
  for (E1 = 1; E1 <= Elements; E1++) {
#else
  for (E1 = Elements; E1 > 0; E1--) {
#endif
    if ((LEVEL(E1) != MinLevel && LEVEL(E1) != MaxLevel) ||
	SWALLOWED(E1)) continue;
#ifdef CHECK_MINCOMMONNODES
    if (PINS(E1) < MinCommonNodes[MaxLevel + 1]) continue;
#endif
    for (E2 = E1 - 1; E2 > 0; E2--) {
      if ((LEVEL(E2) != MaxLevel && LEVEL(E1) == MinLevel) ||
	  (LEVEL(E2) != MinLevel && LEVEL(E1) == MaxLevel) ||
	  SWALLOWED(E2)) continue;

#ifdef CHECK_MINCOMMONNODES
      if (PINS(E2) < MinCommonNodes[MaxLevel + 1]) continue;
#endif
      if (Independent(E1,E2) 
#ifdef CHECK_MINUSEDLEAVES
	  && UsedLeaves(E1,E2) >= MinUsedLeaves[MaxLevel + 1]
#endif
	  && AnyCommonNodes(E1,E2) 
	  && FanoutOK(E1,E2)
	  && !Exists(E1,E2)) {
#ifdef CHECK_MINCOMMONNODES
	int commonnodes;
	commonnodes = CommonNodes(E1, E2, 1);

	if (commonnodes < MinCommonNodes[MaxLevel + 1])
	  continue;
	Printf("Adding element (%d,%d) with %d common nodes\n",
	       E1,E2,commonnodes);
#endif
	AddNewElement(E1,E2);
	if (PossiblyDone && SuccessfulEmbedding(NewN)) {
	  found = NewN;
	  goto done;
	}
	if (NewN >= MAX_ELEMENTS) return(MAX_ELEMENTS);
	if (FatalError) goto done;
      }
    }
  }

 done:
  Elements = NewN;

  ENDPASS(stdout, Level1, Level2);
#ifdef PLACE_DEBUG
  EPILOG(stdout, found);
#endif
  ENDPASS(outfile, Level1, Level2);
  EPILOG(outfile, found);
  if (logging) {
    ENDPASS(logfile, Level1, Level2);
    EPILOG(logfile, found);
    if (NewElements && Logging(Level1,Level2)) {
      PrintOwnership(logfile); PrintC(logfile);
      PrintCSTAR(logfile); Fflush(logfile);
    }
  }

  return(found);
}

#else

int DoAPass(int Level1, int Level2)
/* tries to merge elements of level 'Level1' with elements of level 'Level2' */
/* returns element number if successful embedding found */
{
  int E1, E2;
  int PossiblyDone;
  int MaxLevel, MinLevel;
  int MinDoneLevel, junk;
  int found;

  Pass++;
  STARTPASS(stdout,Level1,Level2);
  STARTPASS(outfile,Level1,Level2);
  if (logging) STARTPASS(logfile,Level1,Level2);

  /* determine the minimum embedding level */
  MaxLevel = MAX(Level1, Level2);
  MinLevel = MIN(Level1, Level2);
  junk = Leaves - 1;
  for (MinDoneLevel = 0; junk; MinDoneLevel++) junk = junk >> 1;
  PossiblyDone = (MaxLevel >= MinDoneLevel - 1);

  NewElements = 0;
  NewSwallowed = 0;
  SumPINS = 0;
  SumCommonNodes = 0;
  SumUsedLeaves = 0;

  found = 0;

  /* make single pass to consider merging  and swallow */
#if 1
  for (E1 = 1; E1 <= Elements; E1++) {
#else
  for (E1 = Elements; E1 > 0; E1--) {
#endif
    if ((LEVEL(E1) != MinLevel && LEVEL(E1) != MaxLevel) ||
/*	(SWALLOWED(E1) && SWALLOWED(E1) != Pass)) */
	SWALLOWED(E1))
      continue;
#ifdef CHECK_MINCOMMONNODES
    if (PINS(E1) < MinCommonNodes[MaxLevel + 1]) continue;
#endif
    for (E2 = E1 - 1; E2 > 0; E2--) {
      if ((LEVEL(E2) != MaxLevel && LEVEL(E1) == MinLevel) ||
	  (LEVEL(E2) != MinLevel && LEVEL(E1) == MaxLevel) ||
	  /* (SWALLOWED(E2) && SWALLOWED(E2) != Pass)) */
	  SWALLOWED(E2))
	continue;

#ifdef CHECK_MINCOMMONNODES
      if (PINS(E2) < MinCommonNodes[MaxLevel + 1]) continue;
#endif
      if (Independent(E1,E2) 
#ifdef CHECK_MINUSEDLEAVES
	  && UsedLeaves(E1,E2) >= MinUsedLeaves[MaxLevel + 1]
#endif
	  && AnyCommonNodes(E1,E2) 
	  && FanoutOK(E1,E2)
	  && !Exists(E1,E2)) {
#ifdef CHECK_MINCOMMONNODES
	int commonnodes;
	commonnodes = CommonNodes(E1, E2, 1);

	if (commonnodes < MinCommonNodes[MaxLevel + 1])
	  continue;
#ifdef PLACE_DEBUG
	Printf("Adding element (%d,%d) with %d common nodes\n",
	       E1,E2,commonnodes);
#endif
#endif
	AddNewElement(E1,E2);
	if (Swallowed(E1, E2) || Swallowed(E2, E1) || SmallEnough(E1,E2)) {
	  SwallowSubTree(E1, Pass);
	  SwallowSubTree(E2, Pass);
#ifdef PLACE_DEBUG
	  Printf("Swallowing (%d,%d) (%d common nodes) into element %d\n",
		 E1,E2,CommonNodes(E1,E2,1),NewN);
#endif
	}
	if (PossiblyDone && SuccessfulEmbedding(NewN)) {
	  found = NewN;
	  goto done;
	}
	if (NewN >= MAX_ELEMENTS) return(MAX_ELEMENTS);
	if (FatalError) goto done;
      }
    }
  }

 done:
  Elements = NewN;

  ENDPASS(stdout, Level1, Level2);
#ifdef PLACE_DEBUG
  EPILOG(stdout, found);
#endif
  ENDPASS(outfile, Level1, Level2);
  EPILOG(outfile, found);
  if (logging) {
    ENDPASS(logfile, Level1, Level2);
    EPILOG(logfile, found);
/*    if (NewElements && Logging(Level1,Level2)) { */
    if (Logging(Level1,Level2)) {
      PrintOwnership(logfile); PrintC(logfile);
      PrintCSTAR(logfile); Fflush(logfile);
    }
  }

  return(found);
}
#endif

int ExhaustivePass(int Level)
/* tries to merge elements of level 'Level' and below */
/* returns element number if successful embedding found */
{
  int E1, E2;
  int found;
  int PossiblyDone;
  int MinDoneLevel, junk;
  
  Pass++;
  STARTPASS(stdout, Level, Level);
  STARTPASS(outfile, Level, Level);
  if (logging) STARTPASS(logfile, Level, Level);
  /* determine the minimum embedding level */
  junk = Leaves - 1;
  for (MinDoneLevel = 0; junk; MinDoneLevel++) junk = junk >> 1;
  PossiblyDone = (Level >= MinDoneLevel - 1);

  NewElements = 0;
  SumPINS = 0;
  SumCommonNodes = 0;
  SumUsedLeaves = 0;

  found = 0;
  for (E1 = 1; E1 <= Elements; E1++) {
    if (LEVEL(E1) != Level) continue;
#if 1
    for (E2 = E1 - 1; E2 > 0; E2--) 
#else
    for (E2 = 1; E2 < E1; E2++)  /* statistically, slightly worse */
#endif
      if (LEVEL(E2) <= LEVEL(E1)
	  && Independent(E1,E2) 
	  && AnyCommonNodes(E1,E2) 
	  && FanoutOK(E1,E2)
	  && !Exists(E1,E2)) {

	AddNewElement(E1,E2);
	if (PossiblyDone && SuccessfulEmbedding(NewN)) {
	  found = NewN;
	  goto done;
	}
	if (NewN >= MAX_ELEMENTS) return(MAX_ELEMENTS);
#if 0
	if (Swallowed(E1,E2)) break;  /* works OK, but makes csrlntk 5 deep*/
#endif
#if 0
	if (Swallowed(E2,E1)) break;  /* largely untested: csrlntk in 5 */
#endif
	if (FatalError) goto done;
      }
  }

 done:
  Elements = NewN;

  ENDPASS(stdout, Level, Level);
  ENDPASS(outfile, Level, Level);
#ifdef PLACE_DEBUG
  EPILOG(stdout, found);
  EPILOG(outfile, found);
#endif
  if (logging) {
    ENDPASS(logfile, Level, Level);
    EPILOG(logfile, found);
    if (NewElements && Logging(Level,-1)) {
      PrintOwnership(logfile); PrintC(logfile);
      PrintCSTAR(logfile); Fflush(logfile);
    }
  }

  return(found);
}


void PROLOG(FILE *f)
{
  long totalsize;
  int junk,  MinDoneLevel;

  /* determine the minimum embedding level */
  junk = Leaves - 1;
  for (MinDoneLevel = 0; junk; MinDoneLevel++) junk = junk >> 1;

  Fprintf(f,"MAX_ELEMENTS = %d, ",MAX_ELEMENTS);
  Fprintf(f,"MAX_LEAVES = %d, ",MAX_LEAVES);
  Fprintf(f,"MAX_NODES = %d, ",MAX_NODES);
  Fprintf(f,"MAX_TREE_DEPTH = %d\n",MAX_TREE_DEPTH); 

  Fprintf(f,"Matrix sizes: M = %ldK, MSTAR = %ldK, C = %ldK, CSTAR = %ldK\n",
	  (long)sizeof(M)/1024,(long)sizeof(MSTAR)/1024,
	  (long)sizeof(C)/1024, (long)sizeof(CSTAR)/1024);
  totalsize = sizeof(M) + sizeof(MSTAR) + sizeof(C) + sizeof(CSTAR);
#ifdef EX_TREE_FOR_EXIST
  totalsize +=  sizeof(ex_array);
  Fprintf(f,"              ex_array = %ldK, total = %ldK\n",
	  (long)sizeof(ex_array)/1024, (long)totalsize/1024);
#else
  Fprintf(f,"              total = %ldK\n", (long)totalsize/1024);
#endif

  Fprintf(f,
" 0: %d elements, %d nodes, %d ports. Earliest embedding level = %d\n", 
	Elements, Nodes, PINS(0), MinDoneLevel); 
  Fflush(f);
}

void EmbedCell(char *cellname, char *filename)
{
  int found;
  int level1, level2;
  int FillingLevel;
/*  int SomeNewElements; */
	
  if (!OpenEmbeddingFile(cellname, filename)) return;
	
  StartTime = CPUTime();
  if (!InitializeMatrices(cellname)) return;
  if (!InitializeExistTest()) return;
  FatalError = 0;
  NewN = Elements;
  Pass = 0;
  CountIndependent = 0;
  CountAnyCommonNodes = 0;
  CountFanoutOK = 0;
  CountExists = 0;
  CountSwallowedElements = 0;

  Fprintf(stdout,"Embedding cell: %s\n",cellname);
  PROLOG(stdout);
  Fprintf(outfile,"Embedding cell: %s\n",cellname);
  PROLOG(outfile);

  if (logging) {
    Fprintf(logfile,"Embedding cell: %s\n",cellname);
    PROLOG(logfile);
    PrintOwnership(logfile);
    PrintC(logfile);
    PrintCSTAR(logfile);
    Fflush(logfile);
  }

  if (Exhaustive) {
    for (level1 = 0; level1 < MAX_TREE_DEPTH; level1++) {
      found = ExhaustivePass(level1);
      if (found || FatalError) goto done;
    }
  }
  else {
#if 1
    /* do not try to be clever about minimizing passes */
    found = -1;  /* fake-out to first call below */
    for (FillingLevel = 0; FillingLevel < MAX_TREE_DEPTH; FillingLevel++) {
      for (level1 = FillingLevel - 1; level1 >= 0 || found == -1; level1--){
	if (found == -1) level1 = 0;
	found = DoAPass(FillingLevel, level1);
	if (found || FatalError) goto done;
	/* now try to go up the ladder */
	/* NewElements = 1; only do it if we added something in DoAPass */
	for (level2 = FillingLevel + 1; 
	     NewElements && level2 < MAX_TREE_DEPTH; level2++) {
	  found = DoAPass(level2, level2);
	  if (found || FatalError) goto done;
	}
      }
    }
#else
    /* try to minimize number of passes; only works for trees with
       leaves at level 0 */
    found = -1;  /* fake-out to first call below */
    SomeNewElements = 1;
    for (FillingLevel = 0; 
	 FillingLevel < MAX_TREE_DEPTH && SomeNewElements; FillingLevel++) {
      SomeNewElements = 0;
      for (level1 = FillingLevel - 1; level1 >= 0 || found == -1; level1--){
	if (found == -1) level1 = 0;
	found = DoAPass(FillingLevel, level1);
	if (NewElements) SomeNewElements = 1;
	if (found || FatalError) goto done;
	/* now try to go up the ladder */
	NewElements = 1;
	for (level2 = FillingLevel + 1; 
	     NewElements && level2 < MAX_TREE_DEPTH; level2++) {
	  found = DoAPass(level2, level2);
	  if (found || FatalError) goto done;
	}
      }
    }
#endif
  }

 done:
  if (FatalError) {
    Fprintf(stdout,"Internal Fatal Error\n");
    Fprintf(outfile,"Internal Fatal Error\n");
    found = 0;
  }
  if (found >= MAX_ELEMENTS) found = 0;
	
  if (found) {
    struct nlist *tp;
    
    tp = LookupCell(cellname);
    FreeEmbeddingTree((struct embed *)(tp->embedding));
    tp->embedding = EmbeddingTree(tp, found);
#if 0
    PrintEmbedding(stdout,cellname,found);
    PrintEmbedding(outfile,cellname,found);
    if (logging) PrintEmbedding(logfile,cellname,found);
#endif
    PrintEmbeddingTree(stdout,cellname,1);
    PrintEmbeddingTree(outfile,cellname,1);
    if (logging) PrintEmbeddingTree(logfile,cellname,1);
  }
  else {
    Fprintf(stdout,"No embedding found. Sorry.\n");
    Fprintf(outfile,"No embedding found. Sorry.\n");
    if (logging) Fprintf(logfile,"No embedding found. Sorry.\n");
  }
  EPILOG(stdout, found);
  EPILOG(outfile, found);
  if (logging) EPILOG(logfile, found);

  CloseEmbeddingFile();
}

