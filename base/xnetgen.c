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


/* xnetgen.c: X11 interface to netgen */

/*
 * define ONE of the following:
 *
 *   X11_HP_WIDGETS
 *   X11_MOTIF_WIDGETS
 *   X11_ATHENA_WIDGETS
 */


#ifdef HAVE_X11

#include "config.h"
#include <stdio.h>

/* #define volatile */  /* hacks for /usr/include/sys/types.h */
/* #define signed */

/* #define XLIB_ILLEGAL_ACCESS */ /* hack for SGI---not supposed to use Display->db */

#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xutil.h>
#include <X11/Shell.h>

#ifdef HPUX
#undef SIGCHLD
#endif

#include <setjmp.h>
#include <signal.h>

#include "netgen.h"
#include "timing.h"
#include "query.h"
#include "xnetgen.h"
#include "netcmp.h"
#include "objlist.h"
#include "print.h"
#include "dbug.h"




Widget toplevel = NULL;

char GlobalFileName[100], 
  GlobalCellName[100], 
  GlobalOtherName[100],
  GlobalDataName[100];

/*********************************************************
 * Menu structure:  attaches label string to a function,
 * and optionally points to a sub-menu.
 *********************************************************/

typedef struct menu_menu {
  char *name;
  void (*func)();
  struct menu_menu *submenu;
  caddr_t data;
} menu_struct;


/**********************************************************************
         USER - SUPPLIED ACTION COMMANDS
**********************************************************************/

Widget GlobalFileWidget;
Widget GlobalCellWidget;
Widget GlobalOtherWidget;
Widget GlobalDataWidget;

char *get_file(void);
char *get_cell(void);
char *get_other(void);
char *get_data(void);

static int timing = 0;
static float StartTime;

void X_END(void)
{
  if (timing) Printf("Execution time: %0.2f\n", ElapsedCPUTime(StartTime));
  Printf("\n");
  X_display_refresh();
}

void X_START(void)
{
  *GlobalFileName = '\0';
  *GlobalCellName = '\0';
  *GlobalOtherName = '\0';
  *GlobalDataName = '\0';
  if (timing) StartTime = CPUTime();
}

void no_command(Widget w, Widget textwidget, caddr_t call_data)
{
  XBell(XtDisplay(w), 100);
  Printf("No such command!\n");
  X_END();
}

void not_yet_implemented(Widget w, Widget textwidget, caddr_t call_data)
{
  XBell(XtDisplay(w), 100);
  Printf("Command not yet implemented!\n");
  X_END();
}

/*******************  OUTPUT FILE FORMATS *****************************/

void write_ntk(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  Ntk(get_cell(), NULL);
  X_END();
}

void write_actel(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  Actel(get_cell(), NULL);
  X_END();
}

void write_wombat(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  Wombat(get_cell(), NULL);
  X_END();
}

void write_ext(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  Ext(get_cell());
  X_END();
}

void write_sim(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  Sim(get_cell());
  X_END();
}

void write_spice(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  SpiceCell(get_cell(), -1, NULL);
  X_END();
}

void write_esacap(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  EsacapCell(get_cell(), NULL);
  X_END();
}

void write_ccode(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  Ccode(get_cell(), NULL);
  X_END();
}

void write_netgen(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  WriteNetgenFile(get_cell(), NULL);
  X_END();
}

static menu_struct WriteMenu[] = {
  { "NTK", write_ntk, NULL, NULL },
  { "Actel", write_actel, NULL, NULL },
  { "Wombat", write_wombat, NULL, NULL },
  { "Spice", write_spice, NULL, NULL },
  { "Esacap", write_esacap, NULL, NULL },
  { "Netgen", write_netgen, NULL, NULL },
  { "Ext",  write_ext, NULL, NULL },
  { "Sim",  write_sim, NULL, NULL },
  { "C code", write_ccode, NULL, NULL },
  { NULL }
};


/*******************  INPUT FILE FORMATS *****************************/

void read_ntk(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  ReadNtk(get_file());
  X_END();
}

void read_actel(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  Printf("Reading ACTEL library.\n");
  X_END();
}

void read_ext(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  ReadExtHier(get_file());
  X_END();
}

void read_sim(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  ReadSim(get_file());
  X_END();
}

void read_spice(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  ReadSpice(get_file());
  X_END();
}

void read_netgen(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  ReadNetgenFile(get_file());
  X_END();
}

static menu_struct ReadMenu[] = {
  { "NTK", read_ntk, NULL, NULL },
  { "Actel Library", read_actel, NULL, NULL },
  { "Spice", read_spice, NULL, NULL },
  { "Ext",  read_ext, NULL, NULL },
  { "Sim",  read_sim, NULL, NULL },
  { "Netgen", read_netgen, NULL, NULL },
  { NULL }
};


/**************************** NETCMP MENU ****************************/

void initialize_netcmp_datastructures(Widget w, Widget textwidget, 
				      caddr_t call_data)
{
  X_START();
  Printf("Comparing cells '%s' and '%s'\n", get_cell(), get_other());
  CreateTwoLists(get_cell(), get_other(), 0);
  Permute();
#ifdef DEBUG_ALLOC
  PrintCoreStats();
#endif
  X_END();
}

void iterate_netcmp(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  if (!Iterate()) Printf("Please iterate again\n");
  else Printf("No fractures made: NETCMP has converged\n");
  X_END();
}

void print_netcmp_automorphisms(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  PrintAutomorphisms();
  X_END();
}

void resolve_automorphisms(Widget w, Widget textwidget, caddr_t call_data)
{
  int automorphisms;

  X_START();
  while ((automorphisms = ResolveAutomorphisms()) > 0) ;
  if (automorphisms == -1) Printf("Graphs do not match.\n");
  else Printf("Circuits match correctly.\n");
  X_END();
}

void converge_netcmp(Widget w, Widget textwidget, caddr_t call_data)
{
  int automorphisms;
#if 1
  X_START();
  while (!Iterate()) ;
  automorphisms = VerifyMatching();
  if (automorphisms == -1) Printf("Graphs do not match.\n");
  else {
    if (automorphisms) 
      Printf("Circuits match with %d automorphisms\n", automorphisms);
    else Printf("Circuits match correctly.\n");
  }
  X_END();
#else
  while (!Iterate()) ;
  /* go check automorphisms */
  print_netcmp_automorphisms(w, textwidget, call_data);
#endif
}

void print_netcmp_classes(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  PrintElementClasses(ElementClasses, -1, 0);
  PrintNodeClasses(NodeClasses, -1, 0);
  X_END();
}

void verify_netcmp(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  if (ElementClasses == NULL || NodeClasses == NULL)
    Printf("Must initialize data structures first\n");
  else {
    int automorphisms;
    automorphisms = VerifyMatching();
    if (automorphisms == -1) Printf("Graphs do not match.\n");
    else {
      if (automorphisms) 
	Printf("Circuits match with %d automorphisms\n", automorphisms);
      else Printf("Circuits match correctly.\n");
    }
  }
  X_END();
}

void equivalence_netcmp_elements(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  Printf("Equivalence elements '%s' and '%s'\n", get_data(), get_other());
  if (EquivalenceElements(get_data(), -1, get_other(), -1)) 
    Printf("Done.\n");
  else Printf("Unable to equivalence elements %s and %s\n",
	      get_data(), get_other());
  X_END();
}

void equivalence_netcmp_nodes(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  Printf("Equivalence nodes '%s' and '%s'\n", get_data(), get_other());
  if (EquivalenceNodes(get_data(), -1, get_other(), -1)) 
    Printf("Done.\n");
  else Printf("Unable to equivalence nodes %s and %s\n",
	      get_data(), get_other());
  X_END();
}

void permute_netcmp_pins(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  Printf("Cell '%s': permuting pins '%s' and '%s'\n", 
	 get_cell(), get_data(), get_other());
  if (PermuteSetup(get_cell(), -1, get_data(), get_other())) 
    Printf("%s == %s\n",get_data(), get_other());
  else Printf("Unable to permute pins %s, %s\n",get_data(), get_other());
  X_END();
}

void permute_netcmp_transistors(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  if (PermuteSetup("n", -1, "drain", "source")) 
    Printf("n-channel: source == drain\n");
  if (PermuteSetup("p", -1, "drain", "source")) 
    Printf("p-channel: source == drain\n");
  if (PermuteSetup("e", -1, "bottom_a", "bottom_b")) 
    Printf("poly cap: permuting poly1 regions\n");
  if (PermuteSetup("r", -1, "end_a", "end_b")) 
    Printf("resistor: permuting endpoints\n");
  if (PermuteSetup("c", -1, "bot", "top")) 
    Printf("capacitor: permuting sides\n");
  X_END();
}

void exhaustive_netcmp_subdivision(Widget w, Widget textwidget, 
				   caddr_t call_data)
{
  X_START();
  ExhaustiveSubdivision = !ExhaustiveSubdivision;
  Printf("Exhaustive subdivision %s\n", 
	 ExhaustiveSubdivision ? "ENABLED" : "DISABLED");
  X_END();
}

void restart_netcmp(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  Printf("Resetting NETCMP data structures\n");
  RegroupDataStructures();
  X_END();
}

void summarize_netcmp_data(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  SummarizeElementClasses(ElementClasses);
  SummarizeNodeClasses(NodeClasses);
  X_END();
}

void sleep_ten_seconds(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  sleep(10);
  X_END();
}

static menu_struct NetcmpMenu[] = {
  { "Initialize", initialize_netcmp_datastructures, NULL, NULL },
  { "Iterate", iterate_netcmp, NULL, NULL },
  { "Converge", converge_netcmp, NULL, NULL },
  { "Print classes", print_netcmp_classes, NULL, NULL },
  { "Verify results", verify_netcmp, NULL, NULL },
  { "Print automorphisms", print_netcmp_automorphisms, NULL, NULL },
  { "Resolve automorphisms", resolve_automorphisms, NULL, NULL },
  { "Equivalence elements", equivalence_netcmp_elements, NULL, NULL },
  { "Equivalence nodes", equivalence_netcmp_nodes, NULL, NULL },
  { "Permute pins", permute_netcmp_pins, NULL, NULL },
  { "Permute source/drains", permute_netcmp_transistors, NULL, NULL },
  { "Exhaustive subdivision", exhaustive_netcmp_subdivision, NULL, NULL },
  { "Restart algorithm", restart_netcmp, NULL, NULL },
  { "Summarize datastructures", summarize_netcmp_data, NULL, NULL },
  { "SLEEP", sleep_ten_seconds, NULL, NULL},
  { NULL }
};

