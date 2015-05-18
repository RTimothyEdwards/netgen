/* define prototypes for printf, etc. */

#ifndef FILE
#include <stdio.h>
#endif

#ifdef VMUNIX
/* common UNIX things that appear to be OS-independent */

extern void exit(int status);
extern int system(const char *s);
extern void perror(char *p);
extern int rand(void);
extern int atoi (const char *str);
extern char *calloc(unsigned num, unsigned size);
extern char *malloc(unsigned size);
extern void free(void *ptr);
extern long strtol (char *str, char **ptr, int base);
extern int fprintf(FILE *stream, char *fmt, ...);
extern int fclose(FILE *f);
extern int fflush(FILE *f);
extern int printf(char *fmt, ...);


#ifdef HPUX
/* library functions missing prototypes */
extern int fscanf(FILE *stream, char *fmt, ...);
extern int sprintf(char *s, char *fmt, ...);
#ifdef va_end
extern int vsprintf(char *s, char *fmt, va_list ap);
#endif

extern void srand(unsigned seed);

extern int memcmp(void *s1, void *s2, int n);
extern int tolower(int c);
extern int toupper(int c);
#endif /* HPUX */


#ifdef BSD
extern double atof(char *str);
extern char *getenv(char *name);

extern void longjmp(void *env, int val);
/*   extern int longjmp(void *env, int val);  this is correct for VAX */
extern int setjmp(void *env);

extern void srand(int seed);
extern bcopy(char *from, char *to, int len);
extern bzero(char *b, int len);

extern int close(int filedes);
extern int read(int filedes, void *buf, unsigned nbyte);
extern int sscanf(char *s, char *fmt, ...);
extern int fsscanf(FILE *f, char *s, char *fmt, ...);

#if defined(va_end) && !defined(NEED_VPRINTF)
extern char *vsprintf(char *s, char *fmt, va_list ap);
#endif

#endif /* BSD */

#endif /* VMUNIX */

