#define NETGEN_COPYRIGHT "Copyright 1990,1991,2002,2004,2005"
#define NETGEN_AUTHOR "Massimo A. Sivilotti, Caltech"
#define NETGEN_DEVELOPER "R. Timothy Edwards, MultiGiG, Inc."

#ifndef _NETGEN_H
#define _NETGEN_H

#ifndef _OBJLIST_H
#include "objlist.h"
#endif

#ifndef NULL
#define NULL 0
#endif

/* various Composition directions */
#define NONE 0
#define HORIZONTAL 1
#define VERTICAL 2

/* netgen.c */
extern void ReopenCellDef(char *name, int file);
extern void CellDef(char *name, int file);
extern void CellDefNoCase(char *name, int file);
extern void EndCell(void);
extern void Port(char *name);
extern int CountPorts(char *name, int file);
extern void SetClass(unsigned char class);
extern struct property *PropertyValue(char *name, int fnum, char *key,
		double slop, double pdefault);
extern struct property *PropertyDouble(char *name, int fnum, char *key,
		double slop, double pdefault);
extern struct property *PropertyInteger(char *name, int fnum, char *key,
		int slop, int pdefault);
extern struct property *PropertyString(char *name, int fnum, char *key,
		int range, char *pdefault);
extern int  PropertyDelete(char *name, int fnum, char *key);
extern int  PropertyTolerance(char *name, int fnum, char *key, int ival,
		double dval);
extern int  PropertyMerge(char *name, int fnum, char *key, int merge_type);
extern void ResolveProperties(char *name1, int file1, char *name2, int file2);
extern void CopyProperties(struct objlist *obj_to, struct objlist *obj_from);
extern int PromoteProperty(struct property *, struct valuelist *);
extern int SetPropertyDefault(struct property *, struct valuelist *);
extern struct objlist *LinkProperties(char *model, struct keyvalue *topptr);
extern int ReduceExpressions(struct objlist *instprop, struct objlist *parprops,
		struct nlist *parent, int glob);
extern void Node(char *name);
extern void Global(char *name);
extern void UniqueGlobal(char *name);
extern void Instance(char *model, char *instancename);
extern void PortList(char *prefix, char *list_template);
extern char *Cell(char *inststr, char *model, ...);
extern int  IsIgnored(char *, int);

/* netcmp.c */
extern struct nlist *LookupClassEquivalent(char *model, int file1, int file2);
extern void AssignCircuits(char *name1, int file1, char *name2, int file2);

/* flatten.c */
extern int PrematchLists(char *, int, char *, int);

/* Define (enumerate) various device classes, largely based on SPICE	*/
/* model types, mixed with some ext/sim types.				*/

#define CLASS_SUBCKT	0	/* Any cell containing components; default */
#define	CLASS_NMOS	1	/* sim "n" */
#define CLASS_PMOS	2	/* sim "p" */
#define CLASS_FET3	3	/* unknown; 3-terminal NMOS or PMOS */
#define	CLASS_NMOS4	4	/* 4-terminal NMOS */
#define CLASS_PMOS4	5	/* 4-terminal PMOS */
#define CLASS_FET4	6	/* unknown; 4-terminal NMOS or PMOS */
#define CLASS_FET	7	/* unknown; 3- or 4-terminal NMOS or PMOS */
#define CLASS_PNP	8
#define CLASS_NPN	9	/* sim "b" */
#define CLASS_BJT	10	/* unknown; PNP or NPN */
#define CLASS_RES	11	/* sim "r" */
#define CLASS_RES3	12	/* 3-terminal resistor */
#define CLASS_CAP	13	/* sim "c" */
#define CLASS_ECAP	14	/* moscap (3-terminal w/source, drain) */
#define CLASS_CAP3	15	/* cap with dummy terminal */
#define CLASS_DIODE	16	/* standard SPICE diode */
#define CLASS_INDUCTOR	17	/* standard SPICE inductor */
#define CLASS_XLINE	18	/* transmission line model */
#define CLASS_MODULE	19	/* sim "x"; black-box subcircuit */
#define CLASS_UNDEF	20	/* not defined; error */

extern char *P(char *fname, char *inststr, char *drain, char *gate, char *source);
extern char *P4(char *fname, char *inststr, char *drain, char *gate, char *source,
		char *bulk);
