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

/* query.c -- simple command-line interpreter */

#include "config.h"

#include <stdio.h>
#include <ctype.h>
#include <setjmp.h>
#include <signal.h>

#ifdef HAVE_MALLINFO
#include <malloc.h>
#endif

#ifdef IBMPC
#include <alloc.h>
#include <process.h> /* for system() */
#include <bios.h>  /* for bioskey() */
#endif

#include "netgen.h"
#include "timing.h"
#include "hash.h"
#include "objlist.h"
#include "query.h"
#include "netfile.h"
#include "print.h"
#include "dbug.h"
#include "netcmp.h"

/*************************************************************************/
/*                                                                       */
/*    I/O support for Query() routine                                    */
/*                                                                       */
/*************************************************************************/

static int SuppressPrompts = 0;
static char InputLine[200];

void typeahead(char *str)
{
  if (strlen(str) + strlen(InputLine) + 3 < sizeof(InputLine)) {
    strcat(InputLine," ");
    strcat(InputLine,str);
  }
  else fprintf(stderr, "InputLine too long: ignored command '%s'\n",str);
}

/* change the following to redirect the input stream */
FILE *promptstring_infile = NULL;

void promptstring(char *prompt, char *buf)
/* tries to get a token out of 'line' variable, 
but reads from 'promptstring_infile' if nec. */
/* copy it to buffer, when found */
/* If interactive, puts out 'prompt' */
{
  char *nexttok;
  char tmpstr[200];
  int echo;

  if (promptstring_infile == NULL)
    promptstring_infile = stdin;

  if (!SuppressPrompts) {
    Printf("%s",prompt);
    Fflush(stdout);
  }
  echo = 1;  /* assume we got it from typeahead */
  nexttok = InputLine;
  while (isspace(*nexttok) && *nexttok != '\0') nexttok++;
  if (*nexttok == '\0') {
    fgets(InputLine, sizeof(InputLine), promptstring_infile);
    if (promptstring_infile == stdin) echo = 0;
    nexttok = InputLine;
    while (isspace(*nexttok) && *nexttok != '\0') nexttok++;
    if (*nexttok == '\0') {
      *buf = '\0';
      return;
    }
  }
  /* nexttok points to beginning of valid token */
  strcpy(tmpstr,nexttok);
  nexttok = tmpstr;
  while (*nexttok != '\0' && !isspace(*nexttok)) nexttok++;
  strcpy(InputLine, nexttok);
  *nexttok = '\0';
  strcpy(buf, tmpstr);
  if (echo && !SuppressPrompts) Printf("%s\n",buf);
}


void InitializeCommandLine(int argc, char **argv)
{
  /* neither of the two Inits below are strictly necessary,		*/
  /* as static objects are initialized to 0 (NULL) automatically	*/

  InitCellHashTable();  
  InitGarbageCollection();

  RemoveCompareQueue();
	
#ifdef TCL_NETGEN
  return;

#else
  if (argv == NULL) return;	/* Don't run Query() */
		
  /* initialize command-line parser, including dbug code */
  DBUG_PROCESS(argv[0]);
  if (argc > 1) {
    int start;
    int usekbd, forceinteractive;
    usekbd = 1;   /* assume we are interactive */
    forceinteractive = 0; /* try to figure it out from cmd line */

    /* quiet prompting if program name is not netgen */
    SuppressPrompts =  (strstr(argv[0], "netgen") == NULL);

    for (start = 1; start < argc; start++) {
      if (argv[start][0] == '-') {
	switch (argv[start][1]) {
	case '#': DBUG_PUSH(&(argv[start][2])); 
	  break;
	case '\0': forceinteractive = 1; 
	  break;
	default: Fprintf(stderr,"Unrecognized switch: %s\n",argv[start]);
	  break;
	}
      }
      else {
	      typeahead(argv[start]);
	      usekbd = 0;
      }
    }
    if (!usekbd && !forceinteractive) typeahead("Q"); /* exit when done */
  }

  /* permit command-line typeahead even for X-windows */
  if (getenv("DISPLAY")) {
    int oldSuppressPrompts = SuppressPrompts;
    SuppressPrompts = 1;
    typeahead("q");  /* get out of one level of query */
    Query();
    SuppressPrompts = oldSuppressPrompts;
  }

#endif  /* TCL_NETGEN */
}

void Initialize(void)
{
#ifdef HAVE_MALLINFO
  char *wasted;

  wasted = (char *)MALLOC(2);  /* need to initialize memory allocator */
#endif

  InitializeCommandLine(0, NULL);
}


/* Print the type of object (mostly diagnostic) */

