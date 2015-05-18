/*
 * Copyright June 1987, Binayak Banerjee
 * All rights reserved.
 *
 * This program may be freely distributed under the same terms and
 * conditions as Fred Fish's Dbug package.
 *
 * Useful macros which I use a lot.
 *
 * Conditionally include some useful files.
 */

# ifndef EOF
#	include <stdio.h>
# endif

/*
 *	For BSD systems, you can include <sysexits.h> for more detailed
 *	exit information.  For non-BSD systems (which also includes
 *	non-unix systems) just map everything to "failure" = 1 and
 *	"success" = 0.		-Fred Fish 9-Sep-87
 */

# ifdef BSD
#	include <sysexits.h>
# else
#	define EX_SOFTWARE 1
#	define EX_DATAERR 1
#	define EX_USAGE 1
#	define EX_OSERR 1
#	define EX_IOERR 1
#	define EX_OK 0
# endif


/*
 * Fred Fish's debugging stuff.  Define DBUG_OFF in order to disable if
 * you don't have these.
 */

# ifndef DBUG_OFF
#	include "dbug.h"		/* Use local version */
# else
#	define DBUG_ENTER(a1)
#	define DBUG_RETURN(a1) return(a1)
#	define DBUG_VOID_RETURN return
#	define DBUG_EXECUTE(keyword,a1)
#	define DBUG_2(keyword,format)
#	define DBUG_3(keyword,format,a1)
#	define DBUG_4(keyword,format,a1,a2)
#	define DBUG_5(keyword,format,a1,a2,a3)
#	define DBUG_PUSH(a1)
#	define DBUG_POP()
#	define DBUG_PROCESS(a1)
#	define DBUG_PRINT(x,y)
#	define DBUG_FILE (stderr)
# endif

#define __MERF_OO_ "%s: Malloc Failed in %s: %d\n"

#define Nil(Typ)	((Typ *) 0)	/* Make Lint happy */

#define MALLOC(Ptr,Num,Typ) do	/* Malloc w/error checking & exit */ \
	if ((Ptr = (Typ *)malloc((Num)*(sizeof(Typ)))) == Nil(Typ)) \
		{fprintf(stderr,__MERF_OO_,my_name,__FILE__,__LINE__);\
		exit(EX_OSERR);} while(0)

#define Malloc(Ptr,Num,Typ) do	/* Weaker version of above */\
	if ((Ptr = (Typ *)malloc((Num)*(sizeof(Typ)))) == Nil(Typ)) \
		fprintf(stderr,__MERF_OO_,my_name,__FILE__,__LINE__);\
		 while(0)

#define FILEOPEN(Fp,Fn,Mod) do	/* File open with error exit */ \
	if((Fp = fopen(Fn,Mod)) == Nil(FILE))\
		{fprintf(stderr,"%s: Couldn't open %s\n",my_name,Fn);\
		exit(EX_IOERR);} while(0)

#define Fileopen(Fp,Fn,Mod) do	/* Weaker version of above */ \
	if((Fp = fopen(Fn,Mod)) == Nil(FILE)) \
		fprintf(stderr,"%s: Couldn't open %s\n",my_name,Fn);\
	while(0)


extern char *my_name;	/* The name that this was called as */
