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

/* test.c  -- top-level (main) routine wrapping a test suite */

#define TESTSHORTS 0
#define TESTNTK 0
#define TESTACTEL 0
#define TESTSCOPING 0
#define TESTEMBEDDED 0
#define TESTPRINT 0

#include <stdio.h>
#ifdef ANSI_LIBRARY
#include <stdlib.h>  /* for getenv */
#endif
#include "netgen.h"
#ifdef TESTPRINT
#include "print.h"
#endif

void test_entry(void) 
{ 

#if TESTPRINT
  Printf("this is a test");
  Ftab(stdout,50);
  Printf("of the tab");
  Printf(" facility\n");
  printf("just testing\n");
#endif /* TESTPRINT */


#if TESTSCOPING
  CellDef("leaf", -1);
  Global("global");
  UniqueGlobal("uniqueglobal"); 
  Port("port");
  Port("W.in");
  Port("E.out");
  N("leaf", "W.in","E.out","global");
  Cell("p","W.in","port","uniqueglobal");

	
  CellDef("twoleaves", -1);
  Node("global");
  Composition = HORIZONTAL;
  Array("leaf",2);
  Connect("leaf*/port","leaf1/port");
  EndCell();

  CellDef("fourleaves", -1);
  Composition = HORIZONTAL;
  Array("twoleaves",2);
  EndCell();
#endif /* TESTSCOPING */


#if TESTNTK
  CellDef ("test", -1);
  Port("in");
  Port("out");
  Node("internal1");
  Node("internal2");
  join("in","internal1");
  join("out","internal2");
  Connect("in","out");
		
  PrintCell("test");
	
  CellDef("test2", -1);
  Port("IN");
  Port("OUT");
  Instance("test","test1");
  Instance("test","test2");
  Instance("n","M1");
  join("IN","test1.in");
  join("test1.out","test2.in");
  join("IN","M1.gate");
  join("OUT","M1.drain");
  join("test1.out","M1.source");
  join("test2.out","OUT");
		
  PrintCell("test2");

  Ntk("test2","test2.ntk");
  Fanout("test2","M1.gate", ALLOBJECTS);
	      
  ReadNtk("exphorn.ntk");
#endif  /* TESTNTK */

#ifdef TESTEMBEDDED
  CellDef("tcamp", -1);
  Port("N.vdd");
  Port("S.gnd");
  Port("W.in");
  Port("W.bias");
  Port("E.out");
  Port("E.bias");
		
  Node("plusin");
  Node("minusin");
		
  Instance("n","bias");
  Instance("n","plus");
  Instance("n","minus");
  Instance("p","mir");
  Instance("p","load");
		
  Connect("mir/gate","mir/drain");
  Connect("mir/gate","load/gate");
  Connect("mir/gate","plus/drain");
  Connect("load/drain","minus/drain");
  Connect("{p,min}*/s*","bias/drain");

  Connect("N.vdd","{mir,load}/source");
  Connect("bias/source","*.gnd");
  Connect("bias/gate","*.bias");
  Connect("minus/gate","minusin");
  Connect("plus/gate","plusin");

  Connect("plusin","*.in");
  Connect("load/drain","*.out");
  Connect("minusin","E.out");
  EndCell();
		

  CellDef("tcdelayline", -1);
  Port("vdd.top");
  Port("gnd.bot");
  Port("left.in");
  Port("left.bias");
  Port("right.out");
  Port("right.bias");
  Instance("tcamp", "tc1");
  Connect("tc1/W.in", "left.in");
  Connect("tc1/W.bias", "left.bias");

  Instance("tcamp", "tc2");
  Connect("tc1/E*", "tc2/W*");
  Instance("tcamp", "tc3");
  Connect("tc2/E*", "tc3/W*");
#if 1
  Instance("tcamp", "tc4");
  Connect("tc3/E*", "tc4/W*");
  Instance("tcamp", "tc5");
  Connect("tc4/E*", "tc5/W*");
  Instance("tcamp", "tc6");
  Connect("tc5/E*", "tc6/W*");
  Instance("tcamp", "tc7");
  Connect("tc6/E*", "tc7/W*");
  Instance("tcamp", "tc8");
  Connect("tc7/E*", "tc8/W*");
#else
  Instance("tcamp", "tc8");
  Connect("tc3/E*", "tc8/W*");
#endif

  Connect("tc8/E.out", "right.out");
  Connect("tc8/E.bias", "right.bias");
  Connect("tc*vdd", "vdd.top");
  Connect("tc*gnd", "gnd.bot");
  EndCell();

  CellDef("wramp", -1);
  Global("vdd");
  Global("gnd");
  Port("W.in");
  Port("W.bias");
  Port("E.out");
  Port("E.bias");

  Node("minusin");
  Node("pmir");
  Node("nmir");
  Node("bias");
  Node("minus");
		
  N("wramp", "W.in", "pmir", "bias");	/* + input */
  N("wramp", "minusin", "nmir", "bias"); /* - input */
  N("wramp", "E.bias", "bias", "gnd");	/* bias */
  P("wramp", "pmir", "pmir", "vdd");
  P("wramp", "nmir", "nmir", "vdd");
  P("wramp", "pmir", "E.out", "vdd");
  P("wramp", "nmir", "minus", "vdd");
  N("wramp", "minus", "minus", "gnd");
#if 0
  N("wramp", "minus", "E.out", "gnd"); 
#else
  /* test named parameter list */
  N("wramp", "gate=minus", "drain=E.out", "source=gnd");
#endif
  Connect("E.bias", "W.bias");
  Connect("minusin", "E.out");
  EndCell();

  CellDef("wrdelayline", -1);
  Composition = HORIZONTAL;
  Place("wramp");
  Place("wramp");
  Place("wramp");
  Place("wramp");
  Place("wramp");
  Place("wramp");
  Place("wramp");
  Place("wramp");
  EndCell();
#endif /* TESTEMBEDDED */

#if TESTSHORTS
  CellDef("short", -1);
  Port("in");
  Port("out");
  Connect("in","out");
  CellDef("top", -1);
  Port("foo");
  Port("bar");
  Instance("short","short");
  Connect("short/in", "foo");
  Connect("short/out","bar");
#endif

#if TESTACTEL
  ActelLib();
#endif /* TESTACTEL */
}




int main(int argc, char **argv)
{
  Finsert(stderr);
  InitializeCommandLine(argc, argv);

#ifdef TEST
  test_entry();
#endif

#ifdef HAVE_X11
  X_main_loop(argc, argv);  /* does not return, if really running X */
#else
  Query();
#endif

  return(0);
}

