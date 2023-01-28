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

/* inetcomp.c -- a simple wrapper to the NETCOMP() function */
#include "config.h"

#include <stdio.h>
#include "netgen.h"

#ifdef HAVE_GETOPT
#include <unistd.h>
#endif /* HAVE_GETOPT */

void STRCPY(char *dest, char *source)
{
  while ((*dest++ = *source++) != '\0') ;
}


int main(int argc, char *argv[])
{
  char cell1[MAX_STR_LEN], cell2[MAX_STR_LEN];

  Debug = 0;
  if (argc != 1) {
    printf ("usage: inetcomp\n");
    return (-1);
  }
  Initialize();
  NETCOMP();

  return(1);
}
