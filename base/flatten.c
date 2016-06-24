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


/*  flatten.c -- flatten hierarchical netlists, either totally, or
                 just particular classes of subcells
*/		 

#include "config.h"

#include <stdio.h>
#include <math.h>

#ifdef IBMPC
#include <alloc.h>
#endif

#ifdef TCL_NETGEN
#include <tcl.h>
#endif

#include "netgen.h"
#include "hash.h"
#include "objlist.h"
#include "print.h"
#include "netcmp.h"

extern struct hashdict spiceparams;

#define OLDPREFIX 1

void flattenCell(char *name, int file)
{
  struct objlist *ParentParams;
  struct objlist *NextObj;
  struct objlist *ChildObjList;
  struct nlist *ThisCell;
  struct nlist *ChildCell;
  struct objlist *tmp, *ob2, *ob3;
  int	notdone, rnodenum;
  char	tmpstr[200];
  int	nextnode, oldmax;
#if !OLDPREFIX
  int     prefixlength;
#endif

  if (Debug) 
    Printf("Flattening cell: %s\n", name);
  if (file == -1)
     ThisCell = LookupCell(name);
  else
     ThisCell = LookupCellFile(name, file);
  if (ThisCell == NULL) {
    Printf("No cell %s found.\n", name);
    return;
  }
  FreeNodeNames(ThisCell);

  ParentParams = ThisCell->cell;
  nextnode = 0;
  for (tmp = ParentParams; tmp != NULL; tmp = tmp->next) 
    if (tmp->node >= nextnode) nextnode = tmp->node + 1;

  notdone = 1;
  while (notdone) {
    notdone = 0;
    for (ParentParams = ThisCell->cell; ParentParams != NULL;
	 ParentParams = NextObj) {
      if (Debug) Printf("Parent = %s, type = %d\n",
			ParentParams->name, ParentParams->type);
      NextObj = ParentParams->next;
      if (ParentParams->type != FIRSTPIN) continue;
      ChildCell = LookupCellFile(ParentParams->model.class, ThisCell->file);
      if (Debug) Printf(" Flattening instance: %s, primitive = %s\n",
			ParentParams->name, (ChildCell->class == CLASS_SUBCKT) ?
			"no" : "yes");
      if (ChildCell->class != CLASS_SUBCKT) continue;
      if (ChildCell == ThisCell) continue;	// Avoid infinite loop

      /* not primitive, so need to flatten this instance */
      notdone = 1;
      /* if this is a new instance, flatten it */
      if (ChildCell->dumped == 0) flattenCell(ParentParams->model.class,
			ChildCell->file);

      ChildObjList = CopyObjList(ChildCell->cell, 1);

      /* update node numbers in child to unique numbers */
      oldmax = 0;
      for (tmp = ChildObjList; tmp != NULL; tmp = tmp->next) 
	if (tmp->node > oldmax) oldmax = tmp->node;
      if (nextnode <= oldmax) nextnode = oldmax + 1;

      for (tmp = ChildObjList; tmp != NULL; tmp = tmp->next) 
	if (tmp->node <= oldmax && tmp->node != -1) {
	  UpdateNodeNumbers(ChildObjList, tmp->node, nextnode);
	  nextnode ++;
	}

      /* copy nodenumbers of ports from parent */
      ob2 = ParentParams;
      for (tmp = ChildObjList; tmp != NULL; tmp = tmp->next) 
	if (IsPort(tmp)) {
	  if (tmp->node != -1) {
	    if (Debug) 
	      Printf("  Sealing port: %d to node %d\n", tmp->node, ob2->node);
	    UpdateNodeNumbers(ChildObjList, tmp->node, ob2->node);
	  }

	/* in pathological cases, the lengths of the port lists may
           change.  This is an error, but that is no reason to allow
           the code to core dump.  We avoid this by placing a 
           superfluous check on ob2->type
        */

	  if (ob2 != NULL)
	    ob2 = ob2->next;
	}
    

      /* delete all port elements from child */
      while (IsPort(ChildObjList)) {
	/* delete all ports at beginning of list */
	if (Debug) Printf("deleting leading port from child\n");
	tmp = ChildObjList->next;
	FreeObjectAndHash(ChildObjList, ChildCell);
	ChildObjList = tmp;
      }
      tmp = ChildObjList;
      while (tmp->next != NULL) {
	if (IsPort(tmp->next)) {
	  ob2 = (tmp->next)->next;
	  if (Debug) Printf("deleting a port from child\n");
	  FreeObjectAndHash(tmp->next, ChildCell);
	  tmp->next = ob2;
	} 
	else tmp = tmp->next;
      }

      /* for each element in child, prepend 'prefix' */
#if !OLDPREFIX
      /* replaces all the sprintf's below */
      strcpy(tmpstr,ParentParams->instance.name);
      strcat(tmpstr,SEPARATOR);
      prefixlength = strlen(tmpstr);
#endif
      for (tmp = ChildObjList; tmp != NULL; tmp = tmp->next) {
	if (tmp->type == PROPERTY) continue;
	else if (IsGlobal(tmp)) {
	   /* Keep the name but search for node of same name in parent	*/
	   /* and replace the node number, if found.			*/

	   for (ob2 = ThisCell->cell; ob2 != NULL; ob2 = ob2->next) {
	      if (ob2->type == tmp->type) {
	         if ((*matchfunc)(tmp->name, ob2->name)) {
		    if (ob2->node >= 0) {
		       // Replace all child objects with this node number
		       rnodenum = tmp->node;
		       for (ob3 = ChildObjList; ob3 != NULL; ob3 = ob3->next) {
			  if (ob3->node == rnodenum)
			     ob3->node = ob2->node;
		       }
		       break;
		    }
		 }
	      }
	   }
	   HashPtrInstall(tmp->name, tmp, &(ThisCell->objdict));
	   continue;
	}

#if OLDPREFIX	
	sprintf(tmpstr, "%s%s%s", ParentParams->instance.name, SEPARATOR,
		tmp->name);
#else
	strcpy(tmpstr+prefixlength,tmp->name);
#endif
	if (Debug) Printf("Renaming %s to %s\n", tmp->name, tmpstr);
	FreeString(tmp->name);
	tmp->name = strsave(tmpstr);
#if OLDPREFIX
	sprintf(tmpstr, "%s%s%s", ParentParams->instance.name, SEPARATOR,
		tmp->instance.name);
#else
	strcpy(tmpstr+prefixlength,tmp->instance.name);
#endif
	FreeString(tmp->instance.name);
	tmp->instance.name = strsave(tmpstr);
	HashPtrInstall(tmp->name, tmp, &(ThisCell->objdict));
	if (tmp->type == FIRSTPIN) 
	  HashPtrInstall(tmp->instance.name, tmp, &(ThisCell->instdict));
      }

      /* splice instance out of parent */
      if (ParentParams == ThisCell->cell) {
	/* ParentParams are the very first thing in the list */
	ThisCell->cell = ChildObjList;
	for (ob2 = ChildObjList; ob2->next != NULL; ob2 = ob2->next) ;
      }
      else {
	/* find ParentParams in ThisCell list */
	for (ob2 = ThisCell->cell; ob2->next != ParentParams; ob2=ob2->next); 
	for (ob2->next = ChildObjList; ob2->next != NULL; ob2 = ob2->next) ;
      }
      /* now, ob2 is last element in child list, so skip and reclaim parent */
      tmp = ParentParams;
      do {
	tmp = tmp->next;
      } while ((tmp != NULL) && (tmp->type > FIRSTPIN));
      ob2->next = tmp;
      while (ParentParams != tmp) {
	ob2 = ParentParams->next;

	/* ParentParams are PORTS */
	FreeObjectAndHash(ParentParams, ThisCell);
	ParentParams = ob2;
      }
      NextObj = ParentParams;
    }				/* repeat until no more instances found */
  }
  CacheNodeNames(ThisCell);
  ThisCell->dumped = 1;		/* indicate cell has been flattened */
}

/*--------------------------------------------------------------*/
/* flattenInstancesOf --					*/
/*								*/
/* Causes all instances of 'instance'  within cell 'name' to be	*/
/* flattened.  For the purpose of on-the-fly flattening of .ext	*/
/* files as they are read in, "name" can be NULL, in which case	*/
/* CurrentCell (global variable) is taken as the parent.	*/
/*								*/
/* NOTE:  do not flatten 'instance' itself !! 			*/
/* Return the number of instances flattened.			*/
/*--------------------------------------------------------------*/

