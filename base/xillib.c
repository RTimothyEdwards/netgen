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


/* xillib.c -- definitions of Xilinx cells for ACTEL cells. */


/* define the following to make pads unique */
/* this is necessary for ntk2adl, but is annoying for PLACE */

#undef USE_UNIQUE_GLOBALS


#include "config.h"
#include <stdio.h>
#include "netgen.h"

static int xilinx_lib_present = 0;

int XilinxLibPresent(void)
{
  return xilinx_lib_present;
}

void XilinxLib(void)
{
  int OldDebug;

  OldDebug = Debug;
  Debug = 0;


  CellDef("OUTBUF", -1);
#ifdef USE_UNIQUE_GLOBALS
     UniqueGlobal("PAD");
#else
     Port("O");
#endif
     Port("I");
     SetClass(CLASS_MODULE);
  EndCell();


  CellDef("INBUF", -1);
     Port("O");
#ifdef USE_UNIQUE_GLOBALS
     UniqueGlobal("PAD");
#else
     Port("I");
#endif
  EndCell();

  CellDef("CLKBUF", -1);
     Port("O");
#ifdef USE_UNIQUE_GLOBALS
     UniqueGlobal("PAD");
#else
     Port("I");
#endif
     SetClass(CLASS_MODULE);
  EndCell();

  CellDef("CLOCK", -1);
     Port("O");
     SetClass(CLASS_MODULE);
  EndCell();

  CellDef("TRIBUFF", -1);
#ifdef USE_UNIQUE_GLOBALS
     UniqueGlobal("PAD");
#else
     Port("O");
#endif
     Port("I");
     Port("!T");
     SetClass(CLASS_MODULE);
  EndCell();


  CellDef("BIBUF", -1);
#ifdef USE_UNIQUE_GLOBALS
     UniqueGlobal("PAD");
#else
     Port("O");
#endif
     Port("I");
     Port("E");
     Port("IN");
     SetClass(CLASS_MODULE);
  EndCell();

	CellDef("AND2", -1);
		Port("1");
		Port("2");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("AND2A", -1);
		Port("!1");
		Port("2");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("AND2B", -1);
		Port("!1");
		Port("!2");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("NAND2", -1);
		Port("1");
		Port("2");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("NAND2A", -1);
		Port("!1");
		Port("2");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("NAND2B", -1);
		Port("!1");
		Port("!2");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("OR2", -1);
		Port("1");
		Port("2");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("OR2A", -1);
		Port("!1");
		Port("2");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("OR2B", -1);
		Port("!1");
		Port("!2");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("NOR2", -1);
		Port("1");
		Port("2");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("NOR2A", -1);
		Port("!1");
		Port("2");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("NOR2B", -1);
		Port("!1");
		Port("!2");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("AND3", -1);
		Port("1");
		Port("2");
		Port("3");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("AND3A", -1);
		Port("!1");
		Port("2");
		Port("3");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("AND3B", -1);
		Port("!1");
		Port("!2");
		Port("3");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("AND3C", -1);
		Port("!1");
		Port("!2");
		Port("!3");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("NAND3", -1);
		Port("1");
		Port("2");
		Port("3");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("NAND3A", -1);
		Port("!1");
		Port("2");
		Port("3");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("NAND3B", -1);
		Port("!1");
		Port("!2");
		Port("3");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("NAND3C", -1);
		Port("!1");
		Port("!2");
		Port("!3");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("OR3", -1);
		Port("1");
		Port("2");
		Port("3");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("OR3A", -1);
		Port("!1");
		Port("2");
		Port("3");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("OR3B", -1);
		Port("!1");
		Port("!2");
		Port("3");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("OR3C", -1);
		Port("!1");
		Port("!2");
		Port("!3");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("NOR3", -1);
		Port("1");
		Port("2");
		Port("3");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("NOR3A", -1);
		Port("!1");
		Port("2");
		Port("3");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("NOR3B", -1);
		Port("!1");
		Port("!2");
		Port("3");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("NOR3C", -1);
		Port("!1");
		Port("!2");
		Port("!3");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("AND4", -1);
		Port("1");
		Port("2");
		Port("3");
		Port("4");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("AND4A", -1);
		Port("!1");
		Port("2");
		Port("3");
		Port("4");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("AND4B", -1);
		Port("!1");
		Port("!2");
		Port("3");
		Port("4");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("AND4C", -1);
		Port("!1");
		Port("!2");
		Port("!3");
		Port("4");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("AND4D", -1);
		Port("!1");
		Port("!2");
		Port("!3");
		Port("!4");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("NAND4", -1);
		Port("1");
		Port("2");
		Port("3");
		Port("4");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("NAND4A", -1);
		Port("!1");
		Port("2");
		Port("3");
		Port("4");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("NAND4B", -1);
		Port("!1");
		Port("!2");
		Port("3");
		Port("4");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("NAND4C", -1);
		Port("!1");
		Port("!2");
		Port("!3");
		Port("4");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("NAND4D", -1);
		Port("!1");
		Port("!2");
		Port("!3");
		Port("!4");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("OR4", -1);
		Port("1");
		Port("2");
		Port("3");
		Port("4");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("OR4A", -1);
		Port("!1");
		Port("2");
		Port("3");
		Port("4");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("OR4B", -1);
		Port("!1");
		Port("!2");
		Port("3");
		Port("4");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("OR4C", -1);
		Port("!1");
		Port("!2");
		Port("!3");
		Port("4");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("OR4D", -1);
		Port("!1");
		Port("!2");
		Port("!3");
		Port("!4");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("NOR4", -1);
		Port("1");
		Port("2");
		Port("3");
		Port("4");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("NOR4A", -1);
		Port("!1");
		Port("2");
		Port("3");
		Port("4");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("NOR4B", -1);
		Port("!1");
		Port("!2");
		Port("3");
		Port("4");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("NOR4C", -1);
		Port("!1");
		Port("!2");
		Port("!3");
		Port("4");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("NOR4D", -1);
		Port("!1");
		Port("!2");
		Port("!3");
		Port("!4");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();


/*
INBUF
CLKBUF
OUTBUF
TRIBUF
BIBUF
*/





	CellDef("BUF", -1);
		Port("I");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("BUFA", -1);
		Port("I");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("INV", -1);
		Port("I");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("INVA", -1);
		Port("I");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("XOR", -1);
		Port("1");
		Port("2");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("XNOR", -1);
		Port("1");
		Port("2");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();
/*
	CellDef("XO1", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("X01A", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("XA1", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("XA1A", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("AX1", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("AX1A", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("AX1B", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("AO1", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("AO1A", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("AO1B", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("AO1C", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("AOI1A", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("AOI1B", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("MAJ3", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("AO2", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("4");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("AO2A", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("4");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("AOI2A", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("4");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("AOI2B", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("4");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("OA1", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("OA1A", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("OA1B", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("OA1C", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("OA3", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("D");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("OA3A", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("D");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("OA3B", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("D");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("OA2", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("D");
		Port("Y");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("OA2A", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("D");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("MX2", -1);
		Port("A");
		Port("B");
		Port("S");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("MX2A", -1);
		Port("A");
		Port("B");
		Port("S");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("MX2B", -1);
		Port("A");
		Port("B");
		Port("S");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("MX2C", -1);
		Port("A");
		Port("B");
		Port("S");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("MX4", -1);
		Port("D0");
		Port("D1");
		Port("D2");
		Port("D3");
		Port("S1");
		Port("S0");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("MXT", -1);
		Port("A");
		Port("B");
		Port("C");
		Port("D");
		Port("S0A");
		Port("S0B");
		Port("S1");
		Port("O");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("HA1", -1);
		Port("A");
		Port("B");
		Port("CO");
		Port("S");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("HA1A", -1);
		Port("A");
		Port("B");
		Port("CO");
		Port("S");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("HA1B", -1);
		Port("A");
		Port("B");
		Port("CO");
		Port("S");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("HA1C", -1);
		Port("A");
		Port("B");
		Port("CO");
		Port("S");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("FA1A", -1);
		Port("A");
		Port("B");
		Port("CI");
		Port("CO");
		Port("S");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("FA1B", -1);
		Port("A");
		Port("B");
		Port("CI");
		Port("CO");
		Port("S");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("FA2A", -1);
		Port("A0");
		Port("A1");
		Port("B");
		Port("CI");
		Port("CO");
		Port("S");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DL1", -1);
		Port("D");
		Port("G");
		Port("Q");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DL1A", -1);
		Port("D");
		Port("G");
		Port("QN");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DL1B", -1);
		Port("D");
		Port("G");
		Port("Q");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DL1C", -1);
		Port("D");
		Port("G");
		Port("QN");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DLC", -1);
		Port("D");
		Port("G");
		Port("Q");
		Port("CLR");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DLCA", -1);
		Port("D");
		Port("G");
		Port("Q");
		Port("CLR");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DLE", -1);
		Port("D");
		Port("G");
		Port("Q");
		Port("E");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DLEA", -1);
		Port("D");
		Port("G");
		Port("Q");
		Port("E");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DLEB", -1);
		Port("D");
		Port("G");
		Port("Q");
		Port("E");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DLEC", -1);
		Port("D");
		Port("G");
		Port("Q");
		Port("E");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DLM", -1);
		Port("A");
		Port("B");
		Port("S");
		Port("G");
		Port("Q");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DLMA", -1);
		Port("A");
		Port("B");
		Port("S");
		Port("G");
		Port("Q");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("JKF", -1);
		Port("J");
		Port("K");
		Port("CLK");
		Port("Q");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("JKFPC", -1);
		Port("J");
		Port("K");
		Port("CLK");
		Port("Q");
		Port("PRE");
		Port("CLR");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("TFC", -1);
		Port("T");
		Port("CLK");
		Port("Q");
		Port("CLR");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DFM", -1);
		Port("A");
		Port("B");
		Port("S");
		Port("CLK");
		Port("Q");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DFMA", -1);
		Port("A");
		Port("B");
		Port("S");
		Port("CLK");
		Port("Q");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DFMB", -1);
		Port("A");
		Port("B");
		Port("S");
		Port("CLK");
		Port("Q");
		Port("CLR");
     		SetClass(CLASS_MODULE);
	EndCell();
*/

	CellDef("DF1", -1);
		Port("D");
		Port("C");
		Port("Q");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DF1A", -1);
		Port("D");
		Port("C");
		Port("!Q");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DF1B", -1);
		Port("D");
		Port("!C");
		Port("Q");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DF1C", -1);
		Port("D");
		Port("!C");
		Port("!Q");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DFC1", -1);
		Port("D");
		Port("C");
		Port("Q");
		Port("RD");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DFC1A", -1);
		Port("D");
		Port("!C");
		Port("Q");
		Port("RD");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DFC1B", -1);
		Port("D");
		Port("C");
		Port("Q");
		Port("!RD");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DFC1C", -1);
		Port("D");
		Port("C");
		Port("!Q");
		Port("RD");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DFC1D", -1);
		Port("D");
		Port("!C");
		Port("Q");
		Port("!RD");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DFC1E", -1);
		Port("D");
		Port("C");
		Port("!Q");
		Port("!RD");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DFC1F", -1);
		Port("D");
		Port("!C");
		Port("!Q");
		Port("RD");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DFC1G", -1);
		Port("D");
		Port("!C");
		Port("!Q");
		Port("!RD");
     		SetClass(CLASS_MODULE);
	EndCell();
/*
	CellDef("DFP1", -1);
		Port("D");
		Port("CLK");
		Port("Q");
		Port("PRE");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DFP1A", -1);
		Port("D");
		Port("CLK");
		Port("Q");
		Port("PRE");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DFP1B", -1);
		Port("D");
		Port("CLK");
		Port("Q");
		Port("PRE");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DFP1C", -1);
		Port("D");
		Port("CLK");
		Port("QN");
		Port("PRE");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DFP1D", -1);
		Port("D");
		Port("CLK");
		Port("Q");
		Port("PRE");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DFP1E", -1);
		Port("D");
		Port("CLK");
		Port("QN");
		Port("PRE");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DFP1F", -1);
		Port("D");
		Port("CLK");
		Port("QN");
		Port("PRE");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DFP1G", -1);
		Port("D");
		Port("CLK");
		Port("QN");
		Port("PRE");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DFPC", -1);
		Port("D");
		Port("CLK");
		Port("Q");
		Port("CLR");
		Port("PRE");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DFPCA", -1);
		Port("D");
		Port("CLK");
		Port("Q");
		Port("CLR");
		Port("PRE");
     		SetClass(CLASS_MODULE);
	EndCell();
*/

	CellDef("DFE", -1);
		Port("D");
		Port("C");
		Port("Q");
		Port("CE");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DFEA", -1);
		Port("D");
		Port("!C");
		Port("Q");
		Port("CE");
     		SetClass(CLASS_MODULE);
	EndCell();

/*
	CellDef("DFEB", -1);
		Port("D");
		Port("C");
		Port("Q");
		Port("CE");
		Port("!RD");
		Port("PRE");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DFEC", -1);
		Port("D");
		Port("CLK");
		Port("Q");
		Port("E");
		Port("CLR");
		Port("PRE");
     		SetClass(CLASS_MODULE);
	EndCell();

	CellDef("DFED", -1);
		Port("D");
		Port("C");
		Port("Q");
		Port("CE");
		Port("PRE");
     		SetClass(CLASS_MODULE);
	EndCell();
*/


  Debug = OldDebug;
  xilinx_lib_present = 1;
}
