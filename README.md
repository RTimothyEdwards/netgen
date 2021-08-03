![CI](https://github.com/lankasaicharan/netgen/actions/workflows/main.yml/badge.svg)

NETGEN:  VERSION 1.5

NETGEN is a general-purpose netlist management system.  It can read
and write several netlist formats, including NTK (Caltech, CMU),
WOMBAT (Berkeley), SIM (Berkeley), ACTEL (Actel Inc., Sunnyvale), and
SPICE (Berkeley).  In addition, a C-language embedded interface exists
for specifying netlists directly.  Please see the documentation file
NETGEN.DOC

NETGEN was intended as an efficient specification language for
hierarchical systems.  Secondary features, such as testing isomorphism
between two netlists, have been added over time.  As netgen has
primarily been used in its capacity as a netlist comparator, this
has essentially become the primary useful feature of netgen.

Version 1.5 (September 2014) was created largely to allow the
well-developed version 1.4 to be marked as the stable version.
Bug fixes will continue on version 1.4 as version 1.5 continues
development.

Version 1.4 (November 2007) is an effort to make netgen into an
industry-standard LVS tool, handling device classes, device
properties, hierarchy, and providing a sensible output making it
easy to trace a problem to its exact source.

Version 1.2/1.3 (Mar. 2003) takes the original netgen and recasts it
as a Tcl extension in the same manner as magic version 7.2 and
IRSIM version 9.6, while retaining the option to compile the original,
non-interpreter-based version(s).  Under Tcl, all of the single-character
commands have been recast as complete function names.  The syntax has
been changed to match Tcl generally-accepted syntactical norms.  The
X11 GUI has been scrapped and may eventually be replaced by a Tk GUI.
Version 1.3 is a minor update on version 1.2 with GNU autoconf and a
number of associated make-process fixes that were applied to magic
and irsim.

REVISION HISTORY:
-----------------
Revision 0: December 29, 2002  (first cut of port to Tcl).
	1) New command-line interface with full-word commands.
	2) The usual "toolscript" stuff with printf() functions
	   passed to Tcl's "eval" function, support for and use
	   of the TkCon console, compilation as a Tcl shared-object file.
	3) New "make config" script to match magic's.

Revision 1: January 10 2003.
	1) Improved "nodes" and "elements" commands so they return relevant
 	   information about specific points in the network.
	2) Added position information to ".sim" file elements, so this can
	   be used to trace back problems to a layout and/or schematic.
	3) Fixed errors in the printing routines.
	4) Improved some horribly inefficient code; speeds up some node
	   printing commands and causes the "compare" command to become
	   nearly instantaneous.
	5) Removed regular-expression matching from the code.  It is
	   intended that Tcl's built-in regular expression matching will
	   suffice, although it will be necessary to rewrite some functions
	   to return Tcl lists instead of just dumping text to the screen.
	   The main effect this has is that arrayed nodes in magic retain
	   their names, simplifying the (intended) interface to magic.

Version 1.2, Revision 0: March 10 2003.
	1) Fixed and extended the log file capability through the Tcl
	   command "log".  Includes the ability to turn screen echo on
	   and off while logging.
	2) Implemented Control-C interrupts during lengthy procedures,
	   both for the TkCon console window and for the terminal-based
	   mode.
	3) Created the scripted command "lvs" to do what the standalone
	   program "netcomp" used to do.

Version 1.3, Revision 0: November 13, 2004.
	1) Implemented the GNU "autoconf" configure method in the same
	   manner as magic-7.3 and irsim-9.7.

For further revision information, see the automatically-generated list
of CVS check-in notes on http://opencircuitdesign.com/netgen/.

BUILDING NETGEN:
----------------
NETGEN version 1.5 uses the same "make" procedure as Magic version 8.1
and IRSIM version 9.7:

   ./configure
   make
   make install

Note: For FreeBSD, use 'gmake' instead.

IN CASE OF FAILURE
-------------------
Please contact me (tim@opencircuitdesign.com) to report compile-time and
run-time errors.  The website at

	http://opencircuitdesign.com/netgen/

has complete information about compiling, configuring, and using netgen
version 1.4.

RUNNING NETGEN UNDER TCL
------------------------
The normal procedure for doing a netlist comparison (LVS) is the following:

1) Build ".sim" files for the two netlists to be compared (e.g., run
   "exttosim" on the magic layout, and generate sim output on an xcircuit
   schematic.
2) Run netgen.
3) As of version 1.4, configuration of LVS is done by commands in a
   command file which is passed to the "lvs" command (see below).  This
   file declares which device classes should be considered matching
   between two circuits, which subcircuits should be considered
   matching between two top-level circuits, and which ports of devices
   or subcircuits permute.
4) As of version 1.2, netgen has a script-level procedure "lvs" which
   takes care of the above sequence of commands, as well as dumping the
   majority of the output to an output file, and reporting only the
   final analysis in the console window.  The syntax of this command is:

	lvs <filename1> <filename2> [<setupfilename>] [<logfilename>]

   If "setupfilename" is not specified, then the default filename "setup.tcl"
   will be used.  If this file does not exist, then a default setup is
   assumed.

   If "logfilename" is not specified, then the default filename "comp.out"
   will be used.

   "lvs" is equivalent to the following sequence of individual commands:

	readnet sim <filename1>
	readnet sim <filename2>
	source <setupfilename>
	compare <filename1> <filename2>
	run converge

5) Interpreting the output:

   The feedback from netgen is still rather crude but improving in each
   generation.  Illegal fragments are generated around areas where the
   netlists cannot be resolved.  The worst matches will be listed at the
   top, which is usually the place to start looking.

   Connectivity of elements and nodes is much easier to trace now, with
   commands:
	
	nodes <element_name> <cellname>
	elements <node_name> <cellname>

   Where "cellname" is the filename (one of the two files loaded for
   comparison).  The "elements" command prints all of the elements
   (transistors, capacitors, resistors, etc.) connected to a specific
   named node.  The "nodes" command prints the node names for each
   pin of the specified element.  For ".sim" netlists containing
   position information for each transistor, all transistor elements
   (and some non-transistor elements such as poly-poly capacitors and
   rpoly resistors extracted from magic) will have names like
   "n@45,376" indicating an n-type transistor at position x=45, y=376
   on the layout.  This naming convention permits tracing errors back
   to the layout and schematic.