int flattenInstancesOf(char *name, int fnum, char *instance)
{
  struct objlist *ParentParams;
  struct objlist *ParentProps;
  struct objlist *NextObj;
  struct objlist *ChildObjList;
  struct nlist *ThisCell;
  struct  nlist *ChildCell;
  struct objlist *tmp, *ob2, *ob3;
  int	notdone, rnodenum;
  char	tmpstr[200];
  int	nextnode, oldmax, numflat = 0;
#if !OLDPREFIX
  int     prefixlength;
#endif

  if (name == NULL) {
    if (CurrentCell == NULL) {
      Printf("Error: no current cell.\n");
      return 0;
    }
    else
      ThisCell = CurrentCell;
  }
  else {
    if (Debug) 
       Printf("Flattening instances of %s within cell: %s\n", instance, name);
    if (fnum == -1)
       ThisCell = LookupCell(name);
    else
       ThisCell = LookupCellFile(name, fnum);
    if (ThisCell == NULL) {
      Printf("No cell %s found.\n", name);
      return 0;
    }
  }
  FreeNodeNames(ThisCell);

  ParentParams = ThisCell->cell;
  nextnode = 0;
  for (tmp = ParentParams; tmp != NULL; tmp = tmp->next) 
    if (tmp->node >= nextnode) nextnode = tmp->node + 1;

  notdone = 1;
  while (notdone) {
    notdone = 0;
    ParentParams = ThisCell->cell;
    for (ParentParams = ThisCell->cell; ParentParams != NULL;
	 ParentParams = NextObj) {
      if (Debug) Printf("Parent = %s, type = %d\n",
			ParentParams->name, ParentParams->type);
      NextObj = ParentParams->next;
      if (ParentParams->type != FIRSTPIN) continue;
      if (!(*matchfunc)(ParentParams->model.class, instance)) continue;

      ChildCell = LookupCellFile(ParentParams->model.class, ThisCell->file);
      if (Debug)
	 Printf(" Flattening instance: %s, primitive = %s\n",
			ParentParams->instance.name, (ChildCell->class == 
			CLASS_SUBCKT) ? "no" : "yes");
      if (ChildCell->class != CLASS_SUBCKT) continue;
      if (ChildCell == ThisCell) continue;	// Avoid infinite loop

      /* Does the parent cell have properties?  If so, save a pointer to them */
      for (ParentProps = ParentParams->next; ParentProps &&
		ParentProps->type != FIRSTPIN;
		ParentProps = ParentProps->next) {
	 if (ParentProps->type == PROPERTY) break;
      }
      if (ParentProps && (ParentProps->type != PROPERTY)) ParentProps = NULL;

      /* not primitive, so need to flatten this instance */
      notdone = 1;
      /* if this is a new instance, flatten it */
      /* if (ChildCell->dumped == 0) flattenCell(ParentParams->model.class, file); */

      ChildObjList = CopyObjList(ChildCell->cell, 1);
      numflat++;

      /* update node numbers in child to unique numbers */
      oldmax = 0;
      for (tmp = ChildObjList; tmp != NULL; tmp = tmp->next) 
	if (tmp->node > oldmax) oldmax = tmp->node;
      if (nextnode <= oldmax) nextnode = oldmax + 1;

      for (tmp = ChildObjList; tmp != NULL; tmp = tmp->next) 
	if (tmp->node <= oldmax && tmp->node > 0) {
	  if (Debug) Printf("Update node %d --> %d\n", tmp->node, nextnode);
	  UpdateNodeNumbers(ChildObjList, tmp->node, nextnode);
	  nextnode++;
	}

      /* copy nodenumbers of ports from parent */
      ob2 = ParentParams;
      for (tmp = ChildObjList; tmp != NULL; tmp = tmp->next) 
	if (IsPort(tmp)) {
	  if (tmp->node > 0) {
	    if (ob2->node == -1) {

	       // Before commiting to attaching to a unconnected node, see
	       // if there is another node in ParentParams with the same
	       // name and a valid node number.  If so, connect them.  In
	       // the broader case, it may be necessary to consider all
	       // nodes, not just those with node == -1, and call join()
	       // here to update all node numbers in the parent cell.
	       // In that case, a more efficient method is needed for
	       // tracking same-name ports.

	       for (ob3 = ParentParams; ob3 && ob3->type >= FIRSTPIN; ob3 = ob3->next) {
		   if (ob3 == ob2) continue;
		   if ((*matchfunc)(ob3->name, ob2->name) && ob3->node != -1) {
		       ob2->node = ob3->node;
		       break;
		   }
	       }
	    }
	    if (Debug) {
	       // Printf("  Sealing port: %d to node %d\n", tmp->node, ob2->node);
	       Printf("Update node %d --> %d\n", tmp->node, ob2->node);
	    }
	    UpdateNodeNumbers(ChildObjList, tmp->node, ob2->node);
	  }

	/* in pathological cases, the lengths of the port lists may
           change.  This is an error, but that is no reason to allow
           the code to core dump.  We avoid this by placing a 
           superfluous check on ob2->type
        */

	  if (ob2 != NULL)
	    ob2 = ob2->next;

	  if (ob2 == NULL) break;
	}

      /* Using name == NULL to indicate that a .ext file is being */
      /* flattened on the fly.  This is quick & dirty.		  */

      if (name != NULL) {
        /* delete all port elements from child */
        while ((ChildObjList != NULL) && IsPort(ChildObjList)) {
	  /* delete all ports at beginning of list */
	  if (Debug) Printf("deleting leading port from child\n");
	  tmp = ChildObjList->next;
	  // FreeObjectAndHash(ChildObjList, ChildCell);
	  FreeObject(ChildObjList);
	  ChildObjList = tmp;
        }
        tmp = ChildObjList;
        while (tmp && (tmp->next != NULL)) {
	  if (IsPort(tmp->next)) {
	    ob2 = (tmp->next)->next;
	    if (Debug) Printf("deleting a port from child\n");
	    // FreeObjectAndHash(tmp->next, ChildCell);
	    FreeObject(tmp->next);
	    tmp->next = ob2;
	  } 
	  else tmp = tmp->next;
        }
      }

      /* for each element in child, prepend 'prefix' */
#if !OLDPREFIX
      /* replaces all the sprintf's below */
      strcpy(tmpstr,ParentParams->instance.name);
      strcat(tmpstr,SEPARATOR);
      prefixlength = strlen(tmpstr);
#endif
      for (tmp = ChildObjList; tmp != NULL; tmp = tmp->next) {
	if (tmp->type == PROPERTY)
	    continue;

	else if (IsGlobal(tmp)) {
	   /* Keep the name but search for node of same name in parent	*/
	   /* and replace the node number, if found.			*/

	   for (ob2 = ThisCell->cell; ob2 != NULL; ob2 = ob2->next) {
	      /* Type in parent may be a port, not a global */
	      if (ob2->type == tmp->type || ob2->type == PORT) {
	         if ((*matchfunc)(tmp->name, ob2->name)) {
		    if (ob2->node >= 0) {
		       // Replace all child objects with this node number
		       rnodenum = tmp->node;
		       for (ob3 = ChildObjList; ob3 != NULL; ob3 = ob3->next) {
			  if (ob3->node == rnodenum)
			     ob3->node = ob2->node;
		       }
		       break;
		    }
		 }
	      }
	   }
	   // Don't hash this if the parent had a port of this name
	   if (!ob2 || ob2->type != PORT)
	      HashPtrInstall(tmp->name, tmp, &(ThisCell->objdict));
	   continue;
	}

#if OLDPREFIX	
	sprintf(tmpstr, "%s%s%s", ParentParams->instance.name, SEPARATOR,
		tmp->name);
#else
	strcpy(tmpstr+prefixlength,tmp->name);
#endif
	if (Debug) Printf("Renaming %s to %s\n", tmp->name, tmpstr);
	FreeString(tmp->name);
	tmp->name = strsave(tmpstr);
#if OLDPREFIX
	sprintf(tmpstr, "%s%s%s", ParentParams->instance.name, SEPARATOR,
		tmp->instance.name);
#else
	strcpy(tmpstr+prefixlength,tmp->instance.name);
#endif
	FreeString(tmp->instance.name);
	tmp->instance.name = strsave(tmpstr);
	HashPtrInstall(tmp->name, tmp, &(ThisCell->objdict));
	if (tmp->type == FIRSTPIN) 
	  HashPtrInstall(tmp->instance.name, tmp, &(ThisCell->instdict));
      }

      /* Do property inheritance */

      if (ParentProps) {
         for (ob2 = ChildObjList; ob2 != NULL; ob2=ob2->next) {

	    /* If the parent cell has properties to declare, then	*/
	    /* pass them on to children.  Use globals only if the	*/
	    /* spiceparams dictionary is active (during file reading	*/
	    /* only).							*/

	    if (ob2->type == PROPERTY)
		ReduceExpressions(ob2, ParentProps, ChildCell,
			(spiceparams.hashtab == NULL) ? 0 : 1);
         }
      }

      /* splice instance out of parent */
      if (ParentParams == ThisCell->cell) {
	/* ParentParams are the very first thing in the list */
	ThisCell->cell = ChildObjList;
	for (ob2 = ChildObjList; ob2 && ob2->next != NULL; ob2 = ob2->next) ;
      }
      else {
	/* find ParentParams in ThisCell list */
	for (ob2 = ThisCell->cell; ob2 && ob2->next != ParentParams; ob2=ob2->next); 
	if (ob2)
	   for (ob2->next = ChildObjList; ob2->next != NULL; ob2 = ob2->next) ;
      }
      /* now, ob2 is last element in child list, so skip and reclaim parent */

      tmp = ParentParams;
      do {
	tmp = tmp->next;
      } while ((tmp != NULL) && ((tmp->type > FIRSTPIN) || (tmp->type == PROPERTY)));
      if (ob2) ob2->next = tmp;
      while (ParentParams != tmp) {
	ob2 = ParentParams->next;
	FreeObjectAndHash(ParentParams, ThisCell);
	ParentParams = ob2;
      }
      NextObj = ParentParams;
    }				/* repeat until no more instances found */
  }
  CacheNodeNames(ThisCell);
  ThisCell->dumped = 1;		/* indicate cell has been flattened */
  return numflat;
}


void Flatten(char *name, int file)
{
    ClearDumpedList(); /* keep track of flattened cells */
    flattenCell(name, file);
}


static char *model_to_flatten;

int flattenoneentry(struct hashlist *p, int file)
{
   struct nlist *ptr;

   ptr = (struct nlist *)(p->ptr);
   if (file == ptr->file) {
      if (!(*matchfunc)(ptr->name, model_to_flatten) && (ptr->class == CLASS_SUBCKT))
	 flattenInstancesOf(ptr->name, file, model_to_flatten);
      else if (ptr->flags & CELL_DUPLICATE) {
	 char *bptr = strstr(ptr->name, "[[");
	 if (bptr != NULL) {
	    *bptr = '\0';
            if (!(*matchfunc)(ptr->name, model_to_flatten)
			&& (ptr->class == CLASS_SUBCKT)) {
	       *bptr = '[';
	       flattenInstancesOf(ptr->name, file, model_to_flatten);
	    }
	    *bptr = '[';
	 }
      }
   }
   return(1);
}


