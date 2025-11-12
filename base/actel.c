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

/*  actel.c -- Output routines for ACTEL's .als format */

#include "config.h"

#include <stdio.h>
#include <stdarg.h>
#include <strings.h>
#include <ctype.h>
#ifdef IBMPC
#include <stdlib.h>  /* for strtol on PC */
#endif

#ifdef TCL_NETGEN
#include <tcl.h>
#endif

#include "netgen.h"
#include "objlist.h"
#include "netfile.h"
#include "hash.h"
#include "print.h"
	
#define ACTELHASHSIZE 99
static long actelhashbase = 0xA00;
static struct hashdict actelnamedict;
static FILE *actelfile;

char *ActelName(char *Name);
int PrintActelName(struct hashlist *ptr)
{
	fprintf(actelfile,"%s == %s\n",ActelName(ptr->name),ptr->name);
	return(1);
}
	
void PrintActelNames(char *filename)
{
  if (filename == NULL) actelfile = stdout;
  else actelfile = fopen(filename,"w");
  RecurseHashTable(&actelnamedict, PrintActelName);
  if (actelfile != stdout) fclose(actelfile);
}

long ActelNameHash(char *name)
/* hashes name into namedict if necessary, then returns address of entry */
{
  struct hashlist *p;

  p = HashInstall(name, &actelnamedict);
  if (p == NULL) return(0);
  if (p->ptr != NULL) return ((long)(p->ptr));
  actelhashbase++;
  p->ptr = (void *)actelhashbase;
  return(actelhashbase);
}


#define ACTELNAMESIZE 3
static char	ActelNames[ACTELNAMESIZE][500] ;
static int	ActelIndex = 0;

char	*ActelName(char *Name)
/* returns a pointer to one of the elements in ActelNames,
   which is a copy of 'name', 
   quoted according to the ACTEL requirements
*/
{
  int	index, index2;
  int	NeedsQuoting;
  char    name[500];
  char    *nm;

  strcpy(name,Name);
  /* strip physical-pin information, if it exists */
  if ((nm = strrchr(name,PHYSICALPIN[0])) != NULL) *nm = '\0';
  if (strlen(name) > 13) {
    ActelIndex = (++ActelIndex) % ACTELNAMESIZE;
    /* format the value of the hashed value of the string */
    sprintf(ActelNames[ActelIndex], "$%lX", ActelNameHash(name));
if (Debug) 
Printf("ActelNameHash returns %s on name %s\n",ActelNames[ActelIndex], name);
    return(ActelNames[ActelIndex]);
  }

  NeedsQuoting = 0;
  if (NULL != strpbrk(name, ".,:; \t\"'\n\r")) NeedsQuoting = 1;

  ActelIndex = (++ActelIndex) % ACTELNAMESIZE;
  if (!NeedsQuoting) {
    strcpy(ActelNames[ActelIndex], name);
    return(ActelNames[ActelIndex]);
  }
  /* else, needs quoting */
  index2 = 0;
  ActelNames[ActelIndex][index2++] = '"';
  for (index = 0; index < strlen(name); index ++) {
    if (name[index] == '"')
      ActelNames[ActelIndex][index2++] = '"';
    ActelNames[ActelIndex][index2++] = name[index];
  }
  ActelNames[ActelIndex][index2++] = '"';
  ActelNames[ActelIndex][index2++] = '\0';
  return(ActelNames[ActelIndex]);
}

void ActelPins(char *name, int format)
/* print out a pins file containing all nodes that:
     1) connect to pad
     2) have names of the format <name>(<int>)

if format = 0, just dump the list of pins
if format = 1, use the actel .pin file format
*/
{
  struct nlist *tp;
  struct objlist *ob, *ob2;
  char *ptr;
  char physicalpin[MAX_STR_LEN];

  tp = LookupCell(name);
  if (tp == NULL) return;
  if (tp->class != CLASS_SUBCKT) return;
  if (format == 1) FlushString("DEF %s.\n", ActelName(name));
  if (format == 0) FlushString("%20s  %3s  %s\n\n",
			       "Pad name","pin","Actel name");

  for (ob = tp->cell; ob != NULL; ob = ob->next) 
    if (IsPortInPortlist(ob,tp) &&
	strcasecmp(ob->name, "GND") &&
	strcasecmp(ob->name, "VDD") ) 
      /* scan entire list, 
	 looking for same nodenum, but physical port assignment */
      for (ob2 = tp->cell; ob2 != NULL; ob2 = ob2->next) {
	if (ob->node == ob2->node &&
	    (ptr = strrchr(ob2->name,PHYSICALPIN[0])) != NULL) {
	  ptr++; /* point to next char */
	  strcpy(physicalpin, ptr);
	  ptr = strchr(physicalpin, ENDPHYSICALPIN[0]);
	  if (ptr == NULL)
	    Printf("Bad Actel Pin specification: %s\n", ob2->name);
	  else {
	    *ptr = '\0';
	    if (format == 0) FlushString("%20s  %3s  %s\n", ob->name,
					 physicalpin,ActelName(ob->name));
	    if (format == 1) FlushString("NET %s; ; PIN:%s.\n",
					 ActelName(ob->name),physicalpin);
	    break;  /* out of for loop */
	  }
	}
      }

  if (format == 1) FlushString("END.\n");
}