extern char *N(char *fname, char *inststr, char *drain, char *gate, char *source);
extern char *N4(char *fname, char *inststr, char *drain, char *gate, char *source,
		char *bulk);
extern char *B(char *fname, char *inststr, char *, char *, char *);
extern char *E(char *fname, char *inststr, char *, char *, char *);
extern char *Cap3(char *fname, char *inststr, char *, char *, char *);
extern char *Res3(char *fname, char *inststr, char *, char *, char *);
extern char *Cap(char *fname, char *inststr, char *, char *);
extern char *Res(char *fname, char *inststr, char *, char *);
extern char *XLine(char *fname, char *inststr, char *, char *, char *, char *);
extern char *Inductor(char *fname, char *inststr, char *, char *);

extern int StringIsValue(char *);
extern char *ConvertParam(char *);
extern int ConvertStringToFloat(char *, double *);
extern char *ScaleStringFloatValue(char *, double);
extern void join(char *node1, char *node2);
extern void Connect(char *tplt1, char *tplt2);
extern void Place(char *name);
extern void Array(char *Cell, int num);
extern void ActelLib(void);
extern void Flatten(char *name, int file);
extern void FlattenInstancesOf(char *model, int file);
extern int  flattenInstancesOf(char *model, int file, char *instance);
extern void FlattenCurrent();
extern void ConvertGlobals(char *name, int fnum);
extern int  CleanupPins(char *name, int fnum);
extern void ConnectAllNodes(char *model, int fnum);
extern int  NoDisconnectedNodes;
extern int  PropertyKeyMatch(char *, char *);
extern int  PropertyValueMatch(char *, char *);

/* objlist.c */
extern void CellDelete(char *name, int file);
extern void InstanceRename(char *from, char *to, int file);

extern int Debug;
extern int VerboseOutput; /* set this to 1 to enable extra output */
extern int IgnoreRC;	  /* set this to 1 to ignore capacitance and resistance */
extern int NoOutput;      /* set this to 1 to disable stdout output */
extern int Composition;	  /* direction of composition */
extern int UnixWildcards; /* TRUE if *,?,{},[] only; false if full REGEXP */
/* magic internal flag to restrict searches to recently placed cells */
extern int QuickSearch;
/* does re"CellDef"ing a cell add to it or overwrite it??? */
extern int AddToExistingDefinition;
/* procedure to facilitate generating file/cell/pin names */
char *Str(char *format, ...);


/* these are defined in data.h */
extern void Initialize(void);
extern void InitializeCommandLine(int argc, char **argv);
#ifdef TCL_NETGEN
extern void PrintAllElements(char *cell, int file);
#else
extern void PrintElement(char *cell, char *list_template);
#endif
extern void Fanout(char *cell, char *node, int filter);
extern void PrintCell(char *name, int file);
extern void Query(void);

/* this is defined in xnetgen.h */
void X_main_loop(int argc, char *argv[]); 

/* output file formats; also defined in netfile.h */
extern void Ntk(char *name, char *filename);
extern void Actel(char *name, char *filename);
extern void Wombat(char *name, char *filename);
extern void Ext(char *name, int fnum);
extern void Sim(char *name, int fnum);
extern void SpiceCell(char *name, int fnum, char *filename);
extern void EsacapCell(char *name, char *filename);
extern void WriteNetgenFile(char *name, char *filename);
extern void Ccode(char *name, char *filename);

/* input file formats, these routines return the name of the top-level cell */
extern char *ReadNtk (char *fname, int *fnum);
extern char *ReadExtHier(char *fname, int *fnum);
extern char *ReadExtFlat(char *fname, int *fnum);
extern char *ReadSim(char *fname, int *fnum);
extern char *ReadSpice(char *fname, int *fnum);
extern char *ReadSpiceLib(char *fname, int *fnum);
extern char *ReadNetgenFile (char *fname, int *fnum);

extern char *ReadNetlist(char *fname, int *fnum);


/* these are defined in place.h */
extern void Embed(char *cellname);
extern void PROTOCHIP(void);

/* these are defined in netcmp.c */
extern void NETCOMP(void);
extern int Compare(char *cell1, char *cell2);

/* this is defined in tclnetgen.c */
#ifdef TCL_NETGEN
extern char *tcl_calloc(size_t, size_t);
#endif

#endif	/* _NETGEN_H */