void FlattenInstancesOf(char *model, int file)
{
   if ((file == -1) && (Circuit1 != NULL) && (Circuit2 != NULL)) {
      FlattenInstancesOf(model, Circuit1->file);
      FlattenInstancesOf(model, Circuit2->file);
      return;
   }
   ClearDumpedList(); /* keep track of flattened cells */
   model_to_flatten = strsave(model);
   RecurseCellFileHashTable(flattenoneentry, file);
   FREE(model_to_flatten);
}

/*
 *-----------------------------------------------------------
 * convertGlobalsOf ---
 *
 *   Called once for each cell that instantiates one or
 *   more cells "model_to_flatten" (global variable).  A
 *   global variable has just been added to the front of
 *   the cell's master pin list.  Get the global variable
 *   name.  Find it in the parent cell or else create it
 *   if it does not exist.  Add the new node to the pin
 *   list for each instance call.
 *   
 *-----------------------------------------------------------
 */

void convertGlobalsOf(char *name, int fnum, char *instance)
{
   struct objlist *ParentParams;
   struct objlist *ChildOb, *Ob2, *Ob;
   struct objlist *newpin, *newnode, *snode, *lnode;
   struct nlist *ThisCell;
   struct nlist *ChildCell;
   int maxnode, maxpin;

   if (name == NULL) {
      if (CurrentCell == NULL) {
	 Printf("Error: no current cell.\n");
	 return;
      }
      else
	 ThisCell = CurrentCell;
   }
   else {
      if (fnum == -1)
         ThisCell = LookupCell(name);
      else
         ThisCell = LookupCellFile(name, fnum);
      if (ThisCell == NULL) {
	 Printf("No cell %s found.\n", name);
	 return;
      }
   }

   FreeNodeNames(ThisCell);

   for (ParentParams = ThisCell->cell; ParentParams != NULL;
		ParentParams = ParentParams->next) {
      if (ParentParams->type != FIRSTPIN) continue;
      if (!(*matchfunc)(ParentParams->model.class, instance)) continue;

      // Move forward to last pin in the pin list.  The "type" record
      // holds the pin numbering, so we want to find the maximum pin
      // number and keep going from there.

      maxpin = 0;
      while (ParentParams->next != NULL) {
	 if (ParentParams->type >= maxpin) maxpin = ParentParams->type + 1;
	 if (ParentParams->next->type < FIRSTPIN) break;
	 else if (!(*matchfunc)(ParentParams->instance.name,
			ParentParams->next->instance.name))
	    break;
	 ParentParams = ParentParams->next;
      }
      if (ParentParams->type >= maxpin) maxpin = ParentParams->type + 1;

      ChildCell = LookupCellFile(ParentParams->model.class, ThisCell->file);
      ChildOb = ChildCell->cell;

      // The node to make local will be the last pin in the child
      while (IsPort(ChildOb) && ChildOb->next != NULL
		&& IsPort(ChildOb->next))
	 ChildOb = ChildOb->next;

      newpin = GetObject();
      if (newpin == NULL) return;	/* Memory allocation error */

      newpin->next = ParentParams->next;
      ParentParams->next = newpin;
      newpin->instance.name = (ParentParams->instance.name) ?
		strsave(ParentParams->instance.name) : NULL;
      newpin->name = (char *)MALLOC(strlen(newpin->instance.name) +
		strlen(ChildOb->name) + 2);
      sprintf(newpin->name, "%s/%s", newpin->instance.name, ChildOb->name);
      newpin->model.class = strsave(ParentParams->model.class);
      newpin->type = maxpin;
      newpin->node = 0;		/* placeholder */

      // Find the next valid unused node number

      maxnode = -1;
      for (Ob2 = ThisCell->cell; Ob2 != NULL; Ob2 = Ob2->next)
	 if (Ob2->node >= maxnode) maxnode = Ob2->node + 1;

      // Does the global node exist in the parent?  Note that
      // the node may have been declared as a port in the parent,
      // which is fine;  we just don't create a new node in the
      // parent for it.

      for (Ob2 = ThisCell->cell; Ob2 != NULL; Ob2 = Ob2->next) {
	 if (IsGlobal(Ob2) || IsPort(Ob2))
	    if ((*matchfunc)(Ob2->name, ChildOb->name)) {
	       // This node may never have been used in the parent.  If
	       // so, give it a valid node number in the parent.
	       if (Ob2->node == -1) Ob2->node = maxnode;
	       newpin->node = Ob2->node;
	       break;
	    }
      }
      if (Ob2 == NULL) {	// No such node;  create it
	 newnode = GetObject();

	 // Place the node after the pin list of the parent cell.
	 lnode = NULL;
	 for (snode = ThisCell->cell; snode && IsPort(snode);
		snode = snode->next)
	    lnode = snode;
	 if (lnode == NULL) {
	    newnode->next = ThisCell->cell;
	    ThisCell->cell = newnode;
	 }
	 else {
	    newnode->next = lnode->next;
	    lnode->next = newnode;
	 }
	 newnode->type = GLOBAL;
	 newnode->node = maxnode;
         newnode->name = (ChildOb->name) ? strsave(ChildOb->name) : NULL;
         // newnode->instance.name = (ParentParams->instance.name) ?
	 //	strsave(ParentParams->instance.name) : NULL;
         // newnode->model.class = strsave(ParentParams->model.class);
	 newnode->instance.name = NULL;
	 newnode->model.class = NULL;
	 newpin->node = maxnode;
	 HashPtrInstall(newnode->name, newnode, &(ThisCell->objdict));
      }

      // Remove any references to the net as a GLOBAL type in the instance

      /*
      Ob2 = ParentParams;
      for (Ob = ParentParams->next; Ob != NULL && Ob->type != FIRSTPIN;) {
	 if (IsGlobal(Ob)) {
	    Ob2->next = Ob->next;
	    FreeObjectAndHash(Ob, ThisCell);
	    Ob = Ob2->next;
	 }
	 else {
	    Ob2 = Ob;
	    Ob = Ob->next;
	 }
      }
      */

      // Now there should be only one object of this name in the instance,
      // which is the pin, and we will set the hash table to point to it.

      HashPtrInstall(newpin->name, newpin, &(ThisCell->objdict));

   }
   CacheNodeNames(ThisCell);
}

/*
 *-----------------------------------------------------------
 * convertglobals ---
 *
 *  Routine to search database for cells that instantiate
 *  cell "model_to_flatten".  For each cell, call the routine
 *  convertGlobalsOf().  Do not call convertGlobalsOf() on
 *  self, and only call convertGlobalsOf() on cells in the
 *  same file.
 *
 *-----------------------------------------------------------
 */

int convertglobals(struct hashlist *p, int file)
{
   struct nlist *ptr;

   ptr = (struct nlist *)(p->ptr);
   if (file == ptr->file)
      if (!(*matchfunc)(ptr->name, model_to_flatten))
         convertGlobalsOf(ptr->name, file, model_to_flatten);
   return 1;
}

/*
 *-----------------------------------------------------------
 * ConvertGlobals ---
 *
 *   Remove global node references in a subcircuit by changing
 *   them to local nodes and adding a port.  Check all parent
 *   cells, adding the global node if it does not exist, and
 *   connecting it to the port of the instance.
 *
 *-----------------------------------------------------------
 */

void ConvertGlobals(char *name, int filenum)
{
   struct nlist *ThisCell;
   struct objlist *ObjList, *Ob2, *NewObj;
   int globalnet, result;

   if (Debug)
      Printf("Converting globals in circuit: %s\n", name);

   if ((filenum == -1) && (Circuit1 != NULL) && (Circuit2 != NULL)) {
      ConvertGlobals(name, Circuit1->file);
      ConvertGlobals(name, Circuit2->file);
      return;
   }

   ThisCell = LookupCellFile(name, filenum);

   if (ThisCell == NULL) {
      Printf("No circuit %s found.\n", name);
      return;
   }

   /* First check if this object has any ports.  If not, it is a top-	*/
   /* level cell, and we do not need to process global nodes.		*/

   for (ObjList = ThisCell->cell; ObjList != NULL; ObjList = ObjList->next) {
      if (IsPort(ObjList))
	 break;
      else
	 return;
   }

   /* Remove the cached node names, because we are changing them */
   FreeNodeNames(ThisCell);

   for (ObjList = ThisCell->cell; ObjList != NULL; ObjList = ObjList->next) {
      if (IsGlobal(ObjList)) {
	 globalnet = ObjList->node;

	 /* Make sure this node is not in the port list already */
	 for (Ob2 = ThisCell->cell; Ob2 != NULL; Ob2 = Ob2->next) {
	    if (Ob2->type != PORT) break;
	    if (Ob2->node == globalnet) break;
	 }
	 if (Ob2 != NULL && IsPort(Ob2) && Ob2->node == globalnet)
	    continue;

	 /* Add this node to the cell as a port */
	 NewObj = GetObject();
	 if (NewObj == NULL) return;	/* Memory allocation error */

	 /* Find the last port and add the new net to the end */
	 for (Ob2 = ThisCell->cell; Ob2 != NULL; Ob2 = Ob2->next)
	    if (IsPort(Ob2) && (Ob2->next == NULL || !IsPort(Ob2->next)))
	       break;

	 if (Ob2 == NULL) {
	    NewObj->next = ThisCell->cell;
	    ThisCell->cell = NewObj;
	 }
	 else {
	    NewObj->next = Ob2->next;
	    Ob2->next = NewObj;
	 }
	 NewObj->type = PORT;
	 NewObj->node = globalnet;
	 NewObj->model.port = -1;
	 NewObj->instance.name = (ObjList->instance.name) ?
			strsave(ObjList->instance.name) : NULL;
	 NewObj->name = (ObjList->name) ? strsave(ObjList->name) : NULL;

	 HashPtrInstall(NewObj->name, NewObj, &(ThisCell->objdict));

	 /* Find all parent cells of this cell.  Find the global node	*/
	 /* if it exists or create it if it doesn't.  Add the node to	*/
	 /* the beginning of the list of pins for this device.		*/

	 ClearDumpedList(); /* keep track of flattened cells */
	 model_to_flatten = strsave(name);
	 RecurseCellFileHashTable(convertglobals, filenum);
	 FREE(model_to_flatten);
      }
   }

   /* Now remove all global nodes from the cell.		*/
   /* Do not remove the hash entry, because we still have a 	*/
   /* node (a pin) of the same name, and have reassigned the	*/
   /* hash table value to it.					*/

   Ob2 = NULL;
   for (ObjList = ThisCell->cell; ObjList != NULL;) {
      if (IsGlobal(ObjList)) {
	 if (Ob2 == NULL)
	    ThisCell->cell = ObjList->next;
	 else
	    Ob2->next = ObjList->next;

	 FreeObject(ObjList);	/* not FreeObjectAndHash(), see above */

	 if (Ob2 == NULL)
	    ObjList = ThisCell->cell;
	 else
	    ObjList = Ob2->next;
      }
      else {
         Ob2 = ObjList;
         ObjList = ObjList->next;
      }
   }

   /* Regenerate the node name cache */
   CacheNodeNames(ThisCell);
}