/**************************** PROTO MENU ****************************/


/* embedding sub-menu */

void proto_embed_greedy(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  ProtoEmbed(get_cell(), 'g');
  X_END();
}

void proto_embed_anneal(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  ProtoEmbed(get_cell(), 'a');
  X_END();
}

void proto_embed_random(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  ProtoEmbed(get_cell(), 'r');
  X_END();
}

void proto_embed_bottup(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  ProtoEmbed(get_cell(), 'o');
  X_END();
}


static menu_struct ProtoEmbedMenu[] = {
  { "Greedy", proto_embed_greedy, NULL, NULL },
  { "Anneal", proto_embed_anneal, NULL, NULL },
  { "Random", proto_embed_random, NULL, NULL },
  { "BottomUp", proto_embed_bottup, NULL, NULL },
  { NULL }
};


/* show-parameters sub-menu */

void proto_print_parameters(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  ProtoPrintParameters();
  X_END();
}

void proto_leaf_pinout(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  SetupLeafPinout(get_other());
  X_END();
}

void proto_rent_exp(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  SetupRentExp(get_other());
  X_END();
}

void proto_tree_fanout(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  SetupTreeFanout(get_other());
  X_END();
}

void proto_min_common_nodes(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  SetupMinCommonNodes(get_other());
  X_END();
}

void proto_min_used_leaves(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  SetupMinUsedLeaves(get_other());
  X_END();
}

static menu_struct ProtoConstMenu[] = {
  { "Show parameters", proto_print_parameters, NULL, NULL },
  { "Leaf pinout", proto_leaf_pinout, NULL, NULL },
  { "Rent's rule exp", proto_rent_exp, NULL, NULL },
  { "Tree fanout", proto_tree_fanout, NULL, NULL },
  { "Common node reqs", proto_min_common_nodes, NULL, NULL },
  { "Leaf containment", proto_min_used_leaves, NULL, NULL },
  { NULL }
};


void proto_toggle_logging(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  ToggleLogging();
  X_END();
}

void proto_toggle_exhaustive(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  ToggleExhaustive();
  X_END();
}

void proto_toggle_debug(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  ToggleDebug();
  X_END();
}

void proto_describe_cell(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  DescribeCell(get_cell(), 0);
  X_END();
}



static menu_struct ProtoMenu[] = {
  { "PROTOCHIP parameters", no_command, ProtoConstMenu, NULL },
  { "Embed cell", no_command, ProtoEmbedMenu, NULL },
  { "Toggle logging", proto_toggle_logging, NULL, NULL },
  { "Toggle exhaustive", proto_toggle_exhaustive, NULL, NULL },
  { "Toggle debug", proto_toggle_debug, NULL, NULL },
  { "Describe cell", proto_describe_cell, NULL, NULL },
  { NULL }
};



/****************************  PRINT MENU ******************************/

 
void print_cell(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  Printf("Contents of cell: %s\n", get_cell());
  PrintCell(get_cell());
  X_END();
}

void print_instances(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  Printf("Instances within cell: %s\n", get_cell());
  PrintInstances(get_cell());
  X_END();
}

void describe_instance(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  Printf("Describe instance: %s\n", get_cell());
  DescribeInstance(get_cell(), -1);
  X_END();
}

void print_nodes(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  Printf("Nodes in cell: %s\n", get_cell());
  PrintNodes(get_cell(), -1);
  X_END();
}

void print_nodes_connected_to(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  Fanout(get_cell(), get_data(), NODE);
  X_END();
}

void print_element(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  Printf("In cell %s\n",  get_cell());
  PrintElement(get_cell(), get_data());
  X_END();
}

void print_ports(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  Printf("Ports in cell: %s\n",  get_cell());
  PrintPortsInCell(get_cell(), -1);
  X_END();
}

void print_leaves(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  Printf("Leaves in cell: %s\n",  get_cell());
  PrintLeavesInCell(get_cell(), -1);
  X_END();
}


static menu_struct PrintMenu[] = {
  { "Print cell", print_cell, NULL, NULL},
  { "Print nodes", print_nodes, NULL, NULL},
  { "Print fanout", print_nodes_connected_to, NULL, NULL},
  { "Print element", print_element, NULL, NULL},
  { "Print instances", print_instances, NULL, NULL},
  { "Describe instance", describe_instance, NULL, NULL},
  { "Print ports", print_ports, NULL, NULL},
  { "Print leaves", print_leaves, NULL, NULL},
  { NULL }
};


/****************************  MAIN MENU *******************************/

void list_cells(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  PrintCellHashTable(1, -1);
  X_END();
}

void read_cell(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  Printf("Read file: %s\n", get_file());
  ReadNetlist(get_file());
  X_END();
}

void flatten_cell(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  Printf("Flatten cell: %s\n", get_cell());
  Flatten(get_cell(), -1);
  X_END();
}

void flatten_instances_of(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  Printf("Flatten instances of: %s\n", get_cell());
  FlattenInstancesOf(get_cell());
  X_END();
}


void print_all_leaves(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  Printf("Leaves\n");
  PrintAllLeaves();
  X_END();
}

void dbug_command(Widget w, Widget textwidget, caddr_t call_data)
{
  X_START();
  Printf("DBUG command: %s\n", get_data());
  DBUG_PUSH(get_data());
  X_END();
}

void read_command_file(Widget w, Widget textwidget, caddr_t call_data)
{
  FILE *oldfile;

  X_START();
  Printf("Reading command file: %s\n", get_file());
  oldfile = promptstring_infile;
  promptstring_infile = fopen(get_file(), "r");
  if (promptstring_infile == NULL)
    Printf("Unable to open command file: %s\n", get_file());
  else {
    Query();
    fclose(promptstring_infile);
    Printf("Finished executing command file: %s\n", get_file());
  }
  promptstring_infile = oldfile;
  X_END();
}

void dump_screen_to_file(Widget w, Widget textwidget, caddr_t call_data)
/* toggle dumping log file based on status of LoggingFile */
{
  Arg Args[10];
  int n;

  X_START();
  n = 0;
  XtSetArg(Args[n], XtNstring, "TOGGLE LOG"); n++;
  XtSetValues(w, Args, n);

  if (LoggingFile == NULL) {
    /* install logging file */
    FILE *file;
    file = fopen(get_file(), "r");
    if (file != NULL && strlen(get_file()) != 0) {
      Printf("File %s exists already.  Output written to 'netgen.log'.\n", 
	     get_file());
      fclose(file);
      file = fopen("netgen.log", "w");
    }
    else file = fopen(get_file(), "w");
    if (file == NULL) Printf("Unable to open file.\n");
    else {
      Printf("Logging enabled.\n");
      LoggingFile = file;
    }
#if 0
    /* dump the contents of the main window, line by line */
    int i;
    for (i = 0; i < data.nitems; i++) {
      fputs(data.chars[i], file);
      fputs("\n", file);
    }
#endif
  }
  else {
    /* log file is open, so close it */
    fclose(LoggingFile);
    LoggingFile = NULL;
    Printf("Logging file closed.\n");
  }
  X_END();
}


void toggle_timing(Widget w, Widget textwidget, caddr_t call_data)
{
  if (timing) {
    Printf("Timing disabled.\n");
    timing = 0;
    X_END;
  }
  else {
    Printf("Timing of commands enabled.\n");
    StartTime = CPUTime();
    X_END();
    timing = 1;
  }
}


void quit(Widget w, Widget textwidget, caddr_t call_data)
{
  if (LoggingFile != NULL) fclose(LoggingFile);
  XtDestroyWidget(toplevel);
  exit(0);
}


static menu_struct Menu[] = {
  { "List cells", list_cells, NULL, NULL},
  { "Print", no_command, PrintMenu, NULL},
  { "Flatten cell", flatten_cell, NULL, NULL},
  { "Flatten instances of", flatten_instances_of, NULL, NULL},
  { "List all leaves", print_all_leaves, NULL, NULL},
  { "Read cell", read_cell, ReadMenu, NULL },
  { "Write cell", no_command, WriteMenu, NULL },
/*
  how to create a popup menu
  { "popup",  pop_it_up, NULL, NULL},
*/
  { "NETCMP", no_command, NetcmpMenu, NULL},
  { "PROTOCHIP", no_command, ProtoMenu, NULL},
  { "DBUG command",   dbug_command, NULL, NULL},
  { "Read command file", read_command_file, NULL, NULL},
  { "Toggle log file", dump_screen_to_file, NULL, NULL},
  { "Toggle timing", toggle_timing, NULL, NULL},
  { "Quit",   quit, NULL, NULL},
  { NULL }
};

#if 0
#define ItemsInMenu(a) (sizeof (a) / sizeof(a[0]))
#else
int ItemsInMenu(menu_struct *menulist)
{
  menu_struct *p;
  int i;

  i = 0;
  for (p = menulist; p->name != NULL; p++) i++;
  return(i);
}
#endif
  

#if defined(X11_MOTIF_WIDGETS) || defined(X11_ATHENA_WIDGETS)

/* menu stuff:  emulate menus with persistent list widgets */

struct menu_list {
  Widget widget;
  menu_struct *menu;
};

/* the first component is the List widget that emulates the menu */
struct menu_list MenuArray[] = {
  {NULL, Menu}, 
  {NULL, WriteMenu}, 
  {NULL, ReadMenu}, 
  {NULL, PrintMenu}, 
  {NULL, NetcmpMenu}, 
  {NULL, ProtoEmbedMenu}, 
  {NULL, ProtoConstMenu}, 
  {NULL, ProtoMenu}, 
};  
#endif /* MOTIF or ATHENA */



#ifdef X11_HP_WIDGETS

/**********************************************************************/
/*             HP-Widget-specific code follows:                       */
/**********************************************************************/


#undef INTERNAL_ARGS
#define CELL_LIST_MENU

#undef INCLUDE_FALLBACK /* requires R4 Toolkit */

#include <X11/Xw/Xw.h>
#include <X11/Xw/Form.h>
#include <X11/Xw/WorkSpace.h>
#include <X11/Xw/ScrollBar.h>

#include <X11/Xw/RCManager.h>
#include <X11/Xw/BBoard.h>
#include <X11/Xw/VPW.h> 

