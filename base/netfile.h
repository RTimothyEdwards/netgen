#ifndef _NETFILE_H
#define _NETFILE_H

#define NTK_EXTENSION ".ntk"
#define ACTEL_EXTENSION ".adl"
#define XILINX_EXTENSION ".xnf"
#define WOMBAT_EXTENSION ".wom"
#define EXT_EXTENSION ".ext"
#define SIM_EXTENSION ".sim"
#define PRM_EXTENSION ".prm"
#define SPICE_EXTENSION ".spice"
#define SPICE_EXT2 ".spc"
#define SPICE_EXT3 ".sp"
#define SPICE_EXT4 ".spi"
#define SPICE_EXT5 ".fspc"
#define SPICE_EXT6 ".cir"
#define SPICE_EXT7 ".ckt"
#define NETGEN_EXTENSION ".ntg"
#define CCODE_EXTENSION ".c.code"
#define ESACAP_EXTENSION ".esa"
#define VERILOG_EXTENSION ".v"
#define SYS_VERILOG_EXTENSION ".sv"

#define LINELENGTH 80

extern int OpenFile(char *filename, int linelen);
extern void CloseFile(char *filename);
extern int IsPortInPortlist(struct objlist *ob, struct nlist *tp);
extern void FlushString (char *format, ...);
extern char *SetExtension(char *buffer, char *path, char *extension);

extern int File;
extern struct hashdict *definitions;

/* input routines */

extern char *nexttok;
#define SKIPTO(a) do {SkipTok(NULL);} while (!match(nexttok,a))
extern char *strdtok0(char *pstring, char *delim1, char *delim2, char isverilog);
extern char *strdtok(char *pstring, char *delim1, char *delim2);
extern char *GetLineAtTok();
extern void SkipTok(char *delimiter);
extern void SkipTokNoNewline(char *delimiter);
extern void SkipTokComments(char *delimiter);
extern void SkipNewLine(char *delimiter);
extern void SpiceTokNoNewline(void);	/* handles SPICE "+" continuation line */
extern void SpiceSkipNewLine(void);	/* handles SPICE "+" continuation line */
extern void InputParseError(FILE *f);
extern int OpenParseFile(char *name, int fnum);
extern int EndParseFile(void);
extern int CloseParseFile(void);
extern void SeekFile(long offset);	/* handles verilog 'for' loops */
extern long TellFile();			/* handles verilog 'for' loops */

#endif /* _NETFILE_H */
