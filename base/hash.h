#ifndef _HASH_H
#define _HASH_H

struct hashlist {
  char *name;
  void *ptr;
  struct hashlist *next;
};

struct hashdict {
  int hashsize;
  int hashfirstindex;
  struct hashlist *hashfirstptr;
  struct hashlist **hashtab;
};


void InitializeHashTable(struct hashdict *dict, int size);
int RecurseHashTable(struct hashdict *dict,
	int (*func)(struct hashlist *elem));
int RecurseHashTableValue(struct hashdict *dict,
	int (*func)(struct hashlist *elem, int), int);
struct nlist *RecurseHashTablePointer(struct hashdict *dict,
	struct nlist *(*func)(struct hashlist *elem, void *),
	void *pointer);
void HashDelete(char *name, struct hashdict *dict);
void HashIntDelete(char *name, int value, struct hashdict *dict);
void HashKill(struct hashdict *dict);


int CountHashTableEntries(struct hashlist *p);
int CountHashTableBinsUsed(struct hashlist *p);

/* these functions return a pointer to a hash list element */
struct hashlist *HashInstall(char *name, struct hashdict *dict);
struct hashlist *HashPtrInstall(char *name, void *ptr, 
		struct hashdict *dict);
struct hashlist *HashIntPtrInstall(char *name, int value, void *ptr, 
		struct hashdict *dict);
struct hashlist *HashInt2PtrInstall(char *name, int c, void *ptr, 
		struct hashdict *dict);

/* these functions return the ->ptr field of a struct hashlist */
void *HashLookup(char *s, struct hashdict *dict);
void *HashIntLookup(char *s, int i, struct hashdict *dict);
void *HashInt2Lookup(char *s, int c, struct hashdict *dict);
void *HashFirst(struct hashdict *dict);
void *HashNext(struct hashdict *dict);

unsigned long hashnocase(char *s, int hashsize);
unsigned long my_hash(char *s, int hashsize);

extern int (*matchfunc)(char *, char *);
/* matchintfunc() compares based on the name and the first	*/
/* entry of the pointer value, which is cast as an integer	*/
extern int (*matchintfunc)(char *, char *, int, int);
extern unsigned long (*hashfunc)(char *, int);

#endif /* _HASH_H */
