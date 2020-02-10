/* Exported global variables */

extern struct ElementClass *ElementClasses;
extern struct NodeClass *NodeClasses;

extern struct nlist *Circuit1;
extern struct nlist *Circuit2;

extern int ExhaustiveSubdivision;

extern int left_col_end;
extern int right_col_end;

#ifdef TCL_NETGEN
#include <tcl.h>
extern int InterruptPending;
#endif

/* Exported procedures */

extern void PrintElementClasses(struct ElementClass *EC, int type, int dolist);
extern void PrintNodeClasses(struct NodeClass *NC, int type, int dolist);
extern void SummarizeNodeClasses(struct NodeClass *NC);
extern void PrintPropertyResults(int do_list);
extern void PrintCoreStats(void);
extern void ResetState(void);
extern void CreateTwoLists(char *name1, int file1, char *name2, int file2,
		int dolist);
extern int Iterate(void);
extern int VerifyMatching(void);
extern void PrintAutomorphisms(void);
extern int ResolveAutomorphisms(void);
extern void PermuteAutomorphisms(void);
extern int Permute(void);
extern int PermuteSetup(char *model, int filenum, char *pin1, char *pin2);
extern int PermuteForget(char *model, int filenum, char *pin1, char *pin2);
extern int EquivalenceElements(char *name1, int file1, char *name2, int file2);
extern int EquivalenceNodes(char *name1, int file1, char *name2, int file2);
extern int EquivalenceClasses(char *name1, int file1, char *name2, int file2);
extern int IgnoreClass(char *name, int file, unsigned char type);
extern int MatchPins(struct nlist *tp1, struct nlist *tp2, int dolist);
extern int PropertyOptimize(struct objlist *ob, struct nlist *tp, int run,
	int series, int comb);

extern int  CreateCompareQueue(char *, int, char *, int);
extern int  GetCompareQueueTop(char **, int *, char **, int *);
extern int  PeekCompareQueueTop(char **, int *, char **, int *);
extern void RemoveCompareQueue();

extern void PrintIllegalClasses();
extern void PrintIllegalNodeClasses();
extern void PrintIllegalElementClasses();

extern void DumpNetwork(struct objlist *ob, int cidx);
extern void DumpNetworkAll(char *name, int file);

#ifdef TCL_NETGEN
extern int EquivalentNode();
extern int EquivalentElement();

extern void enable_interrupt();
extern void disable_interrupt();

extern Tcl_Obj *ListNodeClasses(int legal);
extern Tcl_Obj *ListElementClasses(int legal);
#endif