/* these are for the menus */
#include <X11/Shell.h>
#include <X11/Xw/MenuBtn.h>
#include <X11/Xw/Cascade.h>
#include <X11/Xw/PopupMgr.h>

/* for the one-line editor */
#include <X11/Xw/TextEdit.h>
#include <X11/Xw/SText.h>

#include <X11/Xw/PButton.h>
#include <X11/Xw/TitleBar.h>

#include <X11/Xw/Valuator.h>
#include <X11/Xw/Arrow.h>
#include <X11/Xw/SWindow.h>


#define MAXLINESIZE 300
#define MAXLINES 200
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MARGIN 5

typedef struct {
  char *chars[MAXLINES];   /* Lines of text               */
  int ll[MAXLINES];        /* Length of each line         */
  int rbearing[MAXLINES];  /* right bearing of each line  */
  int descent;		   /* descent below baseline	  */
  int foreground,	   /* Color used for text	  */
      background;
  XFontStruct *font;	   /* The font struct		  */
  GC gc;		   /* A read/write GC		  */
  GC gcread;		   /* A read-only GC		  */
  Widget scrollbar;
  Widget canvas;
  Dimension canvas_height; /* canvas dimensions		  */
  Dimension canvas_width;
  int fontheight;	   /* descent + ascent            */
  int nitems;		   /* number of text lines	  */
  int top;		   /* line at top of window	  */
} text_data, *text_data_ptr;

text_data data;
text_data cells;


static XtResource resources[] = {
  { XtNfont, XtCFont, XtRFontStruct, sizeof(XFontStruct *),
      XtOffset(text_data_ptr, font), XtRString, "Fixed"      },
  { XtNforeground, XtCForeground, XtRPixel, sizeof(int),
      XtOffset(text_data_ptr, foreground), XtRString, "Black"},
  { XtNbackground, XtCBackground, XtRPixel, sizeof(int),
      XtOffset(text_data_ptr, background), XtRString, "White"}
};

String fallback_resources[] = { 
    "*yResizeable:                  True",
    "*yAttachBottom:                True",
    NULL,
  };

char *get_file(void) 
{
  return (XwTextCopyBuffer(GlobalFileWidget));
}

char *get_cell(void) 
{
  return (XwTextCopyBuffer(GlobalCellWidget));
}

char *get_other(void) 
{
  return (XwTextCopyBuffer(GlobalOtherWidget));
}

char *get_data(void) 
{
  return (XwTextCopyBuffer(GlobalDataWidget));
}


add_line(text_data *data, char *buf)
{
  /* this should be followed by a "refresh" as soon as possible */
  int foreground, background, dir, ascent, desc;
  XCharStruct char_info;
  int i;

#define JUMPSIZE 40
  if (data->nitems >= MAXLINES) {
    /* need to shuffle everything forward */
    for (i = 0; i < JUMPSIZE; i++) XtFree(data->chars[i]);

    for (i = JUMPSIZE; i < MAXLINES; i++) {
      data->chars[i-JUMPSIZE] = data->chars[i];
      data->ll[i - JUMPSIZE] = data->ll[i];
      data->rbearing[i - JUMPSIZE] = data->rbearing[i];
    }
    data->nitems -= JUMPSIZE;
    data->top -= JUMPSIZE;
  }

  i = data->nitems;
  data->chars[i] = XtMalloc(strlen(buf) + 1);
  strcpy(data->chars[i], buf);
  data->ll[i] = strlen(data->chars[i]);
  XTextExtents(data->font, data->chars[i], data->ll[i], &dir, &ascent,
	       &desc, &char_info);
  data->rbearing[i] = char_info.rbearing;
  data->descent = desc;
  data->fontheight = ascent + desc;
  while ((data->nitems - data->top) * data->fontheight > data->canvas_height) 
    (data->top)++;

  (data->nitems)++;
}
    

void X_display_line(char *buf)
{
  char *pt, *tmpbuf, *startpt;

  if (toplevel == NULL) {
    /* not using X windows */
    printf("%s", buf);
    return;
  }
  tmpbuf = XtMalloc(strlen(buf) + 1);
  strcpy(tmpbuf, buf);
#if 1
  /* eat last char if it is a '\n' */
  pt = strrchr(tmpbuf, '\n');
  if (pt != NULL && *(pt+1) == '\0') *pt = '\0';

  pt = tmpbuf;
  startpt = tmpbuf;
  for (pt = tmpbuf; *pt != '\0'; pt++) {
    if (*pt == '\n') {
      /* flush this as a single line */
      *pt = '\0';
      add_line(&data, startpt);
      *pt = 'a'; /* anything non-null */
      startpt = pt + 1;
    }
  }
  add_line(&data, startpt);
#else
  pt = tmpbuf;
  startpt = tmpbuf;
  while (*pt != '\0') {
    if (*pt == '\n') {
      /* eat trailing newlines */
      if (*(pt+1) != '\0') {
	*pt = '\0';
	add_line(&data, startpt);
	startpt = ++pt;
      }
    }
    else pt++;
    if (*pt == '\0') add_line(&data, startpt);
  }
#endif
  XtFree(tmpbuf);
}

void X_display_cell(char *buf)
{
  /* should be the same as X_display_line above */
  add_line(&cells, buf);
}


load_file(text_data *data, char *filename)
{
  FILE *fp, *fopen();
  char buf[MAXLINESIZE];
  /* Open the file.  */
  if ((fp = fopen(filename, "r")) == NULL) {
    fprintf(stderr, "Unable to open %s\n", filename);
    exit(1);
  }
  /* Read each line of the file into the buffer */
  while ((fgets(buf, MAXLINESIZE, fp)) != NULL) {
    buf[strlen(buf) - 1] = '\0';  /* strip NL at end */
    add_line(data, buf);
  }

  /* Close the file.  */
  fclose(fp);
}




load_cells(text_data *cells)
{
  int i;
  char buf[MAXLINESIZE];

  i = 0;
  for (i = 0; i < 10; i++) {
    sprintf(buf, "line %d", i);
    add_line(cells, buf);
  }
}

void scroll_bar_moved(Widget w, text_data *data, int sliderpos)
{
  XPoint points[4];
  Region region;
  int xsrc, ysrc, xdest, ydest;
  /* These points are the same for both cases, so set them here.   */
  points[0].x = points[3].x = 0;
  points[1].x = points[2].x = data->canvas_width;
  xsrc = xdest = 0;

/*  fprintf(stderr,"items = %d; slider pos = %d; lines = %d\n", 
	data->nitems, sliderpos, data->canvas_height / data->fontheight); */
  if (sliderpos < data->top) {  /* If we are scrolling down... */
    ysrc = 0;
    /* Convert the slider's position (rows) to pixels. */
    ydest = (data->top - sliderpos) * data->fontheight;
    /* Limit the destination to the window height. */
    if (ydest > data->canvas_height)
      ydest = data->canvas_height;
    /* Fill in the points array with the bounding box of the area that needs
       to be redrawn - that is, the area that is not copied.    */
    points[1].y = points[0].y = 0;
    points[3].y = points[2].y = ydest + data->fontheight;
  } else {                       /* If we are scrolling up... */
    ydest = 0;
    /* Convert the slider's position (rows) to pixels.  */
    ysrc = (sliderpos - data->top) * data->fontheight;
    /* Limit the source to the window height.   */
    if (ysrc > data->canvas_height)
      ysrc = data->canvas_height;
    /* Fill in the points array with the bounding box of the area that needs
       to be redrawn.  This area cannot be copied and must be redrawn.    */
    points[1].y = points[0].y = data->canvas_height - ysrc;
    points[2].y = points[3].y = data->canvas_height;
  }
  /* Set the top line of the text buffer.  */
  data->top = sliderpos;
  /* Copy the scrolled region to its new position.   */
  XCopyArea(XtDisplay(data->canvas), XtWindow(data->canvas),
	    XtWindow(data->canvas), data->gcread, xsrc, ysrc, 
	    data->canvas_width, data->canvas_height, xdest, ydest);
  /* Clear the remaining area of any old text.   */
  XClearArea(XtDisplay(w), XtWindow(data->canvas), points[0].x, points[0].y,
	     0, points[2].y - points[0].y, 0);
  /* Create a region from the points array, and call the XtNexpose callback
     with the calculated region as call_data.   */
  region = XPolygonRegion(points, 4, EvenOddRule);
  XtCallCallbacks(data->canvas, XtNexpose, region);
  /* Free the region.  */
  XDestroyRegion(region);
}

/******************************************************
 * slider.c: utility to make slider move to the sprite
 *           location when clicking the background of
 *           a scrollbar widget.
 ******************************************************/

void slider_selected(Widget w, caddr_t ignore, int sliderpos)
{
  Arg Args[1];
  /* Move the slider bar to the selected point.   */
  XtSetArg(Args[0], XtNsliderOrigin, sliderpos);
  XtSetValues(w, Args, 1);
  /* Call the callback list for XtNsliderMoved to
     alter the colors appropriately.  */
  XtCallCallbacks(w, XtNsliderMoved, sliderpos);
}


create_scrollbar(Widget parent, text_data *data)
{
  int n = 0;
  Arg Args[10];

  n = 0;
#ifdef INTERNAL_ARGS
  XtSetArg(Args[n], XtNyResizable, (XtArgVal)True); n++;
  XtSetArg(Args[n], XtNyAttachBottom, (XtArgVal)True); n++;
  XtSetArg(Args[n], XtNxResizable, (XtArgVal)True); n++;
  XtSetArg(Args[n], XtNxRefName, (XtArgVal)"scrollbar"); n++;
  XtSetArg(Args[n], XtNxAddWidth, (XtArgVal)True); n++;
  XtSetArg(Args[n], XtNxAttachRight, (XtArgVal)True); n++;
#endif

  data->scrollbar = XtCreateManagedWidget("scrollbar", XwscrollbarWidgetClass,
					  parent, NULL, 0);
  XtAddCallback(data->scrollbar, XtNsliderMoved, scroll_bar_moved, data);
  XtAddCallback(data->scrollbar, XtNareaSelected, slider_selected, data);

  /* Scrollbar movements are reported in terms of lines of text.  */
  n = 0;
  XtSetArg(Args[n], XtNsliderMin, 0); n++;
  XtSetArg(Args[n], XtNsliderMax, 2*(data->nitems)); n++;
  XtSetArg(Args[n], XtNsliderOrigin, data->top); n++;
  XtSetArg(Args[n], XtNsliderExtent, data->nitems); n++;
  XtSetValues(data->scrollbar, Args, n);
}


