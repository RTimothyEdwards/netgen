MODULE = base
NETGENDIR = ..
SRCS = actel.c ccode.c greedy.c ntk.c print.c actellib.c embed.c \
 hash.c netfile.c objlist.c query.c anneal.c ext.c netcmp.c netgen.c \
 pdutils.c random.c timing.c bottomup.c flatten.c place.c spice.c \
 wombat.c xilinx.c xillib.c
X11_SRCS = xnetgen.c

include ${NETGENDIR}/defs.mak

SRCS   += ${GR_SRCS}
DFLAGS += ${GR_DFLAGS}
DFLAGS += -DNETGEN_DATE="\"`date`\""
CFLAGS += ${GR_CFLAGS}

include ${NETGENDIR}/rules.mak
