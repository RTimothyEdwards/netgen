#!/bin/bash
#
# For installation, put this file (netgen.sh) in a standard executable path.
# Put startup script "netgen.tcl" and shared library "tclnetgen.so"
# in ${CAD_ROOT}/netgen/tcl/, with a symbolic link from file
# ".wishrc" to "netgen.tcl".
#
# This script starts irsim under the Tcl interpreter,
# reading commands from a special .wishrc script which
# launches irsim and retains the Tcl interactive interpreter.

# Parse for the argument "-c[onsole]".  If it exists, run netgen
# with the TkCon console.  Strip this argument from the argument list.

TKCON=true
BATCH=
GUI=
NETGEN_WISH=/usr/bin/wish
export NETGEN_WISH

# Hacks for Cygwin
if [ ${TERM:=""} = "cygwin" ]; then
   export PATH="$PATH:/usr/lib"
   export DISPLAY=${DISPLAY:=":0"}
fi

# Preserve quotes in arguments (thanks, Stackoverflow!)
arglist=''
for i in "$@" ; do
   case $i in
      -noc*) TKCON=;;
      -bat*) BATCH=true; TKCON=;;
      -gui) GUI=true; TKCON=;;
      *) arglist="$arglist${arglist:+ }\"${i//\"/\\\"}\"";;
   esac
done 

if [ $TKCON ]; then

   exec /usr/local/lib/netgen/tcl/tkcon.tcl \
	-eval "source /usr/local/lib/netgen/tcl/console.tcl" \
	-slave "package require Tk; set argc $#; set argv [list $arglist]; \
	source /usr/local/lib/netgen/tcl/netgen.tcl"

# Run the Python LVS manager GUI

elif [ $GUI ]; then
    exec /usr/local/lib/netgen/python/lvs_manager.py $@

#
# Run the stand-in for wish (netgenexec), which acts exactly like "wish"
# except that it replaces ~/.wishrc with netgen.tcl.  This executable is
# *only* needed when running without the console; the console itself is
# capable of sourcing the startup script.
#

else
   exec /usr/local/lib/netgen/tcl/netgenexec -- "$@"

fi