void refresh(text_data *data)
{
  /* redraw the data window, setting the valuator as required
     to put the last line at the bottom */

  int lines;
  Arg Args[8];
  int n;
  int bottom_of_screen, slider_size;
  XPoint points[4];
  Region region;

  lines = 1;
  if (data->fontheight == 0) bottom_of_screen = 0;
  else {
    lines = data->canvas_height / data->fontheight;

    if (lines > data->nitems) data->top = 0;
    else
      if (data->nitems - data->top > lines) data->top = data->nitems - lines;

    /* scrollbar movements are reported in terms of lines of text.  */

    bottom_of_screen = data->nitems - (data->canvas_height / data->fontheight);
    if (bottom_of_screen <= 0) bottom_of_screen = 0;
  }
  /* fprintf(stderr,"bottom of screen = %d\n",bottom_of_screen); */

  /* set up the valuator */
  n = 0;
  XtSetArg(Args[n], XtNsliderMin, 0); n++;
  XtSetArg(Args[n], XtNsliderMax, bottom_of_screen + lines); n++;
  XtSetArg(Args[n], XtNsliderOrigin, data->top); n++;
  XtSetArg(Args[n], XtNsliderExtent, lines); n++;
  XtSetValues(data->scrollbar, Args, n);

  /* Redraw the entire canvas */
  points[0].x = points[3].x = 0;
  points[1].x = points[2].x = data->canvas_width;
  points[1].y = points[0].y = 0;
  points[2].y = points[3].y = data->canvas_height;

  /* Clear the remaining area of any old text.  */
  XClearArea(XtDisplay(data->canvas), XtWindow(data->canvas), 
	     points[0].x, points[0].y,  0, points[2].y - points[0].y, 0);
  /* Create a region from the points array, and call the XtNexpose callback
     with the calculated region as call_data.   */
  region = XPolygonRegion(points, 4, EvenOddRule);
  XtCallCallbacks(data->canvas, XtNexpose, region);
  /* Free the region.  */
  XDestroyRegion(region);
}

void X_display_refresh(void)
{
  if (toplevel == NULL) return;
  refresh(&cells);
  refresh(&data);
}

void X_clear_display(void)
{
  int i;

  for (i = 0; i < data.nitems; i++) XtFree(data.chars[i]);
  data.top = data.nitems = 0;
  refresh(&data);
}
    
void X_clear_cell(void)
{
  int i;

  for (i = 0; i < cells.nitems; i++) XtFree(cells.chars[i]);
  cells.top = cells.nitems = 0;
  refresh(&cells);
}

create_gcs(text_data *data)
{
  XGCValues gcv;
  Display *dpy = XtDisplay(data->canvas);
  Window w = XtWindow(data->canvas);
  int mask = GCFont | GCForeground | GCBackground;
  int read_only_mask = GCForeground | GCBackground;
  /* Create two graphics contexts.  One is modifiable, one is read only.  */
  gcv.foreground = data->foreground;
  gcv.background = data->background;
  gcv.font = data->font->fid;
  data->gc = XCreateGC(dpy, w, mask, &gcv);
  data->gcread = XtGetGC(data->canvas, read_only_mask, &gcv);
}


void handle_exposures(Widget w, text_data *data, Region region)
{
  int yloc = 0, index = data->top;
  /* Set the clip mask of the GC. */
  XSetRegion(XtDisplay(w), data->gc, region);
  /* Loop through each line until the bottom of the   window is reached, 
     or we run out of lines.  Redraw lines that intersect the exposed region */
  while (index < data->nitems && yloc < data->canvas_height) {
    yloc += data->fontheight;
    if (XRectInRegion(region, 0, yloc - data->fontheight, data->canvas_width,
		      data->fontheight) != RectangleOut) 
      XDrawImageString(XtDisplay(w), XtWindow(w), data->gc, MARGIN, yloc,
		       data->chars[index], data->ll[index]);
    index++;
  }
}


void getsize(Widget w, text_data *data, caddr_t call_data)
{
  Arg Args[2];

  XtSetArg(Args[0], XtNheight, &data->canvas_height);
  XtSetArg(Args[1], XtNwidth, &data->canvas_width);
  XtGetValues(w, Args, 2);
}


/***********************************************************
 * one_line.c: Create a single line editable text field
 ***********************************************************/

/* Just ring the terminal bell. */
static void beep(Widget w, XEvent *event, String *params, int num_params)
{
  /* XBell(XtDisplay(w), 100); */
}

/* Associate the action "beep" with the function. */
static XtActionsRec actionsTable[] = {
  { "beep", beep },
};
/*
 * Override all translations that enter a newline.
 */
static char defaultTranslations[] =
  "Ctrl<Key>J:           beep() \n\
   Ctrl<Key>O:           beep() \n\
   Ctrl<Key>M:           beep() \n\
   <Key> Return:         beep()";

Widget create_one_line_text_widget(char *name, Widget parent)
{
  XFontStruct *font;
  Widget w;
  Arg Args[1];
  XtTranslations trans_table;
  /* Add the actions and compile the translations.  */
  XtAddActions(actionsTable, XtNumber(actionsTable));
  trans_table = XtParseTranslationTable(defaultTranslations);
  /* Create a TextEdit widget. */
  XtSetArg(Args[0], XtNeditType, XwtextEdit);
  w = XtCreateManagedWidget(name, XwtexteditWidgetClass, parent, Args, 1);
  /* Install our translations. */
  XtOverrideTranslations(w, trans_table);
  /* Get the font used by the widget.  */
  XtSetArg(Args[0], XtNfont, &font);
  XtGetValues(w, Args, 1);
  /* Set the widget height according to the font height.  */
#define FONTHEIGHT(f)  ((f)->max_bounds.ascent + (f)->max_bounds.descent)

  XtSetArg(Args[0], XtNheight, FONTHEIGHT(font) + 6);
  XtSetValues(w, Args, 1);

  return(w);
}

/**************************************************************************/

Widget create_menu_manager(Widget parent, char *mgrname)
{
  Widget shell = XtCreatePopupShell(mgrname, shellWidgetClass, parent, NULL,0);
  Widget menu_mgr = 
    XtCreateManagedWidget(mgrname, XwpopupmgrWidgetClass, shell, NULL, 0);
  return(menu_mgr);
}


void create_pane(Widget mgr, char *mgrname, char *name, 
		 menu_struct *menulist, int nitems)
{
  Arg Args[1];
  Widget menupane, pane_shell;
  int i;
  WidgetList buttons;
  /* Allocate a widget list to hold all button widgets.  */
  buttons = (WidgetList) XtMalloc(nitems * sizeof(Widget));
  /* Create a popup shell to hold this pane.  */
  pane_shell = XtCreatePopupShell("pane_shell", shellWidgetClass, mgr,
				  NULL, 0);
  /* Create a Cascade menu pane, and attach it to the given menu manager.   */
  XtSetArg(Args[0], XtNattachTo, (XtArgVal) mgrname);
  menupane = XtCreateManagedWidget(name, XwcascadeWidgetClass, pane_shell,
				   Args, 1);
  /* Create a menu button for each item in the menu.  */
  for (i = 0; i < nitems; i++) {
    buttons[i] = XtCreateWidget(menulist[i].name, XwmenubuttonWidgetClass,
				menupane, NULL, 0);
    XtAddCallback(buttons[i], XtNselect, menulist[i].func, menulist[i].data);
  }
  /* Manage all button widgets.   */
  XtManageChildren(buttons, nitems);
}


/*******************************************************
  Popup widgetry -- not currently used, but good to have around
*********************************************************/


static char *fields[] = { "field1", "field2", "field3" };
static char *labels[] = { "label1", "label2", "label3" };

void pop_it_down(Widget w, caddr_t client_data, caddr_t call_data)
{
  Widget bb, shell;

  bb = XtParent(w);
  shell = XtParent(bb);

  XtPopdown(shell);
  XtDestroyWidget(shell);
}


Widget create_filename_editor(Widget parent)
{
  Widget bb, popup, button;

  popup = 
    XtCreatePopupShell ("popup", overrideShellWidgetClass, parent,  NULL, 0);
  /* Create a BulletinBoard widget to hold the fields.  */
  bb = XtCreateManagedWidget("board", XwbulletinWidgetClass, popup, NULL, 0);

  /* Create a "done" button and register a popdown callback.  */
  button = XtCreateManagedWidget("done", XwpushButtonWidgetClass, bb, NULL, 0);
  XtAddCallback(button, XtNrelease, pop_it_down, NULL);

  create_one_line_text_widget("field1",bb);
  return popup;
}


void pop_it_up(Widget w, caddr_t client_data, caddr_t call_data)
{
  Widget shell;
  Window root, child;
  int root_x, root_y, win_x, win_y, mask;

  XQueryPointer(XtDisplay(w), XtWindow(w), &root, &child, &root_x,
		&root_y, &win_x, &win_y, &mask);

  shell = create_filename_editor(w);

  XtRealizeWidget(shell);
  XtMoveWidget(shell, root_x - 30, root_y - 20);

  XtPopup(shell, XtGrabExclusive);
}

/*********************************************************************/

Widget label_widget(Widget parent, char *string)
{
  Arg Args[10];
  int n;
  
  n = 0;
  XtSetArg(Args[n], XtNstring, string); n++;
  return XtCreateManagedWidget
    ("label", XwstatictextWidgetClass, parent, Args, n);
}

static jmp_buf jmpenv;

static void handler() 
/* static void handler(int sig) */
{
  Fprintf(stderr,"\nInterrupt!!\n");
  Fflush(stderr);
  X_END();
  longjmp(jmpenv,1);
}


static XrmOptionDescRec opTable[] = {
{"-sadfa","Netgen*yResizable", XrmoptionNoArg,	(caddr_t) "True"},
{"-sszyys","Netgen*yAttachBottom", XrmoptionNoArg, (caddr_t) "True"},
};

/*char datastring[] = "*yResizable: True\n*yAttachBottom: True";*/
char datastring[] = "*yAttachBottom: True";

/*
Fileview*canvas.xRefName:	scrollbar
Fileview*canvas.xResizable:	True
Fileview*canvas.xAddWidth:	True
Fileview*canvas.xAttachRight:	True
Fileview*canvas.yAttachTop:	True
*/

