
#define LEAFPINS 15
#define RENTEXP 0.3

/* #ifdef VMUNIX */
/* #define MAX_ELEMENTS 20000 */
/* #define MAX_NODES 72 */
/* #define MAX_LEAVES 64 */
#define MAX_ELEMENTS 5000
#define MAX_NODES 150
#define MAX_TREE_DEPTH 8
/* #define MAX_LEAVES 256 */
#define MAX_LEAVES (1<<MAX_TREE_DEPTH)
#define BITS_PER_LONG 32
/* #endif */ /* VMUNIX */

#ifdef IBMPC
#define MAX_ELEMENTS 100
#define MAX_NODES 20
#define MAX_LEAVES 16
#define MAX_TREE_DEPTH 4
#define BITS_PER_LONG 32
#endif /* IBMPC */

#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)<(b))?(a):(b))
#define POW2(a) (1<<(a))

/* abridged ownership and connectivity matrices */
/* elements, nodes, leaves are indexed from 1 to N, nodes, leaves */
extern unsigned short M[][7];
  /* height, L, R, SWALLOWED, PINS, LEAVES, USED */
#define LEVEL(e)     (M[e][0])
#define L(e)         (M[e][1])
#define R(e)         (M[e][2])
#define SWALLOWED(e) (M[e][3])
#define PINS(e)      (M[e][4])  /* originally C[e][0] */
#define LEAVES(e)    (M[e][5])
#define USED(e)      (M[e][6])

extern unsigned long MSTAR[MAX_ELEMENTS][(MAX_LEAVES / BITS_PER_LONG)  + 1];
#define SetPackedArrayBit(A,B) \
       (A [(B) / BITS_PER_LONG] |= (1L << ((B)%BITS_PER_LONG)))
#define TestPackedArrayBit(A,B) \
       (A [(B) / BITS_PER_LONG] & (1L << ((B)%BITS_PER_LONG)))

extern unsigned char C[MAX_ELEMENTS][MAX_NODES + 1];
extern unsigned char CSTAR[MAX_ELEMENTS][MAX_NODES + 1];

extern int PackedLeaves;
extern int CountExists;

/* data structure to capture embedding */
struct embed {
  struct embed *left;
  struct embed *right;
  struct nlist *cell;
  int instancenumber;
  int level;
};

extern int InitializeExistTest(void);
extern void AddToExistSet(int E1, int E2);
extern int Exists(int E1, int E2);
extern void PrintExistSetStats(FILE *f);

extern void FreeEmbeddingTree(struct embed *E);
extern struct embed *EmbeddingTree(struct nlist *tp, int E);
extern void PrintEmbeddingTree(FILE *outfile, char *cellname, int flatten);


/* different embedding strategies, found in random.c, anneal.c, greedy.c */
extern int RandomPartition(int left, int right, int level);  /* random.c */
extern int AnnealPartition(int left, int right, int level);  /* anneal.c */
extern int GreedyPartition(int left, int right, int level);  /* greedy.c */
extern void EmbedCell(char *cellname, char *filename);       /* bottomup.c */

extern int GradientDescent(int left, int right, int partition);  /* place.c */

/* random data defined in greedy.c */
extern int permutation[];
extern int TopDownStartLevel;
extern int TreeFanout[];
extern int leftnodes[];
extern int rightnodes[];

enum EmbeddingStrategy {random_embedding, greedy, anneal, bottomup} ;

extern void TopDownEmbedCell(char *cellname, char *filename, 
			     enum EmbeddingStrategy strategy);

#define LEFT 1
#define RIGHT 2
#define BOTH 3


/********************  variables in place.c *******************************/

#define IsLeaf(E) (L(E) == 0 && R(E) == 0)

/* abridged ownership and connectivity matrices */
/* elements, nodes, leaves are indexed from 1 to N, nodes, leaves */
extern unsigned short M[MAX_ELEMENTS][7];  
/* height, L, R, SWALLOWED, PINS, LEAVES, USED */

extern unsigned long MSTAR[MAX_ELEMENTS][(MAX_LEAVES / BITS_PER_LONG)  + 1];

extern unsigned char C[MAX_ELEMENTS][MAX_NODES + 1];
extern unsigned char CSTAR[MAX_ELEMENTS][MAX_NODES + 1];

/* elements at level i must have TreeFanout[i] or fewer ports */
extern int TreeFanout[MAX_TREE_DEPTH + 1];       /* tree fanout at each level */

/* elements at level i must share MinCommonNodes[i] nodes between their kids */
/* or swallow a child entirely */
extern int MinCommonNodes[MAX_TREE_DEPTH + 1];   

/* elements at level i must contain at least MinUsedLeaves[i] leaves */
extern int MinUsedLeaves[MAX_TREE_DEPTH + 1];   

extern int Nodes;   /* number of nodes in the cell */
extern int Leaves;  /* number of leaves in the cell */
extern int PackedLeaves; /* == Leaves / BITS_PER_LONG, just to save computation */
extern int Elements; /* number of elements */
extern int NewN, NewElements;
extern int SumPINS, SumCommonNodes, SumUsedLeaves;
extern int NewSwallowed;
extern int Pass;
extern int logging; /* generate output file LOG_FILE_EXT */
extern int selectivelogging; 
extern int LogLevel1; /* automatically log if Level1 == LogLevel1 */
extern int LogLevel2;
extern int FatalError; /* internal error */
extern int Exhaustive; /* slow, methodical */
extern int PlaceDebug; /* interactive debug */

extern FILE *outfile;  /* output file */
extern FILE *logfile;  /* debugging log file */

/* count invokations of different test procedures */
extern int CountIndependent;
extern int CountAnyCommonNodes;
extern int CountFanoutOK;
extern int CountSwallowedElements;

/**************** procedures in place.c *********************************/

extern int CountInLevel(int i, int upto);
extern void AddNewElement (int E1, int E2); 
extern int PartitionFanout(int left, int right, int side);
extern void Dbug_print_cells(int left, int right);
extern int AnyCommonNodes(int E1, int E2);
extern int InitializeMatrices(char *cellname);
extern int OpenEmbeddingFile(char *cellname, char *filename);
extern void CloseEmbeddingFile(void);
extern void ToggleLogging(void);
extern void ToggleDebug(void);
extern void ToggleExhaustive(void);
extern void DescribeCell(char *name, int detail);
extern void ProtoEmbed(char *name, char ch);
extern void ProtoPrintParameters(void);
extern void SetupMinUsedLeaves(char *string);
extern void SetupMinCommonNodes(char *string);
extern void SetupTreeFanout(char *string);
extern void SetupRentExp(char *string);
extern void SetupLeafPinout(char *string);


extern void PrintOwnership(FILE *outfile);
extern void PrintC(FILE *outfile);
extern void PrintCSTAR(FILE *outfile);
extern void PrintE(FILE *outfile, int E);
