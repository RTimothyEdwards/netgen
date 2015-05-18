/*----------------------------------------------------------------------*/
/* netgenexec.c								*/
/*									*/
/* Written by R. Timothy Edwards for MultiGiG, Inc., November 2004	*/
/* This file mainly lifted from the main application routine for	*/
/* "wish" from the Tk distribution.					*/
/*									*/
/* This is a compact re-write of the "wish" executable that calls	*/
/* Tk_MainEx with application-specific processing.  Specifically, 	*/
/* "wish" doesn't allow the startup script (~/.wishrc) to be renamed.	*/
/* However, for netgen running as an extension of Tcl, we want to	*/
/* source the netgen.tcl file instead of ~/.wishrc.  So, all this file	*/
/* really does is to set the Tcl variable "tcl_rcFileName" to		*/
/*  netgen.tcl, so that it will be processed as the startup script,	*/
/* followed by a drop back to the Tcl interpreter command-line main	*/
/* loop.								*/
/*									*/
/* This is a standalone executable.  However, it is only called when	*/
/* "-noconsole" is specified on the UNIX command-line.  When the	*/
/* console is used, the console is capable of sourcing the netgen.tcl	*/
/* script itself, and so it uses "wish" as the executable.  However,	*/
/* the console redirects standard input, so it prevents netgen from	*/
/* being used in a batch processing mode.  Thus, to batch-process with	*/
/* netgen, use "netgen -noc < script.tcl" or, interactively,		*/
/* "netgen -noc << EOF" followed by commands entered from stdin		*/
/* and ending with "EOF".						*/
/*									*/
/* The "netgenexec" method replaces the former use of "wish" with the	*/
/* "netgen" script setting HOME to point to the directory containing	*/
/* ".wishrc", a symbolic link to "netgen.tcl".  That failed to work on	*/
/* remote systems because the $HOME environment variable is also used	*/
/* to find the user's .Xauthority file to authenticate the X11		*/
/* connection.								*/
/*									*/
/* Update:  Because "netgenexec" avoids the Tk console, no part of Tk	*/
/* is ever invoked.  Making "netgenexec" run Tk_Init just causes it to	*/
/* require graphics accessibility that it does not need.  All calls to	*/
/* Tk have been replaced with Tcl calls.				*/
/*----------------------------------------------------------------------*/

#include <stdio.h>

// #include <tk.h>
#include <tcl.h>

/*----------------------------------------------------------------------*/
/* Application initiation.  This is exactly like the AppInit routine	*/
/* for "wish", minus the cruft, but with "tcl_rcFileName" set to	*/
/* "netgen.tcl" instead of "~/.wishrc".					*/
/*----------------------------------------------------------------------*/

int
netgen_AppInit(interp)
    Tcl_Interp *interp;
{
    if (Tcl_Init(interp) == TCL_ERROR) {
	return TCL_ERROR;
    }
    // if (Tk_Init(interp) == TCL_ERROR) {
    //	return TCL_ERROR;
    // }
    // Tcl_StaticPackage(interp, "Tk", Tk_Init, Tk_SafeInit);
    Tcl_StaticPackage(interp, "Tcl", Tcl_Init, Tcl_Init);

    /* This is where we replace the home ".wishrc" file with	*/
    /* netgen's startup script.					*/

    Tcl_SetVar(interp, "tcl_rcFileName", TCL_DIR "/netgen.tcl", TCL_GLOBAL_ONLY);
    return TCL_OK;
}

/*----------------------------------------------------------------------*/
/* The main procedure;  replacement for "wish".				*/
/*----------------------------------------------------------------------*/

int
main(argc, argv)
   int argc;
   char **argv;
{
    // Tk_Main(argc, argv, netgen_AppInit);
    Tcl_Main(argc, argv, netgen_AppInit);
    return 0;
}

/*----------------------------------------------------------------------*/
