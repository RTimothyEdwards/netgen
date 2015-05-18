


                                 D B U G

                       C Program Debugging Package

                                    by

                                Fred Fish




       IIIINNNNTTTTRRRROOOODDDDUUUUCCCCTTTTIIIIOOOONNNN


            Almost every program development environment worthy  of
       the  name provides some sort of debugging facility.  Usually
       this takes the  form  of  a  program  which  is  capable  of
       controlling  execution  of  other programs and examining the
       internal state of other executing programs.  These types  of
       programs will be referred to as external debuggers since the
       debugger is not part of the executing program.  Examples  of
       this  type  of  debugger  include  the aaaaddddbbbb and ssssddddbbbb debuggers
       provided with the UUUUNNNNIIIIXXXX811119 operating system.


            One of the problems associated with developing programs
       in  an  environment  with  good  external  debuggers is that
       developed programs  tend  to  have  little  or  no  internal
       instrumentation.   This  is  usually  not  a problem for the
       developer since he is, or at  least  should  be,  intimately
       familiar  with  the  internal organization, data structures,
       and control flow of the program being  debugged.   It  is  a
       serious   problem   for  maintenance  programmers,  who  are
       unlikely to have such familiarity  with  the  program  being
       maintained,  modified, or ported to another environment.  It
       is also a problem, even for the developer, when the  program
       is  moved  to  an environment with a primitive or unfamiliar
       debugger, or even no debugger.


            On the other hand, _d_b_u_g is an example  of  an  internal
       debugger.  Because it requires internal instrumentation of a
       program, and its  usage  does  not  depend  on  any  special
       capabilities  of  the  execution  environment,  it is always
       available and will  execute  in  any  environment  that  the
       program  itself will execute in.  In addition, since it is a
       complete  package  with  a  specific  user  interface,   all
       programs   which  use  it  will  be  provided  with  similar
       debugging capabilities.  This is in sharp contrast to  other


       __________

        1. UNIX is a trademark of AT&T Bell Laboratories.




                                  - 1 -







       DBUG User Manual                               June 12, 1989



       forms of internal instrumentation where each  developer  has
       their  own, usually less capable, form of internal debugger.
       In summary, because _d_b_u_g is an internal debugger it provides
       consistency across operating environments, and because it is
       available to all developers it provides  consistency  across
       all programs in the same environment.


            The _d_b_u_g package imposes only a slight speed penalty on
       executing programs, typically much less than 10 percent, and
       a modest size penalty,  typically  10  to  20  percent.   By
       defining  a specific C preprocessor symbol both of these can
       be reduced to zero with no changes required  to  the  source
       code.


            The  following  list  is  a  quick   summary   of   the
       capabilities  of  the  _d_b_u_g package.  Each capability can be
       individually enabled or disabled at the time  a  program  is
       invoked   by   specifying   the   appropriate  command  line
       arguments.

               o Execution trace  showing  function  level  control
                 flow    in   a   semi-graphically   manner   using
                 indentation to indicate nesting depth.

               o Output the values of all, or any  subset  of,  key
                 internal variables.

               o Limit  actions  to  a  specific   set   of   named
                 functions.

               o Limit function trace to a specified nesting depth.

               o Label each output line with source file  name  and
                 line number.

               o Label  each  output  line  with  name  of  current
                 process.

               o Push or pop  internal  debugging  state  to  allow
                 execution with built in debugging defaults.

               o Redirect  the  debug  output  stream  to  standard
                 output  (stdout)  or  a  named  file.  The default
                 output stream is  standard  error  (stderr).   The
                 redirection mechanism is completely independent of
                 normal command line redirection  to  avoid  output
                 conflicts.





                                  - 2 -







       DBUG User Manual                               June 12, 1989



       PPPPRRRRIIIIMMMMIIIITTTTIIIIVVVVEEEE DDDDEEEEBBBBUUUUGGGGGGGGIIIINNNNGGGG TTTTEEEECCCCHHHHNNNNIIIIQQQQUUUUEEEESSSS


            Internal instrumentation is already a familiar  concept
       to most programmers, since it is usually the first debugging
       technique  learned.    Typically,   "print statements"   are
       inserted  in the source code at interesting points, the code
       is recompiled and executed,  and  the  resulting  output  is
       examined in an attempt to determine where the problem is.

       The procedure is iterative,  with  each  iteration  yielding
       more  and  more  output,  and  hopefully  the  source of the
       problem is discovered before the output becomes too large to
       deal  with  or  previously  inserted  statements  need to be
       removed.  Figure 1 is an example of this type  of  primitive
       debugging technique.



                 #include <stdio.h>

                 main (argc, argv)
                 int argc;
                 char *argv[];
                 {
                     printf ("argv[0] = %d\n", argv[0]);
                     /*
                      *  Rest of program
                      */
                     printf ("== done ==\n");
                 }


                                   Figure 1
                         Primitive Debugging Technique





            Eventually,  and  usually  after   at   least   several
       iterations,  the  problem  will  be found and corrected.  At
       this point, the newly  inserted  print  statements  must  be
       dealt  with.   One obvious solution is to simply delete them
       all.  Beginners usually do this a few times until they  have
       to  repeat  the entire process every time a new bug pops up.
       The second most obvious solution is to somehow  disable  the
       output,  either  through  the  source code comment facility,
       creation of a debug variable to be switched on or off, or by
       using  the  C  preprocessor.   Figure 2 is an example of all
       three techniques.



                                  - 3 -







       DBUG User Manual                               June 12, 1989





                 #include <stdio.h>

                 int debug = 0;

                 main (argc, argv)
                 int argc;
                 char *argv[];
                 {
                     /* printf ("argv = %x\n", argv) */
                     if (debug) printf ("argv[0] = %d\n", argv[0]);
                     /*
                      *  Rest of program
                      */
                 #ifdef DEBUG
                     printf ("== done ==\n");
                 #endif
                 }


                                   Figure 2
                           Debug Disable Techniques





            Each technique has  its  advantages  and  disadvantages
       with  respect  to  dynamic vs static activation, source code
       overhead, recompilation requirements, ease of  use,  program
       readability,  etc.   Overuse  of  the  preprocessor solution
       quickly leads to problems with source code  readability  and
       maintainability  when  multiple  ####iiiiffffddddeeeeffff  symbols  are  to be
       defined or  undefined  based  on  specific  types  of  debug
       desired.  The source code can be made slightly more readable
       by suitable indentation of the ####iiiiffffddddeeeeffff arguments to match the
       indentation  of  the code, but not all C preprocessors allow
       this.   The  only  requirement  for  the  standard  UUUUNNNNIIIIXXXX   C
       preprocessor is for the '#' character to appear in the first
       column,  but  even  this  seems  like   an   arbitrary   and
       unreasonable  restriction.   Figure  3 is an example of this
       usage.











                                  - 4 -







       DBUG User Manual                               June 12, 1989





                 #include <stdio.h>

                 main (argc, argv)
                 int argc;
                 char *argv[];
                 {
                 #   ifdef DEBUG
                     printf ("argv[0] = %d\n", argv[0]);
                 #   endif
                     /*
                      *  Rest of program
                      */
                 #   ifdef DEBUG
                     printf ("== done ==\n");
                 #   endif
                 }


                                   Figure 3
                       More Readable Preprocessor Usage
































                                  - 5 -







       DBUG User Manual                               June 12, 1989



       FFFFUUUUNNNNCCCCTTTTIIIIOOOONNNN TTTTRRRRAAAACCCCEEEE EEEEXXXXAAAAMMMMPPPPLLLLEEEE


            We will start off learning about  the  capabilities  of
       the  _d_b_u_g  package  by  using  a simple minded program which
       computes the factorial of a  number.   In  order  to  better
       demonstrate  the  function  trace mechanism, this program is
       implemented recursively.  Figure 4 is the main function  for
       this factorial program.



                 #include <stdio.h>
                 /* User programs should use <local/dbug.h> */
                 #include "dbug.h"

                 main (argc, argv)
                 int argc;
                 char *argv[];
                 {
                     register int result, ix;
                     extern int factorial (), atoi ();

                     DBUG_ENTER ("main");
                     DBUG_PROCESS (argv[0]);
                     for (ix = 1; ix < argc && argv[ix][0] == '-'; ix++) {
                         switch (argv[ix][1]) {
                             case '#':
                                 DBUG_PUSH (&(argv[ix][2]));
                                 break;
                         }
                     }
                     for (; ix < argc; ix++) {
                         DBUG_PRINT ("args", ("argv[%d] = %s", ix, argv[ix]));
                         result = factorial (atoi (argv[ix]));
                         printf ("%d\n", result);
                     }
                     DBUG_RETURN (0);
                 }


                                   Figure 4
                          Factorial Program Mainline





            The mmmmaaaaiiiinnnn function is  responsible  for  processing  any
       command   line  option  arguments  and  then  computing  and
       printing the factorial of each non-option argument.



                                  - 6 -







       DBUG User Manual                               June 12, 1989



            First of all, notice that all of the debugger functions
       are  implemented  via  preprocessor  macros.   This does not
       detract from the readability of the code and makes disabling
       all debug compilation trivial (a single preprocessor symbol,
       DDDDBBBBUUUUGGGG____OOOOFFFFFFFF, forces the macro expansions to be null).

            Also notice the inclusion of  the  header  file  ddddbbbbuuuugggg....hhhh
       from the local header file directory.  (The version included
       here is the test version in  the  dbug  source  distribution
       directory).   This file contains all the definitions for the
       debugger macros, which all have the form DDDDBBBBUUUUGGGG____XXXXXXXX............XXXXXXXX.


            The DDDDBBBBUUUUGGGG____EEEENNNNTTTTEEEERRRR macro informs that debugger that we have
       entered  the function named mmmmaaaaiiiinnnn.  It must be the very first
       "executable" line in a function, after all declarations  and
       before any other executable line.  The DDDDBBBBUUUUGGGG____PPPPRRRROOOOCCCCEEEESSSSSSSS macro is
       generally used only once per program to inform the  debugger
       what name the program was invoked with.  The DDDDBBBBUUUUGGGG____PPPPUUUUSSSSHHHH macro
       modifies the current debugger state by saving  the  previous
       state  and  setting  a new state based on the control string
       passed as its argument.  The DDDDBBBBUUUUGGGG____PPPPRRRRIIIINNNNTTTT  macro  is  used  to
       print  the  values of each argument for which a factorial is
       to be computed.  The DDDDBBBBUUUUGGGG____RRRREEEETTTTUUUURRRRNNNN macro  tells  the  debugger
       that  the  end  of the current function has been reached and
       returns a value to  the  calling  function.   All  of  these
       macros will be fully explained in subsequent sections.

            To use the debugger, the factorial program  is  invoked
       with a command line of the form:

                          factorial -#d:t 1 2 3

       The  mmmmaaaaiiiinnnn  function  recognizes  the  "-#d:t"  string  as  a
       debugger  control  string, and passes the debugger arguments
       ("d:t")  to  the  _d_b_u_g  runtime  support  routines  via  the
       DDDDBBBBUUUUGGGG____PPPPUUUUSSSSHHHH macro.  This particular string enables output from
       the DDDDBBBBUUUUGGGG____PPPPRRRRIIIINNNNTTTT macro with the 'd' flag and enables  function
       tracing  with  the 't' flag.  The factorial function is then
       called three times, with the arguments "1",  "2",  and  "3".
       Note  that  the DBUG_PRINT takes exactly ttttwwwwoooo arguments, with
       the second argument (a format string and list  of  printable
       values) enclosed in parenthesis.

            Debug control strings consist of a  header,  the  "-#",
       followed  by  a  colon separated list of debugger arguments.
       Each debugger argument is a single character  flag  followed
       by an optional comma separated list of arguments specific to
       the given flag.  Some examples are:





                                  - 7 -







       DBUG User Manual                               June 12, 1989



                          -#d:t:o
                          -#d,in,out:f,main:F:L

       Note  that  previously  enabled  debugger  actions  can   be
       disabled by the control string "-#".


            The definition of the factorial function, symbolized as
       "N!", is given by:

                         N! = N * N-1 * ... 2 * 1

       Figure 5 is the factorial  function  which  implements  this
       algorithm  recursively.   Note  that this is not necessarily
       the best way to  do  factorials  and  error  conditions  are
       ignored completely.



                 #include <stdio.h>
                 /* User programs should use <local/dbug.h> */
                 #include "dbug.h"

                 int factorial (value)
                 register int value;
                 {
                     DBUG_ENTER ("factorial");
                     DBUG_PRINT ("find", ("find %d factorial", value));
                     if (value > 1) {
                         value *= factorial (value - 1);
                     }
                     DBUG_PRINT ("result", ("result is %d", value));
                     DBUG_RETURN (value);
                 }


                                   Figure 5
                              Factorial Function





            One advantage (some may not consider it  so)  to  using
       the  _d_b_u_g  package  is  that  it  strongly  encourages fully
       structured coding with only one entry and one exit point  in
       each  function.  Multiple exit points, such as early returns
       to escape a loop, may be used, but each such point  requires
       the  use  of  an appropriate DDDDBBBBUUUUGGGG____RRRREEEETTTTUUUURRRRNNNN or DDDDBBBBUUUUGGGG____VVVVOOOOIIIIDDDD____RRRREEEETTTTUUUURRRRNNNN
       macro.




                                  - 8 -







       DBUG User Manual                               June 12, 1989



            To build  the  factorial  program  on  a  UUUUNNNNIIIIXXXX  system,
       compile and link with the command:

                cc -o factorial main.c factorial.c -ldbug

       The "-ldbug" argument  tells  the  loader  to  link  in  the
       runtime support modules for the _d_b_u_g package.  Executing the
       factorial program with a command of the form:

                           factorial 1 2 3 4 5

       generates the output shown in figure 6.



                 1
                 2
                 6
                 24
                 120


                                   Figure 6
                              factorial 1 2 3 4 5





            Function  level  tracing  is  enabled  by  passing  the
       debugger the 't' flag in the debug control string.  Figure 7
       is  the  output  resulting  from  the  command  "factorial -
       #t:o 3 2".





















                                  - 9 -







       DBUG User Manual                               June 12, 1989





                 |   >factorial
                 |   |   >factorial
                 |   |   <factorial
                 |   <factorial
                 2
                 |   >factorial
                 |   |   >factorial
                 |   |   |   >factorial
                 |   |   |   <factorial
                 |   |   <factorial
                 |   <factorial
                 6
                 <main


                                   Figure 7
                              factorial -#t:o 3 2





            Each entry to or return from a function is indicated by
       '>'  for  the  entry  point  and  '<'  for  the  exit point,
       connected by vertical bars to allow matching  points  to  be
       easily found when separated by large distances.


            This trace output indicates that there was  an  initial
       call  to  factorial from main (to compute 2!), followed by a
       single recursive call to factorial to compute 1!.  The  main
       program  then  output  the  result  for  2!  and  called the
       factorial  function  again  with  the  second  argument,  3.
       Factorial  called  itself  recursively to compute 2! and 1!,
       then returned control to main, which output the value for 3!
       and exited.


            Note that there is no matching entry point "main>"  for
       the  return point "<main" because at the time the DDDDBBBBUUUUGGGG____EEEENNNNTTTTEEEERRRR
       macro was reached in main, tracing was not enabled yet.   It
       was  only  after  the  macro  DDDDBBBBUUUUGGGG____PPPPUUUUSSSSHHHH  was  executing that
       tracing became enabled.  This implies that the argument list
       should  be  processed  as  early  as possible since all code
       preceding  the  first  call  to  DDDDBBBBUUUUGGGG____PPPPUUUUSSSSHHHH  is   essentially
       invisible  to ddddbbbbuuuugggg (this can be worked around by inserting a
       temporary   DDDDBBBBUUUUGGGG____PPPPUUUUSSSSHHHH((((aaaarrrrggggvvvv[[[[1111]]]]))))   immediately    after    the
       DDDDBBBBUUUUGGGG____EEEENNNNTTTTEEEERRRR((((""""mmmmaaaaiiiinnnn"""")))) macro.




                                  - 10 -







       DBUG User Manual                               June 12, 1989



            One last note, the trace output normally comes  out  on
       the  standard error.  Since the factorial program prints its
       result on the standard output, there is the  possibility  of
       the  output  on  the  terminal  being  scrambled  if the two
       streams are not synchronized.  Thus the debugger is told  to
       write its output on the standard output instead, via the 'o'
       flag character.   Note  that  no  'o'  implies  the  default
       (standard  error),  a  'o'  with no arguments means standard
       output, and a 'o' with an  argument  means  used  the  named
       file.   I.E,  "factorial -#t:o,logfile 3 2"  would write the
       trace output in "logfile".  Because of  UUUUNNNNIIIIXXXX  implementation
       details,  programs usually run faster when writing to stdout
       rather than stderr, though this is not a prime consideration
       in this example.








































                                  - 11 -







       DBUG User Manual                               June 12, 1989



       UUUUSSSSEEEE OOOOFFFF DDDDBBBBUUUUGGGG____PPPPRRRRIIIINNNNTTTT MMMMAAAACCCCRRRROOOO


            The mechanism used to produce "printf" style output  is
       the DDDDBBBBUUUUGGGG____PPPPRRRRIIIINNNNTTTT macro.


            To allow selection of output from specific macros,  the
       first  argument to every DDDDBBBBUUUUGGGG____PPPPRRRRIIIINNNNTTTT macro is a _d_b_u_g keyword.
       When this keyword appears in the argument list  of  the  'd'
       flag    in    a    debug    control   string,   as   in   "-
       #d,keyword1,keyword2,...:t", output from  the  corresponding
       macro  is enabled.  The default when there is no 'd' flag in
       the control string is to enable output from  all  DDDDBBBBUUUUGGGG____PPPPRRRRIIIINNNNTTTT
       macros.


            Typically, a program will be run once, with no keywords
       specified,  to  determine  what keywords are significant for
       the current problem (the keywords are printed in  the  macro
       output  line).  Then the program will be run again, with the
       desired  keywords,  to  examine  only  specific   areas   of
       interest.


            The second argument to a DDDDBBBBUUUUGGGG____PPPPRRRRIIIINNNNTTTT macro is a standard
       printf  style  format  string  and  one or more arguments to
       print, all enclosed in parenthesis so that they collectively
       become  a  single  macro  argument.   This  is  how variable
       numbers of printf arguments are supported.  Also  note  that
       no  explicit  newline  is  required at the end of the format
       string.  As a matter of style, two or three small DDDDBBBBUUUUGGGG____PPPPRRRRIIIINNNNTTTT
       macros  are  preferable to a single macro with a huge format
       string.  Figure 8 shows the output for default  tracing  and
       debug.



















                                  - 12 -







       DBUG User Manual                               June 12, 1989





                 |   args: argv[2] = 3
                 |   >factorial
                 |   |   find: find 3 factorial
                 |   |   >factorial
                 |   |   |   find: find 2 factorial
                 |   |   |   >factorial
                 |   |   |   |   find: find 1 factorial
                 |   |   |   |   result: result is 1
                 |   |   |   <factorial
                 |   |   |   result: result is 2
                 |   |   <factorial
                 |   |   result: result is 6
                 |   <factorial
                 6
                 <main


                                   Figure 8
                              factorial -#d:t:o 3





            The output from the DDDDBBBBUUUUGGGG____PPPPRRRRIIIINNNNTTTT  macro  is  indented  to
       match  the  trace output for the function in which the macro
       occurs.  When debugging  is  enabled,  but  not  trace,  the
       output starts at the left margin, without indentation.


            To demonstrate selection of specific macros for output,
       figure  9  shows  the  result  when the factorial program is
       invoked with the debug control string "-#d,result:o".



                 factorial: result: result is 1
                 factorial: result: result is 2
                 factorial: result: result is 6
                 factorial: result: result is 24
                 24


                                   Figure 9
                           factorial -#d,result:o 4







                                  - 13 -







       DBUG User Manual                               June 12, 1989



            It is sometimes desirable  to  restrict  debugging  and
       trace  actions  to a specific function or list of functions.
       This is accomplished with the  'f'  flag  character  in  the
       debug  control  string.   Figure  10  is  the  output of the
       factorial program  when  run  with  the  control  string  "-
       #d:f,factorial:F:L:o".  The 'F' flag enables printing of the
       source file name and the 'L' flag enables  printing  of  the
       source file line number.



                    factorial.c:     9: factorial: find: find 3 factorial
                    factorial.c:     9: factorial: find: find 2 factorial
                    factorial.c:     9: factorial: find: find 1 factorial
                    factorial.c:    13: factorial: result: result is 1
                    factorial.c:    13: factorial: result: result is 2
                    factorial.c:    13: factorial: result: result is 6
                 6


                                   Figure 10
                       factorial -#d:f,factorial:F:L:o 3





            The output in figure 10 shows that the "find" macro  is
       in  file  "factorial.c"  at  source  line 8 and the "result"
       macro is in the same file at source line 12.
























                                  - 14 -







       DBUG User Manual                               June 12, 1989



       SSSSUUUUMMMMMMMMAAAARRRRYYYY OOOOFFFF MMMMAAAACCCCRRRROOOOSSSS


            This section summarizes  the  usage  of  all  currently
       defined  macros in the _d_b_u_g package.  The macros definitions
       are found in the user include file ddddbbbbuuuugggg....hhhh from the  standard
       include directory.



               DBUG_ENTER  Used to tell the runtime support  module
                           the  name of the function being entered.
                           The argument must be of type "pointer to
                           character".   The  DBUG_ENTER macro must
                           precede  all  executable  lines  in  the
                           function  just  entered,  and  must come
                           after  all  local  declarations.    Each
                           DBUG_ENTER  macro  must  have a matching
                           DBUG_RETURN or DBUG_VOID_RETURN macro at
                           the  function  exit  points.  DBUG_ENTER
                           macros   used   without    a    matching
                           DBUG_RETURN  or  DBUG_VOID_RETURN  macro
                           will cause  warning  messages  from  the
                           _d_b_u_g package runtime support module.

                           EX: DBUG_ENTER ("main");

              DBUG_RETURN  Used at each exit point  of  a  function
                           containing  a  DBUG_ENTER  macro  at the
                           entry point.  The argument is the  value
                           to  return.   Functions  which return no
                           value    (void)    should    use     the
                           DBUG_VOID_RETURN  macro.  It is an error
                           to     have     a     DBUG_RETURN     or
                           DBUG_VOID_RETURN  macro  in  a  function
                           which has no matching DBUG_ENTER  macro,
                           and  the  compiler  will complain if the
                           macros are actually used (expanded).

                           EX: DBUG_RETURN (value);
                           EX: DBUG_VOID_RETURN;

             DBUG_PROCESS  Used to name the current  process  being
                           executed.   A  typical argument for this
                           macro is "argv[0]", though  it  will  be
                           perfectly happy with any other string.

                           EX: DBUG_PROCESS (argv[0]);

                DBUG_PUSH  Sets a new debugger state by pushing the
                           current  ddddbbbbuuuugggg  state  onto  an  internal



                                  - 15 -







       DBUG User Manual                               June 12, 1989



                           stack and setting up the new state using
                           the  debug  control string passed as the
                           macro argument.  The most  common  usage
                           is to set the state specified by a debug
                           control  string   retrieved   from   the
                           argument  list.   Note  that the leading
                           "-#" in a debug control string specified
                           as  a  command line argument must nnnnooootttt be
                           passed as part of  the  macro  argument.
                           The proper usage is to pass a pointer to
                           the  first  character  aaaafffftttteeeerrrr  the   "-#"
                           string.

                           EX: DBUG_PUSH ((argv[i][2]));
                           EX: DBUG_PUSH ("d:t");
                           EX: DBUG_PUSH ("");

                 DBUG_POP  Restores the previous debugger state  by
                           popping  the state stack.  Attempting to
                           pop more  states  than  pushed  will  be
                           ignored  and  no  warning will be given.
                           The DBUG_POP macro has no arguments.

                           EX: DBUG_POP ();

                DBUG_FILE  The  DBUG_FILE  macro  is  used  to   do
                           explicit I/O on the debug output stream.
                           It is used in the  same  manner  as  the
                           symbols  "stdout"  and  "stderr"  in the
                           standard I/O package.

                           EX: fprintf (DBUG_FILE, "Doing  my   own
                           I/O!0);

             DBUG_EXECUTE  The  DBUG_EXECUTE  macro  is   used   to
                           execute any arbitrary C code.  The first
                           argument is the debug keyword,  used  to
                           trigger  execution of the code specified
                           as the second argument.  This macro must
                           be  used  cautiously  because,  like the
                           DBUG_PRINT macro,  it  is  automatically
                           selected  by  default  whenever  the 'd'
                           flag has no argument list  (I.E.,  a  "-
                           #d:t" control string).

                           EX: DBUG_EXECUTE ("abort", abort ());

                   DBUG_N  These macros, where N is  in  the  range
                           2-5,  are currently obsolete and will be
                           removed in a future  release.   Use  the
                           new DBUG_PRINT macro.



                                  - 16 -







       DBUG User Manual                               June 12, 1989



               DBUG_PRINT  Used to do printing  via  the  "fprintf"
                           library  function  on  the current debug
                           stream, DBUG_FILE.  The  first  argument
                           is  a  debug  keyword,  the  second is a
                           format  string  and  the   corresponding
                           argument  list.   Note  that  the format
                           string and argument  list  are  all  one
                           macro  argument  and mmmmuuuusssstttt be enclosed in
                           parenthesis.

                           EX: DBUG_PRINT ("eof", ("end of file found"));
                           EX: DBUG_PRINT ("type", ("type is %x",
                           type));
                           EX: DBUG_PRINT ("stp", ("%x -> %s", stp,
                           stp -> name));

              DBUG_SETJMP  Used in place of the  setjmp()  function
                           to first save the current debugger state
                           and then  execute  the  standard  setjmp
                           call.    This  allows  the  debugger  to
                           restore its state when the  DBUG_LONGJMP
                           macro  is  used  to  invoke the standard
                           longjmp() call.  Currently all instances
                           of  DBUG_SETJMP  must  occur  within the
                           same function and at the  same  function
                           nesting level.

                           EX: DBUG_SETJMP (env);

             DBUG_LONGJMP  Used in place of the longjmp()  function
                           to  first  restore the previous debugger
                           state  at   the   time   of   the   last
                           DBUG_SETJMP   and   then   execute   the
                           standard  longjmp()  call.   Note   that
                           currently    all   DBUG_LONGJMP   macros
                           restore the state at  the  time  of  the
                           last  DBUG_SETJMP.  It would be possible
                           to  maintain  separate  DBUG_SETJMP  and
                           DBUG_LONGJMP   pairs   by   having   the
                           debugger runtime support module use  the
                           first   argument  to  differentiate  the
                           pairs.

                           EX: DBUG_LONGJMP (env,val);










                                  - 17 -







       DBUG User Manual                               June 12, 1989



       DDDDEEEEBBBBUUUUGGGG CCCCOOOONNNNTTTTRRRROOOOLLLL SSSSTTTTRRRRIIIINNNNGGGG


            The debug control string is used to set  the  state  of
       the   debugger   via  the  DDDDBBBBUUUUGGGG____PPPPUUUUSSSSHHHH  macro.   This  section
       summarizes the currently available debugger options and  the
       flag  characters  which  enable  or  disable them.  Argument
       lists enclosed in '[' and ']' are optional.


                d[,keywords] Enable   output   from   macros   with
                             specified  keywords.   A  null list of
                             keywords implies that all keywords are
                             selected.

                    D[,time] Delay for specified  time  after  each
                             output  line,  to  let  output  drain.
                             Time is given in tenths  of  a  second
                             (value  of 10 is one second).  Default
                             is zero.

               f[,functions] Limit   debugger   actions   to    the
                             specified  list  of functions.  A null
                             list of  functions  implies  that  all
                             functions are selected.

                           F Mark each debugger  output  line  with
                             the name of the source file containing
                             the macro causing the output.

                           g Turn on machine independent profiling.
                             A   profiling  data  collection  file,
                             named dbugmon.out, will be written for
                             postprocessing    by   the   "analyze"
                             program.  The accuracy of this feature
                             is relatively unknown at this time.

                           i Identify  the  process  emitting  each
                             line of debug or trace output with the
                             process id for that process.

                           L Mark each debugger  output  line  with
                             the  source  file  line  number of the
                             macro causing the output.

                           n Mark each debugger  output  line  with
                             the current function nesting depth.

                           N Sequentially  number   each   debugger
                             output  line  starting  at 1.  This is
                             useful  for  reference  purposes  when



                                  - 18 -







       DBUG User Manual                               June 12, 1989



                             debugger  output  is interspersed with
                             program output.

                    o[,file] Redirect the debugger output stream to
                             the   specified   file.   The  default
                             output  stream  is  stderr.   A   null
                             argument  list  causes  output  to  be
                             redirected to stdout.

               p[,processes] Limit   debugger   actions   to    the
                             specified   processes.   A  null  list
                             implies all processes.  This is useful
                             for    processes   which   run   child
                             processes.  Note  that  each  debugger
                             output  line  can  be  marked with the
                             name of the current  process  via  the
                             'P' flag.  The process name must match
                             the    argument    passed    to    the
                             DDDDBBBBUUUUGGGG____PPPPRRRROOOOCCCCEEEESSSSSSSS macro.

                           P Mark each debugger  output  line  with
                             the  name  of the current process from
                             argv[0].  Most useful when used with a
                             process  which  runs  child  processes
                             that are also  being  debugged.   Note
                             that  the  parent process must arrange
                             for the debugger control string to  be
                             passed to the child processes.

                           r Used in conjunction with the DDDDBBBBUUUUGGGG____PPPPUUUUSSSSHHHH
                             macro to reset the current indentation
                             level back to zero.  Most useful  with
                             DDDDBBBBUUUUGGGG____PPPPUUUUSSSSHHHH  macros  used to temporarily
                             alter the debugger state.

                       t[,N] Enable function control flow  tracing.
                             The maximum nesting depth is specified
                             by N, and defaults to 200.
















                                  - 19 -







       DBUG User Manual                               June 12, 1989



       HHHHIIIINNNNTTTTSSSS AAAANNNNDDDD MMMMIIIISSSSCCCCEEEELLLLLLLLAAAANNNNEEEEOOOOUUUUSSSS


            One of the most useful capabilities of the _d_b_u_g package
       is  to  compare  the  executions  of  a given program in two
       different environments.  This is typically done by executing
       the program in the environment where it behaves properly and
       saving the debugger output in a reference file.  The program
       is  then  run with identical inputs in the environment where
       it  misbehaves  and  the  output  is  again  captured  in  a
       reference  file.   The  two  reference  files  can  then  be
       differentially compared to determine exactly where execution
       of the two processes diverges.


            A  related  usage  is  regression  testing  where   the
       execution   of   a   current  version  is  compared  against
       executions of previous versions.  This is most  useful  when
       there are only minor changes.


            It is not difficult to modify an existing  compiler  to
       implement  some  of  the  functionality  of the _d_b_u_g package
       automatically, without source code changes  to  the  program
       being debugged.  In fact, such changes were implemented in a
       version of the Portable C Compiler by  the  author  in  less
       than  a  day.   However,  it is strongly encouraged that all
       newly developed code continue to use the debugger macros for
       the   portability   reasons  noted  earlier.   The  modified
       compiler should be used only for testing existing programs.
























                                  - 20 -







       DBUG User Manual                               June 12, 1989



       CCCCAAAAVVVVEEEEAAAATTTTSSSS


            The _d_b_u_g package works best with  programs  which  have
       "line oriented"  output,  such  as  text processors, general
       purpose utilities, etc.  It can be  interfaced  with  screen
       oriented  programs  such as visual editors by redefining the
       appropriate macros to call special functions for  displaying
       the  debugger  results.   Of  course,  this  caveat  is  not
       applicable if the debugger output is simply  dumped  into  a
       file for post-execution examination.


            Programs which use memory  allocation  functions  other
       than  mmmmaaaalllllllloooocccc  will  usually have problems using the standard
       _d_b_u_g package.  The most common problem is multiply allocated
       memory.





































                                  - 21 -









                                 D B U G

                       C Program Debugging Package

                                    by
9
                                Fred Fish
9


                                 _A_B_S_T_R_A_C_T



       This document introduces _d_b_u_g, a  macro  based  C  debugging
       package  which  has  proven to be a very flexible and useful
       tool for debugging, testing, and porting C programs.


            All of the features of the _d_b_u_g package can be  enabled
       or  disabled dynamically at execution time.  This means that
       production programs will run normally when debugging is  not
       enabled,  and  eliminates  the need to maintain two separate
       versions of a program.


            Many   of   the   things   easily   accomplished   with
       conventional  debugging  tools,  such as symbolic debuggers,
       are difficult or impossible  with  this  package,  and  vice
       versa.   Thus the _d_b_u_g package should _n_o_t be thought of as a
       replacement or substitute for  other  debugging  tools,  but
       simply  as  a useful _a_d_d_i_t_i_o_n to the program development and
       maintenance environment.



















99