/*------------------------------------------------------*/
/* Callback function for UniquePins			*/
/*------------------------------------------------------*/

struct nlist *uniquepins(struct hashlist *p, void *clientdata)
{
   struct nlist *ptr;
   struct objlist *ob, *tob, *ob2, *lob, *nob;
   struct objlist *sob, *firstpin, *saveob;
   struct nlist *tc = (struct nlist *)clientdata;
   int refnode, i;
   int modified = 0;

   ptr = (struct nlist *)(p->ptr);
   if (tc->file == ptr->file) {

      /* Find each instance of cell tc used in cell ptr */

      lob = NULL;
      ob = ptr->cell;
      while (ob != NULL) {
	 while (ob && ob->type != FIRSTPIN) {
	    lob = ob;
	    ob = ob->next;
	 }
         if (ob && ob->model.class != NULL) {
	     firstpin = ob;
	     if (!(*matchfunc)(ob->model.class, tc->name)) {
		lob = ob;
		ob = ob->next;
		continue;
	     }
	 }
	 if (ob == NULL) break;

	 /* 1st pass---resolve node numbers in pin list */

	 tob = tc->cell;
	 ob = firstpin;
         for (ob = firstpin; ob->type >= FIRSTPIN && ob->model.class != NULL;
		ob = ob->next) {
	    if (tob->type == UNKNOWN) {

	       /* If net is different from the net of the first	*/
	       /* such pin, then merge the nets in cell tc.	*/

	       if (tob->model.port == FIRSTPIN) {
		   saveob = firstpin;
		   refnode = firstpin->node;
	       }
	       else {
	          i = FIRSTPIN + 1;
	          for (sob = firstpin->next; sob && sob->type > FIRSTPIN;
				sob = sob->next) {
		      if (tob->model.port == i) {
		         saveob = sob;
		         refnode = sob->node;
		         break;
		      }
		      i++;
		  }
	       }
	       if (ob->node != refnode) {
		  if (refnode == -1) {
		     refnode = ob->node;
		     saveob->node = refnode;
		  }
		  else if (ob->node != -1) {
		     for (sob = ptr->cell; sob != NULL; sob = sob->next)
		        if (sob->node == ob->node)
			   sob->node = refnode;
		  }
	       }
	       modified = 1;

	       // Check if the node we're about to remove is in the
	       // objdict hash table;  if so, replace it with the one
	       // that we are going to keep.

	       if (LookupObject(ob->name, ptr) == ob) {
		  HashPtrInstall(ob->name, saveob, &(ptr->objdict));
	       }
	    }
	    tob = tob->next;
	    if (tob == NULL || (tob->type != PORT && tob->type != UNKNOWN)) break;
	 }

	 /* 2nd pass---remove the pins */

	 tob = tc->cell;
	 ob = firstpin;
         while (ob->type >= FIRSTPIN && ob->model.class != NULL) {
	    if (tob->type == UNKNOWN) {

	       // lob cannot be NULL here by definition;  if there
	       // are duplicate pins, the first is kept, so the first
	       // pin is never removed.

	       lob->next = ob->next;
	       FREE(ob->name);
	       if (ob->instance.name != NULL)
		  FREE(ob->instance.name);
	       FREE(ob);
	       ob = lob->next;
	    }
	    else {
	       lob = ob;
	       ob = ob->next;
	    }
	    tob = tob->next;
	    if (tob == NULL || (tob->type != PORT && tob->type != UNKNOWN)) break;
	 }

	 // Renumber the pins in order.  Since when removing duplicates, the
	 // first entry is always kept, the first pin is never changed, so
 	 // the instdict record is never corrupted.

	 i = FIRSTPIN;
	 firstpin->type = i++;
	 for (sob = firstpin->next; sob && sob->type > FIRSTPIN; sob = sob->next) {
	    sob->type = i++;
	 }
      }
   }
   if (modified) CacheNodeNames(ptr);

   return NULL;
}

/*------------------------------------------------------*/
/* Check a subcircuit for duplicate pins.  If found,	*/
/* remove the duplicate entry or entries, and merge 	*/
/* nets in all parent cells.  Duplicate pins are	*/
/* determined by net number, not by name.		*/
/*							*/
/* This routine must be run after CleanupPins(),	*/
/* otherwise the unique pin may be left with no node	*/
/* number.						*/
/*							*/
/* Return 0 if nothing was modified, 1 otherwise.	*/
/*------------------------------------------------------*/

int UniquePins(char *name, int filenum)
{
   struct nlist *ThisCell;
   struct objlist *ob, *lob, **firstport;
   int maxnode, *nodecount, *firstpin, portcount;
   int haspins = 0;
   int needscleanup = 0;

   if (filenum == -1)
      ThisCell = LookupCell(name);
   else
      ThisCell = LookupCellFile(name, filenum);

   if (ThisCell == NULL) {
      Printf("No cell %s found.\n", name);
      return 0;
   }

   maxnode = 0;
   for (ob = ThisCell->cell; ob != NULL; ob = ob->next) {
      if (ob->type != PORT) break;
      haspins = 1;
      if (ob->node > maxnode) maxnode = ob->node;
   }
   if (haspins == 0) return 0;

   nodecount = (int *)CALLOC(maxnode + 1, sizeof(int));
   firstpin = (int *)CALLOC(maxnode + 1, sizeof(int));
   firstport = (struct objlist **)CALLOC(maxnode + 1, sizeof(struct objlist *));

   portcount = FIRSTPIN;
   for (ob = ThisCell->cell; ob != NULL; ob = ob->next) {
      if (ob->type != PORT) break;
      if (ob->node > 0) {
	 nodecount[ob->node]++;
	 if (nodecount[ob->node] == 2) {
	    Printf("Duplicate pin %s in cell %s\n", ob->name, ThisCell->name);
	 }
	 if (nodecount[ob->node] > 1) {
	    /* Remove this node;  prep for removal by marking with UNKNOWN */
	    ob->type = UNKNOWN;
	    /* Replace port number with first port number used */
	    ob->model.port = firstpin[ob->node];
	    needscleanup = 1;
	 }
	 else {
	    firstpin[ob->node] = portcount;
	    firstport[ob->node] = ob;
	 }
      }
      portcount++;
   }

   if (needscleanup)
      RecurseCellHashTable2(uniquepins, (void *)ThisCell);

   /* Remove all entries marked UNKNOWN */

   lob = NULL;
   for (ob = ThisCell->cell; ob != NULL; ) {
      if (ob->type == UNKNOWN) {
	 struct objlist *testob;

	 testob = LookupObject(ob->name, ThisCell);
	 if (testob == ob) {
	     // The hash table is pointing at the cell we are
	     // about to delete.  Hash the one we're keeping instead.

	     HashPtrInstall(ob->name, firstport[ob->node], &(ThisCell->objdict));
	 }

	 if (lob == NULL) {
	    ThisCell->cell = ob->next;
	    if (ob->instance.name != NULL)
	       FREE(ob->instance.name);
	    FREE(ob);
	    ob = ThisCell->cell;
	 }
	 else {
	    lob->next = ob->next;
	    if (ob->instance.name != NULL)
	       FREE(ob->instance.name);
	    FREE(ob);
	    ob = lob->next;
	 }
      }
      else {
	 if (ob->type != PORT) break;
         lob = ob;
	 ob = ob->next;
      }
   }

   if (needscleanup) CacheNodeNames(ThisCell);

   FREE(nodecount);
   FREE(firstpin);
   FREE(firstport);
   return 1;
}

/*------------------------------------------------------*/
/* Callback function for CleanupPins			*/
/* Note that if the first pin of the instance is a	*/
/* disconnected node, then removing it invalidates the	*/
/* instdict hash.					*/
/*------------------------------------------------------*/

struct nlist *cleanuppins(struct hashlist *p, void *clientdata)
{
   struct nlist *ptr;
   struct objlist *ob, *obt, *lob, *nob, *firstpin;
   struct nlist *tc = (struct nlist *)clientdata;
   int pinnum;

   ptr = (struct nlist *)(p->ptr);
   if (tc->file != ptr->file) return NULL;

