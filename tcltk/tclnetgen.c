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

/* tclnetgen.c ---  Tcl interpreter interface for using netgen */

#include <stdio.h>
#include <stdlib.h>	/* for getenv */
#include <string.h>

#include <tcl.h>

#include "config.h"
#include "netgen.h"
#include "objlist.h"
#include "netcmp.h"
#include "dbug.h"
#include "print.h"
#include "query.h"	/* for ElementNodes() */
#include "hash.h"
#include "xilinx.h"
#include "tech.h"
#include "flatten.h"

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/*-----------------------*/
/* Tcl 8.4 compatibility */
/*-----------------------*/

#ifndef CONST84
#define CONST84
#endif

Tcl_Interp *netgeninterp;
Tcl_Interp *consoleinterp;
int ColumnBase = 0;
char *LogFileName = NULL;

extern int PropertyErrorDetected;

/* Function prototypes for all Tcl command callbacks */

int _netgen_readnet(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_readlib(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_canonical(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_writenet(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_flatten(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_nodes(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_elements(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_debug(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_protochip(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_instances(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_contents(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_describe(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_cells(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_ports(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_model(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_leaves(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_quit(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_reinit(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netgen_log(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
#ifdef HAVE_MALLINFO
int _netgen_printmem(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
#endif
int _netgen_help(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_matching(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_compare(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_iterate(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_summary(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_print(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_format(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_run(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_verify(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_automorphs(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_equate(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_ignore(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_permute(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_property(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_exhaustive(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_symmetry(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_restart(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_global(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);
int _netcmp_convert(ClientData, Tcl_Interp *, int, Tcl_Obj *CONST objv[]);

typedef struct _Cmd {
   char 	*name;
   int		(*handler)();
   char		*helptext;
} Command;

/*------------------------------------------------------*/
/* All netgen commands under Tcl are defined here	*/ 
/*------------------------------------------------------*/
   
Command netgen_cmds[] = {
	{"readnet",		_netgen_readnet,
		"[<format>] <file> [<filenum>]\n   "
		"read a netlist file (default format=auto)"},
	{"readlib",		_netgen_readlib,
		"<format> [<file>]\n   "
		"read a format library"},
	{"canonical",		_netgen_canonical,
		"<valid_cellname>\n   "
		"return top-level cellname and file number"},
	{"writenet", 		_netgen_writenet,
		"<format> <file>\n   "
		"write a netlist file"},
	{"flatten",		_netgen_flatten,
		"[class] [<parent>] <cell>\n   "
		"flatten a hierarchical cell"},
	{"nodes",		_netgen_nodes,
		"[<element>] <cell> <file>\n   "
		"print nodes of an element or cell"},
	{"elements",		_netgen_elements,
		"[<node>] <cell>\n   "
		"print elements of a node or cell"},
	{"debug",		_netgen_debug,
		"on|off|<command>\n   "
		"turn debugging on or off or debug a command"},
	{"protochip",		_netgen_protochip,
		"\n   "
		"embed protochip structure"},
	{"instances",		_netgen_instances,
		"<cell>\n   "
		"list instances of the cell"},
	{"contents",		_netgen_contents,
		"<cell>\n   "
		"list contents of the cell"},
	{"describe",		_netgen_describe,
		"<cell>\n   "
		"describe the cell"},
	{"cells",		_netgen_cells,
		"[list] [-all | -top | filename]\n   "
		"print known cells, optionally from filename only\n   "
		"-all:  print all cells, including primitives\n   "
		"-top:  print all top-level cells"},
	{"ports",		_netgen_ports,
		"<cell>\n   "
		"print ports of the cell"},
	{"model",		_netgen_model,
		"<name> <class>\n   "
		"equate a model name with a device class"},
	{"leaves",		_netgen_leaves,
		"[<cell>]\n   "
		"print leaves of the cell"},
	{"quit",		_netgen_quit,
		"\n   "
		"exit netgen and Tcl"},
	{"reinitialize",	_netgen_reinit,
		"\n   "
		"reintialize netgen data structures"},
	{"log",			_netgen_log,
		"[file <name>|start|end|reset|suspend|resume|echo]\n   "
		"enable or disable output log to file"},
#ifdef HAVE_MALLINFO
	{"memory",		_netgen_printmem,
		"\n   "
		"print memory statistics"},
#endif
	{"help",		_netgen_help,
		"\n   "
		"print this help information"},
	NULL
};

Command netcmp_cmds[] = {
	{"compare",		_netcmp_compare,
		"<valid_cellname1> <valid_cellname2>\n   "
		"declare two cells for netcomp netlist comparison"},
	{"global",		_netcmp_global,
		"<valid_cellname> <nodename>\n	"
		"declare a node (with possible wildcards) in the\n	"
		"hierarchy of <valid_cellname> to be of global scope"},
	{"convert",		_netcmp_convert,
		"<valid_cellname>\n	"
		"convert global nodes to local nodes and pins\n		"
		"in cell <valid_cellname>"},
	{"iterate",		_netcmp_iterate,
		"\n   "
		"do one netcomp iteration"},
	{"summary",		_netcmp_summary,
		"[elements|nodes]\n   "
		"summarize netcomp internal data structure"},
	{"print",		_netcmp_print,
		"\n   "
		"print netcomp internal data structure"},
	{"format",		_netcmp_format,
		"<col1_width> <col2_width>\n   "
		"set width of formatted output"},
	{"run",			_netcmp_run,
		"[converge|resolve]\n   "
		"converge: run netcomp to completion (convergence)\n   "
		"resolve: run to completion and resolve symmetries"},
	{"verify",		_netcmp_verify,
		"[elements|nodes|only|equivalent|unique]\n   "
		"verify results"},
	{"symmetries",	_netcmp_automorphs,
		"\n   "
		"print symmetries"},
	{"equate",		_netcmp_equate,
		"elements [<valid_cellname1>] <name1> [<valid_cellname2>] <name2>\n   "
		"nodes [<valid_cellname1>] <name1> [<valid_cellname2>] <name2>\n   "
		"pins [[<valid_cellname1>] <name1> [<valid_cellname2>] <name2>]\n   "
		"classes <valid_cellname1> [<pins>] <valid_cellname2> [<pins>]\n   "
		"elements: equate two elements\n   "
		"nodes: equate two nodes\n  "
		"classes: equate two device classes\n  "
		"pins: match pins between two cells"},

	{"ignore",		_netcmp_ignore,
		"class <name>\n   "
		"class: ignore any instances of class named <name>"},
		
	{"permute",		_netcmp_permute,
		"[transistors|resistors|capacitors|<model>]\n   "
		"<model>: permute named pins on device model\n   "
		"resistor: enable resistor permutations\n   "
		"capacitor: enable capacitor permutations\n   "
		"transistor: enable transistor permutations\n   "
		"(none): enable transistor and resistor permutations"},
	{"property",		_netcmp_property,
		"default: apply property defaults\n   "
		"<device>|<model> <property_key> [...]\n   "
		"<device>: name of a device type (capacitor, etc.)\n  "
		"<model>: name of a device model\n   "
		"<property_key>: name of the property to compare"},

	{"exhaustive",		_netcmp_exhaustive,
		"\n   "
		"toggle exhaustive subdivision"},
	{"symmetry",		_netcmp_symmetry,
		"(deprecated)"},
	{"restart",		_netcmp_restart,
		"\n   "
		"start over (reset data structures)"},
	{"matching",		_netcmp_matching,
		"[element|node] <name1>\n   "
		"return the corresponding node or element name\n   "
		"in the compared cell"},
	NULL
};
 
/*------------------------------------------------------*/
/* Given a file number, need to find the top-level cell */
/*------------------------------------------------------*/

struct nlist *
GetTopCell(int fnum)
{
    struct nlist *tp;

    tp = FirstCell();
    while (tp != NULL) {
	if (tp->flags & CELL_TOP)
	    if (tp->file == fnum)
		break;
	tp = NextCell();
    }
    return tp;
}

/*------------------------------------------------------*/
/* Common function to parse a Tcl object as either a	*/
/* netlist file name or a file number.			*/
/*------------------------------------------------------*/

int
CommonGetFilenameOrFile(Tcl_Interp *interp, Tcl_Obj *fobj, int *fnumptr)
{
    int result, llen;
    int fnum, ftest;
    char *filename;
    struct nlist *tp;

    result = Tcl_GetIntFromObj(interp, fobj, &ftest);
    if (result != TCL_OK) {
	Tcl_ResetResult(interp);
	filename = Tcl_GetString(fobj);
	tp = LookupCell(filename);
	if (tp == NULL) {
	    Tcl_SetResult(interp, "No such file.\n", NULL);
	    return TCL_ERROR;
	}
	else if (!(tp->flags & CELL_TOP)) {
	    Tcl_SetResult(interp, "Name is not a file.\n", NULL);
	    return TCL_ERROR;
	}
	else fnum = tp->file;
    }
    else {
	fnum = ftest;
    }
    *fnumptr = fnum;
    return TCL_OK; 
}

/*------------------------------------------------------*/
/* Common function to parse a cell name.  This allows	*/
/* several variants on the syntax:			*/
/*							*/
/* (1) <cellname>					*/
/*	Assumes cellname is unique and finds the cell	*/
/* 	and file number.				*/
/*							*/
/* (2) {<cellname> <fnum>}				*/
/*	Finds the cell, given the name and file number	*/
/*	as a list of length 2.				*/
/*							*/
/* (3) {<cellname> <filename>}				*/
/*	Finds the cell, given the name and filename of	*/
/*	the file containing the cell.			*/
/*							*/
/* (4) {<filename> <cellname>}				*/
/*	is also allowed and is backwards-compatible	*/
/*	with the arguments for "lvs".			*/
/*							*/
/* (5) {<fnum> <cellname>}				*/
/*	likewise.					*/
/*							*/
/* (6) <fnum>						*/
/*	refers to the top-level cell of file <fnum>	*/
/*							*/
/* (7) -circuit1					*/
/*	the first circuit being compared, after the 	*/
/*	"compare" command has been issued.		*/
/*							*/
/* (8) -circuit2					*/
/*	the first circuit being compared, after the 	*/
/*	"compare" command has been issued.		*/
/*							*/
/* (9) -current						*/
/*	the most recent circuit/file to be read,	*/
/*	after a "readnet" or "readlib" has been issued.	*/
/*							*/
/* Note that <filename> is equivalent to the top-level	*/
/* cellname.  That allows the order of elements	to be	*/
/* arbitrary.						*/
/*							*/
/* Function returns a Tcl result, and fills in a	*/
/* pointer to the cell structure, and the file number	*/
/* (which is a copy of <fnum>, if provided as an	*/
/* argument).						*/
/*							*/
/* <fnum> == -1 or "*" is (theoretically) treated by	*/
/* all commands as a wildcard matching all netlists.	*/
/*------------------------------------------------------*/

int
CommonParseCell(Tcl_Interp *interp, Tcl_Obj *objv,
	struct nlist **tpr, int *fnumptr)
{
    Tcl_Obj *tobj, *fobj;
    int result, llen;
    int fnum, ftest, index;
    char *filename, *cellname;
    struct nlist *tp, *tp2;

    char *suboptions[] = {
	"-circuit1", "-circuit2", "-current", "*", NULL
    };
    enum SubOptionIdx {
	CIRCUIT1_IDX, CIRCUIT2_IDX, CURRENT_IDX, WILDCARD_IDX
    };

    result = Tcl_ListObjLength(interp, objv, &llen);
    if (result != TCL_OK) return TCL_ERROR;

    if (llen == 2) {

	fnum = -1;

	result = Tcl_ListObjIndex(interp, objv, 0, &tobj);
	if (result != TCL_OK) return TCL_ERROR;

	/* Is 1st argument an integer? */

	result = Tcl_GetIntFromObj(interp, tobj, &ftest);
	if (result != TCL_OK) {
	    Tcl_ResetResult(interp);

	    /* Is 1st argument a special keyword? */
	    if (Tcl_GetIndexFromObj(interp, tobj, (CONST84 char **)suboptions,
			"special", 0, &index) == TCL_OK) {
		switch (index) {
		    case CIRCUIT1_IDX:
			if (Circuit1 == NULL) {
			    Tcl_SetResult(interp, "No circuit has been"
					" declared for comparison\n", NULL);
			    return TCL_ERROR;
			}
			fnum = Circuit1->file;
			result = Tcl_ListObjIndex(interp, objv, 1, &tobj);
			if (result != TCL_OK) return TCL_ERROR;
			break;
		    case CIRCUIT2_IDX:
			if (Circuit2 == NULL) {
			    Tcl_SetResult(interp, "No circuit has been"
					" declared for comparison\n", NULL);
			    return TCL_ERROR;
			}
			fnum = Circuit2->file;
			result = Tcl_ListObjIndex(interp, objv, 1, &tobj);
			if (result != TCL_OK) return TCL_ERROR;
			break;
		    case CURRENT_IDX:
			if (CurrentCell == NULL) {
		            Tcl_SetResult(interp, "No current cell\n", NULL);
			    		return TCL_ERROR;
			}
			fnum = CurrentCell->file;
			result = Tcl_ListObjIndex(interp, objv, 1, &tobj);
			if (result != TCL_OK) return TCL_ERROR;
			break;
		    case WILDCARD_IDX:
			fnum = -2;
			result = Tcl_ListObjIndex(interp, objv, 1, &tobj);
			if (result != TCL_OK) return TCL_ERROR;
			break;
		}
	    }
	    else {
		Tcl_ResetResult(interp);
		fnum = -1;
	    }

	    /* Is 2nd argument an integer? */

	    if (fnum == -1) {
		result = Tcl_ListObjIndex(interp, objv, 1, &fobj);
		if (result != TCL_OK) return TCL_ERROR;

		result = Tcl_GetIntFromObj(interp, fobj, &ftest);
		if (result != TCL_OK) {
		    Tcl_ResetResult(interp);

		    /* Check if 2nd item is a reserved keyword */
		    if (Tcl_GetIndexFromObj(interp, fobj,
				(CONST84 char **)suboptions,
				"special", 0, &index) == TCL_OK) {
			switch (index) {
			    case CIRCUIT1_IDX:
				if (Circuit1 == NULL) {
				    Tcl_SetResult(interp, "No circuit has been"
						" declared for comparison\n", NULL);
				    return TCL_ERROR;
				}
				fnum = Circuit1->file;
				break;
			    case CIRCUIT2_IDX:
				if (Circuit2 == NULL) {
				    Tcl_SetResult(interp, "No circuit has been"
						" declared for comparison\n", NULL);
				    return TCL_ERROR;
				}
				fnum = Circuit2->file;
				break;
			    case CURRENT_IDX:
				if (CurrentCell == NULL) {
			            Tcl_SetResult(interp, "No current cell\n", NULL);
				    		return TCL_ERROR;
				}
				fnum = CurrentCell->file;
				break;
			    case WILDCARD_IDX:
				filename = NULL;
				fnum = -1;
				break;
			}
		    }
		    else { 
			Tcl_ResetResult(interp);
			filename = Tcl_GetString(fobj);
		    }

		    /* Okay, neither argument is an integer, so	*/
		    /* parse both as cell names and figure out	*/
		    /* which one is the same as the top level,	*/
		    /* and call that the filename.		*/
		}
		else {
		    filename = NULL;
		    fnum = ftest;
		}
	    }
	    else if (fnum == -2) {
		filename = NULL;
		fnum = -1;
	    }
	    else
		/* Both file numbers have been provided, so a	*/
		/* filename is not required.			*/
		filename = NULL;
	}
	else {
	    filename = NULL;
	    fnum = ftest;

	    result = Tcl_ListObjIndex(interp, objv, 1, &tobj);
	    if (result != TCL_OK) return TCL_ERROR;
	}
        cellname = Tcl_GetString(tobj);

	if (fnum == -1) {

	    /* If fnum is a wildcard, then we insist that there	*/
	    /* must be at least one cell matching the cellname,	*/
	    /* although the routines should be applied to all	*/
	    /* cells of the given name in all netlists.		*/

	    tp = LookupCell(cellname);
	    if (tp == NULL) {
		Tcl_SetResult(interp, "No such cellname!\n", NULL);
		return TCL_ERROR;
	    }
	    if (filename != NULL) {
		tp2 = LookupCell(filename);
		if (tp2 == NULL) {
		    Tcl_SetResult(interp, "No such cellname!\n", NULL);
		    return TCL_ERROR;
		}
	    }
	    else tp2 = NULL;

	    if (!(tp->flags & CELL_TOP)) {
		if ((tp2 != NULL) && !(tp2->flags & CELL_TOP)) {
		    // Error:  Neither name is a file!
		    Tcl_SetResult(interp, "No filename in list!\n", NULL);
		    return TCL_ERROR;
		}
		else if (tp2 != NULL) {
		    // tp2 is file top, tp is cell
		    fnum = tp2->file;
		    tp = LookupCellFile(cellname, fnum);
		    if (tp == NULL) {
			Tcl_SetResult(interp, "Cell is not in file!\n", NULL);
			return TCL_ERROR;
		    }
		}
	    }
	    else {
		// Arguments are reversed

		fnum = tp->file;
		tp = LookupCellFile(filename, fnum);
		if (tp == NULL) {
		    Tcl_SetResult(interp, "Cell is not in file!\n", NULL);
		    return TCL_ERROR;
		}
	    }
	}
	else {
	    /* File number was given, so just plug it in */	
	    tp = LookupCellFile(cellname, fnum);
	    if (tp == NULL) {
		Tcl_SetResult(interp, "No such cell or bad file number!\n", NULL);
		return TCL_ERROR;
	    }
	}

    } else {
	/* Only one name given;  check if it matches subOption */

	if (Tcl_GetIndexFromObj(interp, objv, (CONST84 char **)suboptions,
			"special", 0, &index) == TCL_OK) {

	    switch (index) {
		case CIRCUIT1_IDX:
		    if (Circuit1 == NULL) {
			Tcl_SetResult(interp, "No circuit has been"
				" declared for comparison\n", NULL);
			return TCL_ERROR;
		    }
		    tp = Circuit1;
		    fnum = Circuit1->file;
		    break;
		case CIRCUIT2_IDX:
		    if (Circuit2 == NULL) {
			Tcl_SetResult(interp, "No circuit has been"
				" declared for comparison\n", NULL);
			return TCL_ERROR;
		    }
		    tp = Circuit2;
		    fnum = Circuit2->file;
		    break;
		case CURRENT_IDX:
		    if (CurrentCell == NULL) {
		        Tcl_SetResult(interp, "No current cell\n", NULL);
			    return TCL_ERROR;
		    }
		    tp = CurrentCell;
		    fnum = CurrentCell->file;
		    break;
		case WILDCARD_IDX:
		    Tcl_SetResult(interp, "Wildcards must be used with "
				"a valid cellname\n", NULL);
		    return TCL_ERROR;
	    }
	}
	else {
	    Tcl_ResetResult(interp);

	    /* Check if it is a file number	*/

	    result = Tcl_GetIntFromObj(interp, objv, &fnum);
	    if (result != TCL_OK) {
		Tcl_ResetResult(interp);

		/* Only one name, which is a cellname.  If not a    */
		/* top-level cell, then it should be a unique name. */

		filename = Tcl_GetString(objv);
		tp = LookupCell(filename);
		if (tp == NULL) {
		    Tcl_SetResult(interp, "No such cell!\n", NULL);
		    return TCL_ERROR;
		}
		if (tp->flags & CELL_TOP)
		    fnum = tp->file;
		else
		    fnum = -1;	// Use wildcard
	    }
	    else {
		/* Given a file number, need to find the top-level cell */
		tp = GetTopCell(fnum);
		if (tp == NULL) {
		    Tcl_SetResult(interp, "No such file number!\n", NULL);
		    return TCL_ERROR;
		}
	    }
	}
    }

    *tpr = tp;
    *fnumptr = fnum;
    return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_canonical			*/
/* Syntax: netgen::canonical <valid_cellname>		*/
/* Formerly: (none)					*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_canonical(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int result;
    struct nlist *np;
    int filenum; 
    Tcl_Obj *lobj;

    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "valid_filename");
	return TCL_ERROR;
    }

    result = CommonParseCell(interp, objv[1], &np, &filenum);
    if (result != TCL_OK) return result;

    lobj = Tcl_NewListObj(0, NULL);

    Tcl_ListObjAppendElement(interp, lobj, Tcl_NewStringObj(np->name, -1));
    Tcl_ListObjAppendElement(interp, lobj, Tcl_NewIntObj(filenum));
    Tcl_SetObjResult(interp, lobj);

    return TCL_OK;
}

/*------------------------------------------------------*/
/* The following code breaks up the Query() command	*/
/* from query.c into individual functions w/arguments	*/
/*------------------------------------------------------*/

/*------------------------------------------------------*/
/* Function name: _netgen_readnet			*/
/* Syntax: netgen::readnet [format] <filename> [<fnum>]	*/
/* Formerly: read r, K, Z, G, and S			*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_readnet(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *formats[] = {
      "automatic", "ext", "extflat", "sim", "ntk", "spice",
      "verilog", "netgen", "actel", "xilinx", NULL
   };
   enum FormatIdx {
      AUTO_IDX, EXT_IDX, EXTFLAT_IDX, SIM_IDX, NTK_IDX,
      SPICE_IDX, VERILOG_IDX, NETGEN_IDX, ACTEL_IDX, XILINX_IDX
   };
   struct nlist *tc;
   int result, index, filenum = -1;
   char *retstr = NULL, *savstr = NULL;

   if (objc > 1) {

      /* If last argument is a number, then force file to belong to	*/
      /* the same netlist as everything else in "filenum".		*/

      if (Tcl_GetIntFromObj(interp, objv[objc - 1], &filenum) != TCL_OK) {
	 Tcl_ResetResult(interp);
	 filenum = -1;
      }
      else if (filenum < 0) {
	 Tcl_SetResult(interp, "Cannot use negative file number!", NULL);
	 return TCL_ERROR;
      }
      else {
	 objc--;
      }
   }

   if (objc == 1 || objc > 3) {
      Tcl_WrongNumArgs(interp, 1, objv, "?format? file ?filenum?");
      return TCL_ERROR;
   }
   else if (objc > 1) {
      if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)formats,
		"format", 0, &index) != TCL_OK) {
	 if (objc == 3)

	    return TCL_ERROR;
	 else {
	    Tcl_ResetResult(interp);
	    index = AUTO_IDX;
	 }
      }
   }

   switch (index) {
      case ACTEL_IDX:
      case XILINX_IDX:
	 if (objc != 2) {
	    Fprintf(stderr, "Warning: argument \"%s\" ignored.  Reading %s library.\n",
		Tcl_GetString(objv[2]), formats[index]);
	 }
	 break;

      case AUTO_IDX:
	 if (objc != 2) {
            Tcl_WrongNumArgs(interp, 1, objv, "file");
            return TCL_ERROR;
	 }
         retstr = Tcl_GetString(objv[1]);
	 break;

      default:
	 if (objc != 3) {
            Tcl_WrongNumArgs(interp, 1, objv, "format file");
            return TCL_ERROR;
	 }
         retstr = Tcl_GetString(objv[2]);
	 break;
   }

   if (retstr) savstr = STRDUP(retstr);

   // Check if the file is already loaded.

   tc = LookupCell(savstr);
   if (tc != NULL) {
      if ((filenum != -1) && (filenum != tc->file)) {
	 Tcl_SetResult(interp, "File is already loaded as a"
		" different file number.", NULL);
	 return TCL_ERROR;
      }
      filenum = tc->file;
   }
   else {

      switch(index) {
         case AUTO_IDX:
            retstr = ReadNetlist(savstr, &filenum);
            break;
         case EXT_IDX:
            retstr = ReadExtHier(savstr, &filenum);
            break;
         case EXTFLAT_IDX:
            retstr = ReadExtFlat(savstr, &filenum);
            break;
         case SIM_IDX:
            retstr = ReadSim(savstr, &filenum);
            break;
         case NTK_IDX:
            retstr = ReadNtk(savstr, &filenum);
            break;
         case SPICE_IDX:
            retstr = ReadSpice(savstr, &filenum);
            break;
         case VERILOG_IDX:
            retstr = ReadVerilog(savstr, &filenum);
            break;
         case NETGEN_IDX:
            retstr = ReadNetgenFile(savstr, &filenum);
            break;
         case ACTEL_IDX:
	    ActelLib();
	    retstr = formats[index];
	    break;
         case XILINX_IDX:
	    XilinxLib();
	    retstr = formats[index];
	    break;
      }
   }

   /* Return the file number to the interpreter */
   Tcl_SetObjResult(interp, Tcl_NewIntObj(filenum));

   if (savstr) FREE(savstr);
   return (retstr == NULL) ? TCL_ERROR : TCL_OK;
}

/*--------------------------------------------------------*/
/* Function name: _netgen_readlib			  */
/* Syntax: netgen::readlib <format> [<filename>] [<fnum>] */
/* Formerly: read X, A					  */
/* Results:						  */
/* Side Effects:					  */
/*--------------------------------------------------------*/

int
_netgen_readlib(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *formats[] = {
      "actel", "spice", "xilinx", NULL
   };
   enum FormatIdx {
      ACTEL_IDX, SPICE_IDX, XILINX_IDX
   };
   int result, index, fnum = -1;
   char *repstr;

   if (objc > 1) {

      /* If last argument is a number, then force file to belong to	*/
      /* the same netlist as everything else in "fnum".			*/

      if (Tcl_GetIntFromObj(interp, objv[objc - 1], &fnum) != TCL_OK) {
	 Tcl_ResetResult(interp);
	 fnum = -1;
      }
      else if (fnum < 0) {
	 Tcl_SetResult(interp, "Cannot use negative file number!", NULL);
	 return TCL_ERROR;
      }
      else {
	 objc--;
      }
   }

   if (objc == 1 || objc > 3) {
      Tcl_WrongNumArgs(interp, 1, objv, "format [file]");
      return TCL_ERROR;
   }
   if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)formats,
	"format", 0, &index) != TCL_OK) {
      return TCL_ERROR;
   }
   switch(index) {
      case ACTEL_IDX:
      case XILINX_IDX:
	 if (objc == 3) {
	    Tcl_WrongNumArgs(interp, 1, objv, "actel | xilinx");
	    return TCL_ERROR;
	 }
	 break;
      case SPICE_IDX:
	 if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 1, objv, "spice file");
	    return TCL_ERROR;
	 }
	 break;
   }

   switch(index) {
      case ACTEL_IDX:
         ActelLib();
         break;
      case SPICE_IDX:
	 repstr = Tcl_GetString(objv[2]);
         ReadSpiceLib(repstr, &fnum);
         break;
      case XILINX_IDX:
         XilinxLib();
         break;
   }

   Tcl_SetObjResult(interp, Tcl_NewIntObj(fnum));
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_writenet			*/
/* Syntax: netgen::write format cellname [filenum]	*/
/* Formerly: k, x, z, w, o, g, s, E, and C		*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_writenet(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *formats[] = {
      "ext", "sim", "ntk", "actel",
      "spice", "verilog", "wombat", "esacap", "netgen",
      "ccode", "xilinx", NULL
   };
   enum FormatIdx {
      EXT_IDX, SIM_IDX, NTK_IDX, ACTEL_IDX,
      SPICE_IDX, VERILOG_IDX, WOMBAT_IDX, ESACAP_IDX, NETGEN_IDX,
      CCODE_IDX, XILINX_IDX
   };
   int result, index, filenum;
   char *repstr;

   if (objc != 3 && objc != 4) {
      Tcl_WrongNumArgs(interp, 1, objv, "format file");
      return TCL_ERROR;
   }
   if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)formats,
		"format", 0, &index) != TCL_OK) {
      return TCL_ERROR;
   }
   repstr = Tcl_GetString(objv[2]);

   if (objc == 4) {
      result = Tcl_GetIntFromObj(interp, objv[3], &filenum);
      if (result != TCL_OK) return result;
   }
   else filenum = -1;

   switch(index) {
      case EXT_IDX:
         Ext(repstr, filenum);
         break;
      case SIM_IDX:
         Sim(repstr, filenum);
         break;
      case NTK_IDX:
         Ntk(repstr,"");
         break;
      case ACTEL_IDX:
	 if (ActelLibPresent() == 0) {
	    Fprintf(stderr, "Warning:  Actel library was not loaded.\n");
	    Fprintf(stderr, "Try \"readlib actel\" before reading the netlist.\n");
	 }
         Actel(repstr,"");
         break;
      case SPICE_IDX:
         SpiceCell(repstr, filenum, "");
         break;
      case VERILOG_IDX:
         VerilogTop(repstr, filenum, "");
         break;
      case WOMBAT_IDX:
         Wombat(repstr,NULL);
         break;
      case ESACAP_IDX:
         EsacapCell(repstr,"");
         break;
      case NETGEN_IDX:
         WriteNetgenFile(repstr,"");
         break;
      case CCODE_IDX:
         Ccode(repstr,"");
         break;
      case XILINX_IDX:
	 if (XilinxLibPresent() == 0) {
	    Fprintf(stderr, "Warning:  Xilinx library was not loaded.\n");
	    Fprintf(stderr, "Try \"readlib xilinx\" before reading the netlist.\n");
	 }
         Xilinx(repstr,"");
         break;
   }
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_flatten			*/
/* Syntax: netgen::flatten mode				*/
/* Formerly: f and F					*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_flatten(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *repstr, *file;
   int result, llen, filenum;
   struct nlist *tp, *tp2;

   if ((objc < 2) || (objc > 4)) {
      Tcl_WrongNumArgs(interp, 1, objv, "?class? valid_cellname");
      return TCL_ERROR;
   }

   result = CommonParseCell(interp, objv[objc - 1], &tp, &filenum);
   if (result != TCL_OK) return result;
   repstr = tp->name;

   if (objc >= 3) {
      char *argv = Tcl_GetString(objv[1]);
      if (!strcmp(argv, "class")) {
	 tp = GetTopCell(filenum);

	 if (objc == 4) {
	    int numflat;
	    tp2 = LookupCellFile(Tcl_GetString(objv[2]), filenum);
	    if (tp2 == NULL) {
		Tcl_SetResult(interp, "No such cell.", NULL);
		return TCL_ERROR;
	    }
	    else {
	        Printf("Flattening instances of %s in cell %s within file %s\n",
			repstr, tp2->name, tp->name);
		numflat = flattenInstancesOf(tp2->name, filenum, repstr);
		if (numflat == 0) {
		   Tcl_SetResult(interp, "No instances found to flatten.", NULL);
		   return TCL_ERROR;
		}
	    }
	 }
	 else {
	    Printf("Flattening instances of %s in file %s\n", repstr, tp->name);
            FlattenInstancesOf(repstr, filenum);
	 }
      }
      else if (!strcmp(argv, "prohibit") || !strcmp(argv, "deny")) {
	 tp = GetTopCell(filenum);
	 Printf("Will not flatten instances of %s in file %s\n", repstr, tp->name);
	 /* Mark cell as placeholder so it will not be flattened */
	 tp->flags |= CELL_PLACEHOLDER;
      }
      else {
	 Tcl_WrongNumArgs(interp, 1, objv, "class valid_cellname");
	 return TCL_ERROR;
      }
   }
   else {
      Printf("Flattening contents of cell %s\n", repstr);
      Flatten(repstr, filenum);
   }
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/* Function name: _netgen_nodes					*/
/* Syntax: netgen::nodes [-list <element>] [<valid_cellname>]	*/
/* Formerly: n and N						*/
/* Results:							*/
/* Side Effects:						*/
/*--------------------------------------------------------------*/

int
_netgen_nodes(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *estr = NULL, *istr = NULL, *cstr, *fstr;
   char *optstart;
   int dolist = 0;
   int fnum, result;
   struct nlist *np = NULL;

   if (objc > 1) {
      optstart = Tcl_GetString(objv[1]);
      if (*optstart == '-') optstart++;
      if (!strcmp(optstart, "list")) {
	 dolist = 1;
	 objv++;
	 objc--;
      }
   }

   if ((objc < 1 || objc > 3) || (objc == 2)) {
      Tcl_WrongNumArgs(interp, 1, objv, "?element? ?cell file?");
      return TCL_ERROR;
   }

   if (objc == 1) {
      if (CurrentCell == NULL) {
	 Tcl_WrongNumArgs(interp, 1, objv, "(cell name required)");
	 return TCL_ERROR;
      }
      cstr = CurrentCell->name;
      fnum = CurrentCell->file;
   }
   else {
      result = CommonParseCell(interp, objv[objc - 1], &np, &fnum);
      if (result != TCL_OK) return result;

      cstr = np->name;
      // If element was specified:
      if (objc == 3) estr = Tcl_GetString(objv[objc - 2]);
   }

   if (estr) {
      if (*estr != '/') {
	 istr = (char *)Tcl_Alloc(strlen(estr) + 2);
	 sprintf(istr, "/%s", estr);
	 estr = istr;
      }
   }

   if (estr) {
      if (dolist) {
	 struct objlist *ob, *nob;
	 Tcl_Obj *lobj, *pobj;
	 int ckto;

	 if (np == NULL) np = LookupCellFile(cstr, fnum);

	 if (np == NULL) {
	    Tcl_SetResult(interp, "No such cell.", NULL);
	    if (istr) Tcl_Free(istr);
	    return TCL_ERROR;
	 }

	 ckto = strlen(estr);
	 for (ob = np->cell; ob != NULL; ob = ob->next) {
	    if (!strncmp(estr, ob->name, ckto)) {
	       if (*(ob->name + ckto) == '/' || *(ob->name + ckto) == '\0')
		  break;
	    }
	 }
	 if (ob == NULL) {
	    Tcl_SetResult(interp, "No such element.", NULL);
	    if (istr) Tcl_Free(istr);
	    return TCL_ERROR;
	 }
	 lobj = Tcl_NewListObj(0, NULL);
	 for (; ob != NULL; ob = ob->next) {
	    if (!strncmp(estr, ob->name, ckto)) {
	       if (*(ob->name + ckto) != '/' && *(ob->name + ckto) != '\0')
		  continue;

	       pobj = Tcl_NewListObj(0, NULL);
               Tcl_ListObjAppendElement(interp, pobj,
			Tcl_NewStringObj(ob->name + ckto + 1, -1));

	       for (nob = np->cell; nob != NULL; nob = nob->next) {
		  if (nob->node == ob->node) {
		     if (nob->type < FIRSTPIN) {
                        Tcl_ListObjAppendElement(interp, pobj,
				Tcl_NewStringObj(nob->name, -1));
		        break;
		     }
		  }
	       }
               Tcl_ListObjAppendElement(interp, lobj, pobj);
	    }
	 }
	 Tcl_SetObjResult(interp, lobj);
      }
      else
         ElementNodes(cstr, estr, fnum);
   }
   else
      PrintNodes(cstr, fnum);
  
   if (istr) Tcl_Free(istr);
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_elements			*/
/* Syntax: netgen::elements [-list <node>] [<cell>]	*/
/* Formerly: e						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_elements(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *nstr = NULL, *cstr;
   struct objlist * (*ListSave)();
   char *optstart;
   int dolist = 0;
   int fnum = -1;
   int result;
   struct nlist *np = NULL;

   if (objc > 1) {
      optstart = Tcl_GetString(objv[1]);
      if (*optstart == '-') optstart++;
      if (!strcmp(optstart, "list")) {
	 dolist = 1;
	 objv++;
	 objc--;
      }
   }
    
   if (objc < 1 || objc > 2) {
      Tcl_WrongNumArgs(interp, 1, objv, "?node? valid_cellname");
      return TCL_ERROR;
   }

   if (objc == 1) {
      if (CurrentCell == NULL) {
	 Tcl_WrongNumArgs(interp, 1, objv, "(cell name required)");
	 return TCL_ERROR;
      }
      cstr = CurrentCell->name;
   }
   else {
      result = CommonParseCell(interp, objv[objc - 1], &np, &fnum);
      if (result != TCL_OK) return result;

      cstr = np->name;
      if (objc == 3)
	 nstr = Tcl_GetString(objv[1]);
   }

   if (nstr) {
      if (dolist) {
	 struct objlist *ob;
	 Tcl_Obj *lobj;
	 int nodenum;

	 if (np == NULL) np = LookupCellFile(cstr, fnum);

	 if (np == NULL) {
	    Tcl_SetResult(interp, "No such cell.", NULL);
	    return TCL_ERROR;
	 }

	 for (ob = np->cell; ob != NULL; ob = ob->next) {
	    if (match(nstr, ob->name)) {
	       nodenum = ob->node;
	       break;
	    }
	 }
	 if (ob == NULL) {
	    Tcl_SetResult(interp, "No such node.", NULL);
	    return TCL_ERROR;
	 }
	 lobj = Tcl_NewListObj(0, NULL);
	 for (ob = np->cell; ob != NULL; ob = ob->next) {
	    if (ob->node == nodenum && ob->type >= FIRSTPIN) {
	       char *obname = ob->name;
	       if (*obname == '/') obname++;
               Tcl_ListObjAppendElement(interp, lobj,
			Tcl_NewStringObj(obname, -1));
	    }
	 }
	 Tcl_SetObjResult(interp, lobj);
      }
      else
         Fanout(cstr, nstr, ALLELEMENTS);
   }
   else {
      PrintAllElements(cstr, fnum);
   }

   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_debug				*/
/* Syntax: netgen::debug [on|off] or debug command	*/
/* Formerly: D						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_debug(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *yesno[] = {
      "on", "off", NULL
   };
   enum OptionIdx {
      YES_IDX, NO_IDX, CMD_IDX
   };
   int result, index;
   char *command;

   if (objc == 1)
      index = YES_IDX;
   else {
      if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)yesno,
		"option", 0, &index) != TCL_OK) {
         index = CMD_IDX;
      }
   }

   switch(index) {
      case YES_IDX:
	 Debug = TRUE;
	 break;
      case NO_IDX:
	 Debug = FALSE;
	 break;
      case CMD_IDX:
	 /* Need to redefine DBUG_PUSH! */
	 command = Tcl_GetString(objv[1]);
	 DBUG_PUSH(command);
   }

   if (index != CMD_IDX)
      Printf("Debug mode is %s\n", Debug?"ON":"OFF");

   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_protochip			*/
/* Syntax: netgen::protochip				*/
/* Formerly: P						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_protochip(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   if (objc != 1) {
      Tcl_WrongNumArgs(interp, 1, objv, "(no arguments)");
      return TCL_ERROR;
   }
   PROTOCHIP();
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_instances			*/
/* Syntax: netgen::instances valid_cellname		*/
/* Formerly: i						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_instances(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *repstr;
   int result;
   int fnum = -1;
   struct nlist *np = NULL;

   if (objc != 2) {
      Tcl_WrongNumArgs(interp, 1, objv, "valid_cellname");
      return TCL_ERROR;
   }

   result = CommonParseCell(interp, objv[1], &np, &fnum);
   if (result != TCL_OK) return result;

   repstr = np->name;
   PrintInstances(repstr, fnum);
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_contents			*/
/* Syntax: netgen::contents valid_cellname		*/
/* Formerly: c						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_contents(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *repstr;
   int result;
   int fnum = -1;
   struct nlist *np = NULL;

   if (objc != 2) {
      Tcl_WrongNumArgs(interp, 1, objv, "valid_cellname");
      return TCL_ERROR;
   }
   result = CommonParseCell(interp, objv[1], &np, &fnum);
   if (result != TCL_OK) return result;

   repstr = np->name;
   PrintCell(repstr, fnum);
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_describe			*/
/* Syntax: netgen::describe valid_cellname		*/
/* Formerly: d						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_describe(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *repstr;
   int file = -1;
   int result;
   struct nlist *np = NULL;

   if (objc != 2) {
      Tcl_WrongNumArgs(interp, 1, objv, "valid_cellname");
      return TCL_ERROR;
   }

   result = CommonParseCell(interp, objv[1], &np, &file);
   if (result != TCL_OK) return result;

   repstr = np->name;
   DescribeInstance(repstr, file);
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_cells				*/
/* Syntax: netgen::cells [list|all] [valid_filename]	*/
/* Formerly: h and H					*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_cells(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *repstr, *filename = NULL;
   char *optstart;
   int filenum = -1;
   struct nlist *np = NULL;
   int result, printopt, dolist = 0, doall = 0, dotop = 0;

   while (objc > 1) {
      optstart = Tcl_GetString(objv[1]);
      if (*optstart == '-') optstart++;
      if (!strcmp(optstart, "list")) {
	 dolist = 1;
	 objv++;
	 objc--;
      }
      else if (!strcmp(optstart, "all")) {
	 doall = 1;
	 objv++;
	 objc--;
      }
      else if (!strcmp(optstart, "top")) {
	 dotop = 1;
	 objv++;
	 objc--;
      }
      else {
	 result = CommonParseCell(interp, objv[1], &np, &filenum);
	 if (result != TCL_OK) return result;
	 objv++;
	 objc--;
      }
   }

   if (objc != 1) {
      Tcl_WrongNumArgs(interp, 1, objv, "[list] [-top] [-all] [valid_filename]");
      return TCL_ERROR;
   }
   else {
      Tcl_Obj *lobj;

      if (dotop) {
	 if (dolist)
	     lobj = Tcl_NewListObj(0, NULL);
	 else
	     Fprintf(stdout, "Top level cells: ");
	 np = FirstCell();
	 while (np != NULL) {
	    if ((np->flags & CELL_TOP) && ((filenum == -1) ||
			(np->file == filenum))) {
		
		if (dolist)
		    Tcl_ListObjAppendElement(interp, lobj,
			Tcl_NewStringObj(np->name, -1));
		else
		    Fprintf(stdout, "%s ", np->name);
	    }
	    np = NextCell();
	 }
	 if (dolist)
	    Tcl_SetObjResult(interp, lobj);
	 else
	    Fprintf(stdout, "\n");

	 return TCL_OK;
      }
      else {
	 if (dolist)
	    printopt = (doall) ? 3 : 2;
	 else
	    printopt = (doall) ? 1 : 0;
         PrintCellHashTable(printopt, filenum);
      }
   }
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_model				*/
/* Syntax: netgen::model valid_cellname class		*/
/* Formerly: (nothing)					*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_model(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   struct nlist *tp, *tp2;
   char *model, *retclass;
   unsigned char class;
   int fnum = -1;
   int result, index, nports, nports2;

   char *modelclasses[] = {
      "undefined", "nmos", "pmos", "pnp", "npn",
      "resistor", "capacitor", "diode",
      "inductor", "module", "blackbox", "xline",
      "moscap", "mosfet", "bjt", "subcircuit", "copy",
      NULL
   };
   enum OptionIdx {
      UNDEF_IDX,  NMOS_IDX, PMOS_IDX, PNP_IDX, NPN_IDX,
      RES_IDX, CAP_IDX, DIODE_IDX, INDUCT_IDX,
      MODULE_IDX, BLACKBOX_IDX, XLINE_IDX, MOSCAP_IDX,
      MOSFET_IDX, BJT_IDX, SUBCKT_IDX, COPY_IDX
   };

   if (objc != 3 && objc != 2) {
      Tcl_WrongNumArgs(interp, 1, objv, "valid_cellname [class]");
      return TCL_ERROR;
   }

   /* Check for "model blackbox on|off"	*/
   /* Behavior is to treat empty subcircuits as blackbox cells */

   if ((objc > 1) && !strcmp(Tcl_GetString(objv[1]), "blackbox")) {
      if ((objc > 2) && !strcmp(Tcl_GetString(objv[2]), "on")) {
	 auto_blackbox = TRUE;
	 return TCL_OK;
      }
      else if ((objc > 2) && !strcmp(Tcl_GetString(objv[2]), "off")) {
	 auto_blackbox = FALSE;
	 return TCL_OK;
      }
      else if (objc == 2) {
	 Tcl_SetObjResult(interp, Tcl_NewBooleanObj(auto_blackbox));
	 return TCL_OK;
      }
   }

   result = CommonParseCell(interp, objv[1], &tp, &fnum);
   if (result != TCL_OK)
      return result;

   if (objc == 3) {
      model = Tcl_GetString(objv[2]);
      nports = NumberOfPorts(model, fnum);

      if (Tcl_GetIndexFromObj(interp, objv[2], (CONST84 char **)modelclasses,
		"class", 0, &index) != TCL_OK) {
	 return TCL_ERROR;
      }
      switch (index) {
	 case UNDEF_IDX:
	    class = CLASS_UNDEF;
	    break;
	 case NMOS_IDX:
	    if (nports != 4 && nports != 3) goto wrongNumPorts;
	    class = (nports == 4) ? CLASS_NMOS4 : CLASS_NMOS;
	    break;
	 case PMOS_IDX:
	    if (nports != 4 && nports != 3) goto wrongNumPorts;
	    class = (nports == 4) ? CLASS_PMOS4 : CLASS_PMOS;
	    break;
	 case PNP_IDX:
	    if (nports != 3) goto wrongNumPorts;
	    class = CLASS_PNP;
	    break;
	 case NPN_IDX:
	    if (nports != 3) goto wrongNumPorts;
	    class = CLASS_NPN;
	    break;
	 case RES_IDX:
	    if (nports != 2 && nports != 3) goto wrongNumPorts;
	    class = (nports == 2) ? CLASS_RES : CLASS_RES3;
	    break;
	 case CAP_IDX:
	    if (nports != 2 && nports != 3) goto wrongNumPorts;
	    class = (nports == 2) ? CLASS_CAP : CLASS_CAP3;
	    break;
	 case DIODE_IDX:
	    if (nports != 2) goto wrongNumPorts;
	    class = CLASS_DIODE;
	    break;
	 case INDUCT_IDX:
	    if (nports != 2) goto wrongNumPorts;
	    class = CLASS_INDUCTOR;
	    break;
	 case XLINE_IDX:
	    if (nports != 4) goto wrongNumPorts;
	    class = CLASS_XLINE;
	    break;
	 case BJT_IDX:
	    if (nports != 3) goto wrongNumPorts;
	    class = CLASS_BJT;
	    break;
	 case MOSFET_IDX:
	    if (nports != 4 && nports != 3) goto wrongNumPorts;
	    class = (nports == 4) ? CLASS_FET4 : CLASS_FET;
	    break;
	 case MOSCAP_IDX:
	    if (nports != 3) goto wrongNumPorts;
	    class = CLASS_ECAP;
	    break;
	 case MODULE_IDX:
	 case BLACKBOX_IDX:
	    class = CLASS_MODULE;
	    break;
	 case SUBCKT_IDX:
	    class = CLASS_SUBCKT;
	    break;
	 case COPY_IDX:
	    /* "copy" is not a class, but indicates that the cell,  */
	    /* if undefined or a module, should have its class	    */
	    /* taken from the other circuit, if that circuit has a  */
	    /* cell of the same name.				    */
	    if (Circuit1 == NULL || Circuit2 == NULL) {
		Tcl_SetResult(interp, "Circuits have not been queued for comparison.",
			NULL);
		return TCL_ERROR;
	    }
	    if (tp == Circuit1) {
		tp2 = LookupCellFile(tp->name, Circuit2->file);
		nports2 = NumberOfPorts(tp2->name, Circuit2->file);
	    }
	    else if (tp == Circuit2) {
		tp2 = LookupCellFile(tp->name, Circuit1->file);
		nports2 = NumberOfPorts(tp2->name, Circuit1->file);
	    }
	    else {
		Tcl_SetResult(interp, "The referenced netlist is not being compared.",
			NULL);
		return TCL_ERROR;
	    }
	    /* Should a non-matching number of ports be considered a fatal error? */
	    // if (nports2 != nports) {
	    //	Tcl_SetResult(interp, "The number of ports for this cell does not "
	    //		"match between netlists.", NULL);
	    //	return TCL_ERROR;
	    // }

	    class = tp2->class;
	    /* To do (maybe): Rename tp ports to match tp2? */

	    break;
      }
      tp->class = class;
   }
   else {
      class = tp->class;

      switch (class) {
	 case CLASS_NMOS: case CLASS_NMOS4:
	    retclass = modelclasses[NMOS_IDX];
	    break;

	 case CLASS_PMOS: case CLASS_PMOS4:
	    retclass = modelclasses[PMOS_IDX];
	    break;

	 case CLASS_FET3: case CLASS_FET4: case CLASS_FET:
	    retclass = "mosfet";
	    break;

	 case CLASS_BJT:
	    retclass = "bipolar";
	    break;

	 case CLASS_NPN:
	    retclass = modelclasses[NPN_IDX];
	    break;

	 case CLASS_PNP:
	    retclass = modelclasses[PNP_IDX];
	    break;

	 case CLASS_RES: case CLASS_RES3:
	    retclass = modelclasses[RES_IDX];
	    break;

	 case CLASS_CAP: case CLASS_ECAP: case CLASS_CAP3:
	    retclass = modelclasses[CAP_IDX];
	    break;

	 case CLASS_SUBCKT:
	    retclass = modelclasses[SUBCKT_IDX];
	    break;

	 case CLASS_MODULE:
	    if (auto_blackbox)
		retclass = modelclasses[BLACKBOX_IDX];
	    else
		retclass = modelclasses[MODULE_IDX];
	    break;

	 default: /* (includes case CLASS_UNDEF) */
	    retclass = modelclasses[UNDEF_IDX];
	    break;
      }
      Tcl_SetResult(interp, retclass, NULL);
   }
   return TCL_OK;

wrongNumPorts:
   Tcl_SetResult(interp, "Wrong number of ports for device", NULL);
   return TCL_ERROR;
}

/*------------------------------------------------------*/
/* Function name: _netgen_ports				*/
/* Syntax: netgen::ports cell				*/
/* Formerly: p						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_ports(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *repstr;
   int result;
   struct nlist *np;
   int filenum = -1;

   if (objc != 2) {
      Tcl_WrongNumArgs(interp, 1, objv, "valid_cellname");
      return TCL_ERROR;
   }

   result = CommonParseCell(interp, objv[1], &np, &filenum);
   if (result != TCL_OK) return result;
   repstr = np->name;

   PrintPortsInCell(repstr, filenum);
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_leaves			*/
/* Syntax: netgen::leaves [valid_cellname]		*/
/* Formerly: l and L					*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_leaves(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *repstr;
   int result;
   int filenum = -1;
   struct nlist *np;

   if (objc != 1 && objc != 2) {
      Tcl_WrongNumArgs(interp, 1, objv, "[valid_cellname]");
      return TCL_ERROR;
   }
   if (objc == 1) {
      Printf("List of all leaf cells:\n");
      PrintAllLeaves();
   }
   else {
      result = CommonParseCell(interp, objv[1], &np, &filenum);
      if (result != TCL_OK) return result;

      repstr = np->name;
      ClearDumpedList();
      PrintLeavesInCell(repstr, filenum);
   }
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_quit				*/
/* Syntax: netgen::quit					*/
/* Formerly: q and Q					*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_quit(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   if (objc != 1) {
      Tcl_WrongNumArgs(interp, 1, objv, "(no arguments)");
      return TCL_ERROR;
   }

   /* Call tkcon's exit routine, which will make sure	*/
   /* the history file is updated before final exit.	*/

   if (consoleinterp == interp)
      Tcl_Exit(TCL_OK);
   else
      Tcl_Eval(interp, "catch {tkcon eval exit}\n");

   return TCL_OK; 	/* Not reached */
}

/*------------------------------------------------------*/
/* Function name: _netgen_reinit			*/
/* Syntax: netgen::reinitialize				*/
/* Formerly: I						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_reinit(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   if (objc != 1) {
      Tcl_WrongNumArgs(interp, 1, objv, "(no arguments)");
      return TCL_ERROR;
   }
   Initialize();
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_log				*/
/* Syntax: netgen::log [option...]			*/
/* Formerly: (xnetgen command only)			*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_log(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *yesno[] = {
      "start", "end", "reset", "suspend", "resume", "file", "echo", "put", NULL
   };
   enum OptionIdx {
      START_IDX, END_IDX, RESET_IDX, SUSPEND_IDX, RESUME_IDX, FILE_IDX,
	ECHO_IDX, PUT_IDX
   };
   int result, index, i;
   char *tmpstr;
   FILE *file;

   if (objc == 1) {
      index = (LoggingFile) ? RESUME_IDX : START_IDX;
   }
   else {
      if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)yesno,
		"option", 0, &index) != TCL_OK) {
	 return TCL_ERROR;
      }
   }

   switch(index) {
      case START_IDX:
      case RESUME_IDX:
	 if (LoggingFile) {
	    Tcl_SetResult(interp, "Already logging output.", NULL);
	    return TCL_ERROR;
	 }
	 break;
      case END_IDX:
      case RESET_IDX:
      case SUSPEND_IDX:
	 if (!LoggingFile) {
	    Tcl_SetResult(interp, "Not logging data.", NULL);
	    return TCL_ERROR;
	 }
	 /* Don't leave echo off if we're stopping the log */
	 if (NoOutput) NoOutput = FALSE;
	 break;
   }

   switch(index) {
      case START_IDX:
      case RESUME_IDX:
      case RESET_IDX:
	 if (LogFileName == NULL) {
	    Tcl_SetResult(interp, "No log file declared.  "
			"Use \"log file <name>\"", NULL);
	    return TCL_ERROR;
	 }
	 break;
   }

   switch(index) {
      case START_IDX:
	 LoggingFile = fopen(LogFileName, "w");
	 if (!LoggingFile) {
	    Tcl_SetResult(interp, "Could not open log file.", NULL);
	    return TCL_ERROR;
	 }
	 break;
      case RESUME_IDX:
	 LoggingFile = fopen(LogFileName, "a");
	 if (!LoggingFile) {
	    Tcl_SetResult(interp, "Could not open log file.", NULL);
	    return TCL_ERROR;
	 }
	 break;
      case END_IDX:
	 fclose(LoggingFile);
	 LoggingFile = FALSE;
	 break;
      case RESET_IDX:
	 fclose(LoggingFile);
	 LoggingFile = fopen(LogFileName, "w");
	 if (!LoggingFile) {
	    Tcl_SetResult(interp, "Could not open log file.", NULL);
	    return TCL_ERROR;
	 }
	 break;
      case SUSPEND_IDX:
	 fclose(LoggingFile);
	 LoggingFile = FALSE;
	 break;
      case FILE_IDX:
	 if (objc == 2)
	    Tcl_SetResult(interp, LogFileName, NULL);
	 else {
	    if (LoggingFile) {
	       fclose(LoggingFile);
	       LoggingFile = FALSE;
	       Printf("Closed old log file \"%s\".\n", LogFileName);
	    }
	    tmpstr = Tcl_GetString(objv[2]);
	    if (LogFileName) Tcl_Free(LogFileName);
	    LogFileName = (char *)Tcl_Alloc(1 + strlen(tmpstr));
	    strcpy(LogFileName, tmpstr);
	 }
	 break;
      case PUT_IDX:
	 // All arguments after "log put" get sent to stdout through Tcl,
	 // and also to the logfile, if the logfile is enabled.
	 for (i = 2; i < objc; i++) {
	    Fprintf(stdout, Tcl_GetString(objv[i]));
	 }
	 if (!NoOutput) Printf("\n");
	 return TCL_OK;
      case ECHO_IDX:
	 if (objc == 2) {
	    Tcl_SetResult(interp, (NoOutput) ? "off" : "on", NULL);
	 }
	 else {
	    int bval;
	    result = Tcl_GetBooleanFromObj(interp, objv[2], &bval);
	    if (result == TCL_OK)
	       NoOutput = (bval) ? FALSE : TRUE;
	    else
	       return result;
	 }
	 if (Debug)
            Printf("Echoing log file \"%s\" output to console %s\n",
			LogFileName, (NoOutput) ? "disabled" : "enabled");
	 return TCL_OK;
   }
   if ((index != FILE_IDX) && (index != ECHO_IDX))
      Printf("Logging to file \"%s\" %s\n", LogFileName,
		(LoggingFile) ? "enabled" : "disabled");

   return TCL_OK;
}

#ifdef HAVE_MALLINFO
/*------------------------------------------------------*/
/* Function name: _netgen_printmem			*/
/* Syntax: netgen::memory				*/
/* Formerly: m						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_printmem(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   if (objc != 1) {
      Tcl_WrongNumArgs(interp, 1, objv, "(no arguments)");
      return TCL_ERROR;
   }
   PrintMemoryStats();
   return TCL_OK;
}
#endif

/*------------------------------------------------------*/
/* Function name: _netcmp_format			*/
/* Syntax:						*/
/*    netgen::format [col1_width [col2_width]]		*/
/* Formerly: (none)					*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netcmp_format(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int col1_width = 41, col2_width = 41;

    if (objc > 1) {
	if (Tcl_GetIntFromObj(interp, objv[1], &col1_width) != TCL_OK)
	    return TCL_ERROR;
	if (objc > 2) {
	    if (Tcl_GetIntFromObj(interp, objv[2], &col2_width) != TCL_OK)
		return TCL_ERROR;
	} else {
	    /* If only one argument is given, then apply it to both columns */
	    col2_width = col1_width;
	}

	if (col1_width <= 0 || col2_width <= 0) {
	    Tcl_SetResult(interp, "Column width cannot be zero or less\n", NULL);
	}

	// Default values for left and right columns are 43 and 87
	left_col_end = col1_width + 2;
	right_col_end = left_col_end + col2_width + 3;
    }
    else if (objc == 1) {
	Tcl_Obj *lobj, *tobj;

	col1_width = left_col_end - 2;
	col2_width = right_col_end - col1_width - 5;

	lobj = Tcl_NewListObj(0, NULL);

	tobj = Tcl_NewIntObj(col1_width);
        Tcl_ListObjAppendElement(interp, lobj, Tcl_NewIntObj(col1_width));
        Tcl_ListObjAppendElement(interp, lobj, Tcl_NewIntObj(col2_width));

	Tcl_SetObjResult(interp, lobj);
	return TCL_OK;
    }
    else {
	Tcl_WrongNumArgs(interp, 1, objv, "[col1_width [col2_width]]");
	return TCL_ERROR;
    }
    return TCL_OK;
}

/*------------------------------------------------------*/
/* The following code breaks up the NETCOMP() command	*/
/* from netcmp.c into individual functions w/arguments	*/
/*------------------------------------------------------*/

/*------------------------------------------------------*/
/* Function name: _netcmp_compare			*/
/* Syntax:						*/
/*    netgen::compare valid_cellname1 valid_cellname2	*/
/* Formerly: c						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netcmp_compare(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *name1, *name2, *file1, *file2, *optstart;
   int fnum1, fnum2, dolist = 0;
   int dohierarchy = FALSE;
   int assignonly = FALSE;
   int argstart = 1, qresult, llen, result;
   int hascontents1, hascontents2;
   struct Correspond *nextcomp;
   struct nlist *tp1 = NULL, *tp2 = NULL;
   Tcl_Obj *flist = NULL;

   if (objc > 1) {
      optstart = Tcl_GetString(objv[1]);
      if (*optstart == '-') optstart++;
      if (!strcmp(optstart, "list")) {
	 dolist = 1;
	 objv++;
	 objc--;
      }
   }

   if (objc > 1) {
      if (!strncmp(Tcl_GetString(objv[argstart]), "assign", 6)) {
	 assignonly = TRUE;
 	 argstart++;
      }
      else if (!strncmp(Tcl_GetString(objv[argstart]), "hier", 4)) {
	 dohierarchy = TRUE;
 	 argstart++;
      }
   }

   fnum1 = -1;
   fnum2 = -1;

   if (((objc - argstart) == 2) || ((dohierarchy && ((objc - argstart) == 0)))) {

      if (dohierarchy && ((objc - argstart) == 0)) {

         qresult = GetCompareQueueTop(&name1, &fnum1, &name2, &fnum2);
         if (qresult == -1) {
	    Tcl_Obj *lobj;

	    // When queue is empty, return a null list 
	    lobj = Tcl_NewListObj(0, NULL);
	    Tcl_SetObjResult(interp, lobj);
	    return TCL_OK;
         }
      }
      else if ((objc - argstart) == 2) {

	 result = CommonParseCell(interp, objv[argstart], &tp1, &fnum1);
         if (result != TCL_OK) return TCL_ERROR;
	 else if (fnum1 == -1) {
	    Tcl_SetResult(interp, "Cannot use wildcard with compare command.\n",
			NULL);
	    return TCL_ERROR;
	 }
         name1 = tp1->name;
	 argstart++;

	 result = CommonParseCell(interp, objv[argstart], &tp2, &fnum2);
         if (result != TCL_OK) return TCL_ERROR;
	 else if (fnum2 == -1) {
	    Tcl_SetResult(interp, "Cannot use wildcard with compare command.\n",
			NULL);
	    return TCL_ERROR;
	 }
         name2 = tp2->name;

         if (dohierarchy) {
	    RemoveCompareQueue();
	    qresult = CreateCompareQueue(name1, fnum1, name2, fnum2);
	    if (qresult != 0) {
	       Tcl_AppendResult(interp, "No such cell ",
			(qresult == 1) ? name1 : name2, NULL);
	       return TCL_ERROR;
	    }
	    GetCompareQueueTop(&name1, &fnum1, &name2, &fnum2);
         }
	 else if (assignonly) {
	    AssignCircuits(name1, fnum1, name2, fnum2);
	    return TCL_OK;
	 }
      }
   }
   else {
      Tcl_WrongNumArgs(interp, 1, objv,
		"[hierarchical] valid_cellname1 valid_cellname2");
      return TCL_ERROR;
   }

   if (fnum1 == fnum2) {
      Tcl_SetResult(interp, "Cannot compare two cells in the same netlist.",
		NULL);
      return TCL_ERROR;
   }

   UniquePins(name1, fnum1);		// Check for and remove duplicate pins
   UniquePins(name2, fnum2);		// Check for and remove duplicate pins

   // Resolve global nodes into local nodes and ports
   if (dohierarchy) {
      ConvertGlobals(name1, fnum1);
      ConvertGlobals(name2, fnum2);
   }

   tp1 = LookupCellFile(name1, fnum1);
   tp2 = LookupCellFile(name2, fnum2);

   hascontents1 = HasContents(tp1);
   hascontents2 = HasContents(tp2);

   if (hascontents1 && !hascontents2 && (tp2->flags & CELL_PLACEHOLDER)) {
       Fprintf(stdout, "\nCircuit 2 cell %s is a black box; will not flatten "
                        "Circuit 1\n", name2);
   }
   else if (hascontents2 && !hascontents1 && (tp1->flags & CELL_PLACEHOLDER)) {
       Fprintf(stdout, "\nCircuit 1 cell %s is a black box; will not flatten "
                        "Circuit 2\n", name1);
   }
   else if (!hascontents1 && !hascontents2 && (tp1->flags & CELL_PLACEHOLDER)
		&& (tp2->flags & CELL_PLACEHOLDER)) {
       /* Two empty subcircuits, don't flatten anything */
       Fprintf(stdout, "\nCircuit 1 cell %s and Circuit 2 cell %s are black"
			" boxes.\n", name1, name2);
   }
   else {
       FlattenUnmatched(tp1, name1, 1, 0);
       FlattenUnmatched(tp2, name2, 1, 0);
       DescribeContents(name1, fnum1, name2, fnum2);

       while (PrematchLists(name1, fnum1, name2, fnum2) > 0) {
          Fprintf(stdout, "Making another compare attempt.\n");
          Printf("Flattened mismatched instances and attempting compare again.\n");
          FlattenUnmatched(tp1, name1, 1, 0);
          FlattenUnmatched(tp2, name2, 1, 0);
          DescribeContents(name1, fnum1, name2, fnum2);
       }
   }
   CreateTwoLists(name1, fnum1, name2, fnum2, dolist);

   // Return the names of the two cells being compared, if doing "compare
   // hierarchical".  If "-list" was specified, then append the output
   // to the end of the list.

   if (dohierarchy) {
      Tcl_Obj *lobj;

      lobj = Tcl_NewListObj(0, NULL);
      Tcl_ListObjAppendElement(interp, lobj, Tcl_NewStringObj(name1, -1));
      Tcl_ListObjAppendElement(interp, lobj, Tcl_NewStringObj(name2, -1));
      Tcl_SetObjResult(interp, lobj);
   }

#ifdef DEBUG_ALLOC
   PrintCoreStats();
#endif

   /* Arrange properties in the two compared cells */
   /* ResolveProperties(name1, fnum1, name2, fnum2); */

   Permute();		/* Apply permutations */
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_iterate			*/
/* Syntax: netgen::iterate				*/
/* Formerly: i						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netcmp_iterate(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   if (objc != 1) {
      Tcl_WrongNumArgs(interp, 1, objv, "(no arguments)");
      return TCL_ERROR;
   }
   if (!Iterate())
      Printf("Please iterate again.\n");
   else
      Printf("No fractures made: we're done.\n");

   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_summary			*/
/* Syntax: netgen::summary [elements|nodes]		*/
/* Formerly: s						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netcmp_summary(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *options[] = {
      "nodes", "elements", NULL
   };
   enum OptionIdx {
      NODE_IDX, ELEM_IDX
   };
   int result, index = -1;

   if (objc != 1 && objc != 2) {
      Tcl_WrongNumArgs(interp, 1, objv, "?nodes|elements?");
      return TCL_ERROR;
   }
   if (objc == 2) {
      if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)options,
		"option", 0, &index) != TCL_OK) {
         return TCL_ERROR;
      }
   }

   if (objc == 1 || index == ELEM_IDX)
      SummarizeElementClasses(ElementClasses);

   if (objc == 1 || index == NODE_IDX)
      SummarizeNodeClasses(NodeClasses);

   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_print				*/
/* Syntax: netgen::print [elements|nodes|queue]		*/
/*		[legal|illegal]				*/
/* Formerly: P						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netcmp_print(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *options[] = {
      "nodes", "elements", "queue", NULL
   };
   enum OptionIdx {
      NODE_IDX, ELEM_IDX, QUEUE_IDX
   };

   /* Note:  The order is such that the type passed to PrintElementClasses()
    * or PrintNodeClasses() is -1 for all, 0 for legal, and 1 for illegal
    */
   char *classes[] = {
      "legal", "illegal", NULL
   };
   enum ClassIdx {
      LEGAL_IDX, ILLEGAL_IDX
   };

   int result, index = -1, class = -1, dolist = 0;
   int fnum1, fnum2;
   char *optstart;

   if (objc > 1) {
      optstart = Tcl_GetString(objv[1]);
      if (*optstart == '-') optstart++;
      if (!strcmp(optstart, "list")) {
	 dolist = 1;
	 objv++;
	 objc--;
      }
   }

   if (objc < 1 || objc > 3) {
      Tcl_WrongNumArgs(interp, 1, objv, "?nodes|elements|queue? ?legal|illegal?");
      return TCL_ERROR;
   }
   if (objc >= 2) {
      if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)options,
		"option", 0, &index) != TCL_OK) {
	 if ((objc == 2) && (Tcl_GetIndexFromObj(interp, objv[1],
		(CONST84 char **)classes, "class", 0, &class) != TCL_OK)) {
            return TCL_ERROR;
	 }
      }
   }
   if (objc == 3 && index != QUEUE_IDX) {
      if (Tcl_GetIndexFromObj(interp, objv[2], (CONST84 char **)classes,
		"class", 0, &class) != TCL_OK) {
         return TCL_ERROR;
      }
   }
   else if (objc == 3) {
      Tcl_WrongNumArgs(interp, 1, objv, "queue [no arguments]");
      return TCL_ERROR;
   }

   enable_interrupt();
   if (objc == 1 || index == NODE_IDX)
      PrintNodeClasses(NodeClasses, class, dolist);

   if (objc == 1 || index == ELEM_IDX)
      PrintElementClasses(ElementClasses, class, dolist);

   if (objc == 2 && index == QUEUE_IDX) {
      char *name1, *name2;
      Tcl_Obj *lobj;
      result = PeekCompareQueueTop(&name1, &fnum1, &name2, &fnum2);
      lobj = Tcl_NewListObj(0, NULL);
      if (result != -1) {
         Tcl_ListObjAppendElement(interp, lobj, Tcl_NewStringObj(name1, -1));
         Tcl_ListObjAppendElement(interp, lobj, Tcl_NewStringObj(name2, -1));
      }
      Tcl_SetObjResult(interp, lobj);
   }

   disable_interrupt();

   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_run				*/
/* Syntax: netgen::run [converge|resolve]		*/
/* Formerly: r and R					*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netcmp_run(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *options[] = {
      "converge", "resolve", NULL
   };
   enum OptionIdx {
      CONVERGE_IDX, RESOLVE_IDX
   };
   int result, index;
   int automorphisms;
   char *optstart;
   int dolist;

   dolist = 0;
   if (objc > 1) {
      optstart = Tcl_GetString(objv[1]);
      if (*optstart == '-') optstart++;
      if (!strcmp(optstart, "list")) {
	 dolist = 1;
	 objv++;
	 objc--;
      }
   }

   if (objc == 1)
      index = RESOLVE_IDX;
   else {
      if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)options,
		"option", 0, &index) != TCL_OK) {
         return TCL_ERROR;
      }
   }

   switch(index) {
      case CONVERGE_IDX:
	 if (ElementClasses == NULL || NodeClasses == NULL) {
	    return TCL_OK;
	 }
	 else {
	    enable_interrupt();
	    while (!Iterate() && !InterruptPending);
	    ExhaustiveSubdivision = 1;
	    while (!Iterate() && !InterruptPending);
	    if (dolist) {
	       result = _netcmp_verify(clientData, interp, 2, objv - 1);
	    }
	    else
	       result = _netcmp_verify(clientData, interp, 1, NULL);
	    disable_interrupt();
	    if (result != TCL_OK) return result;
	 }
	 break;
      case RESOLVE_IDX:
	 if (ElementClasses == NULL || NodeClasses == NULL) {
	    // Printf("Must initialize data structures first.\n");
	    // return TCL_ERROR;
	    return TCL_OK;
	 }
	 else {
	    enable_interrupt();
	    while (!Iterate() && !InterruptPending);
	    ExhaustiveSubdivision = 1;
	    while (!Iterate() && !InterruptPending);
	    automorphisms = VerifyMatching();
	    if (automorphisms > 0) {
	       // First try to resolve automorphisms uniquely using
	       // property matching
	       automorphisms = ResolveAutomorphsByProperty();
	       if (automorphisms > 0) {
	          // Next, attempt to resolve automorphisms uniquely by
	          // using the pin names
		  automorphisms = ResolveAutomorphsByPin();
	       }
	       if (automorphisms > 0) {
	          // Anything left is truly indistinguishable
		  while (ResolveAutomorphisms() > 0);
	       }
	    }

	    if (automorphisms == -1)
	       Fprintf(stdout, "Netlists do not match.\n");
	    else if (automorphisms == -2)
	       Fprintf(stdout, "Netlists match uniquely with port errors.\n");
	    else {
	       if (automorphisms == 0)
	          Fprintf(stdout, "Netlists match uniquely");
	       else
	          Fprintf(stdout, "Netlists match with %d symmetr%s",
				automorphisms, (automorphisms == 1) ? "y" : "ies");
	       if (PropertyErrorDetected) {
	          Fprintf(stdout, " with property errors.\n");
	          PrintPropertyResults(dolist);
	       }
	       else
		  Fprintf(stdout, ".\n");
	    }
	    disable_interrupt();
         }
	 break;
   }
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_verify			*/
/* Syntax: netgen::verify [option]			*/
/* 	options: nodes, elements, only, all,		*/
/*		 equivalent, or unique.			*/
/* 	option "-list" may be used with nodes, elements	*/
/*		 all, or no option.			*/
/* Formerly: v						*/
/* Results:						*/
/*	For only, equivalent, unique:  Return 		*/
/*	 1: verified					*/
/*	 0: not verified				*/
/*	-1: no elements or nodes			*/
/*	-3: verified with property error		*/
/*   equiv option					*/
/*	-2: pin mismatch				*/
/*							*/
/* Side Effects:					*/
/*	For options elements, nodes, and all without	*/
/*	option -list:  Write output to log file.	*/
/*	For -list options, append list to global	*/
/*	variable "lvs_out", if it exists.		*/
/*------------------------------------------------------*/

int
_netcmp_verify(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *options[] = {
      "nodes", "elements", "properties", "only", "all", "equivalent", "unique", NULL
   };
   enum OptionIdx {
      NODE_IDX, ELEM_IDX, PROP_IDX, ONLY_IDX, ALL_IDX, EQUIV_IDX, UNIQUE_IDX
   };
   char *optstart;
   int result, index = -1;
   int automorphisms;
   int dolist = 0;
   Tcl_Obj *egood, *ebad, *ngood, *nbad;

   if (objc > 1) {
      optstart = Tcl_GetString(objv[1]);
      if (*optstart == '-') optstart++;
      if (!strcmp(optstart, "list")) {
	 dolist = 1;
	 egood = ngood = NULL;
	 ebad = nbad = NULL;
	 objv++;
	 objc--;
      }
   }

   if (objc != 1 && objc != 2) {
      Tcl_WrongNumArgs(interp, 1, objv,	
		"?nodes|elements|only|all|equivalent|unique?");
      return TCL_ERROR;
   }
   if (objc == 2) {
      if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)options,
		"option", 0, &index) != TCL_OK) {
         return TCL_ERROR;
      }
   }

   if (ElementClasses == NULL || NodeClasses == NULL) {
      if (index == EQUIV_IDX || index == UNIQUE_IDX)
	 Tcl_SetObjResult(interp, Tcl_NewIntObj(-1));
      else if (CurrentCell != NULL)
	 Fprintf(stdout, "Verify:  cell %s has no elements and/or nodes."
		"  Not checked.\n", CurrentCell->name);
      else
	 Fprintf(stdout, "Verify:  no current cell to verify.\n");
      return TCL_OK;
   }
   else {
      automorphisms = VerifyMatching();
      if (automorphisms == -1) {
	 enable_interrupt();
	 if (objc == 1 || index == NODE_IDX || index == ALL_IDX) {
	     if (Debug == TRUE)
	        PrintIllegalNodeClasses();	// Old style
	     else {
	        FormatIllegalNodeClasses(); // Side-by-side, to log file
	        if (dolist) {
	           nbad = ListNodeClasses(FALSE);	// As Tcl nested list
#if 0
	           ngood = ListNodeClasses(TRUE);	// As Tcl nested list
#endif
		}
	     }
	 }
	 if (objc == 1 || index == ELEM_IDX || index == ALL_IDX) {
	     if (Debug == TRUE)
	        PrintIllegalElementClasses();	// Old style
	     else {
	        FormatIllegalElementClasses();	// Side-by-side, to log file
	        if (dolist) {
	           ebad = ListElementClasses(FALSE); // As Tcl nested list
#if 0
	           egood = ListElementClasses(TRUE); // As Tcl nested list
#endif
		}
	     }
	 }
	 disable_interrupt();
	 if (index == EQUIV_IDX || index == UNIQUE_IDX)
	     Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
	 else
	     Fprintf(stdout, "Netlists do not match.\n");
      }
      else if (automorphisms == -2) {
	 if (index == EQUIV_IDX)
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(1));
	 else if (index == UNIQUE_IDX)
	    Tcl_SetObjResult(interp, Tcl_NewIntObj(-2));
	 else if (index > 0)
	    Fprintf(stdout, "Circuits match uniquely with port errors.\n");
      }
      else {
	 if (automorphisms) {
	    if (index == EQUIV_IDX)
	        Tcl_SetObjResult(interp, Tcl_NewIntObj((int)automorphisms));
	    else if (index == UNIQUE_IDX)
	        Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
	    else if (index > 0)
	        Printf("Circuits match with %d symmetr%s.\n",
			automorphisms, (automorphisms == 1) ? "y" : "ies");
	 }
	 else {
	    if ((index == EQUIV_IDX) || (index == UNIQUE_IDX)) {
		if (PropertyErrorDetected == 0)
	           Tcl_SetObjResult(interp, Tcl_NewBooleanObj(1));
	        else
	           Tcl_SetObjResult(interp, Tcl_NewIntObj(-3));
	    }
	    else if (index > 0) {
	        Fprintf(stdout, "Circuits match uniquely.\n");
		if (PropertyErrorDetected == 0)
		   Fprintf(stdout, ".\n");
		else
		   Fprintf(stdout, "Property errors were found.\n");
	    }
	 }
#if 0
	 if (dolist) {
	    ngood = ListNodeClasses(TRUE);	// As Tcl nested list
	    egood = ListElementClasses(TRUE);	// As Tcl nested list
	 }
#endif
         if ((index == PROP_IDX) && (PropertyErrorDetected != 0)) {
	    PrintPropertyResults(dolist);
	 }
      }
   }

   /* If "dolist" has been specified, then return the	*/
   /* list-formatted output.  For "verify nodes" or	*/
   /* "verify elements", return the associated list.	*/
   /* For "verify" or "verify all", return a nested	*/
   /* list of {node list, element list}.		*/

   if (dolist)
   {
      if (objc == 1 || index == NODE_IDX || index == ALL_IDX) {
	 if (nbad == NULL) nbad = Tcl_NewListObj(0, NULL);
	 Tcl_SetVar2Ex(interp, "lvs_out", NULL,
		Tcl_NewStringObj("badnets", -1),
		TCL_APPEND_VALUE | TCL_LIST_ELEMENT);
	 Tcl_SetVar2Ex(interp, "lvs_out", NULL, nbad,
		TCL_APPEND_VALUE | TCL_LIST_ELEMENT);
#if 0
	 if (ngood == NULL) ngood = Tcl_NewListObj(0, NULL);
	 Tcl_SetVar2Ex(interp, "lvs_out", NULL,
		Tcl_NewStringObj("goodnets", -1),
		TCL_APPEND_VALUE | TCL_LIST_ELEMENT);
	 Tcl_SetVar2Ex(interp, "lvs_out", NULL, ngood,
		TCL_APPEND_VALUE | TCL_LIST_ELEMENT);
#endif
      }
      if (objc == 1 || index == ELEM_IDX || index == ALL_IDX) {
	 if (ebad == NULL) ebad = Tcl_NewListObj(0, NULL);
	 Tcl_SetVar2Ex(interp, "lvs_out", NULL,
		Tcl_NewStringObj("badelements", -1),
		TCL_APPEND_VALUE | TCL_LIST_ELEMENT);
	 Tcl_SetVar2Ex(interp, "lvs_out", NULL, ebad,
		TCL_APPEND_VALUE | TCL_LIST_ELEMENT);
#if 0
	 if (egood == NULL) egood = Tcl_NewListObj(0, NULL);
	 Tcl_SetVar2Ex(interp, "lvs_out", NULL,
		Tcl_NewStringObj("goodelements", -1),
		TCL_APPEND_VALUE | TCL_LIST_ELEMENT);
	 Tcl_SetVar2Ex(interp, "lvs_out", NULL, egood,
		TCL_APPEND_VALUE | TCL_LIST_ELEMENT);
#endif
      }
   }
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_automorphs			*/
/* Syntax: netgen::automorphisms			*/
/* Formerly: a						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netcmp_automorphs(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   if (objc != 1) {
      Tcl_WrongNumArgs(interp, 1, objv, "(no arguments)");
      return TCL_ERROR;
   }
   PrintAutomorphisms();
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_convert			*/
/* Syntax: netgen::convert <valid_cellname>		*/
/* Formerly: nonexistant function			*/
/* Results: none					*/
/* Side Effects:  one or more global nodes changed to	*/
/* 	local scope and ports.				*/
/*------------------------------------------------------*/

int
_netcmp_convert(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    char *cellname;
    int filenum = -1;
    int result;
    struct nlist *np;

    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "valid_cellname");
	return TCL_ERROR;
    }
    result = CommonParseCell(interp, objv[1], &np, &filenum);
    if (result != TCL_OK) return result;
    cellname = np->name;

    ConvertGlobals(cellname, filenum);
    return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_global			*/
/* Syntax: netgen::global <valid_cellname> <name>	*/
/* Formerly: nonexistant function			*/
/* Results: returns number of matching nets found	*/
/* Side Effects:  one or more nodes changed to global	*/
/* 	scope.						*/
/*------------------------------------------------------*/

int
_netcmp_global(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *filename, *cellname, *pattern;
   int numchanged = 0, p, fnum, llen, result;
   struct nlist *tp;

   if (objc < 2) {
      Tcl_WrongNumArgs(interp, 1, objv, "<valid_cellname> <pattern> [...]");
      return TCL_ERROR;
   }

   /* Check if first argument is a file number */

   result = CommonParseCell(interp, objv[1], &tp, &fnum);
   if (result != TCL_OK) return result;
   cellname = tp->name;

   for (p = 2; p < objc; p++) {
      pattern = Tcl_GetString(objv[p]);
      numchanged += ChangeScope(fnum, cellname, pattern, NODE, GLOBAL);
   }
   
   Tcl_SetObjResult(interp, Tcl_NewIntObj(numchanged));
   return TCL_OK;
}


/*------------------------------------------------------*/
/* Function name: _netcmp_ignore			*/
/* Syntax: netgen::ignore [class] <valid_cellname>	*/
/* Formerly: no such command				*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netcmp_ignore(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *options[] = {
      "class", "shorted", NULL
   };
   enum OptionIdx {
      CLASS_IDX, SHORTED_IDX
   };
   int result, index;
   int file = -1;
   struct nlist *np;
   char *name = NULL, *name2 = NULL;

   if (objc >= 3) {
      if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)options,
		"option", 0, &index) == TCL_OK) {
	 objc--;
	 objv++;
      }
      result = CommonParseCell(interp, objv[1], &np, &file);
      if (result != TCL_OK) return result;
      name = np->name;
   }
   else {
      Tcl_WrongNumArgs(interp, 1, objv, "[class] valid_cellname");
      return TCL_ERROR;
   }
   switch (index) {
      case CLASS_IDX:
         IgnoreClass(name, file, IGNORE_CLASS);
	 break;
      case SHORTED_IDX:
         IgnoreClass(name, file, IGNORE_SHORTED);
	 break;
   }
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_equate			*/
/* Syntax: netgen::equate [elements|nodes|classes|pins]	*/
/* Formerly: e and n					*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netcmp_equate(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *options[] = {
      "nodes", "elements", "classes", "pins", NULL
   };
   enum OptionIdx {
      NODE_IDX, ELEM_IDX, CLASS_IDX, PINS_IDX
   };
   int result, index;
   char *name1 = NULL, *name2 = NULL, *optstart;
   struct nlist *tp1, *tp2, *SaveC1, *SaveC2;
   struct objlist *ob1, *ob2;
   struct ElementClass *saveEclass = NULL;
   struct NodeClass *saveNclass = NULL;
   int file1, file2;
   int i, l1, l2, ltest, lent, dolist = 0, doforce = 0, dounique = 0;
   Tcl_Obj *tobj1, *tobj2, *tobj3;

   while (objc > 1) {
      optstart = Tcl_GetString(objv[1]);
      if (*optstart == '-') optstart++;
      if (!strcmp(optstart, "list")) {
	 dolist = 1;
	 objv++;
	 objc--;
      }
      else if (!strcmp(optstart, "force")) {
	 doforce = 1;
	 objv++;
	 objc--;
      }
      else if (!strcmp(optstart, "unique")) {
	 dounique = 1;
	 objv++;
	 objc--;
      }
      else
	 break;
   }

   if ((objc != 2) && (objc != 4) && (objc != 6)) {
      Tcl_WrongNumArgs(interp, 1, objv, "?nodes|elements|classes|pins? name1 name2");
      return TCL_ERROR;
   }
   else {
      if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)options,
		"option", 0, &index) != TCL_OK) {
         return TCL_ERROR;
      }
   }

   /* Something is going on here. . . */
   if (index > PINS_IDX) index = PINS_IDX;

   /* 4-argument form only available for "equate classes", or for other	*/
   /* options if Circuit1 and Circuit2 have been declared.		*/

   if ((objc == 2) && (index == PINS_IDX)) {
      if (Circuit1 == NULL || Circuit2 == NULL) {
	 Tcl_SetResult(interp, "Circuits not being compared, must specify netlists.",
			NULL);
	 return TCL_ERROR;
      }
      tp1 = Circuit1;
      file1 = Circuit1->file;
      tp2 = Circuit2;
      file2 = Circuit2->file;

      name1 = tp1->name;
      name2 = tp2->name;
   }
   else if ((objc == 4) && (index != CLASS_IDX) && (index != PINS_IDX)) {
      if (Circuit1 == NULL || Circuit2 == NULL) {
	 Tcl_SetResult(interp, "Circuits not being compared, must specify netlists.",
			NULL);
	 return TCL_ERROR;
      }
      tp1 = Circuit1;
      file1 = Circuit1->file;
      tp2 = Circuit2;
      file2 = Circuit2->file;

      name1 = Tcl_GetString(objv[2]);
      name2 = Tcl_GetString(objv[3]);
   }

   else if ((objc == 4) && ((index == CLASS_IDX) || (index == PINS_IDX))) {
      result = CommonParseCell(interp, objv[2], &tp1, &file1);
      if (result != TCL_OK) {
	 if (index == CLASS_IDX) {
	    Fprintf(stdout, "Cell to equate does not exist.\n");
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
	    return result;
	 }
	 Tcl_SetResult(interp, "No such object.", NULL);
	 return TCL_ERROR;
      }
      result = CommonParseCell(interp, objv[3], &tp2, &file2);
      if (result != TCL_OK) {
	 if (index == CLASS_IDX) {
	    Fprintf(stdout, "Cell to equate does not exist.\n");
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
	    return result;
	 }
	 Tcl_SetResult(interp, "No such object.", NULL);
	 return TCL_ERROR;
      }
      if (file1 == file2) {
	 if (index == CLASS_IDX) {
	    Fprintf(stdout, "Cells to equate are in the same netlist.\n");
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
	    return TCL_ERROR;
	 }
	 Tcl_SetResult(interp, "Objects in the same netlist cannot be equated.",
				NULL);
	 return TCL_ERROR;
      } 
      name1 = tp1->name;
      name2 = tp2->name;
   }

   else if (objc == 6) {
      result = CommonParseCell(interp, objv[2], &tp1, &file1);
      if (result != TCL_OK) {
	 if (index == CLASS_IDX) {
	    Fprintf(stdout, "Cell to equate does not exist.\n");
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
	 }
	 return result;
      }
      result = CommonParseCell(interp, objv[4], &tp2, &file2);
      if (result != TCL_OK) {
	 if (index == CLASS_IDX) {
	     Fprintf(stdout, "Cell to equate does not exist.\n");
	     Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
	 }
	 return result;
      }

      if (file1 == file2) {
	 if (index == CLASS_IDX) {
	    Tcl_ResetResult(interp);
	    Fprintf(stdout, "Cells to equate are in the same netlist.\n");
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
	    return TCL_ERROR;
	 }
	 Tcl_SetResult(interp, "Cannot equate within the same netlist!\n",
			NULL);
	 return TCL_ERROR;
      } 
      name1 = Tcl_GetString(objv[3]);
      name2 = Tcl_GetString(objv[5]);
   }
   else {
      Tcl_WrongNumArgs(interp, 1, objv, "?nodes|elements|classes|pins? name1 name2");
      return TCL_ERROR;
   }

   switch(index) {
      case NODE_IDX:
	 if (NodeClasses == NULL) {
	    Fprintf(stderr, "Cell has no nodes.\n");
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
	    return TCL_OK;
	 }
	 if (EquivalenceNodes(name1, file1, name2, file2)) {
	    Fprintf(stdout, "Nodes %s and %s are equivalent.\n", name1, name2);
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(1));
	 }
	 else {
	    Fprintf(stderr, "Unable to equate nodes %s and %s.\n",name1, name2);
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
	 }
	 break;

      case ELEM_IDX:
	 if (ElementClasses == NULL) {
	    if (CurrentCell == NULL)
		Fprintf(stderr, "Equate elements:  no current cell.\n");
	    Fprintf(stderr, "Equate elements:  cell %s and/or %s has no elements.\n",
			name1, name2);
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
	    return TCL_OK;
	 }
	 if (EquivalenceElements(name1, file1, name2, file2)) {
	    Fprintf(stdout, "Elements %s and %s are equivalent.\n", name1, name2);
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(1));
	 }
	 else {
	    Fprintf(stderr, "Unable to equate elements %s and %s.\n",name1, name2);
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
	 }
	 break;

      case PINS_IDX:
	 if ((ElementClasses != NULL) && (doforce == TRUE)) {
	    saveEclass = ElementClasses;
	    saveNclass = NodeClasses;
	    ElementClasses = NULL;
	    NodeClasses = NULL;
	 }
	 if ((ElementClasses == NULL) && (auto_blackbox == FALSE)) {
	    if (CurrentCell == NULL) {
		Fprintf(stderr, "Equate elements:  no current cell.\n");
		Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
		return TCL_OK;
	    }
	    else if ((tp1->flags & CELL_PLACEHOLDER) ||
			(tp2->flags & CELL_PLACEHOLDER)) {
		if (tp1->flags & CELL_PLACEHOLDER) {
		    Fprintf(stdout, "Warning: Equate pins:  cell %s "
			"is a placeholder, treated as a black box.\n", name1);
		}
		if (tp2->flags & CELL_PLACEHOLDER) {
		    Fprintf(stdout, "Warning: Equate pins:  cell %s "
			"is a placeholder, treated as a black box.\n", name2);
		}
		// If a cell in either circuit is marked as a black box, then
		// the cells in both circuits should be marked as a black box.
		tp1->flags |= CELL_PLACEHOLDER;
		tp2->flags |= CELL_PLACEHOLDER;
	    }
	    else if (doforce != TRUE) {
		/* When doforce is TRUE, ElementClass has been set to NULL
		 * even though circuits contain elements, so this message
		 * is not correct.
		 */
		Fprintf(stdout, "Equate pins:  cell %s and/or %s "
			"has no elements.\n", name1, name2);
		/* This is not necessarily an error, so go ahead and match pins. */
	    }
	 }
	 if (ElementClasses == NULL) {
	    /* This may have been called outside of a netlist compare,	*/
	    /* probably to force name matching of pins on black-box	*/
	    /* devices.  But MatchPins only works if tp1 == Circuit1	*/
	    /* and tp2 == Circuit2, so preserve these values and 	*/
	    /* recover afterward (what a hack).				*/
	    SaveC1 = Circuit1;
	    SaveC2 = Circuit2;
	    Circuit1 = tp1;
	    Circuit2 = tp2;
	 }

	 // Check for and remove duplicate pins.  Normally this is called
	 // from "compare", but since "equate pins" may be called outside
	 // of and before "compare", pin uniqueness needs to be ensured.

	 UniquePins(tp1->name, tp1->file);
	 UniquePins(tp2->name, tp2->file);

	 result = MatchPins(tp1, tp2, dolist);
	 if (result == 2) {
	    Fprintf(stdout, "Cells have no pins;  pin matching not needed.\n");
	 }
	 else if (result > 0) {
	    Fprintf(stdout, "Cell pin lists are equivalent.\n");
	 }
	 else if (result == -1) {
	    Fprintf(stdout, "Cell pin lists for %s and %s do not match.\n",
			name1, name2);
	 }
	 else if (result == -2) {
	    Fprintf(stdout, "Attempt to match empty cell to non-empty cell.\n");
	 }
	 else {
	    Fprintf(stdout, "Cell pin lists for %s and %s altered to match.\n",
			name1, name2);
	 }
	 Tcl_SetObjResult(interp, Tcl_NewIntObj(result));
	 if (ElementClasses == NULL) {
	    /* Recover temporarily set global variables (see above) */
	    Circuit1 = SaveC1;
	    Circuit2 = SaveC2;

	    /* Recover ElementClasses if forcing pins on mismatched circuits */
	    if (doforce == TRUE) {
	       ElementClasses = saveEclass;
	       NodeClasses = saveNclass;
	    }
	 }
	 break;

      case CLASS_IDX:

	 if (objc == 6) {

	    /* Apply additional matching of pins */
	    /* Objects must be CLASS_MODULE or CLASS_SUBCKT */

	    if (tp1->class != CLASS_MODULE && tp1->class != CLASS_SUBCKT) {
	       Tcl_SetResult(interp, "Device class is not black box"
			" or subcircuit!", NULL);
	       return TCL_ERROR;
	    }
	    if (tp2->class != tp1->class) {
	       Tcl_SetResult(interp, "Device classes are different,"
			" cannot match pins!", NULL);
	       return TCL_ERROR;
	    }

	    /* Count the list items, and the number of ports in each cell */
	    result = Tcl_ListObjLength(interp, objv[3], &l1);
	    if (result != TCL_OK) return TCL_ERROR;
	    result = Tcl_ListObjLength(interp, objv[5], &l2);
	    if (result != TCL_OK) return TCL_ERROR;
	    if (l1 != l2) {
	       Tcl_SetResult(interp, "Pin lists are different length,"
			" cannot match pins!", NULL);
	       return TCL_ERROR;
	    }
	    ltest = 0;
	    for (ob1 = tp1->cell; ob1 != NULL; ob1 = ob1->next) {
	       if (ob1->type == PORT) ltest++;
	    } 
	    if (ltest != l1) {
	       Tcl_SetResult(interp, "List length does not match "
			" number of pins in cell.", NULL);
	       return TCL_ERROR;
	    }
	    ltest = 0;
	    for (ob2 = tp2->cell; ob2 != NULL; ob2 = ob2->next) {
	       if (ob2->type == PORT) ltest++;
	    } 
	    if (ltest != l2) {
	       Tcl_SetResult(interp, "List length does not match "
			" number of pins in cell.", NULL);
	       return TCL_ERROR;
	    }

	    /* 1st pin list:  Check that all list items have 1 or 2	*/
	    /* entries, and that all of them have the same number	*/

	    result = Tcl_ListObjIndex(interp, objv[3], 0, &tobj1);
	    if (result != TCL_OK) return result;
	    result = Tcl_ListObjLength(interp, tobj1, &lent);
	    if (result != TCL_OK) return result;
	    if (lent > 2) {
		Tcl_SetResult(interp, "All list items must have one"
			" or two entries.", NULL);
		return TCL_ERROR;
	    }

	    for (i = 1; i < l1; i++) {
		result = Tcl_ListObjIndex(interp, objv[3], i, &tobj1);
		if (result != TCL_OK) return result;
		result = Tcl_ListObjLength(interp, tobj1, &ltest);
		if (result != TCL_OK) return result;
		if (ltest != lent) {
		    Tcl_SetResult(interp, "All list items must have the"
				" same number of entries.", NULL);
		    return TCL_ERROR;
		}
	    }

	    /* If the first pin is a list of 2, then all items	*/
	    /* must be lists of two.  If the cell is a		*/
	    /* placeholder, then match the pin number against	*/
	    /* the 2nd list item, and rename the pin.  		*/

	    if (lent == 2) {
		for (i = 0; i < l1; i++) {
		    result = Tcl_ListObjIndex(interp, objv[3], i, &tobj1);
		    result = Tcl_ListObjIndex(interp, tobj1, 0, &tobj2);
		    if (result != TCL_OK) return result;
		    result = Tcl_ListObjIndex(interp, tobj1, 1, &tobj3);
		    if (result != TCL_OK) return result;

		    for (ob1 = tp1->cell; ob1 != NULL; ob1 = ob1->next) {
			if (ob1->type == PORT) {
			    if ((*matchfunc)(ob1->name, Tcl_GetString(tobj3))) {
				FREE(ob1->name);
				ob1->name = strsave(Tcl_GetString(tobj2));
				Tcl_GetIntFromObj(interp, tobj3, &ob1->model.port);
				break;
			    }
			}
		    }
		}
	    }

	    /* If the first pin is a list of 1, then all items	*/
	    /* must be single items.  If the cell is a		*/
	    /* placeholder, then flag an error;  relying on	*/
	    /* numerical order would be ambiguous.		*/

	    else {	/* lent == 1 */
		if (tp1->flags & CELL_PLACEHOLDER) {
		    Tcl_SetResult(interp, "No pin order information "
				" for the cell.", NULL);
		    return TCL_ERROR;
		}
		/* else nothing to do here. . . need to parse	*/
		/* the second list before we can do anything.	*/
	    }

	    /* 2st pin list:  Check that all list items have 1 or 2	*/
	    /* entries, and that all of them have the same number	*/

	    result = Tcl_ListObjIndex(interp, objv[5], 0, &tobj2);
	    if (result != TCL_OK) return result;
	    result = Tcl_ListObjLength(interp, tobj2, &lent);
	    if (result != TCL_OK) return result;
	    if (lent > 2) {
		Tcl_SetResult(interp, "All list items must have one"
			" or two entries.", NULL);
		return TCL_ERROR;
	    }

	    for (i = 1; i < l2; i++) {
		result = Tcl_ListObjIndex(interp, objv[5], i, &tobj2);
		if (result != TCL_OK) return result;
		result = Tcl_ListObjLength(interp, tobj2, &ltest);
		if (result != TCL_OK) return result;
		if (ltest != lent) {
		    Tcl_SetResult(interp, "All list items must have the"
				" same number of entries.", NULL);
		    return TCL_ERROR;
		}
	    }

	    /* Repeat for the 2nd cell:  If the first pin is a	*/
	    /* list of 2, then all items must be lists of two.  */
	    /* If the cell is a	placeholder, then match the pin	*/
	    /* number against the 2nd list item, and rename the	*/
	    /* pin.  						*/

	    if (lent == 2) {
		for (i = 0; i < l2; i++) {
		    result = Tcl_ListObjIndex(interp, objv[5], i, &tobj1);
		    result = Tcl_ListObjIndex(interp, tobj1, 0, &tobj2);
		    if (result != TCL_OK) return result;
		    result = Tcl_ListObjIndex(interp, tobj1, 1, &tobj3);
		    if (result != TCL_OK) return result;

		    for (ob2 = tp2->cell; ob2 != NULL; ob2 = ob2->next) {
			if (ob2->type == PORT) {
			    if ((*matchfunc)(ob2->name, Tcl_GetString(tobj3))) {
				FREE(ob2->name);
				ob2->name = strsave(Tcl_GetString(tobj2));
				Tcl_GetIntFromObj(interp, tobj3, &ob2->model.port);
				break;
			    }
			}
		    }
		}
	    }

	    /* On the 2nd cell, if the first pin is a list of	*/
	    /* 1, and the cell is a placeholder, then we have	*/
	    /* no idea how to order the pins and must flag an	*/
	    /* error.						*/

	    else {	/* lent == 1 */
		if (tp2->flags & CELL_PLACEHOLDER) {
		    Tcl_SetResult(interp, "No pin order information "
				" for the cell.", NULL);
		    return TCL_ERROR;
		}
	    }
	 }

	 if (EquivalenceClasses(tp1->name, file1, tp2->name, file2, dounique)) {
	    Fprintf(stdout, "Device classes %s and %s are equivalent.\n",
			tp1->name, tp2->name);
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(1));
	 }
	 else {
	    Fprintf(stderr, "Unable to equate device classes %s and %s.\n",
			tp1->name, tp2->name);
	    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
	 }
	 break;
   }
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_property			*/
/* Syntax: netgen::property <device>|<model> [<option>	*/
/*	 <property_key> [...]]				*/
/* Where <option> is one of:				*/
/*	add	  --- add new property			*/
/*	remove	  --- delete existing property		*/
/*	tolerance --- set property tolerance		*/
/*	associate --- associate property with a pin	*/
/*	topology  --- set exact/relaxed matching	*/
/*	merge	  --- (deprecated)			*/
/* or							*/
/*	netgen::property default			*/
/* or							*/
/*	netgen::property <device>|<model> <option>	*/
/*		yes|no					*/
/* Where <option> is one of:				*/
/*     series	--- allow/prohibit series combination	*/
/*     parallel --- allow/prohibit parallel combination	*/
/* or							*/
/*	netgen::property parallel none			*/
/*		--- prohibit parallel combinations by	*/
/*		    default (for all devices).		*/
/*							*/
/* series|parallel options are:				*/
/*	enable|disable|none|{<key> <combine_option>}	*/
/*							*/
/* combine options are:					*/
/*	par|add|par_critical|add_critical		*/
/*							*/
/* Formerly: (none)					*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netcmp_property(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int fnum, i, llen;
    struct nlist *tp;
    struct property *kl, *kllast, *klnext;
    Tcl_Obj *tobj1, *tobj2, *tobj3;
    double dval;
    int ival, argstart;

    char *options[] = {
	"add", "create", "remove", "delete", "tolerance", "merge", "serial",
	"series", "parallel", "associate", "topology", NULL
    };
    enum OptionIdx {
	ADD_IDX, CREATE_IDX, REMOVE_IDX, DELETE_IDX, TOLERANCE_IDX, MERGE_IDX,
	SERIAL_IDX, SERIES_IDX, PARALLEL_IDX, ASSOCIATE_IDX, TOPOLOGY_IDX
    };
    int result, index, idx2;

    char *suboptions[] = {
	"integer", "double", "value", "string", "expression", NULL
    };
    enum SubOptionIdx {
	INTEGER_IDX, DOUBLE_IDX, VALUE_IDX, STRING_IDX, EXPRESSION_IDX
    };

    /* Note: "merge" has been deprecated, but kept for backwards compatibility.	*/
    /* It has been replaced by "combineoptions" below, used with "series" and	*/
    /* "parallel".								*/

    char *mergeoptions[] = {
	"none", "add", "add_critical", "par", "par_critical",
	"parallel", "parallel_critical", "ser_critical", "ser",
	"serial_critical", "series_critical", "serial", "series", NULL
    };

    enum MergeOptionIdx {
	NONE_IDX, ADD_ONLY_IDX, ADD_CRIT_IDX,
	PAR_ONLY_IDX, PAR_CRIT_IDX, PAR2_ONLY_IDX, PAR2_CRIT_IDX,
	SER_CRIT_IDX, SER_IDX, SER2_CRIT_IDX, SER3_CRIT_IDX, SER2_IDX, SER3_IDX
    };

    char *combineoptions[] = {
	"none", "par", "add", "critical", NULL
    };

    enum CombineOptionIdx {
	COMB_NONE_IDX, COMB_PAR_IDX, COMB_ADD_IDX, COMB_CRITICAL_IDX
    };

    char *yesno[] = {
	"on", "yes", "true", "enable", "allow",
	"off", "no", "false", "disable", "prohibit", NULL
    };

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "valid_cellname ?option?");
	return TCL_ERROR;
    }

    char *topo[] = {
	"strict", "relaxed", NULL
    };

    /* Check for special command "property default" */
    if ((objc == 2) && (!strcmp(Tcl_GetString(objv[1]), "default"))) {

	/* For each FET device, do "merge {w add_critical}" and	*/
	/* "remove as ad ps pd".  This allows parallel devices	*/
	/* to be added by width, and prevents attempts to	*/
	/* compare source/drain area and perimeter.		*/

	tp = FirstCell();
	while (tp != NULL) {
	    switch (tp->class) {
		case CLASS_NMOS: case CLASS_PMOS: case CLASS_FET3:
		case CLASS_NMOS4: case CLASS_PMOS4: case CLASS_FET4:
		case CLASS_FET:
		    PropertyMerge(tp->name, tp->file, "w", MERGE_P_ADD | MERGE_P_CRIT,
				MERGE_ALL_MASK);
		    PropertyDelete(tp->name, tp->file, "as");
		    PropertyDelete(tp->name, tp->file, "ad");
		    PropertyDelete(tp->name, tp->file, "ps");
		    PropertyDelete(tp->name, tp->file, "pd");
		    break;
		case CLASS_RES: case CLASS_RES3:
		    PropertyMerge(tp->name, tp->file, "w",
				MERGE_P_PAR | MERGE_S_CRIT, MERGE_ALL_MASK);
		    PropertyMerge(tp->name, tp->file, "l",
				MERGE_S_ADD | MERGE_P_CRIT, MERGE_ALL_MASK);
		    PropertyMerge(tp->name, tp->file, "value",
				MERGE_S_ADD | MERGE_P_PAR, MERGE_ALL_MASK);
		    tp->flags |= COMB_SERIES;
		    break;
		case CLASS_CAP: case CLASS_ECAP: case CLASS_CAP3:
		    /* NOTE:  No attempt to modify perimeter, length, or width */
		    PropertyMerge(tp->name, tp->file, "area",
				MERGE_P_ADD | MERGE_S_PAR, MERGE_ALL_MASK);
		    PropertyMerge(tp->name, tp->file, "value",
				MERGE_P_ADD | MERGE_S_PAR, MERGE_ALL_MASK);
		    tp->flags |= COMB_SERIES;
		    break;
		case CLASS_INDUCTOR:
		    PropertyMerge(tp->name, tp->file, "value",
				MERGE_S_ADD | MERGE_P_PAR, MERGE_ALL_MASK);
		    tp->flags |= COMB_SERIES;
		    break;
	    }
	    tp = NextCell();
	}
	return TCL_OK;
    }
    else if ((objc == 3) && (!strcmp(Tcl_GetString(objv[1]), "parallel"))) {
	if (!strcmp(Tcl_GetString(objv[2]), "none")) {
	    GlobalParallelNone = TRUE;
	    SetParallelCombine(FALSE);
	}
	else if (!strcmp(Tcl_GetString(objv[2]), "all")) {
	    GlobalParallelNone = FALSE;
	    SetParallelCombine(TRUE);
	}
	else if (!strcmp(Tcl_GetString(objv[2]), "connected")) {
	    GlobalParallelOpen = FALSE;
	}
	else if (!strcmp(Tcl_GetString(objv[2]), "open")) {
	    GlobalParallelOpen = TRUE;
	}
	else {
	    Tcl_SetResult(interp, "Bad option, should be property parallel "
			"none|all|connected", NULL);
	    return TCL_ERROR;
	}
	return TCL_OK;
    }
    else if ((objc == 3) && ((!strcmp(Tcl_GetString(objv[1]), "series")) ||
		(!strcmp(Tcl_GetString(objv[1]), "serial")))) {
	if (!strcmp(Tcl_GetString(objv[2]), "none")) {
	    SetSeriesCombine(FALSE);
	}
	else if (!strcmp(Tcl_GetString(objv[2]), "all")) {
	    SetSeriesCombine(TRUE);
	}
	else {
	    Tcl_SetResult(interp, "Bad option, should be property series none|all",
			NULL);
	    return TCL_ERROR;
	}
	return TCL_OK;
    }
    else if ((objc > 1) && (!strcmp(Tcl_GetString(objv[1]), "topology"))) {
	if (objc == 2) {
	    if (ExactTopology)
		Tcl_SetResult(interp, "Strict topology property matching.",
			NULL);
	    else
		Tcl_SetResult(interp, "Relaxed topology property matching.",
			NULL);
	}
	else if (objc == 3) {
	    if (Tcl_GetIndexFromObj(interp, objv[2],
			(CONST84 char **)topo,
			"topology", 0, &idx2) == TCL_OK) {
		if (idx2 == 0)
		    ExactTopology = TRUE;
		else if (idx2 == 1)
		    ExactTopology = FALSE;
		else {
		    Tcl_SetResult(interp, "Topology matching type must be "
				"'strict' or 'relaxed'.", NULL);
	    	    return TCL_ERROR;
		}
	    }
	}
	else {
	    Tcl_WrongNumArgs(interp, 1, objv, "strict|relaxed");
	    return TCL_ERROR;
	}
	return TCL_OK;
    }

    result = CommonParseCell(interp, objv[1], &tp, &fnum);
    if (result != TCL_OK) return result;

    if (objc == 2) {
	/* Print all properties of the cell as key/type/tolerance triplets */
	tobj1 = Tcl_NewListObj(0, NULL);

	kl = (struct property *)HashFirst(&(tp->propdict));
	while (kl != NULL) {
	    tobj2 = Tcl_NewListObj(0, NULL);

	    tobj3 = Tcl_NewStringObj(kl->key, -1);
	    Tcl_ListObjAppendElement(interp, tobj2, tobj3);

	    if (kl->type == PROP_DOUBLE)
		tobj3 = Tcl_NewStringObj("double", -1);
	    else if (kl->type == PROP_VALUE)
		tobj3 = Tcl_NewStringObj("value", -1);
	    else if (kl->type == PROP_INTEGER)
		tobj3 = Tcl_NewStringObj("integer", -1);
	    else if (kl->type == PROP_EXPRESSION)
		tobj3 = Tcl_NewStringObj("expression", -1);
	    else
		tobj3 = Tcl_NewStringObj("string", -1);
	    Tcl_ListObjAppendElement(interp, tobj2, tobj3);

	    if (kl->type == PROP_INTEGER)
		tobj3 = Tcl_NewIntObj(kl->slop.ival);
	    else
		tobj3 = Tcl_NewDoubleObj(kl->slop.dval);
	    Tcl_ListObjAppendElement(interp, tobj2, tobj3);

	    Tcl_ListObjAppendElement(interp, tobj1, tobj2);

	    kl = (struct property *)HashNext(&(tp->propdict));
	}
	Tcl_SetObjResult(interp, tobj1);
    }
    else {
	if (Tcl_GetIndexFromObj(interp, objv[2], (CONST84 char **)options,
		"option", 0, &index) != TCL_OK) {
	    index = ADD_IDX;
	    argstart = 2;
	}
	else
	    argstart = 3;

	switch (index) {
	    case SERIAL_IDX:
	    case SERIES_IDX:
	    case PARALLEL_IDX:
                if (objc == 3) {
		    if (index == SERIAL_IDX || index == SERIES_IDX) {
			tobj1 = Tcl_NewBooleanObj((tp->flags & COMB_SERIES) ? 1 : 0);
			Tcl_SetObjResult(interp, tobj1);
			return TCL_OK;
		    }
		    else {
			tobj1 = Tcl_NewBooleanObj((tp->flags & COMB_NO_PARALLEL) ? 0 : 1);
			Tcl_SetObjResult(interp, tobj1);
			return TCL_OK;
		    }
		}
		else if (objc < 4) {
		    Tcl_WrongNumArgs(interp, 2, objv, "series|parallel enable|disable");
		    return TCL_ERROR;
		}

		for (i = 3; i < objc; i++) {
		    // Each value must be a list of two, or a yes/no answer.

		    if (Tcl_GetIndexFromObj(interp, objv[i],
				(CONST84 char **)yesno,
				"combine", 0, &idx2) == TCL_OK) {
			if (idx2 <= 4) {	/* true, enable, etc. */
			    if (index == SERIAL_IDX || index == SERIES_IDX)
				tp->flags |= COMB_SERIES;
			    else
				tp->flags &= ~COMB_NO_PARALLEL;
			}
			else {	/* false, disable, etc. */
			    if (index == SERIAL_IDX || index == SERIES_IDX)
				tp->flags &= ~COMB_SERIES;
			    else
				tp->flags |= COMB_NO_PARALLEL;
			}
			continue;
		    }

		    result = Tcl_ListObjLength(interp, objv[i], &llen);
		    if ((result != TCL_OK) || (llen != 2)) {
			Tcl_SetResult(interp, "Not a {key merge_type} pair list.",
					NULL);
		    }
		    else {
			int mergeval = MERGE_NONE;
			int mergemask = MERGE_NONE;

			result = Tcl_ListObjIndex(interp, objv[i], 0, &tobj1);
			if (result != TCL_OK) return result;
			result = Tcl_ListObjIndex(interp, objv[i], 1, &tobj2);
			if (result != TCL_OK) return result;

			result = Tcl_GetIndexFromObj(interp, tobj2,
				(CONST84 char **)combineoptions,
				"combine_type", 0, &idx2);
			if (result != TCL_OK) return result;

			if (index == SERIAL_IDX || index == SERIES_IDX) {
			    mergemask = MERGE_S_MASK;
			    switch (idx2) {
				case COMB_NONE_IDX:
				    mergeval &= ~(MERGE_S_MASK);
				    tp->flags &= ~COMB_SERIES;
				    break;
				case COMB_PAR_IDX:
				    mergeval = MERGE_S_PAR;
				    tp->flags |= COMB_SERIES;
				    break;
				case COMB_ADD_IDX:
				    mergeval |= MERGE_S_ADD;
				    tp->flags |= COMB_SERIES;
				    break;
				case COMB_CRITICAL_IDX:
				    mergeval |= MERGE_S_CRIT;
				    tp->flags |= COMB_SERIES;
				    break;
			    }
			}
			else {	/* index == PARALLEL_IDX */
			    mergemask = MERGE_P_MASK;
			    switch (idx2) {
				case COMB_NONE_IDX:
				    mergeval &= ~(MERGE_P_MASK);
				    tp->flags |= COMB_NO_PARALLEL;
				    break;
				case COMB_PAR_IDX:
				    mergeval |= MERGE_P_PAR;
				    tp->flags &= ~COMB_NO_PARALLEL;
				    break;
				case COMB_ADD_IDX:
				    mergeval |= MERGE_P_ADD;
				    tp->flags &= ~COMB_NO_PARALLEL;
				    break;
				case COMB_CRITICAL_IDX:
				    mergeval |= MERGE_P_CRIT;
				    tp->flags &= ~COMB_NO_PARALLEL;
				    break;
			    }
			}
			PropertyMerge(tp->name, fnum, Tcl_GetString(tobj1), mergeval,
				mergemask);
		    }
		}
		break;

	    case ADD_IDX:
	    case CREATE_IDX:
		if ((objc - argstart) == 0) {
		    Tcl_WrongNumArgs(interp, 1, objv, "property_key ...");
		    return TCL_ERROR;
		}
		for (i = argstart; i < objc; i++) {
		    result = Tcl_ListObjLength(interp, objv[i], &llen);
		    switch (llen) {
			case 1:
			    /* String or double, from context, default tolerance */
			    if (Tcl_GetDoubleFromObj(interp, objv[i], &dval)
						!= TCL_OK) {
				Tcl_ResetResult(interp);
			 	PropertyString(tp->name, fnum,
					Tcl_GetString(objv[i]),
					(int)0, (char *)NULL);
			    }
			    else
			 	PropertyDouble(tp->name, fnum,
					Tcl_GetString(objv[i]),
					(double)0.01, (double)0.0);
			    break;

			case 2:
			    result = Tcl_ListObjIndex(interp, objv[i], 0, &tobj1);
			    if (result != TCL_OK) return TCL_ERROR;
			    result = Tcl_ListObjIndex(interp, objv[i], 1, &tobj2);
			    if (result != TCL_OK) return TCL_ERROR;

			    /* {key, type} or {key, tolerance} duplet */

			    if (Tcl_GetIndexFromObj(interp, tobj2,
					(CONST84 char **)suboptions,
					"type", 0, &idx2) != TCL_OK) {
				Tcl_ResetResult(interp);
				if (Tcl_GetDoubleFromObj(interp, tobj2, &dval)
						!= TCL_OK) {
				    Tcl_ResetResult(interp);
			 	    PropertyDouble(tp->name, fnum,
						Tcl_GetString(tobj1), dval, 0.0);
				}
				else {
			 	    PropertyDouble(tp->name, fnum,
						Tcl_GetString(tobj1), dval, 0.0);
				}
			    }
			    else {
				switch (idx2) {
				    case INTEGER_IDX:
			 		PropertyInteger(tp->name, fnum,
						Tcl_GetString(tobj1), 0, 0);
					break;
				    case DOUBLE_IDX:
			 		PropertyDouble(tp->name, fnum,
						Tcl_GetString(tobj1),
						(double)0.01, 0.0);
					break;
				    case STRING_IDX:
			 		PropertyString(tp->name, fnum,
						Tcl_GetString(tobj1), 0, NULL);
					break;
				}
			    }
			    break;

			case 3:
			    result = Tcl_ListObjIndex(interp, objv[i], 0, &tobj1);
			    if (result != TCL_OK) return TCL_ERROR;
			    result = Tcl_ListObjIndex(interp, objv[i], 1, &tobj2);
			    if (result != TCL_OK) return TCL_ERROR;
			    result = Tcl_ListObjIndex(interp, objv[i], 2, &tobj3);
			    if (result != TCL_OK) return TCL_ERROR;

			    /* {key, type, tolerance} triplet */

			    if (Tcl_GetIndexFromObj(interp, tobj2,
					(CONST84 char **)suboptions,
					"type", 0, &idx2) != TCL_OK)
				return TCL_ERROR;

			    switch (idx2) {
				case INTEGER_IDX:
					if (Tcl_GetIntFromObj(interp, tobj3, &ival)
						!= TCL_OK)
					    return TCL_ERROR;
			 		PropertyInteger(tp->name, fnum,
						Tcl_GetString(tobj1), ival, 0);
					break;
				case DOUBLE_IDX:
					if (Tcl_GetDoubleFromObj(interp, tobj3, &dval)
						!= TCL_OK)
					    return TCL_ERROR;
			 		PropertyDouble(tp->name, fnum,
						Tcl_GetString(tobj1), dval, 0.0);
					break;
				case VALUE_IDX:
					if (Tcl_GetDoubleFromObj(interp, tobj3, &dval)
						!= TCL_OK)
					    return TCL_ERROR;
			 		PropertyValue(tp->name, fnum,
						Tcl_GetString(tobj1), dval, 0.0);
					break;
				case STRING_IDX:
					if (Tcl_GetIntFromObj(interp, tobj3, &ival)
						!= TCL_OK)
					    return TCL_ERROR;
			 		PropertyString(tp->name, fnum,
						Tcl_GetString(tobj1), ival, NULL);
					break;
				case EXPRESSION_IDX:
			 		PropertyString(tp->name, fnum,
						Tcl_GetString(tobj1), 0,
						Tcl_GetString(tobj3));
					break;
			    }
			    break;
		    }
		}
		break;

	    case REMOVE_IDX:
	    case DELETE_IDX:
		if (objc == 3) {
		    /* "remove" without additional arguments means	*/
		    /* delete all properties.				*/
		    RecurseHashTable(&(tp->propdict), freeprop);
		    HashKill(&(tp->propdict));
		    InitializeHashTable(&(tp->propdict), OBJHASHSIZE);
		}
		else {
		    for (i = 3; i < objc; i++)
			PropertyDelete(tp->name, fnum, Tcl_GetString(objv[i]));
		}
		break;

	    case ASSOCIATE_IDX:
		if (objc == 3) {
		    Tcl_WrongNumArgs(interp, 1, objv, "{property_key pin_name} ...");
		    return TCL_ERROR;
		}
		for (i = 3; i < objc; i++) {
		    // Each value must be a duplet
		    result = Tcl_ListObjLength(interp, objv[i], &llen);
		    if ((result != TCL_OK) || (llen != 2)) {
			Tcl_SetResult(interp, "Not a {key pin} pair list.",
					NULL);
		    }
		    else {
			result = Tcl_ListObjIndex(interp, objv[i], 0, &tobj1);
			if (result != TCL_OK) return result;
			result = Tcl_ListObjIndex(interp, objv[i], 1, &tobj2);
			if (result != TCL_OK) return result;
			PropertyAssociatePin(tp->name, fnum, Tcl_GetString(tobj1),
					Tcl_GetString(tobj2));
		    }
		}
		break;

	    case TOLERANCE_IDX:
		if (objc == 3) {
		    Tcl_WrongNumArgs(interp, 1, objv, "{property_key tolerance} ...");
		    return TCL_ERROR;
		}
		for (i = 3; i < objc; i++) {
		    // Each value must be a duplet
		    result = Tcl_ListObjLength(interp, objv[i], &llen);
		    if ((result != TCL_OK) || (llen != 2)) {
			Tcl_SetResult(interp, "Not a {key tolerance} pair list.",
					NULL);
		    }
		    else {
			result = Tcl_ListObjIndex(interp, objv[i], 0, &tobj1);
			if (result != TCL_OK) return result;
			result = Tcl_ListObjIndex(interp, objv[i], 1, &tobj2);
			if (result != TCL_OK) return result;

			result = Tcl_GetIntFromObj(interp, tobj2, &ival);
			if (result != TCL_OK) {
			    Tcl_ResetResult(interp);
			    if (!strncasecmp(Tcl_GetString(tobj2), "inf", 3)) {
				ival = 1<<30;
				dval = 1.0E+300;
			    }
			    else if ((result = Tcl_GetDoubleFromObj(interp, tobj2, &dval))
					!= TCL_OK)
				return result;
			}
			PropertyTolerance(tp->name, fnum, Tcl_GetString(tobj1),
					ival, dval);
		    }
		}
		break;

	    case MERGE_IDX:
		// NOTE: This command option is deprecated, kept for backwards
		// compatibility, with updated flag values.  This command format
		// is unable to specify a property as being a critical property
		// for merging both in series and in parallel.

		if (objc == 3) {
		    Tcl_WrongNumArgs(interp, 1, objv, "{property_key merge_type} ...");
		    return TCL_ERROR;
		}
		for (i = 3; i < objc; i++) {
		    // Each value must be a duplet
		    result = Tcl_ListObjLength(interp, objv[i], &llen);
		    if ((result != TCL_OK) || (llen != 2)) {
			Tcl_SetResult(interp, "Not a {key merge_type} pair list.",
					NULL);
		    }
		    else {
			int mergeval;

			result = Tcl_ListObjIndex(interp, objv[i], 0, &tobj1);
			if (result != TCL_OK) return result;
			result = Tcl_ListObjIndex(interp, objv[i], 1, &tobj2);
			if (result != TCL_OK) return result;

			result = Tcl_GetIndexFromObj(interp, tobj2,
				(CONST84 char **)mergeoptions,
				"merge_type", 0, &idx2);
			if (result != TCL_OK) return result;

			switch (idx2) {
			    case NONE_IDX:
				mergeval = MERGE_NONE;
				break;
			    case ADD_ONLY_IDX:
				mergeval = MERGE_P_ADD;
				break;
			    case ADD_CRIT_IDX:
				mergeval = MERGE_P_ADD | MERGE_P_XCRIT;
				break;
			    case PAR_ONLY_IDX:
			    case PAR2_ONLY_IDX:
				mergeval = MERGE_P_PAR;
				break;
			    case PAR_CRIT_IDX:
			    case PAR2_CRIT_IDX:
				mergeval = MERGE_P_PAR | MERGE_P_XCRIT;
				break;
			    case SER_CRIT_IDX:
			    case SER2_CRIT_IDX:
			    case SER3_CRIT_IDX:
				mergeval = MERGE_S_ADD | MERGE_S_XCRIT;
				break;
			    case SER_IDX:
			    case SER2_IDX:
			    case SER3_IDX:
				mergeval = MERGE_S_ADD;
				break;
			}
			PropertyMerge(tp->name, fnum, Tcl_GetString(tobj1), mergeval,
				MERGE_ALL_MASK);
		    }
		}
		break;

	}
    }
    return TCL_OK;
}

/*--------------------------------------------------------------*/
/* Function name: _netcmp_permute				*/
/* Syntax: netgen::permute [default]				*/
/*	   netgen::permute permute_class			*/
/*	   netgen::permute [pins] valid_cellname pin1 pin2	*/
/*	   netgen::permute forget valid_cellname		*/
/*	   netgen::permute forget				*/
/* Formerly: t							*/
/* Results:							*/
/* Side Effects:						*/
/*--------------------------------------------------------------*/

int
_netcmp_permute(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *model, *pin1, *pin2;
   char *permuteclass[] = {
      "transistors", "resistors", "capacitors", "inductors",
		"default", "forget", "pins", NULL
   };
   enum OptionIdx {
      TRANS_IDX, RES_IDX, CAP_IDX, IND_IDX, DEFLT_IDX, FORGET_IDX, PINS_IDX
   };
   int result, index, fnum = -1;
   struct nlist *tp = NULL;

   if (objc > 5) {
      Tcl_WrongNumArgs(interp, 1, objv, "?valid_cellname pin1 pin2?");
      return TCL_ERROR;
   }
   if (objc == 1) {
      index = DEFLT_IDX;
   }
   else {
      if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)permuteclass,
		"permute class", 0, &index) != TCL_OK) {
	    if (objc != 4) {
		Tcl_WrongNumArgs(interp, 1, objv, "?valid_cellname pin1 pin2?");
		return TCL_ERROR;
	    }
	    result = CommonParseCell(interp, objv[1], &tp, &fnum);
	    if (result != TCL_OK) {
		Fprintf(stdout, "No such device \"%s\".\n",
			Tcl_GetString(objv[1]));
		return result;
	    }
	    index = PINS_IDX;
	    pin1 = Tcl_GetString(objv[2]);
	    pin2 = Tcl_GetString(objv[3]);
      }
      else if (index == PINS_IDX) {
	 if (objc != 5) {
	    Tcl_WrongNumArgs(interp, 1, objv, "pins ?valid_cellname pin1 pin2?");
	    return TCL_ERROR;
	 }
	 result = CommonParseCell(interp, objv[2], &tp, &fnum);
	 if (result != TCL_OK) {
	     Fprintf(stdout, "No such device \"%s\".\n",
			Tcl_GetString(objv[2]));
	     return result;
	 }
	 pin1 = Tcl_GetString(objv[3]);
	 pin2 = Tcl_GetString(objv[4]);
      }
      else if (index == FORGET_IDX) {
	 if (objc < 3) {
	    /* General purpose permute forget */
	    tp = FirstCell();
	    while (tp != NULL) {
	       PermuteForget(tp->name, tp->file, NULL, NULL);
	       tp = NextCell();
	    }
	    return TCL_OK;
	 }
	 else {
	    /* Specific permute forget */
	    result = CommonParseCell(interp, objv[2], &tp, &fnum);
	    if (result != TCL_OK) {
		Fprintf(stdout, "No such device \"%s\".\n",
			Tcl_GetString(objv[2]));
		return result;
	    }
	    if (objc == 5) {
	       pin1 = Tcl_GetString(objv[3]);
	       pin2 = Tcl_GetString(objv[4]);
	       if (PermuteForget(tp->name, fnum, pin1, pin2))
	          Fprintf(stdout, "Model %s pin %s != %s\n", tp->name, pin1, pin2);
	       else
	          Fprintf(stderr, "Unable to reset model %s pin permutation %s, %s.\n",
			tp->name, pin1, pin2);
	    }
	    else {
	       if (PermuteForget(tp->name, fnum, NULL, NULL))
	          Fprintf(stdout, "No permutations on circuit %s\n", tp->name);
	       else
	          Fprintf(stderr, "Unable to reset model %s pin permutations.\n",
			tp->name);
	    }
	    return TCL_OK;
	 }
      }
   }

   if (objc == 1 || objc == 2) {
      tp = FirstCell();
      while (tp != NULL) {
	 switch (tp->class) {
	    case CLASS_NMOS: case CLASS_PMOS: case CLASS_FET3:
	    case CLASS_NMOS4: case CLASS_PMOS4: case CLASS_FET4:
	    case CLASS_FET:
	       if (index == TRANS_IDX || index == DEFLT_IDX)
	          PermuteSetup(tp->name, tp->file, "source", "drain");
	       break;
	    case CLASS_RES: case CLASS_RES3:
	       if (index == RES_IDX || index == DEFLT_IDX)
	          PermuteSetup(tp->name, tp->file, "end_a", "end_b");
	       break;
	    case CLASS_INDUCTOR:
	       if (index == IND_IDX || index == DEFLT_IDX)
	          PermuteSetup(tp->name, tp->file, "end_a", "end_b");
	       break;
	    case CLASS_CAP: case CLASS_ECAP: case CLASS_CAP3:
	       if (index == CAP_IDX)
	          PermuteSetup(tp->name, tp->file, "top", "bottom");
	       break;
	 }
	 tp = NextCell();
      }
   }
   else if (index == PINS_IDX) {
      if (PermuteSetup(tp->name, fnum, pin1, pin2))
         Fprintf(stdout, "Model %s pin %s == %s\n", tp->name, pin1, pin2);
      else
         Fprintf(stderr, "Unable to permute model %s pins %s, %s.\n",
			tp->name, pin1, pin2);
   }
   else {
      Tcl_WrongNumArgs(interp, 1, objv, "?valid_cellname pin1 pin2?");
      return TCL_ERROR;
   }
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_symmetry			*/
/* Syntax: netgen::symmetry [fast|full]			*/
/* Formerly: x						*/
/* Results:						*/
/* Side Effects:					*/
/* Notes:  Deprecated, retained for compatibility.	*/
/*------------------------------------------------------*/

int
_netcmp_symmetry(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   Printf("Symmetry breaking method has been deprecated.\n");
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_exhaustive			*/
/* Syntax: netgen::exhaustive [on|off]			*/
/* Formerly: x						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netcmp_exhaustive(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *yesno[] = {
      "on", "off", NULL
   };
   enum OptionIdx {
      YES_IDX, NO_IDX
   };
   int result, index;

   if (objc == 1)
      index = -1;
   else {
      if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)yesno,
		"option", 0, &index) != TCL_OK)
         return TCL_ERROR;
   }

   switch(index) {
      case YES_IDX:
	 ExhaustiveSubdivision = TRUE;
	 break;
      case NO_IDX:
	 ExhaustiveSubdivision = FALSE;
	 break;
   }
   Printf("Exhaustive subdivision %s.\n", 
	     ExhaustiveSubdivision ? "ENABLED" : "DISABLED");

   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_restart			*/
/* Syntax: netgen::restart				*/
/* Formerly: o						*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netcmp_restart(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   if (objc != 1) {
      Tcl_WrongNumArgs(interp, 1, objv, "(no arguments)");
      return TCL_ERROR;
   }
   RegroupDataStructures();
   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netgen_help				*/
/* Syntax: netgen::help					*/
/* Formerly: [any invalid command]			*/
/* Results:						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netgen_help(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   int n;

   if (objc != 1) {
      Tcl_WrongNumArgs(interp, 1, objv, "(no arguments)");
      return TCL_ERROR;
   }

   for (n = 0; netgen_cmds[n].name != NULL; n++) {
      Printf("netgen::%s", netgen_cmds[n].name);
      Printf(" %s\n", netgen_cmds[n].helptext);
   }
   for (n = 0; netcmp_cmds[n].name != NULL; n++) {
      Printf("netgen::%s", netcmp_cmds[n].name);
      Printf(" %s\n", netcmp_cmds[n].helptext);
   }

   return TCL_OK;
}

/*------------------------------------------------------*/
/* Function name: _netcmp_matching			*/
/* Syntax: netgen::matching [element|node] <name>	*/
/* Formerly: [no such function]				*/
/* Results: 						*/
/* Side Effects:					*/
/*------------------------------------------------------*/

int
_netcmp_matching(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *options[] = {
      "nodes", "elements", NULL
   };
   enum OptionIdx {
      NODE_IDX, ELEM_IDX
   };
   int result, index;
   struct objlist *obj;
   char *name;

   if (objc != 2 &&  objc != 3) {
      Tcl_WrongNumArgs(interp, 1, objv, "?node|element? name");
      return TCL_ERROR;
   }

   if (objc == 2) {
      index = NODE_IDX;
      name = Tcl_GetString(objv[1]);
   }
   else {
      if (Tcl_GetIndexFromObj(interp, objv[1], (CONST84 char **)options,
		"option", 0, &index) != TCL_OK) {
         return TCL_ERROR;
      }
      name = Tcl_GetString(objv[2]);
   }

   switch(index) {
      case NODE_IDX:
	 result = EquivalentNode(name, NULL, &obj);
	 if (result > 0)
	    Tcl_SetResult(interp, obj->name, NULL);
	 else {
	    if (result < 0)
	       Tcl_SetResult(interp, "No such node.", NULL);
	    else
	       Tcl_SetResult(interp, "No matching node.", NULL);
	    return TCL_ERROR;
	 }
	 break;
      case ELEM_IDX:
	 result = EquivalentElement(name, NULL, &obj);
	 if (result > 0)
	    Tcl_SetResult(interp, obj->name, NULL);
	 else {
	    if (result < 0)
	       Tcl_SetResult(interp, "No such element.", NULL);
	    else
	       Tcl_SetResult(interp, "No matching element.", NULL);
	    return TCL_ERROR;
	 }
	 break;
   }

   if (obj == NULL) {
      Tcl_SetResult(interp, "Cannot find equivalent node", NULL);
      return TCL_ERROR;
   }
   return TCL_OK;
}


/*------------------------------------------------------*/
/* Define a calloc() function for Tcl			*/
/*------------------------------------------------------*/

char *tcl_calloc(size_t asize, size_t nbytes)
{
   size_t tsize = asize * nbytes;
   char *cp = Tcl_Alloc((int)tsize);
   bzero((void *)cp, tsize);
   return cp;
}

/*------------------------------------------------------*/
/* Redefine the printf() functions for use with tkcon	*/
/*------------------------------------------------------*/

void tcl_vprintf(FILE *f, const char *fmt, va_list args_in)
{
    va_list args;
    static char outstr[128] = "puts -nonewline std";
    char *outptr, *bigstr = NULL, *finalstr = NULL;
    int i, nchars, result, escapes = 0, limit;

    strcpy (outstr + 19, (f == stderr) ? "err \"" : "out \"");
    outptr = outstr;

    va_copy(args, args_in);
    nchars = vsnprintf(outptr + 24, 102, fmt, args);
    va_end(args);

    if (nchars >= 102)
    {
	va_copy(args, args_in);
	bigstr = Tcl_Alloc(nchars + 26);
	strncpy(bigstr, outptr, 24);
	outptr = bigstr;
	vsnprintf(outptr + 24, nchars + 2, fmt, args);
	va_end(args);
    }
    else if (nchars == -1) nchars = 126;

    for (i = 24; *(outptr + i) != '\0'; i++) {
	if (*(outptr + i) == '\"' || *(outptr + i) == '[' ||
	    	*(outptr + i) == ']' || *(outptr + i) == '\\' ||
		*(outptr + i) == '$')
	    escapes++;
	if (*(outptr + i) == '\n')
	    ColumnBase = 0;
	else
	    ColumnBase++;
    }

    if (escapes > 0)
    {
	finalstr = Tcl_Alloc(nchars + escapes + 26);
	strncpy(finalstr, outptr, 24);
	escapes = 0;
	for (i = 24; *(outptr + i) != '\0'; i++)
	{
	    if (*(outptr + i) == '\"' || *(outptr + i) == '[' ||
	    		*(outptr + i) == ']' || *(outptr + i) == '\\' ||
			*(outptr + i) == '$')
	    {
	        *(finalstr + i + escapes) = '\\';
		escapes++;
	    }
	    *(finalstr + i + escapes) = *(outptr + i);
	}
        outptr = finalstr;
    }

    *(outptr + 24 + nchars + escapes) = '\"';
    *(outptr + 25 + nchars + escapes) = '\0';

    result = Tcl_Eval(consoleinterp, outptr);

    if (bigstr != NULL) Tcl_Free(bigstr);
    if (finalstr != NULL) Tcl_Free(finalstr);
}
    
/*------------------------------------------------------*/
/* Console output flushing which goes along with the	*/
/* routine tcl_vprintf() above.				*/
/*------------------------------------------------------*/

void tcl_stdflush(FILE *f)
{   
   Tcl_SavedResult state;
   static char stdstr[] = "::flush stdxxx";
   char *stdptr = stdstr + 11;
    
   Tcl_SaveResult(netgeninterp, &state);
   strcpy(stdptr, (f == stderr) ? "err" : "out");
   Tcl_Eval(netgeninterp, stdstr);
   Tcl_RestoreResult(netgeninterp, &state);
}

/*------------------------------------------------------*/
/* Define a version of strdup() that uses Tcl_Alloc	*/
/* to match the use of Tcl_Free() for calls to FREE()	*/
/* Note objlist.h and config.h definitions for		*/
/* strsave() and STRDUP().				*/
/*------------------------------------------------------*/

char *Tcl_Strdup(const char *s)
{
   char *snew;
   int slen;

   slen = 1 + strlen(s);
   snew = Tcl_Alloc(slen);
   if (snew != NULL)
      memcpy(snew, s, slen);

   return snew;
}

/*------------------------------------------------------*/
/* Experimental---generate an interrupt condition	*/
/* from a Control-C in the console window.		*/
/* The console script binds this procedure to Ctrl-C.	*/
/*------------------------------------------------------*/

int _tkcon_interrupt(ClientData clientData,
    Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   InterruptPending = 1;
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/* Allow Tcl to periodically do (Tk) window events.  This	*/
/* will not cause problems because netgen is not inherently	*/
/* window based and only the console defines window commands.	*/
/* This also works with the terminal-based method although	*/
/* in that case, Tcl_DoOneEvent() should always return 0.	*/
/*--------------------------------------------------------------*/

int check_interrupt() {
   Tcl_DoOneEvent(TCL_WINDOW_EVENTS | TCL_DONT_WAIT);
   if (InterruptPending) {
      Fprintf(stderr, "Interrupt!\n");
      return 1;
   }
   return 0;
}

/*------------------------------------------------------*/
/* Tcl package initialization function			*/
/*------------------------------------------------------*/

int Tclnetgen_Init(Tcl_Interp *interp)
{
   int n;
   char keyword[128];
   char *cadroot;

   /* Sanity checks! */
   if (interp == NULL) return TCL_ERROR;

   /* Remember the interpreter */
   netgeninterp = interp;

   if (Tcl_InitStubs(interp, "8.5", 0) == NULL) return TCL_ERROR;
  
   for (n = 0; netgen_cmds[n].name != NULL; n++) {
      sprintf(keyword, "netgen::%s", netgen_cmds[n].name);
      Tcl_CreateObjCommand(interp, keyword, netgen_cmds[n].handler,
		(ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
   }
   for (n = 0; netcmp_cmds[n].name != NULL; n++) {
      sprintf(keyword, "netgen::%s", netcmp_cmds[n].name);
      Tcl_CreateObjCommand(interp, keyword, netcmp_cmds[n].handler,
		(ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
   }

   Tcl_Eval(interp, "namespace eval netgen namespace export *");

   /* Set $CAD_ROOT as a Tcl variable */

   cadroot = getenv("CAD_ROOT");
   if (cadroot == NULL) cadroot = CAD_DIR;
   Tcl_SetVar(interp, "CAD_ROOT", cadroot, TCL_GLOBAL_ONLY);

   Tcl_PkgProvide(interp, "Tclnetgen", NETGEN_VERSION);

   if ((consoleinterp = Tcl_GetMaster(interp)) == NULL)
      consoleinterp = interp;
   else
      Tcl_CreateObjCommand(consoleinterp, "netgen::interrupt", _tkcon_interrupt,
		(ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);

   InitializeCommandLine(0, NULL);
   sprintf(keyword, "Netgen %s.%s compiled on %s\n", NETGEN_VERSION,
		NETGEN_REVISION, NETGEN_DATE);
   Printf(keyword);

   return TCL_OK;	/* Drop back to interpreter for input */
}
