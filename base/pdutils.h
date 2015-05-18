
/* emulator functions found in pdutils.c */

extern float RandomUniform(void);
extern long RandomSeed(long seed);
extern int Random(int max);

#ifdef NEED_STRING
extern char *strtok(char *s, char *delim);
extern int strcspn(char *s, char *reject);
extern char *strpbrk(char *s, char *breakat);
#endif /* NEED_STRING */

#ifdef NEED_STRSTR
extern char *strstr(char *s, char *wanted);
#endif

#ifdef NEED_STRCASECMP
extern int strcasecmp(char *s1, char *s2);
#endif

#ifdef NEED_STRDUP
#ifdef __STDC__
extern char *strdup(const char *s);
#else
extern char *strdup(char *s);
#endif
#endif /* NEED_STRDUP */

#ifdef NEED_STRTOL
extern long strtol(char *str, char **ptr, int base);
#endif

#ifdef NEED_VPRINTF
/* try to find out whether we use va_end, or ... */
#if defined(__STDC__) || defined(IBMPC)
#include <stdarg.h>
#else
#include <varargs.h>
#endif

#ifdef va_end
#include <stdio.h>
extern int vsprintf(char *dest, char *format, va_list args);
extern int vfprintf(FILE *dest, char *format, va_list args);
extern int vprintf(char *format, va_list args);
#else
extern int vsprintf(char *dest, char *format, ...);
#endif /* va_end */
#endif /* NEED_VPRINTF */

#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)<(b))?(a):(b))
