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

/* netcomp.c -- a simple wrapper to provide netlist comparison functionality */

#include <stdio.h>
#include "netgen.h"

#ifdef HAVE_GETOPT
#include <unistd.h>
#endif /* HAVE_GETOPT */

#ifndef HAVE_X11
/* the following two procedures need to be defined to
 * permit linking with netgen.o even if HAVE_X11 has
 * been disabled
 */

void X_display_line(char *buf)
{
  printf("%s", buf);
}

void X_display_refresh(void)
{
  fflush(stdout);
}
#endif

void STRCPY(char *dest, char *source)
{
  while ((*dest++ = *source++) != '\0') ;
}


int main(int argc, char *argv[])
{
#ifndef HAVE_GETOPT
  char cell1[200], cell2[200];
  int filenum = -1;

  Debug = 0;
  if (argc < 3 || argc > 5) {
    printf ("usage: netcomp <file 1> <file 2> [<cell 1> [<cell 2>]] \n");
    return (-1);
  }
  Initialize();

  STRCPY(cell1, ReadNetlist(argv[1], &filenum));
  if (argc >= 4) STRCPY(cell1, argv[3]);  /* if explicit cell name specified */

  STRCPY(cell2, ReadNetlist(argv[2], &filenum));
  if (argc == 5) STRCPY(cell2, argv[4]);  /* if explicit cell name specified */
#else
  char cell1[200], cell2[200];
  int usage = 0;
  int args;
  int c;

  Debug = 0;
  VerboseOutput = 0;
  IgnoreRC = 0;
  while ((c = getopt(argc, argv, "ivq")) != EOF) {
    switch (c) {
    case 'i':
      IgnoreRC = 1;
      break;
    case 'v':
      VerboseOutput = 1;
      break;
    case 'q':
      NoOutput = 1;
      break;
    default:
      printf("Unknown flag: -%c\n", (char)c);
      usage = 1;
    }
  }

  args = argc - optind;
  if (args < 2 || args > 4) {
    printf("Wrong number of file/cell name arguments.\n");
    usage = 1;
  }

  if (usage) {
 printf ("usage: netcomp [-i] [-v] [-q] <file 1> <file 2> [<cell 1> [<cell 2>]]\n");
 printf ("	 -i = don't try to match resistances and capacitances\n");
 printf ("       -v = verbose output\n");
 printf ("       -q = no output (only results and return code)\n");
    return (-1);
  }

  Initialize();
/*  NoDisconnectedNodes = 1;   we now do this in Compare(), AFTER reading cells */

  STRCPY(cell1, ReadNetlist(argv[optind]));
  if (args >= 3) STRCPY(cell1, argv[optind + 2]);

  STRCPY(cell2, ReadNetlist(argv[optind + 1]));
  if (args == 4) STRCPY(cell2, argv[optind + 3]);
#endif

  printf("Comparing cells: %s (circuit 1) and %s (circuit2).\n\n", cell1, cell2);
  Flatten(cell1, -1);
  Flatten(cell2, -1);
  if (Compare(cell1, cell2)) {
    printf("Cells are identical.\n");
    return(0);
  }
  printf("Cells are different.\n");
  return(1);
}
