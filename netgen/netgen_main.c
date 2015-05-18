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

/* netgen_main.c  -- top-level (main) routine */

#include <stdio.h>
#ifdef ANSI_LIBRARY
#include <stdlib.h>  /* for getenv */
#endif
#include "netgen.h"

int main(int argc, char **argv)
{
  Finsert(stderr);
  InitializeCommandLine(argc, argv);

#ifdef HAVE_X11
  X_main_loop(argc, argv);  /* does not return, if really running X */
#else
  Query();
#endif

  return(0);
}

