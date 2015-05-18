#ifdef HAVE_REGCMP

typedef char *Regexp;
Regexp regcmp(char *s, ...);
#define RegexpCompile(a) regcmp(a, (char *)0)
#define REGEXP_FREE_TEMPLATE

char *regex(char *re, char *s);
#define RegexpMatch(a,b) (regex((a),(b)) != NULL)
#endif /* HAVE_REGCMP */


#ifdef HAVE_RE_COMP

typedef char *Regexp;
Regexp re_comp(char *s);
#define RegexpCompile(a) re_comp(a)
/* re_comp returns an error code, not a regexp, so... */
#undef REGEXP_FREE_TEMPLATE

extern int re_exec(char *s);
#define RegexpMatch(a,b) re_exec(b)
#endif /* HAVE_RE_COMP */


#if !defined(HAVE_RE_COMP) && !defined(HAVE_REGCMP)

/*
 * Definitions etc. for our own regexp(3) routines.
 *
 * THESE ARE SHAMELESSLY STOLEN FROM HENRY SPENCER'S UTZOO ROUTINES
 *  (prototypes added by MAS)	 
 */

#define NSUBEXP  10
typedef struct regexp {
        char *startp[NSUBEXP];
        char *endp[NSUBEXP];
        char regstart;          /* Internal use only. */
        char reganch;           /* Internal use only. */
        char *regmust;          /* Internal use only. */
        int regmlen;            /* Internal use only. */
        char program[1];        /* Unwarranted chumminess with compiler. */
} regexp;

typedef struct regexp *Regexp;
#define RegexpCompile(a) regcomp(a)
#define RegexpMatch(a,b) regexec((a),(b))

Regexp regcomp(char *exp);
#define REGEXP_FREE_TEMPLATE

extern int regexec(regexp *prog, char *string);
extern void regsub(regexp *prog, char *source, char *dest);
extern void regerror(char *s);

#endif /* neither HAVE_RE_COMP nor HAVE_REGCMP */