   /* Find each instance of cell tc used in cell ptr */

   lob = NULL;
   for (ob = ptr->cell; ob != NULL; ) {
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
	  if (ob == NULL) break;

	  firstpin = ob;
	  pinnum = FIRSTPIN;
	  obt = tc->cell;

          while (ob && obt && (ob->type > FIRSTPIN || ob == firstpin) &&
			ob->model.class != NULL) {
	     nob = ob->next;
	     if ((obt->type == PORT) && (obt->node == -1)) {

	        /* Remove this pin */

		if (ob == firstpin) firstpin = nob;

		if (lob == NULL) {
		   ptr->cell = ob->next;
		}
		else {
		   lob->next = ob->next;
		}

	        // Check if the node we're about to remove is in the
	        // objdict hash table
	        if (LookupObject(ob->name, ptr) == ob) {
		   HashDelete(ob->name, &(ptr->objdict));
	        }

		FREE(ob->name);
		if (ob->instance.name != NULL) FREE(ob->instance.name);
		if (ob->model.class != NULL) FREE(ob->model.class);
		FREE(ob);
	     }
	     else {
		lob = ob;
		ob->type = pinnum++;	// Renumber pins in order
	     }
	     ob = nob;
	     obt = obt->next;
	  }

	  /* Rehash the instdict, in case the first pin got removed */
	  if (firstpin && (firstpin->type == FIRSTPIN))
	     HashPtrInstall(firstpin->instance.name, firstpin, &(ptr->instdict));
      }
   }
   return NULL;		/* Keep the search going */
}

/*------------------------------------------------------*/
/* Check a circuit for pins that are not connected to	*/
/* any real node, and remove them from the circuit and	*/
/* all instances of that circuit.			*/
/*							*/
/* Return 0 if nothing was modified, 1 otherwise.	*/
/*------------------------------------------------------*/

int CleanupPins(char *name, int filenum)
{
   struct nlist *ThisCell;
   struct objlist *ob, *lob, *nob;
   int needscleanup = 0;

   if (filenum == -1)
      ThisCell = LookupCell(name);
   else
      ThisCell = LookupCellFile(name, filenum);

   if (ThisCell == NULL) {
      Printf("No cell %s found.\n", name);
      return 0;
   }

   // Avoid a loop through all cells unless we have to do it.

   for (ob = ThisCell->cell; ob != NULL; ob = ob->next) {
      if (ob->type != PORT) break;
      if (ob->node == -1) {
	 needscleanup = 1;
	 break;
      }
   }

   if (needscleanup == 0) return 0;

   // If there is only one port in the cell, don't remove it
   if (ob && ob == ThisCell->cell && ob->next && ob->next->type != PORT)
      return 0;

   // Remove the disconnected nodes from all instances of the cell

   RecurseCellHashTable2(cleanuppins, (void *)ThisCell);

   // Remove the disconnected nodes from cell

   lob = NULL;
   for (ob = ThisCell->cell; ob != NULL; ) {
      if (ob->type == UNKNOWN) {
	 lob = ob;
         ob = ob->next;
	 continue;
      }
      else if (ob->type != PORT) break;
      nob = ob->next;
      if (ob->node == -1) {
	 if (lob == NULL) {
	    ThisCell->cell = ob->next;
	 }
	 else {
	    lob->next = ob->next;
	 }

	 // Check if the node we're about to remove is in the
	 // objdict hash table
	 if (LookupObject(ob->name, ThisCell) == ob) {
	     HashDelete(ob->name, &(ThisCell->objdict));
	 }

	 FREE(ob->name);
	 if (ob->instance.name != NULL)
	    FREE(ob->instance.name);
	 FREE(ob);
      }
      else
	 lob = ob;
      ob = nob;
   }
   return 1;
}

/*------------------------------------------------------*/

typedef struct ecompare {
    struct nlist *cell1;
    struct nlist *cell2;
    int num1, num2;
    int add1, add2;
    char refcount;
} ECompare;

typedef struct ecomplist *ECompListPtr;

typedef struct ecomplist {
    ECompare *ecomp;
    ECompListPtr next;
} ECompList;

/*------------------------------------------------------*/
/* Split a device into multiple devices based on a	*/
/* critical property; e.g., MOSFET width.  Devices with	*/
/* property name equal to kl->key and value "value"	*/
/* will be split into "ndev" devices with the property	*/
/* of each divided down by "ndev".			*/
/*------------------------------------------------------*/

void
SplitDevice(struct nlist *tc, struct nlist *cell, struct property *kl,
	double value, int ndev, int file1, int file2, int which)
{
    struct objlist *ob, *ob2, *newdevs, *ob3, *lastob, *nob;
    struct nlist *tsub;
    unsigned char found;
    int file = (which == 0) ? file1 : file2;
   
    newdevs = NULL;  	/* Linked list of the copied devices */

    for (ob = tc->cell; ob; ob = ob->next) {
	if (ob->type == FIRSTPIN) {
	    tsub = LookupCellFile(ob->model.class, file);
	    if (tsub == cell) {

		// Advance ob to property list
		found = 0;
		for (ob2 = ob->next; ob2 && ob2->type != FIRSTPIN; ob2 = ob2->next) {
		    if (ob2->type == PROPERTY) {
			struct valuelist *kv;
			int i;
			double dval = 0.0;
			for (i = 0; ; i++) {
			    kv = &(ob2->instance.props[i]);
			    if (kv->type == PROP_ENDLIST) break;
			    if ((*matchfunc)(kv->key, kl->key)) {
				switch(kv->type) {
				    case PROP_INTEGER:
					dval = (double)kv->value.ival;
					break;
				    case PROP_DOUBLE:
				    case PROP_VALUE:
					dval = kv->value.dval;
					break;
				}
				break;
			    }
			}

			/* To-do:  Account for slop */

			if (dval == value) {
			    switch(kv->type) {
				case PROP_INTEGER:
				    kv->value.ival /= ndev;
				    found = 1;
				    break;
				case PROP_DOUBLE:
				case PROP_VALUE:
				    kv->value.dval /= ndev;
				    found = 1;
				    break;
			    }
			}
		    }
		    if (found) break;
		}
		if (found) {
		    int i;
		    for (i = 1; i < ndev; i++) {
			ob3 = CopyObjList(ob, 0);	// Make exact copy
			for (nob = ob3; nob->next != NULL; nob = nob->next);
			nob->next = newdevs;
			newdevs = ob3;
		    }
		}
	    }
	}
	lastob = ob;
    }
    lastob->next = newdevs;	// Append new devices to list
}

/*------------------------------------------------------*/
/* Survey a specific device in a cell and sort into a	*/
/* hash by critical property.				*/
/*------------------------------------------------------*/

void
SurveyDevice(struct nlist *tc, struct hashdict *devdict,
		struct nlist *cell, struct property *kl,
		int file1, int file2, int which)
{
    struct objlist *ob, *ob2;
    struct nlist *tsub, *teq;
    ECompare *dcomp, *ncomp, *qcomp;
    int file = (which == 0) ? file1 : file2;
    int ofile = (which == 0) ? file2 : file1;
    char *p1str, *p2str, *d1str, *d2str;

    for (ob = tc->cell; ob; ob = ob->next) {
	if (ob->type == FIRSTPIN) {

	    tsub = LookupCellFile(ob->model.class, file);
	    if (tsub == cell) {

		p1str = (char *)MALLOC(strlen(ob->model.class) + 20);
		sprintf(p1str, "%s", ob->model.class);

		if (tsub->flags & CELL_DUPLICATE) {
		    // Always register a duplicate under the original name
		    d1str = strstr(p1str, "[[");
		    if (d1str) *d1str = '\0';
		}
		d1str = p1str + strlen(p1str);
		sprintf(d1str, "::");
		d1str += 2;

		teq = LookupClassEquivalent(ob->model.class, file, ofile);
		p2str = (char *)MALLOC(strlen(teq->name) + 20);
	        sprintf(p2str, "%s", teq->name);

		if (teq->flags & CELL_DUPLICATE) {
		    // Always register a duplicate under the original name
		    d2str = strstr(p2str, "[[");
		    if (d2str) *d2str = '\0';
		}
		d2str = p2str + strlen(p2str);
		sprintf(d2str, "::");
		d2str += 2;

		// Advance ob to property list
		// To-do:  Quantize values according to slop

		for (ob2 = ob->next; ob2 && ob2->type != FIRSTPIN; ob2 = ob2->next) {
		    if (ob2->type == PROPERTY) {
			struct valuelist *kv;
			int i;
			for (i = 0; ; i++) {
			    kv = &(ob2->instance.props[i]);
			    if (kv->type == PROP_ENDLIST) break;
			    if ((*matchfunc)(kv->key, kl->key)) {
				switch(kv->type) {
				    case PROP_INTEGER:
					sprintf(d1str, "%d", kv->value.ival);
					sprintf(d2str, "%d", kv->value.ival);
					break;
				    case PROP_DOUBLE:
				    case PROP_VALUE:
					// To-do:  Round to tolerance
					sprintf(d1str, "%g", kv->value.dval);
					sprintf(d2str, "%g", kv->value.dval);
					break;
				}
			        break;
			    }
			}
			break;
		    }
		}
		if (*d1str == '\0') {
		    // No critical property instanced, so use default
		    switch(kl->type) {
			case PROP_INTEGER:
			    sprintf(d1str, "%d", kl->pdefault.ival);
			    sprintf(d2str, "%d", kl->pdefault.ival);
			    break;
			case PROP_DOUBLE:
			case PROP_VALUE:
			    // To-do:  Round to tolerance
			    sprintf(d1str, "%g", kl->pdefault.dval);
			    sprintf(d2str, "%g", kl->pdefault.dval);
			    break;
		    }
		}

		// Create hash key from object class and the critical
		// property affecting the device count.

		dcomp = (ECompare *)HashInt2Lookup(p1str, file, devdict);

		// Fill in the values for this device::property combination

		if (dcomp == NULL) {
		    ncomp = (ECompare *)MALLOC(sizeof(ECompare));
		    if (which == 0) {
			ncomp->num1 = 1;
			ncomp->num2 = 0;
			ncomp->cell1 = tsub;
			ncomp->cell2 = teq;
		    }
		    else {
			ncomp->num1 = 0;
			ncomp->num2 = 1;
			ncomp->cell2 = tsub;
			ncomp->cell1 = teq;
		    }
		    ncomp->add1 = 0;
		    ncomp->add2 = 0;
		    ncomp->refcount = (char)1;

		    HashInt2PtrInstall(p1str, file, ncomp, devdict);
		    if (teq != NULL) {
			qcomp = (ECompare *)HashInt2Lookup(p2str, ofile,
					devdict);
			if (qcomp == NULL) {
			    HashInt2PtrInstall(p2str, ofile, ncomp, devdict);
			    ncomp->refcount++;
			}
		    }
		}
		else {
		    if (which == 0) 
			dcomp->num1++;
		    else
			dcomp->num2++;
		}

		FREE(p1str);
		FREE(p2str);
	    }
	}
    }
}