void actelCell(char *name)
{
  struct nlist *tp, *tp2;
  struct objlist *ob;
  int maxnode;	 /* maximum node number assigned in cell */
  int nodenum;	 /* node number currently being dumped in NET */
  int nodedumped;/* flag set true when first element of net written */
  int netdumped; /* flag set true when NET stmt. written for given nodenum */
  int vddnode, gndnode;  /* flags set to true when processing power rail */

  tp = LookupCell(name);
  if (tp == NULL) {
    Printf ("No cell '%s' found.\n", name);
    return;
  }
  /* do NOT dump primitive cells */
  if (tp->class != CLASS_SUBCKT) 
    return;

  /* check to see that all children have been dumped */
  ob = tp->cell;
  while (ob != NULL) {
    tp2 = LookupCell(ob->model.class);
    if ((tp2 != NULL) && !(tp2->dumped)) 
      actelCell(tp2->name);
    ob = ob->next;
  }

  /* print out header list */
  FlushString("DEF %s", ActelName(tp->name));
  netdumped = 0;
  nodedumped = 0;
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    if (IsPortInPortlist(ob, tp) && 
	strcasecmp(ob->name, "GND")&&
	strcasecmp(ob->name, "VDD") ) {
      /* unique port */
      if (nodedumped) 
	FlushString (", ");
      else 
	nodedumped = 1;
      if (!netdumped) 
	FlushString("; ");
      /*			FlushString("%s", ActelName(ob->name)); */
      FlushString("%s", ActelName(NodeAlias(tp, ob)));
      netdumped = 1;
    }
  }
  FlushString(".\n");

#if 0
/*  I'm not sure whether this works.  It is based on a reading of 
  the .adl syntax manual, and has never been tested.
*/

  /* run through cell's contents, defining all global elements */
  ob = tp->cell;
  while (ob != NULL) {
    if ((ob->type == GLOBAL)
	&& match(ob->name, NodeAlias(tp, ob)))
      FlushString ("SIG %s; GLOBAL.\n", ActelName(ob->name));
    ob = ob->next;
  }
#endif

  /* now run through cell's contents, print instances */
  ob = tp->cell;
  while (ob != NULL) {
    if (ob->type == FIRSTPIN) { /* this is an instance */
      if ((LookupCell(ob->model.class))->class != CLASS_SUBCKT)
	FlushString ("USE ADLIB:%s; %s.\n",
		     ActelName(ob->model.class), ActelName(ob->instance.name));
      else
	FlushString ("USE %s; %s.\n",
		     ActelName(ob->model.class), ActelName(ob->instance.name));
    }
    ob = ob->next;
  }

  /* now print out nets */
  ob = tp->cell;
  /* find maximum nodenumber in use */
  maxnode = -1;
  while (ob != NULL) {
    if (ob->node >  maxnode) 
      maxnode = ob->node;
    ob = ob->next;
  }
#if 1
  for (nodenum = 1; nodenum <= maxnode; nodenum++) {
    nodedumped = 0;
    netdumped = 0;
    vddnode = 0;
    gndnode = 0;
    for (ob = tp->cell; ob != NULL; ob = ob->next) {
      if (ob->node == nodenum && 
	  (IsPortInPortlist(ob, tp) || ob->type >= FIRSTPIN)) {
	char *nm;

	nm = strchr(ob->name, SEPARATOR[0]);
	/* suppress pins that are global connections to power rails */
	/* i.e., all pins that are <instancename>/VDD */
	if (nm == NULL || (strcasecmp(nm+1,"VDD") && strcasecmp(nm+1,"GND"))) {
	  if (!netdumped)
	    FlushString("NET %s; ", ActelName(NodeAlias(tp, ob)));
	  netdumped = 1;
	  /* print out the element in list */
	  if (!(strcasecmp(ob->name, "GND"))) gndnode = 1;
	  else if (!(strcasecmp(ob->name, "VDD"))) vddnode = 1;
	  else {
	    if (nodedumped) 
	      FlushString(", ");
	    /* write it out if it is a REAL port or a PIN */
	    if (ob->type >= FIRSTPIN)
	      FlushString("%s:%s", ActelName(ob->instance.name),
	      /* was strchr below, but failed for FLATTENED objects 12/12/88 */
			  ActelName(strrchr(ob->name, SEPARATOR[0]) + 1));
	    else
	      FlushString("%s", ActelName(NodeAlias(tp, ob))); 
	    nodedumped = 1;
	  }
	}
      }
    }
    /* changed from "if (nodedumped)" to "if (netdumped)" 
       to correctly terminate power and ground nets that have no pins
       (i.e., they only connect to ports of cells that have their
       own (internal) connections to these global resources */
    if (netdumped) {
      if (gndnode) {
	if (nodedumped) FlushString("; ");
	FlushString("GLOBAL, POWER:GND");
      }
      if (vddnode) {
	if (nodedumped) FlushString("; ");
	FlushString("GLOBAL, POWER:VCC");
      }
      FlushString(".\n");
    }
  }