void X_main_loop(int argc, char *argv[])
{
  /* toplevel widget is global */
  Widget pane, frame, cells_frame, menu_mgr, title, data_entry;
  Arg Args[10];
  int n;
  XrmDatabase rdb;


  /* if we're not using X, just call good old Query(); */
  if (getenv("DISPLAY") == NULL) {
    Query();
    return;
  }

/* Xnetgen used to be Fileview */
#ifdef INCLUDE_FALLBACK
  XtAppContext app_con;
  toplevel = XtAppInitialize(&app_con, "NewNetgen", NULL, 0,
			     &argc, argv, fallback_resources, NULL, 0);
#else
  toplevel = XtInitialize(argv[0], "Netgen", 
			  opTable, XtNumber(opTable), &argc, argv);
#endif


  rdb = XrmGetStringDatabase(datastring);
  if (rdb != NULL)
    XrmMergeDatabases(rdb, &((XtDisplay(toplevel))->db));

  XtGetApplicationResources
    (toplevel, &data, resources, XtNumber(resources), NULL, 0);
  /* Read the file specified in argv[1] into the text buffer.  */
#if 0
  load_file(&data, (argc == 2) ? argv[1] : NULL);
#else
  X_display_line("Netgen" NETGEN_VERSION "." NETGEN_REVISION);
#endif

  XtGetApplicationResources
    (toplevel, &cells, resources, XtNumber(resources), NULL, 0);
#if 0
  load_cells(&cells); 
#else
  X_display_line("Netgen" NETGEN_VERSION "." NETGEN_REVISION);
#endif

  n = 0;
#ifdef INTERNAL_ARGS
  XtSetArg(Args[n], XtNyResizable, (XtArgVal)True); n++;
  XtSetArg(Args[n], XtNyAttachBottom, (XtArgVal)True); n++;
  XtSetArg(Args[n], XtNxResizable, (XtArgVal)True); n++;
  XtSetArg(Args[n], XtNxRefName, (XtArgVal)"scrollbar"); n++;
  XtSetArg(Args[n], XtNxAddWidth, (XtArgVal)True); n++;
  XtSetArg(Args[n], XtNxAttachRight, (XtArgVal)True); n++;
#endif
  /* Create a VPane widget as a base to allow children to be resized easily. */
  pane = XtCreateManagedWidget("pane", XwvPanedWidgetClass, toplevel, Args, n);

  /* create a top command and title area */
  title = label_widget(pane, NETGEN_VERSION);


  /* create data entry area in the form of a RowCol widget */
  n = 0;
  XtSetArg(Args[0], XtNcolumns, 2); n++;
  data_entry = XtCreateManagedWidget("rowcol",XwrowColWidgetClass,pane,Args,n);

  label_widget(data_entry, "File name:");
  GlobalFileWidget = create_one_line_text_widget("field1", data_entry);

  label_widget(data_entry, "Cell name:");
  GlobalCellWidget = create_one_line_text_widget("field1", data_entry);

  label_widget(data_entry, "Element:");
  GlobalDataWidget = create_one_line_text_widget("field1", data_entry);

  label_widget(data_entry, "2nd cell/elem:");
  GlobalOtherWidget = create_one_line_text_widget("field1", data_entry);


#ifdef CELL_LIST_MENU
  /* create the top cell list area */

  n = 0;
  cells_frame = XtCreateManagedWidget
    ("framework", XwformWidgetClass, pane, Args, n);
  create_scrollbar(cells_frame, &cells);

  /* Create the drawing surface.  */
  n = 0;
  XtSetArg(Args[n], XtNwidth, (XtArgVal)500); n++;
  XtSetArg(Args[n], XtNheight, (XtArgVal)100); n++; 
  cells.canvas = XtCreateManagedWidget
    ("canvas", XwworkSpaceWidgetClass, cells_frame, Args, n);
  XtAddCallback(cells.canvas, XtNexpose, handle_exposures, &cells);
  XtAddCallback(cells.canvas, XtNresize, getsize, &cells);
#endif

  /* create the viewing area */
  n = 0;
#ifdef INTERNAL_ARGS
  XtSetArg(Args[n], XtNyResizable, (XtArgVal)True); n++;
  XtSetArg(Args[n], XtNyAttachBottom, (XtArgVal)True); n++;
  XtSetArg(Args[n], XtNxResizable, (XtArgVal)True); n++;
  XtSetArg(Args[n], XtNxRefName, (XtArgVal)"scrollbar"); n++;
  XtSetArg(Args[n], XtNxAddWidth, (XtArgVal)True); n++;
  XtSetArg(Args[n], XtNxAttachRight, (XtArgVal)True); n++;
#endif
/*  XtSetArg(Args[n], XtNxAttachTop, (XtArgVal)True); n++; */
  frame = XtCreateManagedWidget("framework", XwformWidgetClass, pane, Args, n);
  create_scrollbar(frame, &data);

  /* Create the drawing surface.  */
  n = 0;
  XtSetArg(Args[n], XtNwidth, (XtArgVal)500); n++;
  XtSetArg(Args[n], XtNheight, (XtArgVal)400); n++; 

#ifdef INTERNAL_ARGS
  XtSetArg(Args[n], XtNyResizable, (XtArgVal)True); n++;
  XtSetArg(Args[n], XtNyAttachBottom, (XtArgVal)True); n++;
  XtSetArg(Args[n], XtNxResizable, (XtArgVal)True); n++;
  XtSetArg(Args[n], XtNxRefName, (XtArgVal)"scrollbar"); n++;
  XtSetArg(Args[n], XtNxAddWidth, (XtArgVal)True); n++;
  XtSetArg(Args[n], XtNxAttachRight, (XtArgVal)True); n++;
#endif
  data.canvas = XtCreateManagedWidget
    ("canvas", XwworkSpaceWidgetClass, frame, Args, n);
  XtAddCallback(data.canvas, XtNexpose, handle_exposures, &data);
  XtAddCallback(data.canvas, XtNresize, getsize, &data);


  /* Create the menu manager.  */
  menu_mgr = create_menu_manager(title, "menu_mgr");
  /* Create the Main menu pane. */
  create_pane(menu_mgr, "menu_mgr", "Netgen Main Menu", Menu, 
	      ItemsInMenu(Menu));
  /* Create sub menus for various items on the main menu.  */
  create_pane(menu_mgr, "Print", "PRINT menu", PrintMenu, 
	      ItemsInMenu(PrintMenu));
  create_pane(menu_mgr, "Write cell", "WRITE menu", WriteMenu, 
	      ItemsInMenu(WriteMenu));
  create_pane(menu_mgr, "Read cell", "READ menu", ReadMenu, 
	      ItemsInMenu(ReadMenu));
  create_pane(menu_mgr, "NETCMP", "NETCMP menu", NetcmpMenu, 
	      ItemsInMenu(NetcmpMenu));
  create_pane(menu_mgr, "PROTOCHIP", "PROTOCHIP menu", ProtoMenu, 
	      ItemsInMenu(ProtoMenu));
  /* Create sub-sub menus for Protochip sub-menu */
  create_pane(menu_mgr, "Embed cell", "EMBED Algorithm", ProtoEmbedMenu, 
	      ItemsInMenu(ProtoEmbedMenu));
  create_pane(menu_mgr, "PROTOCHIP parameters", "PROTOCHIP Parameters",
	      ProtoConstMenu, ItemsInMenu(ProtoConstMenu));

  XtRealizeWidget(toplevel);

  /* Create the graphics contexts after realizing the widgets
     because create_gcs requires a valid window ID.   */
  create_gcs(&data);
  create_gcs(&cells); 

  /* a little magic to initialize all windows to bottom */
  getsize(cells.canvas, &cells, NULL);
  scroll_bar_moved(cells.scrollbar, &cells, 0); 
  refresh(&cells);

  getsize(data.canvas, &data, NULL);
  scroll_bar_moved(data.scrollbar, &data, 0); 
  refresh(&data);

#ifdef INCLUDE_FALLBACK
  XtAppMainLoop(app_con);
#else
  /* install a vector to trap ^C */
  setjmp(jmpenv);
  signal(SIGINT,handler);
  XtMainLoop();
#endif
}

#endif /* X11_HP_WIDGETS */

#ifdef X11_MOTIF_WIDGETS

/*************************************/
/*    MOTIF widget code              */
/*************************************/

/* define the following for scrolled text windows */
#undef USE_SCROLLING_TEXT

#include <Xm/Xm.h>
#include <Xm/List.h>
#include <Xm/Text.h>
#include <Xm/PanedW.h>
#include <Xm/Label.h>
#include <Xm/RowColumn.h>
#include <Xm/PushB.h>
#include <Xm/SelectioB.h>

XmStringCharSet cs = "ISOLatin1";

static char prompt_response[100];
int prompt_done;
int calling_editor;  /* which string are we trying to get ? */
#define FILE_NAME 1
#define CELL_NAME 2
#define OTHER_NAME 3
#define DATA_NAME 4

void prompt_callback(Widget w, caddr_t closure, caddr_t call_data)
{
  char *c;
  XmSelectionBoxCallbackStruct *cb = (XmSelectionBoxCallbackStruct *)call_data;

  XmStringGetLtoR(cb->value, cs, &c);
  strcpy(prompt_response, c);
  XtFree(c);
  prompt_done = 1;
}
  
void prompt_cancel_callback(Widget w, caddr_t closure, caddr_t call_data)
{
  *prompt_response = '\0';
  prompt_done = 1;
}

void prompt_save_callback(Widget w, caddr_t closure, caddr_t call_data)
{
  prompt_callback(w, closure, call_data);
  /* now we need to save the data in the original editor window */
  switch (calling_editor) {
  case FILE_NAME:
    XmTextSetString(GlobalFileWidget, prompt_response);
    break;
  case CELL_NAME:
    XmTextSetString(GlobalCellWidget, prompt_response);
    break;
  case OTHER_NAME:
    XmTextSetString(GlobalOtherWidget, prompt_response);
    break;
  case DATA_NAME:
    XmTextSetString(GlobalDataWidget, prompt_response);
    break;
  }
}

