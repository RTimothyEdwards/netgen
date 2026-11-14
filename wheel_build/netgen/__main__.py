import os
import sys
from pathlib import Path

__here__ = Path(__file__).resolve().parent
netgen_bin = __here__ / "lib" / "netgen" / "tcl" / "netgenexec"


def launch_netgen():
    env = os.environ.copy()
    env["CAD_ROOT"] = str(__here__ / "lib")
    env["TCL_LIBRARY"] = str(__here__ / "lib" / "tcl9.1")
    tkcon = True
    batch = False
    arglist = []
    for arg in sys.argv[1:]:
        if arg == "-gui":
            print(
                "The netgen wheel build does not support the GUI mode.",
                file=sys.stderr,
            )
            sys.exit(1)
        elif arg.startswith("-noc"):
            tkcon = False
        elif arg.startswith("-bat"):
            batch = True
            arglist.append(arg)  # used by netgen.tcl to exit after sourcing script
        else:
            arglist.append(arg)
    if tkcon or not batch:
        print(
            "The netgen wheel build requires the -noconsole -batch flags.",
            file=sys.stderr,
        )
        sys.exit(1)
    os.execle(netgen_bin, "netgen", "--", *arglist, env)


if __name__ == "__main__":
    launch_netgen()
