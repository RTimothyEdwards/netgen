/*  objlist.h -- core allocation, list generation by regexps */

#ifndef _OBJLIST_H
#define _OBJLIST_H

#define SEPARATOR "/"
#define INSTANCE_DELIMITER "#"
#define PORT_DELIMITER "."
#define PHYSICALPIN "("
#define ENDPHYSICALPIN ")"

#define PORT (-1)
#define GLOBAL (-2)
#define UNIQUEGLOBAL (-3)
#define PROPERTY (-4)		/* For element properties; e.g., length, width */
#define ALLELEMENTS (-5)	/* for doing searches; e.g., Fanout() */
#define ALLOBJECTS (-6)		/* for doing searches; e.g., Fanout() */
#define UNKNOWN (-7)		/* for error checking */
#define NODE 0
#define FIRSTPIN 1
#define IsPort(a) ((a)->type == PORT)
#define IsNonProxyPort(a) (((a)->type == PORT) && ((a)->model.port != PROXY))
#define IsGlobal(a) (((a)->type == GLOBAL) || ((a)->type == UNIQUEGLOBAL))

#define PROXY (0)		/* Used in model.port record of ports */

/* Lists of device properties.  Order is defined at the time of	*/
/* the cell definition; values are sorted at the time instances	*/
/* are read.							*/

/* Part 1a: Define the types of tokens used in an expression */

#define TOK_NONE        0
#define TOK_DOUBLE      1
#define TOK_STRING      2
#define TOK_MULTIPLY    3
#define TOK_DIVIDE      4
#define TOK_PLUS        5
#define TOK_MINUS       6
#define TOK_FUNC_OPEN   7
#define TOK_FUNC_CLOSE  8
#define TOK_GT          9
#define TOK_LT          10
#define TOK_GE          11
#define TOK_LE          12
#define TOK_EQ          13
#define TOK_NE          14
#define TOK_GROUP_OPEN  15
#define TOK_GROUP_CLOSE 16
#define TOK_FUNC_IF     17
#define TOK_FUNC_THEN   18
#define TOK_FUNC_ELSE   19
#define TOK_SGL_QUOTE	20
#define TOK_DBL_QUOTE	21

/* Part 1b: Stack structure used to hold expressions in tokenized form */

struct tokstack {
   int toktype;
   union {
       double dvalue;
       char *string;
   } data;
   struct tokstack *next;
   struct tokstack *last;
};

/* Part 1c: Define the types of property values */

#define PROP_STRING	0
#define PROP_EXPRESSION 1	/* Same as STRING, handled differently */
#define PROP_INTEGER	2
#define PROP_DOUBLE	3
#define PROP_VALUE	4	/* Same as DOUBLE, handled differently */
#define PROP_ENDLIST	5	/* End of the property record. */

/* Part 1d:  Linked list of values for temporary unordered storage when	*/
/* reading a netlist.  Values are string only, to be promoted later if	*/
/* needed.								*/

struct keyvalue {
  char *key;
  char *value;
  struct keyvalue *next;
};

/* Part 2:  Values (corresponding to the keys, and kept in the instance record) */

struct valuelist {
  char *key;
  unsigned char type;		/* string, integer, double, value, expression */
  union {
     char *string;
     double dval;
     int ival;
     struct tokstack *stack;	/* expression in tokenized form */
  } value;
};

/* Part 3:  Keys & Defaults (kept in the cell record as a hash table) */

struct property {
  char *key;			/* name of the property */
  unsigned char idx;		/* index into valuelist */
  unsigned char type;		/* string, integer, double, value, expression */
  union {
     char *string;
     double dval;
     int ival;
     struct tokstack *stack;
  } pdefault;			/* Default value */
  union {
     double dval;
     int ival;
  } slop;			/* slop allowance in property */
};

/*-------------------------------*/
/* list of objects within a cell */
/*-------------------------------*/

struct objlist {
  char *name;		/* unique name for the port/node/pin/property */
  int type;		/* -1 for port,  0 for internal node,
			   else index of the pin on element */
  union {
     char *class;		/* name of element class; nullstr for nodes */
     int   port;		/* Port number, if type is a port */
  } model;
  union {
     char *name;		/* unique name for the instance, or */
				/* (string) value of property for properties */
     struct valuelist *props;	/* Property record */
  } instance;
  int node;		/* the electrical node number of the port/node/pin */
  struct objlist *next;
};

extern struct objlist *LastPlaced; 

/* Record structure for maintaining lists of cell classes to ignore */

struct IgnoreList {
    char *class;
    int file;
    struct IgnoreList *next;
};

/* Record structure for handling pin permutations in a cell	*/
/* Linked list structure allows multiple permutations per cell.	*/

struct Permutation {
    char *pin1;
    char *pin2;
    struct Permutation *next;
};

#define OBJHASHSIZE 997 /* the size of the object and instance hash lists */
                        /* prime numbers are good choices as hash sizes */
                        /* 101 is a good number for IBMPC */

/* cell definition for hash table */
/* NOTE: "file" must come first for the hash matching by name and file */