char *DialogWidget(char *prompt)
{
  Widget w;
  Arg Args[10];
  int n;

  *prompt_response = '\0';
  prompt_done = 0;
  n = 0;
  XtSetArg(Args[n], XmNselectionLabelString, 
	   XmStringCreateLtoR(prompt, cs)); n++;
  XtSetArg(Args[n], XmNdialogType, XmDIALOG_PROMPT); n++;
  XtSetArg(Args[n], XmNdialogStyle, XmDIALOG_APPLICATION_MODAL); n++;
  XtSetArg(Args[n], XmNautoUnmanage, True); n++;
  XtSetArg(Args[n], XmNhelpLabelString, XmStringCreateLtoR("Save", cs)); n++;

  XtSetArg(Args[n], XmNminHeight, 100); n++;
  XtSetArg(Args[n], XmNminWidth, 100); n++;

  w = XmCreatePromptDialog(toplevel, "prompt", Args, n);
  XtAddCallback(w, XmNokCallback, prompt_callback, NULL);
  XtAddCallback(w, XmNcancelCallback, prompt_cancel_callback, NULL);
  XtAddCallback(w, XmNhelpCallback, prompt_save_callback, NULL);
  XtManageChild(w); 

  /* we want the application to WAIT for any callback */
  while (!prompt_done) 
    XtProcessEvent(XtIMAll);

  return(prompt_response);
}

char *get_something(char *buf, Widget w, char *prompt)
{
  char *c;

  if (strlen(buf)) return(buf);

  c = XmTextGetString(w);
  strcpy(buf, c);
  XtFree(c);
  if (strlen(buf) == 0) {
    /* bring up a dialog widget to get the filename */
    strcpy(buf, DialogWidget(prompt));
  }
  return (buf);
}

char *get_file(void) 
{
  calling_editor = FILE_NAME;
  return(get_something(GlobalFileName, GlobalFileWidget, "Enter File Name"));
}

char *get_cell(void) 
{
  calling_editor = CELL_NAME;
  return(get_something(GlobalCellName, GlobalCellWidget, "Enter Cell Name"));
}

char *get_other(void) 
{
  calling_editor = OTHER_NAME;
  return(get_something(GlobalOtherName, GlobalOtherWidget, "Enter Name"));
}

char *get_data(void) 
{
  calling_editor = DATA_NAME;
  return(get_something(GlobalDataName, GlobalDataWidget, "Enter Data"));
}


Widget pane, frame, cells_frame, menu_mgr, title, data_entry;

#define DATABUFSIZ 4000

char data_buf[DATABUFSIZ];
char *data_endptr = data_buf;

void X_display_line(char *buf)
{
  if (toplevel == NULL) {
    /* not using X windows */
    printf("%s", buf);
    return;
  }

  if (data_endptr + strlen(buf) > data_buf + DATABUFSIZ) {
    char *cp;
    /* find the next line */
    cp = data_buf + strlen(buf);
    while (*cp != '\n' && cp < data_endptr) cp++;

    memcpy(data_buf, cp, data_endptr - cp + 1);
    data_endptr -= (cp - data_buf);
  }
  /* now just copy the string */
  strcpy(data_endptr, buf);
  data_endptr += strlen(buf);
  memset(data_endptr, 0, data_buf + DATABUFSIZ - data_endptr - 1);
}

#ifdef USE_SCROLLED_TEXT
void X_display_refresh(void)
{
  Arg Args[10];
  int n;

  short lines;
  char *cp;
  XmTextPosition offset, cursor;
  Boolean showcur;

  if (toplevel == NULL) return;

  n = 0;
  XtSetArg(Args[n], XmNrows, &lines); n++;
  XtSetArg(Args[n], XmNtopPosition, &offset); n++;
  XtSetArg(Args[n], XmNcursorPosition, &cursor); n++;
  XtSetArg(Args[n], XmNautoShowCursorPosition, &showcur); n++;
  XtGetValues(frame, Args, n);
  printf("Before: %d lines in the display, %ld chars offset %ld cursor (showing %d)\n",
	 (int)lines, (long)offset, (long)cursor, (int)showcur);

  cp = data_endptr;
  while (cp > data_buf && lines > 0) 
    if (*cp-- == '\n') lines--;
  
printf("lines = %d, cp = %p, data_buf = %p\n", (int)lines, cp, data_buf);
  if (lines == 0) cp++;
  if (cp < data_buf) cp = data_buf;

  /* set the editor to display the string */
  n = 0;
  XtSetArg(Args[n], XmNvalue, data_buf); n++;
#if 0
  printf("Setting offset to %d\n", (int)((XmTextPosition)(cp - data_buf)));
  XtSetArg(Args[n], XmNtopPosition, (XmTextPosition)(cp - data_buf)); n++; 
/*  XtSetArg(Args[n], XmNtopPosition, (XmTextPosition)5); n++; */
#else
  XtSetArg(Args[n], XmNautoShowCursorPosition, (XtArgVal)True); n++;
  printf("Setting cursor position to %d\n", 
	 (int)((XmTextPosition)(data_endptr - data_buf)));
  XtSetArg(Args[n], XmNcursorPosition, 
	   (XmTextPosition)(data_endptr - data_buf)); n++;
#endif  

  XtSetValues(frame, Args, n);

  n = 0;
  XtSetArg(Args[n], XmNrows, &lines); n++;
  XtSetArg(Args[n], XmNtopPosition, &offset); n++;
  XtSetArg(Args[n], XmNcursorPosition, &cursor); n++;
  XtGetValues(frame, Args, n);
  printf("After: %d lines in the display, %ld chars offset %ld cursor\n",
	 (int)lines, (long)offset, (long)cursor);
}
#else /* not USE_SCROLLED_TEXT */

void X_display_refresh(void)
/* very simple code that scrolls the window as required */
{
  Arg Args[10];
  int n;

  short lines;
  char *cp;

  if (toplevel == NULL) return;

  n = 0;
  XtSetArg(Args[n], XmNrows, &lines); n++;
  XtGetValues(frame, Args, n);

  cp = data_endptr;
  while (cp > data_buf && lines > 0) 
    if (*cp-- == '\n') lines--;
  
  if (lines == 0) cp++;
  if (cp < data_buf) cp = data_buf;

  /* set the editor to display the string */
  n = 0;
  XtSetArg(Args[n], XmNvalue, cp); n++;
  XtSetValues(frame, Args, n);
}

#endif /* not USE_SCROLLED_TEXT */



int menu_index_by_menu(menu_struct *mp)
{
  int i;
  for (i = 0; i < (sizeof(MenuArray)/sizeof(MenuArray[0])); i++) 
    if (MenuArray[i].menu == mp) return(i);
printf("menu_index_by_menu: this should never happen\n");
  return(0);
}


int menu_index_by_widget(Widget w)
{
  int i;
  for (i = 0; i < (sizeof(MenuArray)/sizeof(MenuArray[0])); i++) 
    if (MenuArray[i].widget == w) return(i);
  fprintf(stderr,"menu_index_by_widget: this should never happen\n");
  return(0);
}

menu_struct *find_menu_by_widget(Widget w)
{
  int i;
  for (i = 0; i < (sizeof(MenuArray)/sizeof(MenuArray[0])); i++) 
    if (MenuArray[i].widget == w) return(MenuArray[i].menu);
  fprintf(stderr,"find_menu_by_widget: this should never happen\n");
  return(NULL);
}

void ActivateMenu(menu_struct *mp);

void MenuCallback(Widget w, caddr_t closure, caddr_t call_data)
{
  XmListCallbackStruct *cb = (XmListCallbackStruct *)call_data;
  int i, ItemCount;
  menu_struct *menu;

  menu = find_menu_by_widget(w); 

  ItemCount = ItemsInMenu(menu);
  for (i = 0; i < ItemCount; i++) {
    char *s;
    XmStringGetLtoR(cb->item, cs, &s);
    if (!strcmp(s, menu[i].name)) {
      /* printf("Trying to run function: %s\n", menu[i].name); */
      if (menu[i].submenu != NULL) ActivateMenu(menu[i].submenu);
      else (*(menu[i].func))(w, NULL, NULL);
      break;
    }
  }
  XmListDeselectAllItems(w); 
}

void MenuDestroyCallback(Widget w, caddr_t closure, caddr_t call_data)
{
  int menunum;

  menunum = menu_index_by_widget(w);
  MenuArray[menunum].widget = NULL;
}

Widget make_menu(menu_struct *menu)
/* returns the List widget for the menu, NOT the top shell widget */
{
  int n;
  Arg Args[20];
  Widget top, widget;
  XmString *Items;
  int ItemCount;

  top = 
    XtCreateApplicationShell("menuShell", topLevelShellWidgetClass, NULL, 0);

  ItemCount = ItemsInMenu(menu);
  Items = (XmString *)CALLOC(ItemCount, sizeof(XmString));
  for (n = 0;  n < ItemCount; n++)
    Items[n] = (XmString)XmStringCreateLtoR(menu[n].name, cs);

  n = 0;
  XtSetArg(Args[n], XmNitems, (XtArgVal)Items); n++;
  XtSetArg(Args[n], XmNitemCount, (XtArgVal)ItemCount); n++;
  XtSetArg(Args[n], XmNvisibleItemCount, (XtArgVal)ItemCount); n++;
  XtSetArg(Args[n], XmNselectionPolicy, (XtArgVal)XmSINGLE_SELECT); n++; 

  XtSetArg(Args[n], XmNlistMarginHeight, (XtArgVal)20); n++;
  XtSetArg(Args[n], XmNlistMarginWidth, (XtArgVal)30); n++;
  XtSetArg(Args[n], XmNborderWidth, (XtArgVal)10); n++;
  
  widget = XmCreateList(top,"menu", Args, n);

  XtManageChild(widget);
  XtAddCallback(widget, XmNsingleSelectionCallback, MenuCallback, NULL);
  XtAddCallback(widget, XmNdestroyCallback, MenuDestroyCallback, NULL);

#if 0
  /* put all menus at a fixed position; this doesn't work for mwm */
  n = 0;
  XtSetArg(Args[n], XmNx, (XtArgVal)20); n++;
  XtSetArg(Args[n], XmNy, (XtArgVal)(30 + 50*menu_index_by_menu(menu))); n++;
  XtSetValues(top, Args, n);
#endif

  XtRealizeWidget(top);

  /* put all menus at a fixed position */
  n = 0;
  XtSetArg(Args[n], XmNx, (XtArgVal)20); n++;
  XtSetArg(Args[n], XmNy, (XtArgVal)(30 + 50*menu_index_by_menu(menu))); n++;
  XtSetValues(top, Args, n);

  return(widget);
}