void PrintObjectType(int type)
{
   switch(type) {
      case UNIQUEGLOBAL:
	Printf("Unique Global");
	break;
      case GLOBAL:
	Printf("Global");
	break;
      case PORT:
	Printf("Port");
	break;
      case PROPERTY:
	Printf("Properties");
	break;
      case NODE:
	Printf("Net");
	break;
      default:
	if (type < 0) 
	   Printf("Error!");
	else
	   Printf("Pin %d", type);
	break;
   }
}
	


/*************************************************************************/
/*                                                                       */
/*    Some convenient routines for printing internal data structures     */
/*                                                                       */
/*************************************************************************/

#ifndef TCL_NETGEN

/*--------------------------------------------------------------*/
/* generate and print a list of elements that match a regexp	*/
/*--------------------------------------------------------------*/

void PrintElement(char *cell, char *list_template)
{
	
  struct objlist *list;
	
  if (strlen(cell))  CurrentCell = LookupCell(cell);
  list = List(list_template);
  Printf("Devices matching template: %s\n",list_template);
  while (list != NULL) {
    Printf ("   %s\n",list->name);
    list = list->next;
  }
}

#else

/*--------------------------------------------------------------*/
/* PrintElement() for use with Tcl---return all elements,	*/
/* let Tcl sort them out.					*/
/*--------------------------------------------------------------*/

void PrintAllElements(char *cell, int filenum)
{
  struct nlist *np;
  struct objlist *ob;
  char *sfx;

  if ((filenum == -1) && (Circuit1 != NULL) && (Circuit2 != NULL)) {
      PrintAllElements(cell, Circuit1->file);
      PrintAllElements(cell, Circuit2->file);
      return;
  }

  if (((cell == NULL) || (*cell == '\0')) && (CurrentCell != NULL))
      np = CurrentCell;
  else
      np = LookupCellFile(cell, filenum);
	
  if (np == NULL) {
    Printf("Circuit '%s' not found.\n",cell);
    return;
  }
	
  ob = np->cell;
  for (ob = np->cell; ob != NULL; ob = ob->next) {
    if (ob->type == FIRSTPIN) {
       if ((sfx = strrchr(ob->name, '/')) != NULL) *sfx = '\0';
       Printf("%s\n", ob->name);
       if (sfx != NULL) *sfx = '/';
    }
  }
}

#endif

/*--------------------------------------------------------------*/
/* Print connectivity between objects belonging to a specific	*/
/* node.  'filter' may be used to restrict the returned list to	*/
/* a specific type of object (node, element, port, pin, etc.).	*/
/*--------------------------------------------------------------*/

void Fanout(char *cell, char *node, int filter)
{
  struct nlist *np;
  struct objlist *ob;
  int nodenum;

  if (*cell == '\0') np = CurrentCell;
  else np = LookupCell(cell);
	
  if (np == NULL) {
    Printf("Cell '%s' not found.\n",cell);
    return;
  }
	
  nodenum = -999;
  for (ob = np->cell; ob != NULL; ob = ob->next) {
    if ((*matchfunc)(node, ob->name)) {
      nodenum = ob->node;
      break;
    }
  }

  /* now print out all elements that connect to that node */

  if (nodenum == -999)
    Printf("Net '%s' not found in circuit '%s'.\n", node, cell);
  else if (nodenum < 0)
    Printf("Net '%s' is disconnected.\n", node);
  else {
    if (ob != NULL)
       PrintObjectType(ob->type);
    else
       Printf("Object");
    Printf (" '%s' in circuit '%s' connects to:\n", node, cell);
    ob = np->cell;
    while (ob != NULL) {
      char *obname = ob->name;
      if (*obname == '/') obname++;
      if (ob->node == nodenum)
	 if (filter == ALLOBJECTS) {
	   Printf("  %s (", obname);
	   PrintObjectType(ob->type);
	   Printf(")\n");
	 }
	 else if ((filter == ALLELEMENTS) && (ob->type >= FIRSTPIN)) {
	   Printf("  %s\n", obname);
	 }
	 else if (ob->type == filter) {
	   Printf("  %s\n", obname);
	 }
      ob = ob->next;
    }
  }
}
	
#ifdef TCL_NETGEN

/* Print the nodes connected to each pin of the specified element */