/*------------------------------------------------------*/
/* Survey the contents of a cell and sort into a hash	*/
/*------------------------------------------------------*/

void
SurveyCell(struct nlist *tc, struct hashdict *compdict, int file1, int file2, int which)
{
    struct objlist *ob;
    struct nlist *tsub, *teq;
    ECompare *ecomp, *qcomp, *ncomp;
    int file = (which == 0) ? file1 : file2;
    int ofile = (which == 0) ? file2 : file1;
    char *dstr;

    for (ob = tc->cell; ob; ob = ob->next) {
	if (ob->type == FIRSTPIN) {
	    tsub = LookupCellFile(ob->model.class, file);
	    if (tsub->flags & CELL_DUPLICATE) {
	       // Always register a duplicate under the original name
	       dstr = strstr(ob->model.class, "[[");
	       if (dstr) *dstr = '\0';
	    }
	    else dstr = NULL;

	    teq = LookupClassEquivalent(ob->model.class, file, ofile);
	    ecomp = (ECompare *)HashInt2Lookup(ob->model.class, file, compdict);

	    if (ecomp == NULL) {
		ncomp = (ECompare *)MALLOC(sizeof(ECompare));
		if (which == 0) {
		   ncomp->num1 = 1;
		   ncomp->num2 = 0;
		   ncomp->cell1 = tsub;
		   ncomp->cell2 = teq;
		}
		else {
		   ncomp->num1 = 0;
		   ncomp->num2 = 1;
		   ncomp->cell2 = tsub;
		   ncomp->cell1 = teq;
		}
		ncomp->add1 = 0;
		ncomp->add2 = 0;
		ncomp->refcount = (char)1;

		HashInt2PtrInstall(ob->model.class, file, ncomp, compdict);
		if (teq != NULL) {
		    char *bstr = NULL;
		    if (teq->flags & CELL_DUPLICATE) {
			bstr = strstr(teq->name, "[[");
			if (bstr) *bstr = '\0';
		    }
		    qcomp = (ECompare *)HashInt2Lookup(teq->name, ofile, compdict);
		    if (qcomp == NULL) {
			HashInt2PtrInstall(teq->name, ofile, ncomp, compdict);
			ncomp->refcount++;
		    }
		    if (bstr) *bstr = '[';
		}
	    }
	    else {
		if (which == 0) 
		   ecomp->num1++;
		else
		   ecomp->num2++;
	    }
	    if (dstr) *dstr = '[';
	}
    }
}

/*------------------------------------------------------*/
/* Create a preliminary matchup of two cells.		*/
/* For each type of cell in the instance lists, if	*/
/* there is a mismatch between the total count of 	*/
/* instances, where the instances are subcells, then	*/
/* determine whether decomposing the cells into their	*/
/* constituent parts one or more times would make the	*/
/* lists match better.  If so, flatten those cells in	*/
/* both parents.					*/
/*							*/
/* If there is a mismatch between instances of low-	*/
/* level devices, determine if the mismatches can be	*/
/* resolved by parallel/serial combining, according to	*/
/* combination rules.					*/
/* 							*/
/* Return the number of modifications made.		*/
/*------------------------------------------------------*/