void RaiseMenu(Widget w)
{
  /* just bring it up; remember to bring up the SHELL, the list's parent !!! */
  XMapRaised(XtDisplay(XtParent(w)), XtWindow(XtParent(w)));
}

void ActivateMenu(menu_struct *mp)
/* if it exists, raise it, otherwise create it */
{
  int menunum;
  Widget w;

  menunum = menu_index_by_menu(mp);
  w = MenuArray[menunum].widget;

  if (w != NULL) RaiseMenu(w);
  else MenuArray[menunum].widget = make_menu(mp);
}


void ActivateTopMenuProc(Widget w, caddr_t closure, caddr_t call_data)
/* raise all menus that exist, and create the top-level if required */
{
  int i;

  for (i = 0; i < sizeof(MenuArray)/sizeof(MenuArray[0]); i++)
    if (MenuArray[i].widget != NULL) RaiseMenu(MenuArray[i].widget);

  /* make sure the main menu is raised in any event */
  ActivateMenu(Menu);
}  



Widget label_widget(Widget parent, char *string)
{
  Arg Args[10];
  int n;
  Widget w;

  n = 0;
  XtSetArg(Args[n], XmNlabelString,
	   (XtArgVal)XmStringCreate(string, cs)); n++;
  w = XmCreateLabel(parent, "label", Args, n);
  XtManageChild(w);
  return w;
}

Widget entry_widget(Widget parent, char *string)
{
  Arg Args[10];
  int n;
  Widget w;

  n = 0; 
  XtSetArg(Args[n], XmNeditMode, (XtArgVal)XmSINGLE_LINE_EDIT); n++;
  w = XmCreateText(data_entry, "entry", Args, n);
  XtManageChild(w);
  return w;
}

Widget text_widget(Widget parent, char *string)
{
  Arg Args[10];
  int n;
  Widget w;

  n = 0;
  XtSetArg(Args[n], XmNeditable, (XtArgVal)False); n++;
  XtSetArg(Args[n], XmNeditMode, XmMULTI_LINE_EDIT); n++;
#ifdef USE_SCROLLED_TEXT
  XtSetArg(Args[n], XmNscrollHorizontal, (XtArgVal)False); n++;
  XtSetArg(Args[n], XmNscrollVertical, (XtArgVal)True); n++;
  w = XmCreateScrolledText(parent, string, Args, n);
#else
  w = XmCreateText(parent, string, Args, n);
#endif
  XtManageChild(w);
  return w;
}

void X_main_loop(int argc, char *argv[])
{
  /* toplevel widget is global */
  Arg Args[10];
  int n;

  /* if we're not using X, just call good old Query(); */
  if (getenv("DISPLAY") == NULL) {
    Query();
    return;
  }

  toplevel = XtInitialize(argv[0], "Netgen", NULL, 0, &argc, argv);
  /* create a vertical pane window to permit children to be resized */
  pane = XmCreatePanedWindow(toplevel,"pane",NULL, 0);
  XtManageChild(pane);

  /* create a top command and title area */
  n = 0;
  XtSetArg(Args[n], XmNlabelString,
	   (XtArgVal)XmStringCreate("Netgen" NETGEN_VERSION "." NETGEN_REVISION, cs));
  n++;
  title = XmCreatePushButton(pane, "title", Args, n);
  XtAddCallback(title, XmNactivateCallback, ActivateTopMenuProc, NULL);
  XtManageChild(title);

  /* create a data entry area for the four text fields */
  n = 0;
  XtSetArg(Args[n], XmNnumColumns, 2); n++;
  XtSetArg(Args[n], XmNpacking, XmPACK_COLUMN); n++;
  data_entry = XmCreateRowColumn(pane, "rowcol", Args, n);
  XtManageChild(data_entry);

  /* put 4 labels in the rowcol widget, and four text editors */
  label_widget(data_entry, "File Name:");
  label_widget(data_entry, "Cell Name:");
  label_widget(data_entry, "Element:");
  label_widget(data_entry, "2nd cell/elem:");
  GlobalFileWidget = entry_widget(data_entry, NULL);
  GlobalCellWidget = entry_widget(data_entry, NULL);
  GlobalDataWidget = entry_widget(data_entry, NULL);
  GlobalOtherWidget = entry_widget(data_entry, NULL);

  /* create two text widgets */
  cells_frame = text_widget(pane, "cells");
  frame = text_widget(pane, "text");

/*
Can't change scrolling policy after the window is created 
  n = 0;
  XtSetArg(Args[n], XmNscrollingPolicy, (XtArgVal)XmAUTOMATIC); n++;
  XtSetValues(XtParent(frame), Args, n);
*/

  XtRealizeWidget(toplevel);
  XtMainLoop();
}
#endif /* X11_MOTIF_WIDGETS */



#ifdef X11_ATHENA_WIDGETS

/*************************************/
/*    ATHENA widget code              */
/*************************************/

#include <X11/Xaw/Command.h>
#include <X11/Xaw/Paned.h>
#include <X11/Xaw/AsciiText.h>
#include <X11/Xaw/List.h>
#include <X11/Xaw/Label.h>
#include <X11/Xaw/Form.h>

XtAppContext app_con;

int calling_editor;  /* which string are we trying to get ? */
#define FILE_NAME 1
#define CELL_NAME 2
#define OTHER_NAME 3
#define DATA_NAME 4


char *get_something(char *buf, Widget w, char *prompt)
{
  Arg Args[10];
  int n;

  char *c;

  if (strlen(buf)) return(buf);

  n = 0;
  XtSetArg(Args[n], XtNstring, &c); n++;
  XtGetValues(w, Args, n);
  strcpy(buf, c);
  return (buf);
}

char *get_file(void) 
{
  calling_editor = FILE_NAME;
  return(get_something(GlobalFileName, GlobalFileWidget, "Enter File Name"));
}

char *get_cell(void) 
{
  calling_editor = CELL_NAME;
  return(get_something(GlobalCellName, GlobalCellWidget, "Enter Cell Name"));
}

char *get_other(void) 
{
  calling_editor = OTHER_NAME;
  return(get_something(GlobalOtherName, GlobalOtherWidget, "Enter Name"));
}

char *get_data(void) 
{
  calling_editor = DATA_NAME;
  return(get_something(GlobalDataName, GlobalDataWidget, "Enter Data"));
}


Widget pane, frame, cells_frame, menu_mgr, title, data_entry;

#define DATABUFSIZ 4000

char data_buf[DATABUFSIZ];
char *data_endptr = data_buf;

void X_display_line(char *buf)
{
  if (toplevel == NULL) {
    /* not using X windows */
    printf("%s", buf);
    return;
  }

  if (data_endptr + strlen(buf) > data_buf + DATABUFSIZ) {
    char *cp;
    /* find the next line */
    cp = data_buf + strlen(buf);
    while (*cp != '\n' && cp < data_endptr) cp++;

    memcpy(data_buf, cp, data_endptr - cp + 1);
    data_endptr -= (cp - data_buf);
  }
  /* now just copy the string */
  strcpy(data_endptr, buf);
  data_endptr += strlen(buf);
  memset(data_endptr, 0, data_buf + DATABUFSIZ - data_endptr - 1);
}

#ifdef USE_SCROLLED_TEXT
void X_display_refresh(void)
{
  Arg Args[10];
  int n;

  short lines;
  char *cp;
  XmTextPosition offset, cursor;
  Boolean showcur;

  if (toplevel == NULL) return;

  n = 0;
  XtSetArg(Args[n], XmNrows, &lines); n++;
  XtSetArg(Args[n], XmNtopPosition, &offset); n++;
  XtSetArg(Args[n], XmNcursorPosition, &cursor); n++;
  XtSetArg(Args[n], XmNautoShowCursorPosition, &showcur); n++;
  XtGetValues(frame, Args, n);
  printf("Before: %d lines in the display, %ld chars offset %ld cursor (showing %d)\n",
	 (int)lines, (long)offset, (long)cursor, (int)showcur);

  cp = data_endptr;
  while (cp > data_buf && lines > 0) 
    if (*cp-- == '\n') lines--;
  
printf("lines = %d, cp = %p, data_buf = %p\n", (int)lines, cp, data_buf);
  if (lines == 0) cp++;
  if (cp < data_buf) cp = data_buf;

  /* set the editor to display the string */
  n = 0;
  XtSetArg(Args[n], XmNvalue, data_buf); n++;
#if 0
  printf("Setting offset to %d\n", (int)((XmTextPosition)(cp - data_buf)));
  XtSetArg(Args[n], XmNtopPosition, (XmTextPosition)(cp - data_buf)); n++; 
/*  XtSetArg(Args[n], XmNtopPosition, (XmTextPosition)5); n++; */
#else
  XtSetArg(Args[n], XmNautoShowCursorPosition, (XtArgVal)True); n++;
  printf("Setting cursor position to %d\n", 
	 (int)((XmTextPosition)(data_endptr - data_buf)));
  XtSetArg(Args[n], XmNcursorPosition, 
	   (XmTextPosition)(data_endptr - data_buf)); n++;
#endif  

  XtSetValues(frame, Args, n);

  n = 0;
  XtSetArg(Args[n], XmNrows, &lines); n++;
  XtSetArg(Args[n], XmNtopPosition, &offset); n++;
  XtSetArg(Args[n], XmNcursorPosition, &cursor); n++;
  XtGetValues(frame, Args, n);
  printf("After: %d lines in the display, %ld chars offset %ld cursor\n",
	 (int)lines, (long)offset, (long)cursor);
}
#else /* not USE_SCROLLED_TEXT */

void X_display_refresh(void)
/* very simple code that scrolls the window as required */
{
  Arg Args[10];
  int n;

  Dimension height;
  Position topmargin, botmargin;
  XFontStruct *font;
  short lines;
  char *cp;
  int fontheight;

  if (toplevel == NULL) return;

  n = 0;
  XtSetArg(Args[n], XtNheight, &height); n++;
  XtSetArg(Args[n], XtNtopMargin, &topmargin); n++;
  XtSetArg(Args[n], XtNbottomMargin, &botmargin); n++;
  XtSetArg(Args[n], XtNfont, &font); n++;
  XtGetValues(frame, Args, n);
  fontheight = font->max_bounds.ascent + font->max_bounds.descent; 
  printf("got height = %d, topmargin = %d, botmargin = %d, fontht = %d\n",
	 (int)height, (int)topmargin, (int)botmargin, fontheight);


  lines = (height - topmargin - botmargin) / fontheight ;

  cp = data_endptr;
  while (cp > data_buf && lines > 0) 
    if (*cp-- == '\n') lines--;
  
  if (lines == 0) cp++;
  if (cp < data_buf) cp = data_buf;

  /* set the editor to display the string */
  n = 0;
  XtSetArg(Args[n], XtNstring, cp); n++;
  XtSetValues(frame, Args, n);
}