void ElementNodes(char *cell, char *element, int fnum)
{
  struct nlist *np;
  struct objlist *ob, *nob, *nob2;
  int ckto;
  char *elementname, *obname;

  if ((fnum == -1) && (Circuit1 != NULL) && (Circuit2 != NULL)) {
      ElementNodes(cell, element, Circuit1->file);
      ElementNodes(cell, element, Circuit2->file);
      return;
  }

  if (((cell == NULL) || (*cell == '\0')) && (CurrentCell != NULL))
      np = CurrentCell;
  else
      np = LookupCellFile(cell, fnum);
	
  if (np == NULL) {
    Printf("Circuit '%s' not found.\n",cell);
    return;
  }

  elementname = element;
  if (*elementname == '/') elementname++;

  ckto = strlen(elementname);
  for (ob = np->cell; ob != NULL; ob = ob->next) {
    obname = ob->name;
    if (*obname == '/') obname++;
    if (!strncmp(elementname, obname, ckto))
       if (*(obname + ckto) == '/' || *(obname + ckto) == '\0')
	  break;
  }
  if (ob == NULL) {
    Printf("Device '%s' not found in circuit '%s'.\n", elementname, cell);
    return;
  }

  Printf("Device '%s' Pins:\n", elementname);
  for (; ob != NULL; ob = ob->next) {
    obname = ob->name;
    if (*obname == '/') obname++;
    if (!strncmp(elementname, obname, ckto)) {
       if (*(obname + ckto) != '/' && *(obname + ckto) != '\0')
	  continue;

       Printf("   ");
       PrintObjectType(ob->type);
       Printf(" (%s)", obname + ckto + 1);
       for (nob = np->cell; nob != NULL; nob = nob->next) {
	 if (nob->node == ob->node) {
	    if (nob->type == NODE) {
	       Printf(" = %s", nob->name);
	       break;
	    }
	    else if (nob->type == PORT) {
	       Printf(" = %s (port of %s)", nob->name, cell);
	       break;
	    }
	    else if (nob->type == GLOBAL) {
	       Printf(" = %s (global)", nob->name);
	       break;
	    }
	    else if (nob->type == UNIQUEGLOBAL) {
	       Printf(" = %s (unique global)", nob->name);
	       break;
	    }
 	 }
       }
       Printf("\n");
    }
  }
}

#endif  /* TCL_NETGEN */

/*----------------------------------------------------------------------*/
/* Find all nodes by name or wildcard pattern match, in the scope of	*/
/* the cell "cellname", and change them to the type given by "type".	*/
/* Changes are only allowed among nodes of type NODE, GLOBAL or		*/
/* UNIQUEGLOBAL.							*/
/*----------------------------------------------------------------------*/

int ChangeScopeCurrent(char *pattern, int typefrom, int typeto)
{
   /* Note that List() is supposed to operate on CurrentCell	*/
   /* but we want to be able to change scope on any cell.	*/

   struct objlist *plist, *psrch;
   struct nlist *tp;
   int numchanged = 0;

   plist = List(pattern);
   while (plist != NULL) {
      if (plist->type == typefrom) {
         for (psrch = CurrentCell->cell; psrch != NULL; psrch = psrch->next) {
	    if (psrch->type == typefrom && (*matchfunc)(psrch->name, plist->name)) {
	       psrch->type = typeto;
	       Printf("Cell %s:  Net %s changed to %s\n",
		   CurrentCell->name, psrch->name, psrch->type == NODE ?
		   "local" : psrch->type == GLOBAL ? "global" :
		   psrch->type == UNIQUEGLOBAL ? "unique global" : "unknown");
	       numchanged++;
	    }
	 }
      }
      plist = plist->next;
   }

   /* Recursively search descendants, if they exist */

   if (CurrentCell != NULL) {
      for (psrch = CurrentCell->cell; psrch != NULL; psrch = psrch->next) {
         if (psrch->type == FIRSTPIN) {
	    numchanged += ChangeScope(CurrentCell->file, psrch->model.class,
			pattern, typefrom, typeto);
         }
      }
   }

   return numchanged;
}

/* Structure to pass information to doglobalscope() */

typedef struct _gsd {
   int fnum;
   char *pattern;
   int typefrom;
   int typeto;
   int *numchanged;
} gsdata;

/*----------------------------------------------------------------------*/
/* Function called by hash search on cells, to call ChangeScopeCurrent	*/
/*----------------------------------------------------------------------*/

struct nlist *doglobalscope(struct hashlist *p, void *clientdata)
{
   struct nlist *ptr;
   struct objlist *ob, *lob, *nob;
   int file, numchanged;
   gsdata *gsd = (gsdata *)clientdata;

   ptr = (struct nlist *)(p->ptr);
   file = gsd->fnum;

   if ((file != -1) && (ptr->file != file)) return NULL;