#else
  /* this code incorrectly wrote power and ground nets when these
     nodes were only connected to VDD and GND pins of instances
     (i.e., they were not connected to logic inputs) */
  for (nodenum = 1; nodenum <= maxnode; nodenum++) {
    nodedumped = 0;
    netdumped = 0;
    vddnode = 0;
    gndnode = 0;
    for (ob = tp->cell; ob != NULL; ob = ob->next) {
      if (ob->node == nodenum && 
	  (IsPortInPortlist(ob, tp) || ob->type >= FIRSTPIN)) {
	char *nm;

	nm = strchr(ob->name, SEPARATOR[0]);
	/* suppress pins that are global connections to power rails */
	if (nm == NULL || (strcasecmp(nm+1,"VDD") && strcasecmp(nm+1,"GND"))) {
	  if (!netdumped)
	    FlushString("NET %s; ", ActelName(NodeAlias(tp, ob)));
	  netdumped = 1;
	  /* print out the element in list */
	  if (!(strcasecmp(ob->name, "GND"))) gndnode = 1;
	  else if (!(strcasecmp(ob->name, "VDD"))) vddnode = 1;
	  else {
	    if (nodedumped) 
	      FlushString(", ");
	    /* write it out if it is a REAL port or a PIN */
	    if (ob->type >= FIRSTPIN)
	      FlushString("%s:%s", ActelName(ob->instance.name),
	      /* was strchr below, but failed for FLATTENED objects 12/12/88 */
			  ActelName(strrchr(ob->name, SEPARATOR[0]) + 1));
	    else
	      FlushString("%s", ActelName(NodeAlias(tp, ob))); 
	    nodedumped = 1;
	  }
	}
      }
    }
    if (nodedumped) {
      if (gndnode) 
	FlushString("; GLOBAL, POWER:GND");
      if (vddnode) 
	FlushString("; GLOBAL, POWER:VCC");
      FlushString(".\n");
    }
  }
#endif


#if 0
  /* old style; one pin per net statement.  Ok, but ugly (but doesn't
   handle VDD and GND pins correctly */
  ob = tp->cell;
  while (ob != NULL) {
    if (ob->type != NODE) {
      FlushString ("NET NET%d; ", ob->node);
      if (IsPort(ob)) 
	FlushString("%s.\n",
		    ActelName(ob->name));
      else if (ob->type >= FIRSTPIN) 
	FlushString("%s:%s.\n",
		    ActelName(ob->model.class), 
		    /* was strchr below 12/12/88 */
		    ActelName(strrchr(ob->name, SEPARATOR[0]) + 1));
    }
    ob = ob->next;
  }
#endif  

  FlushString ("END.\n\n");
  tp->dumped = 1;		/* set dumped flag */
}


void Actel(char *name, char *filename)
{
  char Path[500];
  char FileName[500];

  if (LookupCell(name) == NULL) {
    Printf("No such cell name: %s\n", name);
    return;
  }
  if (filename == NULL || strlen(filename) == 0)  
    strcpy(Path, name);
  else 
    strcpy(Path,filename);

  SetExtension(FileName, Path, ACTEL_EXTENSION);
  if (!OpenFile(FileName, 80)) {
    Printf("Failed to open file named: %s\n",FileName);
    perror("Actel(): Unable to open output file.");
    return;
  }
  ClearDumpedList();
  InitializeHashTable(&actelnamedict, ACTELHASHSIZE);
  if (LookupCell(name) != NULL) 
    actelCell(name);
  CloseFile(FileName);

  SetExtension(FileName, Path, ".pin");
  OpenFile(FileName, 80);
  ActelPins(name,1);
  CloseFile(FileName);

  SetExtension(FileName, Path, ".pads");
  OpenFile(FileName, 80);
  ActelPins(name,0);
  CloseFile(FileName);

  SetExtension(FileName, Path, ".crt");
  OpenFile(FileName, 80);
  FlushString("DEF %s.\n", ActelName(name));
  FlushString("END.\n");
  CloseFile(FileName);

  SetExtension(FileName, Path, ".nam");
  PrintActelNames(FileName);
}

