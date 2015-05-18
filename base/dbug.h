/*
           DBUG -- Copyright Abandoned, 1987, Fred Fish

                   This file has been modified by Mass Sivilotti, 1989.
                   Please see dbug/dbug.h for more details.

*/

/*
 *  FILE
 *
 *	dbug.h    user include file for programs using the dbug package
 *
 *  SYNOPSIS
 *
 *	#include "dbug.h"
 *
 *  SCCS ID
 *
 *	@(#)dbug.h	1.12 4/2/89
 *
 *  DESCRIPTION
 *
 *	Programs which use the dbug package must include this file.
 *	It contains the appropriate macros to call support routines
 *	in the dbug runtime library.
 *
 *	To disable compilation of the macro expansions define the
 *	preprocessor symbol "DBUG_OFF".  This will result in null
 *	macros expansions so that the resulting code will be smaller
 *	and faster.  (The difference may be smaller than you think
 *	so this step is recommended only when absolutely necessary).
 *	In general, tradeoffs between space and efficiency are
 *	decided in favor of efficiency since space is seldom a
 *	problem on the new machines).
 *
 *	All externally visible symbol names follow the pattern
 *	"_db_xxx..xx_" to minimize the possibility of a dbug package
 *	symbol colliding with a user defined symbol.
 *	
 *	The DBUG_<N> style macros are obsolete and should not be used
 *	in new code.  Macros to map them to instances of DBUG_PRINT
 *	are provided for compatibility with older code.  They may go
 *	away completely in subsequent releases.
 *
 *  AUTHOR
 *
 *	Fred Fish
 *	(Currently employed by Motorola Computer Division, Tempe, Az.)
 *	hao!noao!mcdsun!fnf
 *	(602) 438-3614
 *
 */

/*
 *	Internally used dbug variables which must be global.
 */

#ifndef DBUG_OFF
    extern int _db_on_;			/* TRUE if debug currently enabled */
    extern FILE *_db_fp_;		/* Current debug output stream */
    extern char *_db_process_;		/* Name of current process */
    extern int _db_keyword_ (char *s);	/* Accept/reject keyword */
    extern void _db_push_ (char *s);	/* Push state, set up new state */
    extern void _db_pop_ (void);	/* Pop previous debug state */
    extern void _db_enter_ (char *func, char *file, int line, char **sfunc,
	    char **sfile, int *slevel, char **sframe);
                                        /* New user function entered */
    extern void _db_return_ (int line, char **sfunc, char **sfile, int *lvl);
                                        /* User function return */
    extern void _db_pargs_ (int line, char *key); /* Remember args for line */
    extern void _db_doprnt_ (char *format, ...); /* Print debug output */
    extern void _db_setjmp_ (void);	/* Save debugger environment */
    extern void _db_longjmp_ ();	/* Restore debugger environment */
# endif

/*
 *	These macros provide a user interface into functions in the
 *	dbug runtime support library.  They isolate users from changes
 *	in the MACROS and/or runtime support.
 *
 *	The symbols "__LINE__" and "__FILE__" are expanded by the
 *	preprocessor to the current source file line number and file
 *	name respectively.
 *
 *	WARNING ---  Because the DBUG_ENTER macro allocates space on
 *	the user function's stack, it must precede any executable
 *	statements in the user function.
 *
 */

# ifdef DBUG_OFF
#    define DBUG_ENTER(a1)
#    define DBUG_RETURN(a1) return(a1)
#    define DBUG_VOID_RETURN return
#    define DBUG_EXECUTE(keyword,a1)
#    define DBUG_PRINT(keyword,arglist)
#    define DBUG_2(keyword,format)		/* Obsolete */
#    define DBUG_3(keyword,format,a1)		/* Obsolete */
#    define DBUG_4(keyword,format,a1,a2)	/* Obsolete */
#    define DBUG_5(keyword,format,a1,a2,a3)	/* Obsolete */
#    define DBUG_PUSH(a1)
#    define DBUG_POP()
#    define DBUG_PROCESS(a1)
#    define DBUG_FILE (stderr)
#    define DBUG_SETJMP setjmp
#    define DBUG_LONGJMP longjmp
# else
#    define DBUG_ENTER(a) \
	auto char *_db_func_; auto char *_db_file_; auto int _db_level_; \
	auto char *_db_framep_; \
	_db_enter_ (a,__FILE__,__LINE__,&_db_func_,&_db_file_,&_db_level_, \
		    &_db_framep_)
#    define DBUG_LEAVE \
	(_db_return_ (__LINE__, &_db_func_, &_db_file_, &_db_level_))
#    define DBUG_RETURN(a1) return (DBUG_LEAVE, (a1))
/*   define DBUG_RETURN(a1) {DBUG_LEAVE; return(a1);}  Alternate form */
#    define DBUG_VOID_RETURN {DBUG_LEAVE; return;}
#    define DBUG_EXECUTE(keyword,a1) \
	{if (_db_on_) {if (_db_keyword_ (keyword)) { a1 }}}
#    define DBUG_PRINT(keyword,arglist) \
	{if (_db_on_) {_db_pargs_(__LINE__,keyword); _db_doprnt_ arglist;}}
#    define DBUG_2(keyword,format) \
	DBUG_PRINT(keyword,(format))		/* Obsolete */
#    define DBUG_3(keyword,format,a1) \
	DBUG_PRINT(keyword,(format,a1))		/* Obsolete */
#    define DBUG_4(keyword,format,a1,a2) \
	DBUG_PRINT(keyword,(format,a1,a2))	/* Obsolete */
#    define DBUG_5(keyword,format,a1,a2,a3) \
	DBUG_PRINT(keyword,(format,a1,a2,a3))	/* Obsolete */
#    define DBUG_PUSH(a1) _db_push_ (a1)
#    define DBUG_POP() _db_pop_ ()
#    define DBUG_PROCESS(a1) (_db_process_ = a1)
#    define DBUG_FILE (_db_fp_)
#    define DBUG_SETJMP(a1) (_db_setjmp_ (), setjmp (a1))
#    define DBUG_LONGJMP(a1,a2) (_db_longjmp_ (), longjmp (a1, a2))
# endif