   CurrentCell = ptr;
   numchanged = ChangeScopeCurrent(gsd->pattern, gsd->typefrom, gsd->typeto);
   *(gsd->numchanged) += numchanged;

   return ptr;
}

/*----------------------------------------------------------------------*/
/* Wrapper for ChangeScopeCurrent().  Takes a cellname as an 		*/
/* argument, instead of assuming the existance of a valid CurrentCell	*/
/*----------------------------------------------------------------------*/

int ChangeScope(int fnum, char *cellname, char *pattern, int typefrom, int typeto)
{
   struct nlist *SaveCell, *tp;
   int numchanged = 0;

   if ((fnum == -1) && (Circuit1 != NULL) && (Circuit2 != NULL)) {
      numchanged += ChangeScope(Circuit1->file, cellname,
			pattern, typefrom, typeto);
      numchanged += ChangeScope(Circuit2->file, cellname,
			pattern, typefrom, typeto);
      return numchanged;
   }

   SaveCell = CurrentCell;

   if (cellname == NULL) {
      /* No cellname given, so search all cells in file fnum */
      gsdata locdata;

      locdata.fnum = fnum;
      locdata.pattern = pattern;
      locdata.typefrom = typefrom;
      locdata.typeto = typeto;
      locdata.numchanged = &numchanged;

      RecurseCellHashTable2(doglobalscope, (void *)(&locdata));
   }
   else {

      CurrentCell = LookupCellFile(cellname, fnum);
      if (CurrentCell != NULL) {
         numchanged = ChangeScopeCurrent(pattern, typefrom, typeto);
      }
      else {
         Printf("No circuit '%s' found.\n", cellname);
      }
   }
   CurrentCell = SaveCell;
   return numchanged;
}

/*--------------------------------------------------------------------*/
/* print all nodes in cell 'name', together with their connectivities */
/*--------------------------------------------------------------------*/

typedef struct _noderecord {
  char *name;					/* node name */
  int uniqueglobal, global, port, node, pin;	/* counts */
} noderecord;
  
void PrintNodes(char *name, int filenum)
{
  int nodenum, nodemax;
  struct nlist *tp;
  struct objlist *ob;
  int maxnamelen;
  noderecord *nodelist;
  int uniqueglobals, globals, ports, nodes, pins;
	
  if ((filenum == -1) && (Circuit1 != NULL) && (Circuit2 != NULL)) {
      PrintNodes(name, Circuit1->file);
      PrintNodes(name, Circuit2->file);
      return;
  }

  tp = LookupCellFile(name, filenum);

  if (tp == NULL) {
    Printf ("No circuit '%s' found.\n",name);
    return;
  }
  Printf("Circuit: '%s'\n",tp->name);
  
  nodemax = 0;
  maxnamelen = 0;
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    int len = strlen(NodeAlias(tp, ob));
    if (len > maxnamelen) maxnamelen = len;
    if (ob->node > nodemax) nodemax = ob->node;
  }
  nodelist = (noderecord *) CALLOC((nodemax + 1), sizeof(noderecord));
  
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    nodenum = ob->node;
    if (nodenum < 0) continue;
    /* repeat bits of objlist.c here for speed */
    if (tp->nodename_cache != NULL) {
      nodelist[nodenum].name = tp->nodename_cache[nodenum]->name;
    }
    else {
      /* Overwrite name, in order of precedence */
      if ((nodelist[nodenum].port == 0) &&
	  ((IsPort(ob)) ||
	  ((nodelist[nodenum].node == 0) &&
	  ((ob->type == NODE) ||
	  ((nodelist[nodenum].uniqueglobal == 0) &&
	  ((ob->type == UNIQUEGLOBAL) ||
	  ((nodelist[nodenum].global == 0) &&
	  ((ob->type == GLOBAL) ||
	  ((nodelist[nodenum].pin == 0) &&
	  (ob->type >= FIRSTPIN))))))))))
	nodelist[nodenum].name = ob->name;

    }
    switch (ob->type) {
      case UNIQUEGLOBAL:
	nodelist[nodenum].uniqueglobal++; break;
      case GLOBAL:
	nodelist[nodenum].global++; break;
      case PORT:
	nodelist[nodenum].port++; break;
      case NODE:
	nodelist[nodenum].node++; break;
      case PROPERTY:
	break;
      default:
	nodelist[nodenum].pin++; break;
    }
  }

  for (nodenum = 0; nodenum <= nodemax; nodenum++) {
    if (nodelist[nodenum].name == NULL) continue;

    pins = nodelist[nodenum].pin;
    ports = nodelist[nodenum].port;
    globals = nodelist[nodenum].global;
    uniqueglobals = nodelist[nodenum].uniqueglobal;
    nodes = nodelist[nodenum].node;

    Printf("Net %d (%s):", nodenum, nodelist[nodenum].name);
    Ftab(NULL, maxnamelen + 15);
    Printf("Total = %d,", pins + ports + nodes + globals + uniqueglobals);
    if (ports)
      Printf(" Ports = %d,", ports);
    Ftab(NULL, maxnamelen + 40);
    if (pins)
      Printf("Pins = %d,", pins);
    Ftab(NULL, maxnamelen + 52);
    if (nodes)
      Printf("Nets = %d,", nodes);
    Ftab(NULL, maxnamelen + 63);
    if (globals)
      Printf("Globals = %d,", globals);
    Ftab(NULL, maxnamelen + 80);
    if (uniqueglobals)
      Printf("UniqueGlobals = %d", uniqueglobals);
    Printf("\n");
  }

  FREE(nodelist);
}
  
