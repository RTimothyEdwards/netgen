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

void PrintElementClasses(struct ElementClass *EC, int type, int dolist);
void PrintNodeClasses(struct NodeClass *NC, int type, int dolist);
void SummarizeNodeClasses(struct NodeClass *NC);
void PrintPropertyResults(int do_list);
void PrintCoreStats(void);
void ResetState(void);
void CreateTwoLists(char *name1, int file1, char *name2, int file2,
		int dolist);
int Iterate(void);
int VerifyMatching(void);
void PrintAutomorphisms(void);
int ResolveAutomorphisms(void);
void PermuteAutomorphisms(void);
int Permute(void);
int PermuteSetup(char *model, int filenum, char *pin1, char *pin2);
int PermuteForget(char *model, int filenum, char *pin1, char *pin2);
int EquivalenceElements(char *name1, int file1, char *name2, int file2);
int EquivalenceNodes(char *name1, int file1, char *name2, int file2);
int EquivalenceClasses(char *name1, int file1, char *name2, int file2);
int IgnoreClass(char *name, int file, unsigned char type);
int MatchPins(struct nlist *tp1, struct nlist *tp2, int dolist);
int PropertyOptimize(struct objlist *ob, struct nlist *tp, int run,
	int series, int comb);

int  CreateCompareQueue(char *, int, char *, int);
int  GetCompareQueueTop(char **, int *, char **, int *);
int  PeekCompareQueueTop(char **, int *, char **, int *);
void RemoveCompareQueue();

void PrintIllegalClasses();
void PrintIllegalNodeClasses();
void PrintIllegalElementClasses();

void DumpNetwork(struct objlist *ob, int cidx);
void DumpNetworkAll(char *name, int file);

void RegroupDataStructures();
void FormatIllegalElementClasses();
void FormatIllegalNodeClasses();
int ResolveAutomorphsByProperty();
int ResolveAutomorphsByPin();
void SummarizeElementClasses(struct ElementClass *EC);
int remove_group_tags(struct objlist *ob);


#ifdef TCL_NETGEN
int EquivalentNode();
int EquivalentElement();

void enable_interrupt();
void disable_interrupt();

Tcl_Obj *ListNodeClasses(int legal);
Tcl_Obj *ListElementClasses(int legal);
#endif