#endif /* not USE_SCROLLED_TEXT */


int menu_index_by_menu(menu_struct *mp)
{
  int i;
  for (i = 0; i < (sizeof(MenuArray)/sizeof(MenuArray[0])); i++) 
    if (MenuArray[i].menu == mp) return(i);
printf("menu_index_by_menu: this should never happen\n");
  return(0);
}


int menu_index_by_widget(Widget w)
{
  int i;
  for (i = 0; i < (sizeof(MenuArray)/sizeof(MenuArray[0])); i++) 
    if (MenuArray[i].widget == w) return(i);
  fprintf(stderr,"menu_index_by_widget: this should never happen\n");
  return(0);
}

menu_struct *find_menu_by_widget(Widget w)
{
  int i;
  for (i = 0; i < (sizeof(MenuArray)/sizeof(MenuArray[0])); i++) 
    if (MenuArray[i].widget == w) return(MenuArray[i].menu);
  fprintf(stderr,"find_menu_by_widget: this should never happen\n");
  return(NULL);
}

void ActivateMenu(menu_struct *mp);

void MenuCallback(Widget w, caddr_t closure, caddr_t call_data)
{
  XawListReturnStruct *cb = (XawListReturnStruct *)call_data;
  int i, ItemCount;
  menu_struct *menu;

  menu = find_menu_by_widget(w); 

  ItemCount = ItemsInMenu(menu);
  for (i = 0; i < ItemCount; i++) {
    if (!strcmp(cb->string, menu[i].name)) {
      /* printf("Trying to run function: %s\n", menu[i].name); */
      if (menu[i].submenu != NULL) ActivateMenu(menu[i].submenu);
      else (*(menu[i].func))(w, NULL, NULL);
      break;
    }
  }
  XawListUnhighlight(w); 
}

void MenuDestroyCallback(Widget w, caddr_t closure, caddr_t call_data)
{
  int menunum;

  menunum = menu_index_by_widget(w);
  MenuArray[menunum].widget = NULL;
}

Widget make_menu(menu_struct *menu)
/* returns the List widget for the menu, NOT the top shell widget */
{
  int n;
  Arg Args[20];
  Widget top, widget;
  String *Items;
  int ItemCount;

  top = 
    XtAppCreateShell(NULL,"menuShell", applicationShellWidgetClass,
		     XtDisplay(toplevel), NULL, 0);

  ItemCount = ItemsInMenu(menu);
  Items = (String *)CALLOC(ItemCount + 1, sizeof(String));

  for (n = 0;  n < ItemCount; n++)
    Items[n] = XtNewString(menu[n].name);
  Items[ItemCount] = NULL;

  n = 0;
  XtSetArg(Args[n], XtNdefaultColumns, 1); n++;
  XtSetArg(Args[n], XtNforceColumns, True); n++;
  XtSetArg(Args[n], XtNlist, Items); n++;
  XtSetArg(Args[n], XtNnumberStrings, ItemCount); n++;
  widget = XtCreateManagedWidget("menu", listWidgetClass, top, Args, n);
  XtAddCallback(widget, XtNcallback, MenuCallback, NULL);
  XtAddCallback(widget, XtNdestroyCallback, MenuDestroyCallback, NULL);
/*  XawListChange(widget, Items, ItemCount, 0, True); */

  /* put all menus at a fixed position; this doesn't work for mwm */
  n = 0;
  XtSetArg(Args[n], XtNx, (XtArgVal)20); n++;
  XtSetArg(Args[n], XtNy, (XtArgVal)(30 + 50*menu_index_by_menu(menu))); n++;
  XtSetValues(top, Args, n);

  XtRealizeWidget(top);
  return(widget);
}


void RaiseMenu(Widget w)
{
  /* just bring it up; remember to bring up the SHELL, the list's parent !!! */
  XMapRaised(XtDisplay(XtParent(w)), XtWindow(XtParent(w)));
}

void ActivateMenu(menu_struct *mp)
/* if it exists, raise it, otherwise create it */
{
  int menunum;
  Widget w;

  menunum = menu_index_by_menu(mp);
  w = MenuArray[menunum].widget;

  if (w != NULL) RaiseMenu(w);
  else MenuArray[menunum].widget = make_menu(mp);
}


void ActivateTopMenuProc(Widget w, caddr_t closure, caddr_t call_data)
/* raise all menus that exist, and create the top-level if required */
{
  int i;

  for (i = 0; i < sizeof(MenuArray)/sizeof(MenuArray[0]); i++)
    if (MenuArray[i].widget != NULL) RaiseMenu(MenuArray[i].widget);

  /* make sure the main menu is raised in any event */
  ActivateMenu(Menu);
}  



Widget label_widget(Widget parent, char *string)
{
  Arg Args[10];
  int n;
  Widget w;

  n = 0;
  XtSetArg(Args[n], XtNlabel,  (XtArgVal)string); n++;
  w = XtCreateManagedWidget("label", labelWidgetClass, parent, Args, n);
  return w;
}

Widget entry_widget(Widget parent, char *string)
{
  Arg Args[10];
  int n;
  Widget w;

  n = 0; 
  XtSetArg(Args[n], XtNeditType, XawtextEdit); n++;
  w = XtCreateManagedWidget("entry", asciiTextWidgetClass, 
			    data_entry, Args, n);
  return w;
}

Widget text_widget(Widget parent, char *string)
{
  Arg Args[10];
  int n;
  Widget w;

  n = 0;
  XtSetArg(Args[n], XtNeditType, XawtextRead); n++;
  w = XtCreateManagedWidget(string, asciiTextWidgetClass, 
			    parent, Args, n);
  return w;
}

void make_data_entry_area(Widget parent)
{
  Widget l1, l2, l3, l4;
  Arg Args[10];
  int n;

  /* put 4 labels in the rowcol widget, and four text editors */
  l1 = label_widget(data_entry, "File Name:");

  l2 = label_widget(data_entry, "Cell Name:");
  n = 0;
  XtSetArg(Args[n], XtNfromVert, l1); n++;
  XtSetValues(l2, Args, n);

  l3 = label_widget(data_entry, "Element:");
  n = 0;
  XtSetArg(Args[n], XtNfromVert, l2); n++;
  XtSetValues(l3, Args, n);

  l4 = label_widget(data_entry, "2nd cell/elem:");
  n = 0;
  XtSetArg(Args[n], XtNfromVert, l3); n++;
  XtSetValues(l4, Args, n);

  GlobalFileWidget = entry_widget(data_entry, NULL);
  n = 0;
  XtSetArg(Args[n], XtNfromHoriz, l1); n++;
  XtSetValues(GlobalFileWidget, Args, n);

  GlobalCellWidget = entry_widget(data_entry, NULL);
  n = 0;
  XtSetArg(Args[n], XtNfromHoriz, l2); n++;
  XtSetArg(Args[n], XtNfromVert, GlobalFileWidget); n++;
  XtSetValues(GlobalCellWidget, Args, n);

  GlobalDataWidget = entry_widget(data_entry, NULL);
  n = 0;
  XtSetArg(Args[n], XtNfromHoriz, l3); n++;
  XtSetArg(Args[n], XtNfromVert, GlobalCellWidget); n++;
  XtSetValues(GlobalDataWidget, Args, n);

  GlobalOtherWidget = entry_widget(data_entry, NULL);
  n = 0;
  XtSetArg(Args[n], XtNfromHoriz, l4); n++;
  XtSetArg(Args[n], XtNfromVert, GlobalDataWidget); n++;
  XtSetValues(GlobalOtherWidget, Args, n);
}

String fallback_resources[] = { 
    "*input:                  True",
    "*Paned*width:            350",
    "*label.label:            At least one of each Athena Widget.",
    "*Dialog.label:           I am a Dialog widget.",
    "*Dialog.value:           Enter new value here.",
    "*Dialog*command*label:   ok",
    "*Dialog*resizable:       True",
    "*Viewport*allowVert:     True",
    "*scrollbar*orientation:  horizontal",
    "*scrollbar*length:       100",
    "*text*height:            75",
    "*text*editType:          edit",
    "*text*scrollVertical:    whenNeeded",
    "*text*scrollHorizonal:   whenNeeded",
    NULL,
};

void X_main_loop(int argc, char *argv[])
{
  /* toplevel widget is global */
  Arg Args[10];
  int n;

  /* if we're not using X, just call good old Query(); */
  if (getenv("DISPLAY") == NULL) {
    Query();
    return;
  }

  toplevel = XtAppInitialize(&app_con, "Netgen", NULL, 0, &argc, argv,
			     fallback_resources, NULL, 0);

  /* create a vertical pane window to permit children to be resized */
  pane = XtCreateManagedWidget("pane", panedWidgetClass, toplevel,NULL, 0);

  /* create a top command and title area */
  n = 0;
  XtSetArg(Args[n], XtNlabel, (XtArgVal)"Netgen" NETGEN_VERSION "." NETGEN_REVISION);
  n++;
  title = XtCreateManagedWidget("title", commandWidgetClass, pane, Args, n);
  XtAddCallback(title, XtNcallback, ActivateTopMenuProc, NULL);

  /* create a data entry area for the four text fields */
/*
  n = 0;
  data_entry = XtCreateManagedWidget("rowcol", boxWidgetClass, pane, Args, n);
*/
  n = 0;
  data_entry = XtCreateManagedWidget("rowcol", formWidgetClass, pane, Args, n);

  make_data_entry_area(data_entry);

  /* create two text widgets */
  cells_frame = text_widget(pane, "cells");
  frame = text_widget(pane, "text");

  XtRealizeWidget(toplevel);
  XtAppMainLoop(app_con);
}
#endif /* X11_ATHENA_WIDGETS */

#endif  /* HAVE_X11 */