void PrintCell(char *name, int fnum)
{
  struct nlist *tp;
  struct objlist *ob;
  int maxnamelen;
	
  if ((fnum == -1) && (Circuit1 != NULL) && (Circuit2 != NULL)) {
      PrintCell(name, Circuit1->file);
      PrintCell(name, Circuit2->file);
      return;
  }

  tp = LookupCellFile(name, fnum);
  if (tp == NULL) {
    Printf ("No circuit '%s' found.\n",name);
    return;
  }
  maxnamelen = 0;
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
	  int len;
	  if ((len = strlen(ob->name)) > maxnamelen) maxnamelen = len;
  }
  
  Printf("Circuit: '%s'\n", tp->name);
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    Printf ("%s ", ob->name[1] == ':' ? ob->name : ob->name);
    Ftab(NULL, maxnamelen + 2);
    switch (ob->type) {
      case UNIQUEGLOBAL:	
	Printf("unique global"); break;
      case GLOBAL:
	Printf("global"); break;
      case PORT:
	Printf("port"); break;
      case NODE:
	Printf("node"); break;
      case PROPERTY:
	Printf("properties"); break;
      default:
	Printf("pin %d", ob->type);
	break;
    }
    Ftab(NULL, 40);
    if (ob->type != PROPERTY)
       Printf(" Net #: %d", ob->node);
    Printf("\n");
  }
}

void PrintInstances(char *name, int filenum)
{
  struct nlist *tp;
  struct objlist *ob;
  int instancecount;
	
  if ((filenum == -1) && (Circuit1 != NULL) && (Circuit2 != NULL)) {
      PrintInstances(name, Circuit1->file);
      PrintInstances(name, Circuit2->file);
      return;
  }

  tp = LookupCellFile(name, filenum);
  if (tp == NULL) {
    Printf ("No circuit '%s' found.\n",name);
    return;
  }
  Printf("Circuit: '%s'\n",tp->name);
  instancecount = 0;
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    if (ob->type == FIRSTPIN) {
      struct objlist *ob2;
      int port, node, global, uniqueglobal, pin;
      int ports, nodes, globals, uniqueglobals, pins;

      port = node = global = uniqueglobal = pin = 0;
      instancecount++;

      ob2 = ob;
      do {
	struct objlist *ob3;

	ports = nodes = globals = uniqueglobals = pins = 0;
	for (ob3 = tp->cell; ob3 != NULL; ob3 = ob3->next)
	  if (ob3->node == ob2->node)
	    switch (ob3->type) {
	    case UNIQUEGLOBAL: uniqueglobals++; break;
	    case GLOBAL: globals++; break;
	    case PORT:   ports++; break;
	    case NODE:   nodes++; break;
	    case PROPERTY: break;
	    default:     pins++; break;
	    }
	pin++;
	if (uniqueglobals) uniqueglobal++;
	else if (globals) global++;
	else if (ports) port++;
	else if (nodes) node++;
	ob2 = ob2->next;
      } while (ob2 != NULL && ob2->type > FIRSTPIN);
/*      Fflush(stdout); */
      Printf("%s (class: %s)", ob->instance, ob->model.class);
      Ftab(NULL,35);
      Printf("%2d pins ->",pin);
      if (port) Printf("%2d ports,",port);
      Ftab(NULL,55);
      if (node) Printf("%2d nodes,",node);
      Ftab(NULL,65);
      if (global) Printf("%2d globals,",global);
      Ftab(NULL,75);
      if (uniqueglobal) Printf("%2d ug",uniqueglobal);
      Printf("\n");
    }
  }
  Printf("Cell %s contains %d instances.\n",name,instancecount);
}