struct nlist {
  int file;		/* internally ordered file to which cell belongs, or -1 */
  char *name;
  int number;		/* number of instances defined */
  int dumped;		/* instance count, and general-purpose marker */
  unsigned char flags;
  unsigned char class;
  unsigned long classhash;	/* randomized hash value for cell class */
  struct Permutation *permutes;	/* list of permuting pins */
  struct objlist *cell;
  struct hashlist **objtab;  /* hash table of object names */
  struct hashlist **insttab; /* hash table of instance names */
  struct hashlist **proptab; /* hash table of property keys */
  struct objlist **nodename_cache;
  long nodename_cache_maxnodenum;  /* largest node number in cache */
  void *embedding;   /* this will be cast to the appropriate data structure */
  struct nlist *next;
};

/* Defined nlist structure flags */

#define CELL_MATCHED		0x01	/* cell matched to another */
#define CELL_NOCASE		0x02	/* cell is case-insensitive (e.g., SPICE) */
#define CELL_TOP		0x04	/* cell is a top-level cell */
#define CELL_PLACEHOLDER	0x08	/* cell is a placeholder cell */
#define CELL_PROPSMATCHED	0x10	/* properties matched to matching cell */
#define CELL_DUPLICATE		0x20	/* cell has a duplicate */

/* Flags for combination allowances */

#define COMB_SERIAL		0x20
#define COMB_PARALLEL		0x40

extern struct nlist *CurrentCell;
extern struct objlist *CurrentTail;
extern void AddToCurrentCell(struct objlist *ob);
extern void AddToCurrentCellNoHash(struct objlist *ob);
extern void AddInstanceToCurrentCell(struct objlist *ob);
extern void FreeObject(struct objlist *ob);
extern void FreeObjectAndHash(struct objlist *ob, struct nlist *ptr);
extern void FreePorts(char *cellname);
extern struct IgnoreList *ClassIgnore;

extern int NumberOfPorts(char *cellname);
extern struct objlist *InstanceNumber(struct nlist *tp, int inst);

extern struct objlist *List(char *list_template);
extern struct objlist *ListExact(char *list_template);
extern struct objlist *ListCat(struct objlist *ls1, struct objlist *ls2);
extern int ListLen(struct objlist *head);
extern int ListLength(char *list_template);
extern struct nlist *LookupPrematchedClass(struct nlist *, int);
extern struct objlist *LookupObject(char *name, struct nlist *WhichCell);
extern struct objlist *LookupInstance(char *name, struct nlist *WhichCell);
extern struct objlist *CopyObjList(struct objlist *oldlist);
extern void UpdateNodeNumbers(struct objlist *lst, int from, int to);

/* Function pointer to List or ListExact, allowing regular expressions	*/
/* to be enabled/disabled.						*/

extern struct objlist * (*ListPtr)();

extern void PrintCellHashTable(int full, int file);
extern struct nlist *LookupCell(char *s);
extern struct nlist *LookupCellFile(char *s, int f);
extern struct nlist *InstallInCellHashTable(char *name, int f);
extern void InitCellHashTable(void);
extern void ClearDumpedList(void);
extern int RecurseCellHashTable(int (*foo)(struct hashlist *np));
extern int RecurseCellFileHashTable(int (*foo)(struct hashlist *, int), int);
extern struct nlist *RecurseCellHashTable2(struct nlist *(*foo)(struct hashlist *,
		void *), void *);
extern struct nlist *FirstCell(void);
extern struct nlist *NextCell(void);

extern char *NodeName(struct nlist *tp, int node);
extern char *NodeAlias(struct nlist *tp, struct objlist *ob);
extern void FreeNodeNames(struct nlist *tp);
extern void CacheNodeNames(struct nlist *tp);


/* enable the following line to debug the core allocator */
/* #define DEBUG_GARBAGE */
   
#ifdef DEBUG_GARBAGE
extern struct objlist *GetObject(void);
extern struct keyvalue *NewKeyValue(void);
extern struct property *NewProperty(void);
extern struct valuelist *NewPropValue(int entries);
extern void FreeString(char *foo);
extern char *strsave(char *s);
#else /* not DEBUG_GARBAGE */
#define GetObject() ((struct objlist*)CALLOC(1,sizeof(struct objlist)))
#define NewProperty() ((struct property*)CALLOC(1,sizeof(struct property)))
#define NewPropValue(a) ((struct valuelist*)CALLOC((a),sizeof(struct valuelist)))
#define NewKeyValue() ((struct keyvalue*)CALLOC(1,sizeof(struct keyvalue)))
#define FreeString(a) (FREE(a))
#define strsave(a) (STRDUP(a))
#endif /* not DEBUG_GARBAGE */

extern int freeprop(struct hashlist *p);

extern int  match(char *, char *);
extern int  matchnocase(char *, char *);
extern int  matchfile(char *, char *, int, int);
extern int  matchfilenocase(char *, char *, int, int);

extern void GarbageCollect(void);
extern void InitGarbageCollection(void);
extern void AddToGarbageList(struct objlist *head);

#ifdef HAVE_MALLINFO
void PrintMemoryStats(void);
#endif

#endif  /* _OBJLIST_H */



