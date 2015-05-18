extern void Fprintf(FILE *f, char *format, ...);
extern void Printf(char *format, ...);
extern int Fcursor(FILE *f);
extern int Fwrap(FILE *f, int col);
extern void Ftab(FILE *f, int col);
extern FILE *Fopen(char *name, char *mode);
extern void Fflush(FILE *f);
extern int Fclose(FILE *f);
extern void Finsert(FILE *f);

extern FILE *LoggingFile;
extern int NoOutput;