void DescribeInstance(char *name, int file)
{
  struct nlist *tp, *tp2;
  struct objlist *ob;
  unsigned char *instlist;
  int nodemax, nodenum;

  int instancecount;
  int node, nodenumber, morenodes, disconnectednodes;
	
  if ((file == -1) && (Circuit1 != NULL) && (Circuit2 != NULL)) {
      DescribeInstance(name, Circuit1->file);
      DescribeInstance(name, Circuit2->file);
      return;
  }

  tp = LookupCellFile(name, file);

  if (tp == NULL) {
    Printf("No circuit '%s' found.\n",name);
    return;
  }
  Printf("Circuit: '%s'\n",tp->name);

  /* First pass counts total number of entries */
  nodemax = 0;
  disconnectednodes = 0;
  for (ob = tp->cell; ob != NULL; ob = ob->next) 
  {
    if (ob->node > nodemax) nodemax = ob->node;
    else if ((ob->node == -1) && (ob->model.port != PROXY)) {
      if (!(tp->flags & CELL_PLACEHOLDER))
      {
	if (disconnectednodes == 0) Fprintf(stderr, "\n");
        disconnectednodes++;
        Fprintf(stderr, "Cell %s disconnected node: %s\n", tp->name, ob->name);
      }
    }
  }
  instlist = (unsigned char *) CALLOC((nodemax + 1), sizeof(unsigned char));

  /* Second pass finds total number of unique nodes */
  for (ob = tp->cell; ob != NULL; ob = ob->next) 
    if (ob->node > 0)
      instlist[ob->node] = (unsigned char) 1;

  /* Now add them all together */
  nodenumber = 0;
  for (nodenum = 1; nodenum <= nodemax; nodenum++)
     if (instlist[nodenum] == (unsigned char) 1) nodenumber++;

  /* And we're done with this record */
  FREE(instlist);

  /* Now collect the relevant information per node */

  ClearDumpedList();

  instancecount = 0;
  for (ob = tp->cell; ob != NULL; ob = ob->next) {
    if (ob->type == FIRSTPIN) {
      instancecount++;
      tp2 = LookupCellFile(ob->model.class, tp->file);
      tp2->dumped++;
    }
  }
  Printf("Circuit %s contains %d device instances.\n", name, instancecount);

  /* print out results */
  tp = FirstCell();
  while (tp != NULL) {
    if (tp->dumped) {
      Printf("  Class: %s", tp->name);
      Ftab(NULL, 30);
      Printf(" instances: %3d\n", tp->dumped);
    }
    tp = NextCell();
  }
  Printf("Circuit contains %d nets", nodenumber);
  if (disconnectednodes) 
    Printf(", and %d disconnected pin%s", disconnectednodes,
		((disconnectednodes == 1) ? "" : "s"));
  Printf(".\n");

}

void PrintPortsInCell(char *cellname, int filenum)
{
  struct nlist *np;
  struct objlist *ob;
  int portcount;

  if ((filenum == -1) && (Circuit1 != NULL) && (Circuit2 != NULL)) {
      PrintPortsInCell(cellname, Circuit1->file);
      PrintPortsInCell(cellname, Circuit2->file);
      return;
  }

  np = LookupCellFile(cellname, filenum);
  if (np == NULL) {
    Printf("No circuit: %s\n",cellname);
    return;
  }
  portcount = 0;
  for (ob = np->cell; ob != NULL; ob = ob->next) 
    if (IsPort(ob)) {
      portcount++;
      Printf("%s\n", ob->name);
    }
  Printf("Cell %s contains %d ports.\n",cellname, portcount);
}

void PrintLeavesInCell(char *cellname, int filenum)
{
  struct nlist *np;
  struct objlist *ob;
  int am_a_leaf;

  if ((filenum == -1) && (Circuit1 != NULL) && (Circuit2 != NULL)) {
      PrintLeavesInCell(cellname, Circuit1->file);
      PrintLeavesInCell(cellname, Circuit2->file);
      return;
  }

  np = LookupCellFile(cellname, filenum);
  if (np == NULL) {
    Printf("No circuit: %s\n",cellname);
    return;
  }
  if (np->dumped) return;
  np->dumped = 1;

  if (np->class != CLASS_SUBCKT) {
    Printf("%s; %d ports; Primitive.\n", cellname, NumberOfPorts(cellname));
    return;
  }

  /* otherwise, consider it's children */
  am_a_leaf = 1; /* assume I am a leaf */
  for (ob = np->cell; ob != NULL; ob = ob->next)
    if (ob->type == FIRSTPIN) {
      /* if I contain an instance, I cannot be a leaf */
      PrintLeavesInCell(ob->model.class, filenum);
      am_a_leaf = 0;
    }

  if (am_a_leaf) Printf("%s; %d ports\n", cellname, NumberOfPorts(cellname));
  return;
}