int
PrematchLists(char *name1, int file1, char *name2, int file2)
{
    struct nlist *tc1, *tc2, *tsub1, *tsub2;
    struct objlist *ob1, *ob2, *lob;
    struct hashdict compdict;
    ECompare *ecomp, *ncomp;
    ECompList *list0X, *listX0;
    int match, modified = 0;

    if (file1 == -1)
	tc1 = LookupCell(name1);
    else
	tc1 = LookupCellFile(name1, file1);

    if (file2 == -1)
	tc2 = LookupCell(name2);
    else
	tc2 = LookupCellFile(name2, file2);

    if (tc1 == NULL || tc2 == NULL) return;

    InitializeHashTable(&compdict, OBJHASHSIZE);

    // Gather information about instances of cell "name1"
    SurveyCell(tc1, &compdict, file1, file2, 0);

    // Gather information about instances of cell "name2"
    SurveyCell(tc2, &compdict, file1, file2, 1);

    // Find all instances of one cell that have fewer in
    // the compared circuit.  Check whether subcircuits
    // in the hierarchy of each instance contain devices
    // or subcircuits that have more in the compared circuit.

    listX0 = list0X = NULL;
    ecomp = (ECompare *)HashFirst(&compdict);
    while (ecomp != NULL) {

	/* Case 1:  Both cell1 and cell2 classes are subcircuits, */
	/* and flattening both of them improves the matching.	  */

	if ((ecomp->num1 != ecomp->num2) && (ecomp->cell2 != NULL) &&
			(ecomp->cell1 != NULL) &&
			(ecomp->cell2->class == CLASS_SUBCKT) &&
			(ecomp->cell1->class == CLASS_SUBCKT)) {
	    ecomp->add2 = -ecomp->num2;
	    ecomp->add1 = -ecomp->num1;
	    match = 1;
	    for (ob2 = ecomp->cell2->cell; ob2; ob2 = ob2->next) {
		if (ob2->type == FIRSTPIN) {
	   	    ncomp = (ECompare *)HashInt2Lookup(ob2->model.class,
				ecomp->cell2->file, &compdict);
		    if (ncomp != NULL) {
			if ((ncomp->num1 > ncomp->num2) &&
				((ncomp->add2 + ecomp->num2) >=
				(ncomp->add1 + ecomp->num1))) {
			    ncomp->add2 += ecomp->num2;
			    ncomp->add1 += ecomp->num1;
			}
			else if ((ncomp->num1 < ncomp->num2) &&
				((ncomp->add2 + ecomp->num2) <=
				(ncomp->add1 + ecomp->num1))) {
			    ncomp->add2 += ecomp->num2;
			    ncomp->add1 += ecomp->num1;
			}
			else {
			   match = 0;
			   break;
			}
		    }
		    else {
		       match = 0;
		       break;
		    }
		}
	    }
	    if (match) {
		if (ecomp->cell1) {
		    Fprintf(stdout, "Flattening instances of %s in cell %s"
				" makes a better match\n", ecomp->cell1->name,
				name1);
		    flattenInstancesOf(name1, file1, ecomp->cell1->name); 
		}
		if (ecomp->cell2) {
		    Fprintf(stdout, "Flattening instances of %s in cell %s"
				" makes a better match\n", ecomp->cell2->name,
				name2);
		    flattenInstancesOf(name2, file2, ecomp->cell2->name); 
		}
		modified++;
	    }

	    /* Reset or apply the count adjustments */
	    if (ecomp->cell2)
	    for (ob2 = ecomp->cell2->cell; ob2; ob2 = ob2->next) {
		if (ob2->type == FIRSTPIN) {
		    ncomp = (ECompare *)HashInt2Lookup(ob2->model.class,
					ecomp->cell2->file, &compdict);
		    if (ncomp != NULL) {
			if (match) {
			    ncomp->num1 += ncomp->add1;
			    ncomp->num2 += ncomp->add2;
			}
			ncomp->add1 = 0;
			ncomp->add2 = 0;
		    }
		}
	    }
	    if (match) {
		ecomp->num1 = 0;
		ecomp->num2 = 0;
	    }
	    ecomp->add1 = 0;
	    ecomp->add2 = 0;

	    /* If the pair was unresolved, and the number of	*/
	    /* instances is either N:0 or 0:M, add to a list so	*/
	    /* they can be cross-checked at the end.		*/

	    if ((ecomp->num1 != 0) && (ecomp->num2 == 0)) {
		ECompList *newcomplist;

		newcomplist = (ECompList *)MALLOC(sizeof(ECompList));
		newcomplist->ecomp = ecomp;
		newcomplist->next = listX0;
		listX0 = newcomplist;
	    }
	    else if ((ecomp->num1 == 0) && (ecomp->num2 != 0)) {
		ECompList *newcomplist;

		newcomplist = (ECompList *)MALLOC(sizeof(ECompList));
		newcomplist->ecomp = ecomp;
		newcomplist->next = list0X;
		list0X = newcomplist;
	    }
	}

	/* Case 2:  Cell2 class is a subcircuit, and flattening	*/
	/* (it without regard to cell1) improves the matching.	*/

	else if ((ecomp->num1 != ecomp->num2) && (ecomp->cell2 != NULL) &&
			(ecomp->cell2->class == CLASS_SUBCKT)) {
	    ecomp->add2 = -ecomp->num2;
	    match = 1;
	    for (ob2 = ecomp->cell2->cell; ob2; ob2 = ob2->next) {
		if (ob2->type == FIRSTPIN) {
	   	    ncomp = (ECompare *)HashInt2Lookup(ob2->model.class,
				ecomp->cell2->file, &compdict);
		    if (ncomp != NULL) {
			if ((ncomp->num1 > ncomp->num2) &&
				((ncomp->add2 + ecomp->num2) <=
				ncomp->num1)) {
			    ncomp->add2 += ecomp->num2;
			}
			else {
			   match = 0;
			   break;
			}
		    }
		    else {
		       match = 0;
		       break;
		    }
		}
	    }
	    if (match) {
		if (ecomp->cell2) {
		    Fprintf(stdout, "Flattening instances of %s in cell %s"
				" makes a better match\n", ecomp->cell2->name,
				name2);
		    flattenInstancesOf(name2, file2, ecomp->cell2->name); 
		}
		modified++;
	    }

	    /* Reset or apply the count adjustments */
	    if (ecomp->cell2)
	    for (ob2 = ecomp->cell2->cell; ob2; ob2 = ob2->next) {
		if (ob2->type == FIRSTPIN) {
		    ncomp = (ECompare *)HashInt2Lookup(ob2->model.class,
					ecomp->cell2->file, &compdict);
		    if (ncomp != NULL) {
			if (match) {
			    ncomp->num2 += ncomp->add2;
			}
			ncomp->add2 = 0;
		    }
		}
	    }
	    if (match) {
		ecomp->num2 = 0;
	    }
	    ecomp->add2 = 0;
	}
	
	/* Case 3:  Cell1 class is a subcircuit, and flattening	*/
	/* (it without regard to cell1) improves the matching.	*/

	else if ((ecomp->num1 != ecomp->num2) && (ecomp->cell1 != NULL) &&
			(ecomp->cell1->class == CLASS_SUBCKT)) {
	    ecomp->add1 = -ecomp->num1;
	    match = 1;
	    for (ob2 = ecomp->cell1->cell; ob2; ob2 = ob2->next) {
		if (ob2->type == FIRSTPIN) {
	   	    ncomp = (ECompare *)HashInt2Lookup(ob2->model.class,
				ecomp->cell1->file, &compdict);
		    if (ncomp != NULL) {
			if ((ncomp->num2 > ncomp->num1) &&
				((ncomp->add1 + ecomp->num1) <=
				ncomp->num2)) {
			    ncomp->add1 += ecomp->num1;
			}
			else {
			   match = 0;
			   break;
			}
		    }
		    else {
		       match = 0;
		       break;
		    }
		}
	    }
	    if (match) {
		if (ecomp->cell1) {
		    Fprintf(stdout, "Flattening instances of %s in cell %s"
				" makes a better match\n", ecomp->cell1->name,
				name1);
		    flattenInstancesOf(name1, file1, ecomp->cell1->name); 
		}
		modified++;
	    }

	    /* Reset or apply the count adjustments */
	    if (ecomp->cell1)
	    for (ob2 = ecomp->cell1->cell; ob2; ob2 = ob2->next) {
		if (ob2->type == FIRSTPIN) {
		    ncomp = (ECompare *)HashInt2Lookup(ob2->model.class,
					ecomp->cell1->file, &compdict);
		    if (ncomp != NULL) {
			if (match) {
			    ncomp->num1 += ncomp->add1;
			}
			ncomp->add1 = 0;
		    }
		}
	    }
	    if (match) {
		ecomp->num1 = 0;
	    }
	    ecomp->add1 = 0;
	}
	ecomp = (ECompare *)HashNext(&compdict);
    }

    // Check for cells, either low-level devices or subcircuits,
    // that have properties allowing devices to be merged.  If
    // the classes of both cells are the same, and the number of
    // instances is different, and merging the device with more
    // instances improves the matching, then perform the merge.

    ecomp = (ECompare *)HashFirst(&compdict);
    while (ecomp != NULL) {

	if ((ecomp->num1 != ecomp->num2) &&
			(ecomp->cell1 != NULL) &&
			(ecomp->cell2 != NULL) &&
			(ecomp->cell1->classhash == ecomp->cell2->classhash)) {

	    struct hashdict devdict;
	    ECompare *dcomp;

	    // Determine if either device has mergeable properties.  If so,
	    // sort the device into bins by critical (mergeable) property,
	    // and merge devices where merging makes a better match.

	    struct property *kl1, *kl2;
	    double slop = 0.0;

	    // Look for a mergeable property in cell1

	    kl1 = (struct property *)HashFirst(&(ecomp->cell1->propdict));
	    while (kl1 != NULL) {
		if (kl1->merge == MERGE_ADD_CRIT || kl1->merge == MERGE_PAR_CRIT)
		    break;
		kl1 = (struct property *)HashNext(&(ecomp->cell1->propdict));
	    }

	    // Look for the equivalent property in cell2 (mergeable or not).
	    // If cell1 had no mergeable properties, then look for one is cell2.

	    kl2 = (struct property *)HashFirst(&(ecomp->cell2->propdict));
	    while (kl2 != NULL) {
		if (kl1 != NULL) {
		   if ((*matchfunc)(kl1->key, kl2->key))
		      break;
		}
		else if (kl2->merge == MERGE_ADD_CRIT || kl2->merge == MERGE_PAR_CRIT)
		    break;
		kl2 = (struct property *)HashNext(&(ecomp->cell2->propdict));
	    }
	    if (kl2 != NULL) {
		// Get slop value
		switch (kl2->type) {
		    case PROP_INTEGER:
			slop = (double)kl2->slop.ival;
			break;
		    case PROP_DOUBLE:
		    case PROP_VALUE:
			slop = kl2->slop.dval;
			break;
		}
	    }

	    // If cell2 had a mergeable property but cell1 didn't, then look
	    // through cell1 again to find the equivalent property.

	    if ((kl1 == NULL) && (kl2 != NULL)) {
		kl1 = (struct property *)HashFirst(&(ecomp->cell1->propdict));
		while (kl1 != NULL) {
		    if ((*matchfunc)(kl1->key, kl2->key))
			break;
		    kl1 = (struct property *)HashNext(&(ecomp->cell1->propdict));
		}
	    }
	    if (kl1 != NULL) {
		// Get slop value
		switch (kl1->type) {
		    case PROP_INTEGER:
			slop = MAX(slop, (double)kl1->slop.ival);
			break;
		    case PROP_DOUBLE:
		    case PROP_VALUE:
			slop = MAX(slop, kl1->slop.dval);
			break;
		}
	    }

	    if ((kl1 != NULL) && (kl2 != NULL)) {

		double dval, dval1, dval2, mindev, pd, df, dr;
		unsigned char dosplit;
		char *valptr;
		int ndev;

		// Create the device hash table

		InitializeHashTable(&devdict, OBJHASHSIZE);

		// Populate the device hash table

		SurveyDevice(tc1, &devdict, ecomp->cell1, kl1, file1, file2, 0);
		SurveyDevice(tc2, &devdict, ecomp->cell2, kl2, file1, file2, 1);

		// Scan the device hash table.  If devices can be merged
		// and this improves the matching between cells, then do
		// the merge.

		mindev = 1E20;
		dval1 = dval2 = 0.0;

		dcomp = (ECompare *)HashFirst(&devdict);
		while (dcomp != NULL) {
		    if ((dcomp->num1 == 0) || (dcomp->num2 == 0)) {
			valptr = strstr(devdict.hashfirstptr->name, "::");
			if (sscanf(valptr + 2, "%lg", &dval) == 1) {
			    if (dval < mindev) mindev = dval;
			    if (dcomp->num1 == 0)
				dval2 += (dval * dcomp->num2) / dcomp->refcount;
			    else
				dval1 += (dval * dcomp->num1) / dcomp->refcount;
			}
		    }
		    dcomp = (ECompare *)HashNext(&devdict);
		}

		// If dval2 and dval1 agree within slop, and both are
		// divisible by mindev, then break up all devices into
		// sizes of mindev.

		dosplit = 0;
		pd = 2 * fabs(dval1 - dval2) / (dval1 + dval2);
		if (pd < slop) {
		    df = dval1 / mindev;
		    dr = round(df);
		    pd = 2 * fabs(df - dr) / (df + dr);
		    if (pd < slop) dosplit = 1;
		}

		if (dosplit) {
		    dcomp = (ECompare *)HashFirst(&devdict);
		    while (dcomp != NULL) {
			if (dcomp->num1 == 0 || dcomp->num2 == 0) {
			    valptr = strstr(devdict.hashfirstptr->name, "::");
			    sscanf(valptr + 2, "%lg", &dval);
			    ndev = (int)round(dval / mindev);
			}
			if (dcomp->num1 == 0) {
			    SplitDevice(tc2, ecomp->cell2, kl2, dval, ndev,
					file1, file2, 1);
			    modified++;
			}
			else if (dcomp->num2 == 0) {
			    SplitDevice(tc1, ecomp->cell1, kl1, dval, ndev,
					file1, file2, 0);
			    modified++;
			}
			dcomp = (ECompare *)HashNext(&devdict);
		    }
		}

		// Free the device hash table

		dcomp = (ECompare *)HashFirst(&devdict);
		while (dcomp != NULL) {
		    if (--dcomp->refcount == (char)0) FREE(dcomp);
		    dcomp = (ECompare *)HashNext(&devdict);
		}
		HashKill(&devdict);
	    }
	}

	ecomp = (ECompare *)HashNext(&compdict);
    } 

    // Remove non-matching zero-value devices.  This can
    // be done on a per-instance basis.

    ecomp = (ECompare *)HashFirst(&compdict);
    while (ecomp != NULL) {
	if ((ecomp->num1 != ecomp->num2) && (ecomp->cell1 != NULL) &&
			(ecomp->cell1->class == CLASS_RES)) {
	    int node1 = -1, node2 = -1;
	    lob = NULL;
	    for (ob1 = tc1->cell; ob1; ) {
		if (ob1->type == FIRSTPIN) {
		    tsub1 = LookupCellFile(ob1->model.class, file1);
		    if (tsub1 == ecomp->cell1) {
			node1 = ob1->node;
			for (ob1 = ob1->next;  ob1 && ob1->type != FIRSTPIN; ) {
			    if (ob1->type == FIRSTPIN + 1)
				node2 = ob1->node;
			    if (ob1->type == PROPERTY && node1 != -1 && node2 != -1) {
				if (ob1->instance.props != NULL) {
				    struct valuelist *kv;
				    int i;
				    int found = 0;
				    for (i = 0; ; i ++) {
					kv = &(ob1->instance.props[i]);
					if (kv->type == PROP_ENDLIST) break;
					if ((*matchfunc)(kv->key, "value") ||
						(*matchfunc)(kv->key, "length")) {
					    switch(kv->type) {
						case PROP_STRING:
						    if ((*matchfunc)(kv->value.string,
								"0"))
							found = 1;
						    break;
						case PROP_INTEGER:
						    if (kv->value.ival == 0)
							found = 1;
						    break;
						case PROP_DOUBLE:
						case PROP_VALUE:
						    if (kv->value.dval == 0.0)
							found = 1;
						    break;
						case PROP_EXPRESSION:
						    /* Unresolved expression */
						    break;
					    }
					}
					if (found) break;
				    }
				    if (found) {
					Fprintf(stdout, "Removing zero-valued device"
						"%s from cell %s makes a better match\n",
						tsub1->name,
						tc1->name);

					/* merge node of endpoints */
					for (ob2 = tc1->cell; ob2; ob2 = ob2->next) {
					    if (ob2->node == node2)
						ob2->node = node1;
					}

					/* snip, snip.  Excise this device */
					if (lob == NULL) {
					    ob2 = tc1->cell;
					    tc1->cell = ob1->next;
					}
					else {
					    ob2 = lob->next;
					    lob->next = ob1->next;
					}

					/* free the device */
					for (; ob2 && ob2 != ob1->next; ) {
					    struct objlist *nob;
					    nob = ob2->next;
					    FreeObjectAndHash(ob2, tc1);
					    ob2 = nob;
					}

					/* Remove from list */
					ecomp->num1--;
					modified++;

					ob1 = lob;
				    }
				    else
					ob1 = ob1->next;
				}
				else
				    ob1 = ob1->next;
			    }
			    else
				ob1 = ob1->next;
			}
		    }
		    else {
			lob = ob1;
			ob1 = ob1->next;
		    }
		}
		else {
		    lob = ob1;
		    ob1 = ob1->next;
		}
	    }
	}

	// Repeat the last section for the other circuit

	if ((ecomp->num1 != ecomp->num2) && (ecomp->cell2 != NULL) &&
			(ecomp->cell2->class == CLASS_RES)) {
	    int node1 = -1, node2 = -1;
	    lob = NULL;
	    for (ob2 = tc2->cell; ob2; ) {
		if (ob2->type == FIRSTPIN) {
		    tsub2 = LookupCellFile(ob2->model.class, file2);
		    if (tsub2 == ecomp->cell2) {
			node1 = ob2->node;
			for (ob2 = ob2->next;  ob2 && ob2->type != FIRSTPIN; ) {
			    if (ob2->type == FIRSTPIN + 1)
				node2 = ob2->node;
			    if (ob2->type == PROPERTY && node1 != -1 && node2 != -1) {
				if (ob2->instance.props != NULL) {
				    struct valuelist *kv;
				    int i;
				    int found = 0;
				    for (i = 0; ; i ++) {
					kv = &(ob2->instance.props[i]);
					if (kv->type == PROP_ENDLIST) break;
					if ((*matchfunc)(kv->key, "value") ||
						(*matchfunc)(kv->key, "length")) {
					    switch(kv->type) {
						case PROP_STRING:
						    if ((*matchfunc)(kv->value.string,
								"0"))
							found = 1;
						    break;
						case PROP_INTEGER:
						    if (kv->value.ival == 0)
							found = 1;
						    break;
						case PROP_DOUBLE:
						case PROP_VALUE:
						    if (kv->value.dval == 0.0)
							found = 1;
						    break;
						case PROP_EXPRESSION:
						    /* Unresolved expression. */
						    break;
					    }
					}
					if (found) break;
				    }
				    if (found) {
					Fprintf(stdout, "Removing zero-valued device"
						"%s from cell %s makes a better match\n",
						tsub2->name,
						tc2->name);

					/* merge node of endpoints */
					for (ob1 = tc2->cell; ob1; ob1 = ob1->next) {
					    if (ob1->node == node2)
						ob1->node = node1;
					}

					/* snip, snip.  Excise this device */
					if (lob == NULL) {
					    ob1 = tc2->cell;
					    tc2->cell = ob2->next;
					}
					else {
					    ob1 = lob->next;
					    lob->next = ob2->next;
					}

					/* free the device */
					for (; ob1 && ob1 != ob2->next; ) {
					    struct objlist *nob;
					    nob = ob1->next;
					    FreeObjectAndHash(ob1, tc2);
					    ob1 = nob;
					}

					/* Remove from list */
					ecomp->num1--;
					modified++;

					ob2 = lob;
				    }
				    else
					ob2 = ob2->next;
				}
				else
				    ob2 = ob2->next;
			    }
			    else
				ob2 = ob2->next;
			}
		    }
		    else {
			lob = ob2;
			ob2 = ob2->next;
		    }
		}
		else {
		    lob = ob2;
		    ob2 = ob2->next;
		}
	    }
	}
	ecomp = (ECompare *)HashNext(&compdict);
    }

    // Finally, check all entries in listX0 vs. all entries in list0X to see
    // if flattening one side will improve the matching.  Ignore entries
    // that are duplicates (already matched).  Also do this only if there
    // are no other modifications, as this rule is relaxed compared to other
    // rules, and the other rules should be exhaustively applied first.

    if ((listX0 != NULL) && (list0X != NULL) && (modified == 0)) {
	ECompare *ecomp0X, *ecompX0;
	ECompList *elist0X, *elistX0;
	for (elistX0 = listX0; elistX0; elistX0 = elistX0->next) {
	    ecompX0 = elistX0->ecomp;

	    for (elist0X = list0X; elist0X; elist0X = elist0X->next) {
		ecomp0X = elist0X->ecomp;

		// Check that this has not already been processed and flattened
		if (ecompX0->num1 > 0 && ecomp0X->num2 > 0) {

		    // Are any components of ecompX0->cell1 in the ecomp0X list?

		    for (ob1 = ecompX0->cell1->cell; ob1; ob1 = ob1->next) {
			if (ob1->type == FIRSTPIN) {
			    char *dstr = NULL;
			    tc1 = LookupCellFile(ob1->model.class, ecompX0->cell1->file);
			    if (tc1->flags & CELL_DUPLICATE) {
				dstr = strstr(ob1->model.class, "[[");
				if (dstr) *dstr = '\0';
			    }
		   	    ncomp = (ECompare *)HashInt2Lookup(ob1->model.class,
					ecompX0->cell1->file, &compdict);
			    if (dstr) *dstr = '[';
			    if ((ncomp == ecomp0X) && (ecomp0X->num2 <= ecompX0->num1)) {
				Fprintf(stdout, "Flattening instances of %s in cell %s"
					" makes a better match\n", ecompX0->cell1->name,
					name1);
				flattenInstancesOf(name1, file1, ecompX0->cell1->name); 
			        ecompX0->num1 = 0;
			        ecomp0X->num1 += ecompX0->num1;
				modified++;
				break;
			    }
			}
		    }

		    // Are any components of ecomp0X->cell2 in the ecompX0 list?

		    for (ob2 = ecomp0X->cell2->cell; ob2; ob2 = ob2->next) {
			if (ob2->type == FIRSTPIN) {
			    char *dstr = NULL;
			    tc2 = LookupCellFile(ob2->model.class, ecomp0X->cell2->file);
			    if (tc2->flags & CELL_DUPLICATE) {
				dstr = strstr(ob1->model.class, "[[");
				if (dstr) *dstr = '\0';
			    }
		   	    ncomp = (ECompare *)HashInt2Lookup(ob2->model.class,
					ecomp0X->cell2->file, &compdict);
			    if (dstr) *dstr = '[';
			    if ((ncomp == ecompX0) && (ecompX0->num1 <= ecomp0X->num2)) {
				Fprintf(stdout, "Flattening instances of %s in cell %s"
					" makes a better match\n", ecomp0X->cell2->name,
					name2);
				flattenInstancesOf(name2, file2, ecomp0X->cell2->name); 
			        ecomp0X->num2 = 0;
			        ecompX0->num2 += ecomp0X->num2;
				modified++;
				break;
			    }
			}
		    }
		}
	    }
	}
    }

    // Free the hash table and its contents.

    ecomp = (ECompare *)HashFirst(&compdict);
    while (ecomp != NULL) {
	if (--ecomp->refcount == (char)0) FREE(ecomp);
	ecomp = (ECompare *)HashNext(&compdict);
    } 
    HashKill(&compdict);

    // Free the 0:X and X:0 lists
    while (listX0 != NULL) {
	ECompList *nextptr = listX0->next;
	FREE(listX0);
	listX0 = nextptr;
    }
    while (list0X != NULL) {
	ECompList *nextptr = list0X->next;
	FREE(list0X);
	list0X = nextptr;
    }
    return modified;
}
