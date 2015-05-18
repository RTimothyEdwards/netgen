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

/* wombat.c -- writing files for WOMBAT */

#include "config.h"

#include <stdio.h>
#include <stdarg.h>

#include "netgen.h"
#include "hash.h"
#include "objlist.h"
#include "netfile.h"
#include "print.h"

void Wombat(char *name, char *filename)
{
  struct objlist *ob, *ob2;
  struct nlist *tp, *tp2;
  char FileName[500];

  if (filename == NULL || strlen(filename) == 0) 
    SetExtension(FileName, name, WOMBAT_EXTENSION);
  else strcpy(FileName, filename);

  if (!OpenFile(FileName, 0)) {
    SetExtension(FileName, FileName, WOMBAT_EXTENSION);
    if (!OpenFile(FileName, 0)) {
      perror("Wombat(): Unable to open output file.");
      return;
    }
  }
  tp = LookupCell(name);
  if (tp == NULL) {
    Printf ("No cell '%s' found.\n", name);
    return;
  }

  /* now run through cell's contents, print instances */
  ob = tp->cell;
  while (ob != NULL) {
    if (ob->type == FIRSTPIN) {
      /* this is an instance */
      FlushString ("%s %s ", ob->instance.name, ob->model.class);

      /* print out parameter list */
      ob2 = ob;
      tp2 = LookupCell(ob->model.class);
      do {
	char *nm;
#if 1
	struct objlist *newob;

	/* was strchr 12/12/88 */
	nm = strrchr(ob2->name, SEPARATOR[0]) + 1;
	newob = LookupObject(nm, tp2);
	if (match(nm, NodeAlias(tp2, newob)))
	  FlushString ("%s ", NodeAlias(tp, ob2));
#else
	int nodenum;

	nm = strrchr(ob2->name, SEPARATOR[0]) + 1;
	nodenum = LookupObject(nm, tp2)->node;
	if ((nodenum == -1) || 
	    match(nm, NodeName(tp2, nodenum)))
	  FlushString ("%s ", NodeName(tp, ob2->node));
#endif
	ob2 = ob2->next;
      } while ((ob2 != NULL) && (ob2->type > FIRSTPIN));
      FlushString("\n");
    }
    ob = ob->next;
  }
  CloseFile(FileName);
}