static int PrintLeavesInCellHash(struct hashlist *p)
/* print leaves in hash table that are INSTANCED by other cells */
{
  struct nlist *ptr;

  ptr = (struct nlist *)(p->ptr);
  if ((ptr->class == CLASS_SUBCKT)) PrintLeavesInCell(ptr->name, ptr->file);
  return(0);
}

void PrintAllLeaves(void)
{
  ClearDumpedList();
  RecurseCellHashTable(PrintLeavesInCellHash);
}

static jmp_buf jmpenv;

/* static void handler(void) */
static void handler(int sig)
{
  fprintf(stderr,"\nInterrupt (%d)!!\n", sig);
  fflush(stderr);
  longjmp(jmpenv,1);
}

#ifndef TCL_NETGEN

void Query(void)
{
  /* little interactive debugger */
  char reply;
  char repstr[100];
  char repstr2[100];
  float StartTime;   /* for elapsed CPU times */
  int Timing;  /* if true, print times of each command */
  int filenum = -1;
	
  if (!SuppressPrompts)
    Printf("Netgen %s.%s: %s, %s, %s\n", NETGEN_VERSION, NETGEN_REVISION,
		NETGEN_COPYRIGHT, NETGEN_AUTHOR, NETGEN_DEVELOPER);
  setjmp(jmpenv);
  signal(SIGINT,handler);
  Timing = 0;
  StartTime = 0.0;
  do {
    promptstring("NETGEN command: ",repstr);
    if (Timing) StartTime = CPUTime();
    reply = repstr[0];
    switch (reply) {
    case 'h' : PrintCellHashTable(0, filenum); break;
    case 'H' : PrintCellHashTable(1, filenum); break;
    case 'N' :
      promptstring("Enter circuit name: ", repstr);
      PrintNodes(repstr, -1);
      break;
    case 'n' : 
      promptstring("Enter element name: ", repstr);
      if (CurrentCell == NULL) 
	promptstring("Enter circuit name:    ", repstr2);
      else strcpy(repstr2, CurrentCell->name);
      Fanout(repstr2, repstr, ALLOBJECTS);
      break;
    case 'e' : 
      promptstring("Enter element name: ", repstr);
      if (CurrentCell == NULL) 
	promptstring("Enter circuit name:    ", repstr2);
      else strcpy(repstr2, CurrentCell->name);
      PrintElement(repstr2,repstr);
      break;
    case 'c' : 
      promptstring("Enter circuit name: ", repstr);
      PrintCell(repstr, filenum);
      break;
    case 'i' :
      promptstring("Enter circuit name: ", repstr);
      PrintInstances(repstr, filenum);
      break;
    case 'd':
      promptstring("Enter circuit name: ", repstr);
      DescribeInstance(repstr, filenum);
      break;
/* output file formats */
    case 'k' : 
      promptstring("Write NTK: Enter circuit name: ", repstr);
      Ntk(repstr,"");
      break;
    case 'x' : 
      promptstring("Write EXT: Enter circuit name: ", repstr);
      Ext(repstr, filenum);
      break;
    case 'z' : 
      promptstring("Write SIM: Enter circuit name: ", repstr);
      Sim(repstr, filenum);
      break;
    case 'w' : 
      promptstring("Write WOMBAT: circuit name: ", repstr);
      Wombat(repstr,NULL);
      break;
    case 'a' : 
      promptstring("Write ACTEL: circuit name: ", repstr);
      Actel(repstr,"");
      break;
    case 's':
      promptstring("Write SPICE: circuit name: ", repstr);
      SpiceCell(repstr, filenum, "");
      break;
    case 'v':
      promptstring("Write Verilog: circuit name: ", repstr);
      VerilogModule(repstr, filenum, "");
      break;
    case 'E':
      promptstring("Write ESACAP: circuit name: ", repstr);
      EsacapCell(repstr,"");
      break;
    case 'g':
      promptstring("Write NETGEN: circuit name: ", repstr);
      WriteNetgenFile(repstr,"");
      break;
    case 'C':
      promptstring("Write C code: circuit name: ", repstr);
      Ccode(repstr,"");
      break;
/* input file formats */
    case 'r':
    case 'R':
      promptstring("Read file: ",repstr);
      ReadNetlist(repstr, &filenum);
      break;
    case 'X' : 
      promptstring("Read EXT: Enter file name: ", repstr);
      ReadExtHier(repstr, &filenum);
      break;
    case 'Z' : 
      promptstring("Read SIM: Enter file name: ", repstr);
      ReadSim(repstr, &filenum);
      break;
    case 'K' : 
      promptstring("Read NTK: file? ", repstr);
      ReadNtk(repstr, &filenum);
      break;
    case 'A' : 
      printf("Reading ACTEL library.\n");
      ActelLib();
      break;
    case 'S':
      promptstring("Read SPICE (.ckt) file? ", repstr);
      ReadSpice(repstr, &filenum);
      break;
    case 'V':
      promptstring("Read Verilog (.v) file? ", repstr);
      ReadVerilog(repstr, &filenum);
      break;
    case 'G' : 
      promptstring("Read NETGEN: file? ", repstr);
      ReadNetgenFile(repstr, &filenum);
      break;

    case 'f' : 
      promptstring("Enter circuit name to flatten: ", repstr);
      Flatten(repstr, filenum);
      break;
    case 'F' : 
      promptstring("Enter class of circuit to flatten: ", repstr);
      FlattenInstancesOf(repstr, filenum);
      break;
    case 'p' : 
      promptstring("Enter circuit name: ", repstr);
      PrintPortsInCell(repstr, filenum);
      break;
    case 'T' : 
      NETCOMP();
      break;
    case 'l' : 
      ClearDumpedList();
      promptstring("Enter circuit name: ", repstr);
      PrintLeavesInCell(repstr, filenum);
      break;
    case 'L' : 
      printf("List of all leaf circuits:\n");
      PrintAllLeaves();
      break;
    case 'D' : Debug = !Debug; 
      printf("Debug mode is %s\n", Debug?"ON":"OFF");
      break;
    case 't': StartTime = CPUTime();
      Timing = !Timing;
      printf("Timing of commands %s.\n", Timing?"enabled":"disabled");
      break;
    case '#':
      if (strlen(repstr) > 1) {
	char *command;
	command = repstr+1;
	DBUG_PUSH(command);
      }
      else {
	promptstring("Dbug command? ",repstr);
	DBUG_PUSH(repstr);
      }
      break;
    case '!': 
#ifdef IBMPC
      system("");
#else
      system("/bin/csh");
#endif
      break;
    case 'I' : Initialize(); break;
    case 'q' : break;
    case 'Q' : exit(0);
    case 'P' : PROTOCHIP(); break;
#ifdef HAVE_MALLINFO
    case 'm': PrintMemoryStats(); break;
#endif
    case '<' :
      {
	FILE *oldfile;
	promptstring("Read from file? ",repstr);
	oldfile = promptstring_infile;
	promptstring_infile = fopen(repstr,"r");
	if (promptstring_infile == NULL) 
	  Printf("Unable to open command file: %s\n", repstr);
	else {
	  Query();
	  fclose(promptstring_infile);
	}
	promptstring_infile = oldfile;
	break;
      }
    default : 
      fflush(stdout);
      if (strlen(repstr)) fprintf(stderr,"Unknown command: %c\n",reply);
      fflush(stderr);
      printf(
"WRITE: nt(k),e(x)t,sim (z),(w)ombat,(a)ctel,net(g)en,(s)pice,(E)sacap,(C)\n");
      printf(
"READ:  (r)ead, nt(K), e(X)t, sim (Z), (A)ctel library, net(G)en, (S)pice\n");
      printf(
"Print LISTS: (d)escribe circuit (c)ontents, (i)nstances, (p)orts, (l)eaves (L)\n"
	    );
      printf(
"PRINT: (n)ode connectivity (N), (e)lement list, (h)ash table entries (H))\n");
      printf(
"CELL OPS: (f)latten circuit, (F)latten instance, make circuit primiti(v)e (V=all)\n"
	    );

      printf("toggle (D)ebug, (t)ime commands, embed (P)rotochip, ne(T)cmp\n");
      printf("(!) push shell, (<) read input file");

#ifdef HAVE_MALLINFO
      printf(", show (m)emory usage");
#endif
      printf("\n");
      printf("(q)uit, (Q)uit immediately, re-(I)nitialize \n");
      break;
    }
    if (Timing) {
      printf("CPU time used by last operation = %0.2f s\n", 
	     ElapsedCPUTime(StartTime));
      fflush(stdout); /* just in case we have been redirected */
    }
  } while (reply != 'q');
  signal(SIGINT,SIG_DFL);
}

#endif
